#include "term.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

namespace ds::term {

namespace {

termios g_saved;
bool g_savedValid = false;

// What open() sends and close() undoes. The kitty flags: 1 disambiguate
// escape codes, 2 report event types (press / repeat / release), 8 report
// all keys as escape codes (without it the letters come as plain text and
// have no release event).
const char *const KITTY_PUSH = "\x1b[>11u";
const char *const KITTY_POP = "\x1b[<u";
const char *const ENTER_SCREEN =
    "\x1b[?1049h"  // alternate screen
    "\x1b[?25l"    // hide the cursor
    "\x1b[?7l"     // no auto wrap (the last cell must not scroll)
    "\x1b[?1004h"  // focus events
    "\x1b[2J\x1b[H";
const char *const LEAVE_SCREEN =
    "\x1b[0m"
    "\x1b[?1004l"
    "\x1b[?7h"
    "\x1b[?25h"
    "\x1b[?1049l";

int64_t nowMsFallback() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

}  // namespace

bool Term::open(const Options &opts) {
  fd_ = STDIN_FILENO;
  if (!isatty(fd_) || !isatty(STDOUT_FILENO)) {
    std::fprintf(stderr, "devoursphere: stdin and stdout must be a terminal\n");
    return false;
  }
  if (tcgetattr(fd_, &g_saved) != 0) {
    std::perror("tcgetattr");
    return false;
  }
  g_savedValid = true;
  termios raw = g_saved;
  cfmakeraw(&raw);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(fd_, TCSAFLUSH, &raw) != 0) {
    std::perror("tcsetattr");
    return false;
  }
  open_ = true;

  keys_ = Keys::LEGACY;
  if (opts.keys == Keys::KITTY || (opts.keys == Keys::AUTO && queryKitty())) {
    keys_ = Keys::KITTY;
  }
  write(ENTER_SCREEN, std::strlen(ENTER_SCREEN));
  if (keys_ == Keys::KITTY) write(KITTY_PUSH, std::strlen(KITTY_PUSH));
  pending_.clear();
  return true;
}

void Term::close() {
  if (!open_) return;
  open_ = false;
  if (keys_ == Keys::KITTY) write(KITTY_POP, std::strlen(KITTY_POP));
  write(LEAVE_SCREEN, std::strlen(LEAVE_SCREEN));
  if (g_savedValid) tcsetattr(fd_, TCSAFLUSH, &g_saved);
}

// Ask "CSI ? u" (the kitty flags query) followed by "CSI c" (primary device
// attributes, which every terminal answers). A terminal that knows the
// protocol answers both, in order; one that does not answers only the
// second. The wait is bounded for a terminal that answers neither.
bool Term::queryKitty() {
  const char *q = "\x1b[?u\x1b[c";
  write(q, std::strlen(q));
  std::string buf;
  bool kitty = false;
  const int64_t t0 = nowMsFallback();
  while (nowMsFallback() - t0 < 1000) {
    pollfd pfd = {fd_, POLLIN, 0};
    if (::poll(&pfd, 1, 100) <= 0) continue;
    char tmp[256];
    ssize_t n = ::read(fd_, tmp, sizeof(tmp));
    if (n <= 0) continue;
    buf.append(tmp, (size_t)n);
    // Walk the "CSI ?" replies that are complete so far
    size_t at = 0;
    while ((at = buf.find("\x1b[?", at)) != std::string::npos) {
      size_t fin = at + 3;
      while (fin < buf.size() &&
             ((uint8_t)buf[fin] < 0x40 || (uint8_t)buf[fin] > 0x7e))
        fin++;
      if (fin >= buf.size()) break;  // not complete yet
      if (buf[fin] == 'u') kitty = true;
      if (buf[fin] == 'c')
        return kitty;  // DA1: the terminal has said all it will
      at = fin + 1;
    }
  }
  return kitty;
}

void Term::size(int *cols, int *rows) const {
  winsize ws = {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
  } else {
    *cols = 80;
    *rows = 24;
  }
}

void Term::write(const char *p, size_t n) const {
  while (n > 0) {
    ssize_t w = ::write(STDOUT_FILENO, p, n);
    if (w < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      return;
    }
    p += w;
    n -= (size_t)w;
  }
}

void Term::wait(int timeoutMs) {
  pollfd pfd = {fd_, POLLIN, 0};
  ::poll(&pfd, 1, timeoutMs < 0 ? 0 : timeoutMs);
  poll(nowMsFallback());
}

void Term::poll(int64_t nowMs) {
  char tmp[1024];
  for (;;) {
    ssize_t n = ::read(fd_, tmp, sizeof(tmp));
    if (n <= 0) break;
    if (pending_.empty()) pendingMs_ = nowMs;
    pending_.append(tmp, (size_t)n);
    if ((size_t)n < sizeof(tmp)) break;
  }
  parse(nowMs, false);
  // A lone ESC (no continuation within ESC_ALONE_MS) is the Escape key
  if (!pending_.empty() && nowMs - pendingMs_ >= ESC_ALONE_MS)
    parse(nowMs, true);
}

void Term::parse(int64_t nowMs, bool flush) {
  size_t pos = 0;
  while (pos < pending_.size()) {
    size_t used =
        parseOne(pending_.data() + pos, pending_.size() - pos, nowMs, flush);
    if (used == 0) break;  // incomplete: wait for more bytes
    pos += used;
  }
  pending_.erase(0, pos);
  if (!pending_.empty() && pos > 0) pendingMs_ = nowMs;
}

// One key or sequence at the front of the buffer. Returns the bytes used,
// or 0 when the sequence is incomplete (unless `flush`, which takes a lone
// ESC as the Escape key).
size_t Term::parseOne(const char *p, size_t n, int64_t nowMs, bool flush) {
  const uint8_t c = (uint8_t)p[0];
  if (c != 0x1b) {
    // A plain byte: legacy text key (in kitty mode with flag 8 these do not
    // occur, except for a terminal that ignored the push)
    uint32_t key = c;
    if (c == 3)
      key = KEY_CTRL_C;
    else if (c == 12)
      key = KEY_CTRL_L;
    else if (c == '\r' || c == '\n')
      key = KEY_ENTER;
    else if (c >= 'A' && c <= 'Z')
      key = c - 'A' + 'a';
    else if (c >= 0x80)
      return 1;  // UTF-8 continuation: ignore
    else if (c < 32 && c != 27)
      return 1;
    onEvent(key, 1, nowMs);
    return 1;
  }
  if (n == 1) {
    if (!flush) return 0;
    onEvent(KEY_ESC, 1, nowMs);
    return 1;
  }
  // SS3 (application cursor keys): ESC O A..D
  if (p[1] == 'O') {
    if (n < 3) return flush ? 1 : 0;
    switch (p[2]) {
      case 'A': onEvent(KEY_UP, 1, nowMs); break;
      case 'B': onEvent(KEY_DOWN, 1, nowMs); break;
      case 'C': onEvent(KEY_RIGHT, 1, nowMs); break;
      case 'D': onEvent(KEY_LEFT, 1, nowMs); break;
      default: break;
    }
    return 3;
  }
  if (p[1] != '[') {
    // ESC + something else (alt-key in legacy terminals): drop both
    onEvent(KEY_ESC, 1, nowMs);
    return 1;
  }
  // CSI: parameters (digits ; :) then a final byte 0x40..0x7E
  size_t i = 2;
  bool priv = false;
  if (i < n && p[i] == '?') {
    priv = true;
    i++;
  }
  int params[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
  int pi = 0, si = 0;
  bool any = false;
  while (i < n) {
    const char ch = p[i];
    if (ch >= '0' && ch <= '9') {
      if (pi < 4 && si < 2) {
        int &v = params[pi][si];
        v = (v < 0 ? 0 : v) * 10 + (ch - '0');
        any = true;
      }
      i++;
    } else if (ch == ';') {
      pi++;
      si = 0;
      i++;
    } else if (ch == ':') {
      si++;
      i++;
    } else if ((uint8_t)ch >= 0x40 && (uint8_t)ch <= 0x7e) {
      break;
    } else {
      i++;  // intermediate bytes: skip
    }
  }
  if (i >= n) return flush ? n : 0;  // final byte not yet here
  const char fin = p[i];
  const size_t used = i + 1;
  (void)any;
  if (priv) return used;  // replies to queries: nothing to do here
  if (fin == 'I') {
    focusLost_ = false;
    return used;
  }
  if (fin == 'O') {
    focusLost_ = true;
    releaseAll();
    return used;
  }
  // kitty: the event type is the second sub-parameter of the modifiers
  const int mods = params[1][0] < 1 ? 0 : params[1][0] - 1;
  const int type = params[1][1] < 1 ? 1 : params[1][1];
  uint32_t key = KEY_NONE;
  switch (fin) {
    case 'A': key = KEY_UP; break;
    case 'B': key = KEY_DOWN; break;
    case 'C': key = KEY_RIGHT; break;
    case 'D': key = KEY_LEFT; break;
    case 'u': {
      const int code = params[0][0];
      if (code == 27)
        key = KEY_ESC;
      else if (code == 13)
        key = KEY_ENTER;
      else if (code == 32)
        key = KEY_SPACE;
      else if (code == 'c' && (mods & 4))
        key = KEY_CTRL_C;
      else if (code == 'l' && (mods & 4))
        key = KEY_CTRL_L;
      else if (code >= 'A' && code <= 'Z')
        key = (uint32_t)(code - 'A' + 'a');
      else if (code > 32 && code < 127)
        key = (uint32_t)code;
      break;
    }
    default: break;
  }
  if (key != KEY_NONE) onEvent(key, type, nowMs);
  return used;
}

Term::KeyState *Term::slot(uint32_t key, bool create) {
  KeyState *free = nullptr;
  for (auto &s : states_) {
    if (s.key == key) return &s;
    if (!free && s.key == 0) free = &s;
  }
  if (!create) return nullptr;
  if (!free) free = &states_[0];  // full: reuse (never in practice)
  *free = KeyState{};
  free->key = key;
  return free;
}

void Term::onEvent(uint32_t key, int type, int64_t nowMs) {
  KeyState *s = slot(key, true);
  if (type == 3) {  // release
    s->down = false;
    s->key = 0;
    return;
  }
  const bool repeat =
      type == 2 || (keys_ == Keys::LEGACY && s->down &&
                    nowMs - s->lastMs < HOLD_FIRST_MS + HOLD_REPEAT_MS);
  if (!repeat) s->firstMs = nowMs;
  s->lastMs = nowMs;
  s->down = true;
  if (pressCount_ < (int)(sizeof(presses_) / sizeof(presses_[0]))) {
    presses_[pressCount_++] = {key, repeat};
  }
}

void Term::releaseAll() {
  for (auto &s : states_) s = KeyState{};
}

bool Term::held(uint32_t key, int64_t nowMs) const {
  for (const auto &s : states_) {
    if (s.key != key || !s.down) continue;
    if (keys_ == Keys::KITTY) return true;
    if (nowMs - s.firstMs < HOLD_FIRST_MS) return true;
    return nowMs - s.lastMs < HOLD_REPEAT_MS;
  }
  return false;
}

int Term::takePresses(KeyPress *out, int max) {
  int n = pressCount_ < max ? pressCount_ : max;
  for (int i = 0; i < n; i++) out[i] = presses_[i];
  pressCount_ = 0;
  return n;
}

}  // namespace ds::term
