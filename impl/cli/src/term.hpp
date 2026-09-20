// The terminal: raw mode, the alternate screen, key events and the window
// size. Everything here is POSIX (termios, poll, ioctl) plus escape
// sequences, so it works on Linux, macOS and the BSDs alike.
//
// Keys come in one of two ways (SPEC.md, "入力"):
//
// - kitty keyboard protocol: the terminal reports press, repeat and release
//   for every key, so a held button is simply a key without its release yet.
// - legacy: the terminal only sends bytes while a key is pressed or
//   auto-repeats, and never says when it is released. A key is taken as
//   held for HOLD_FIRST_MS after its first byte (the auto-repeat delay is
//   about half a second) and then for HOLD_REPEAT_MS after each repeat.
//
// The protocol is chosen at open(): the terminal is asked whether it knows
// the kitty protocol, and the reply (or the lack of one) decides.
#pragma once

#include <cstdint>
#include <string>

namespace ds::term {

enum class Keys { AUTO, KITTY, LEGACY };

struct Options {
  Keys keys = Keys::AUTO;
};

// Key identifiers: printable keys are their lower-case code point, the
// others take these values
enum : uint32_t {
  KEY_NONE = 0,
  KEY_ESC = 27,
  KEY_ENTER = 13,
  KEY_SPACE = 32,
  KEY_UP = 0x10000,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_CTRL_C,  // Ctrl-C however it arrives (byte 3 or 'c' with ctrl)
  KEY_CTRL_L,  // redraw
};

// A press (or an auto repeat) of a key, as the game loop consumes them for
// the edge-triggered things (debug cheats, quit)
struct KeyPress {
  uint32_t key;
  bool repeat;
};

class Term {
 public:
  ~Term() { close(); }

  // Enter raw mode and the alternate screen, hide the cursor, ask for key
  // events. False (with a message on stderr) when stdin is not a terminal.
  bool open(const Options &opts);
  // Put everything back. Safe to call twice, and from a signal handler's
  // point of view it only writes and calls tcsetattr.
  void close();
  bool isOpen() const { return open_; }

  Keys keys() const { return keys_; }  // KITTY or LEGACY once open

  // Read what the terminal sent (without blocking) and update the key
  // state. `nowMs` is the caller's monotonic clock.
  void poll(int64_t nowMs);
  // Wait up to `timeoutMs` for input, then poll(). Returns early on input.
  void wait(int timeoutMs);

  // Whether a key is held right now (see the header comment)
  bool held(uint32_t key, int64_t nowMs) const;
  // The presses since the last call (at most a few per frame)
  int takePresses(KeyPress *out, int max);

  // The window in cells. Re-read on every call (it is a cheap ioctl), so a
  // resize is seen on the next frame.
  void size(int *cols, int *rows) const;

  // Write bytes to the terminal in full (retrying short writes)
  void write(const char *p, size_t n) const;
  void write(const std::string &s) const { write(s.data(), s.size()); }

  // Focus lost (the terminal told us): all keys are released
  bool focusLost() const { return focusLost_; }

 private:
  static constexpr int MAX_KEYS = 32;
  static constexpr int64_t HOLD_FIRST_MS = 600;
  static constexpr int64_t HOLD_REPEAT_MS = 120;
  static constexpr int64_t ESC_ALONE_MS = 50;

  struct KeyState {
    uint32_t key = 0;
    bool down = false;    // kitty: between press and release
    int64_t firstMs = 0;  // legacy: the first byte of this hold
    int64_t lastMs = 0;   // legacy: the latest byte
  };

  bool open_ = false;
  Keys keys_ = Keys::LEGACY;
  int fd_ = 0;
  bool focusLost_ = false;
  std::string pending_;    // bytes read but not yet parsed (partial sequence)
  int64_t pendingMs_ = 0;  // when the pending ESC arrived
  KeyState states_[MAX_KEYS];
  KeyPress presses_[16];
  int pressCount_ = 0;

  bool queryKitty();
  void parse(int64_t nowMs, bool flush);
  size_t parseOne(const char *p, size_t n, int64_t nowMs, bool flush);
  void onEvent(uint32_t key, int type, int64_t nowMs);  // 1 press 2 rep 3 rel
  KeyState *slot(uint32_t key, bool create);
  void releaseAll();
};

}  // namespace ds::term
