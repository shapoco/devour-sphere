// Devour Sphere in a terminal. See SPEC.md.
//
// The simulation runs at sim::TICK_RATE from a monotonic clock; frames are
// rendered at 320x240, turned into text (aa.hpp) and written whenever the
// frame period has passed and the ticks are caught up. Keys come from the
// terminal (term.hpp), sounds are the terminal bell, and the high score is
// a 16-byte file in the current directory.

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <unistd.h>

#include "aa.hpp"
#include "devoursphere/devoursphere.hpp"
#include "devoursphere/sim/high_score_record.hpp"
#include "term.hpp"

namespace g2 = shapoco::gfx2d;
using namespace devoursphere;

namespace {

constexpr int FB_W = 320, FB_H = 240;
constexpr const char *HIGH_SCORE_FILE = "devoursphere.highscore";
constexpr int64_t TICK_US = 1000000 / sim::TICK_RATE;
constexpr int MAX_CATCHUP = 8;  // ticks per loop before time is dropped
constexpr int64_t BEEP_GAP_US = 100000;

// The sounds that ring the bell: the player hit, the player killed, an
// enemy killed -- and the mute coming off (a transition of Game::muted(),
// watched in the loop: the sim asks for MENU_SELECT there, but that one
// also rings for every cursor move). Everything else is silent: a bell has
// one pitch and no length, and a bell per shot would be a bell always.
constexpr uint32_t BEEP_MASK = 1u << (int)sim::SoundKind::HIT_PLAYER |
                               1u << (int)sim::SoundKind::ENEMY_KILLED_SMALL |
                               1u << (int)sim::SoundKind::ENEMY_KILLED_BIG |
                               1u << (int)sim::SoundKind::PLAYER_KILLED;

struct Args {
  ds::aa::Options aa;
  ds::term::Options term;
  int fps = 30;
  bool beep = true;
  bool stats = false;
  bool debug = false;
  int level = 0;
  bool autoPlay = false;
  uint32_t seed = 0;
  int once = 0;  // > 0: print one frame after that many ticks and exit
  int onceCols = 80, onceRows = 30;
};

uint16_t g_fb[FB_W * FB_H];
uint8_t g_arena[256 * 1024];
sim::Game g_game;
render::Renderer g_renderer;
ds::term::Term g_term;

int64_t nowUs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void onSignal(int sig) {
  g_term.close();
  std::signal(sig, SIG_DFL);
  raise(sig);
}

// --- High score file --------------------------------------------------------

uint32_t g_storedHighScore = 0;

void loadHighScore() {
  FILE *fp = std::fopen(HIGH_SCORE_FILE, "rb");
  if (!fp) return;
  uint8_t rec[sim::HIGH_SCORE_RECORD_BYTES];
  const bool ok = std::fread(rec, 1, sizeof(rec), fp) == sizeof(rec);
  std::fclose(fp);
  uint32_t score;
  int sphere;
  if (ok && sim::decodeHighScoreRecord(rec, &score, &sphere)) {
    g_game.setHighScore(score, sphere);
    g_storedHighScore = score;
  }
}

// Writes when the game holds a better score than the file (the title demo
// never scores: Game::keepHighScore). Through a temporary file and rename,
// so a crash mid-write leaves the old record.
void saveHighScore() {
  g_game.keepHighScore();
  if (g_game.highScore() <= g_storedHighScore) return;
  uint8_t rec[sim::HIGH_SCORE_RECORD_BYTES];
  sim::encodeHighScoreRecord(rec, g_game.highScore(), g_game.highScoreSphere());
  std::string tmp = std::string(HIGH_SCORE_FILE) + ".tmp";
  FILE *fp = std::fopen(tmp.c_str(), "wb");
  if (!fp) return;
  const bool ok = std::fwrite(rec, 1, sizeof(rec), fp) == sizeof(rec);
  std::fclose(fp);
  if (ok && std::rename(tmp.c_str(), HIGH_SCORE_FILE) == 0) {
    g_storedHighScore = g_game.highScore();
  } else {
    std::remove(tmp.c_str());
  }
}

// --- Arguments --------------------------------------------------------------

void usage() {
  std::printf(
      "Devour Sphere in a terminal (%s)\n"
      "usage: devoursphere [options]\n"
      "  --mode=ascii|braille|half   how pixels become characters (ascii)\n"
      "  --colors=true|256           SGR palette (true)\n"
      "  --color=common|bright       which color a cell takes (common)\n"
      "  --threshold=N               a pixel is lit at this luma, 0..255 (26)\n"
      "  --fps=N                     display rate cap (30); the game ticks at "
      "%d Hz\n"
      "  --keys=auto|kitty|legacy    key events (auto: ask the terminal)\n"
      "  --beep=on|off               bell: player hit / killed, enemy killed, "
      "unmute (on)\n"
      "  --stats                     a line of timings on the first row\n"
      "  --debug                     debug mode: 1-8 are cheats (see SPEC.md)\n"
      "  --level=N --auto --seed=N   start on sphere N / AI drives / seed\n"
      "  --once=N [--size=CxR]       print one frame after N ticks and exit\n"
      "keys: arrows / WASD move, space / J / L / Enter fire, I K C V B N M "
      "dodge,\n"
      "      Esc / P pause (DOWN there: sound on/off), Q or Ctrl-C quit, "
      "Ctrl-L redraw\n",
      sim::VERSION_STRING, sim::TICK_RATE);
}

bool parseArgs(int argc, char **argv, Args &a) {
  for (int i = 1; i < argc; i++) {
    const char *s = argv[i];
    const char *v = std::strchr(s, '=');
    std::string key = v ? std::string(s, v - s) : s;
    std::string val = v ? v + 1 : "";
    if (key == "--help" || key == "-h") {
      usage();
      return false;
    } else if (key == "--mode") {
      if (val == "ascii")
        a.aa.mode = ds::aa::Mode::ASCII;
      else if (val == "braille")
        a.aa.mode = ds::aa::Mode::BRAILLE;
      else if (val == "half")
        a.aa.mode = ds::aa::Mode::HALF;
      else
        goto bad;
    } else if (key == "--colors") {
      if (val == "true")
        a.aa.palette = ds::aa::Palette::TRUECOLOR;
      else if (val == "256")
        a.aa.palette = ds::aa::Palette::C256;
      else
        goto bad;
    } else if (key == "--color") {
      if (val == "common")
        a.aa.brightColor = false;
      else if (val == "bright")
        a.aa.brightColor = true;
      else
        goto bad;
    } else if (key == "--threshold") {
      a.aa.threshold = std::atoi(val.c_str());
    } else if (key == "--fps") {
      a.fps = std::atoi(val.c_str());
      if (a.fps < 1) a.fps = 1;
    } else if (key == "--keys") {
      if (val == "auto")
        a.term.keys = ds::term::Keys::AUTO;
      else if (val == "kitty")
        a.term.keys = ds::term::Keys::KITTY;
      else if (val == "legacy")
        a.term.keys = ds::term::Keys::LEGACY;
      else
        goto bad;
    } else if (key == "--beep") {
      a.beep = val != "off";
    } else if (key == "--stats") {
      a.stats = true;
    } else if (key == "--debug") {
      a.debug = true;
    } else if (key == "--level") {
      a.level = std::atoi(val.c_str());
    } else if (key == "--auto") {
      a.autoPlay = true;
    } else if (key == "--seed") {
      a.seed = (uint32_t)std::strtoul(val.c_str(), nullptr, 10);
    } else if (key == "--once") {
      a.once = std::atoi(val.c_str());
      if (a.once < 1) a.once = 1;
    } else if (key == "--size") {
      a.onceCols = std::atoi(val.c_str());
      const char *x = std::strchr(val.c_str(), 'x');
      a.onceRows = x ? std::atoi(x + 1) : 0;
      if (a.onceCols < 1 || a.onceRows < 1) goto bad;
    } else {
    bad:
      std::fprintf(stderr, "devoursphere: bad option \"%s\" (--help)\n", s);
      return false;
    }
  }
  return true;
}

// --- The game
// -----------------------------------------------------------------

uint8_t buttonsNow(int64_t nowMs) {
  using namespace ds::term;
  const auto h = [&](uint32_t k) { return g_term.held(k, nowMs); };
  uint8_t b = 0;
  if (h(KEY_LEFT) || h('a')) b |= sim::Button::LEFT;
  if (h(KEY_RIGHT) || h('d')) b |= sim::Button::RIGHT;
  if (h(KEY_UP) || h('w')) b |= sim::Button::UP;
  if (h(KEY_DOWN) || h('s')) b |= sim::Button::DOWN;
  if (h(KEY_SPACE) || h('j') || h('l') || h(KEY_ENTER)) b |= sim::Button::A;
  if (h('i') || h('k') || h('c') || h('v') || h('b') || h('n') || h('m'))
    b |= sim::Button::B;
  if (h(KEY_ESC) || h('p')) b |= sim::Button::PAUSE;
  return b;
}

void debugKey(int key) {
  if (!g_game.debugMode()) return;
  switch (key) {
    case 1: g_game.debugTakeUpgrade(sim::UpgradeKind::SHIELD); break;
    case 2: g_game.debugTakeUpgrade(sim::UpgradeKind::OVERDRIVE); break;
    case 3: g_game.debugTakeUpgrade(sim::UpgradeKind::THRUSTER); break;
    case 4: g_game.debugTakeUpgrade(sim::UpgradeKind::EXTRA_CORE); break;
    case 5: g_game.debugScaleSize(true); break;
    case 6: g_game.debugScaleSize(false); break;
    case 7: g_game.debugHeal(-25); break;
    case 8: g_game.debugHeal(25); break;
    default: break;
  }
}

void renderFrame(float dt) {
  g2::Surface s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, FB_W, FB_H, g_fb);
  g_renderer.beginFrame(g_game, dt);
  g_renderer.renderBand(s, 0, FB_H, 0);
  g_renderer.endFrame();
}

// --once: the frame as lines of text on stdout, no terminal handling
int runOnce(const Args &a) {
  ds::aa::Converter conv;
  conv.init(a.aa);
  conv.setScreen(a.onceCols, a.onceRows, FB_W, FB_H);
  for (int i = 0; i < a.once; i++) {
    g_game.tick(0);
    g_renderer.pollEffects(g_game);
  }
  renderFrame(1.0f / 30);
  conv.convert(g_fb, FB_W, FB_H, FB_W * 2);
  const std::string text = conv.dump();
  std::fwrite(text.data(), 1, text.size(), stdout);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  Args a;
  if (!parseArgs(argc, argv, a)) return 1;

  const uint32_t seed =
      a.seed ? a.seed : (uint32_t)std::time(nullptr) ^ (uint32_t)getpid();
  g_game.reset(seed);
  g_renderer.init(FB_W, FB_H, g_arena, sizeof(g_arena));
  render::ControlHints hints;
  hints.pause = "ESC / P: RESUME   Q: QUIT   DOWN: SOUND ON / OFF";
  g_renderer.setControlHints(hints);
  if (a.level > 0) g_game.debugStartSphere(a.level, 0);
  g_game.debugAutoPlayer(a.autoPlay);
  g_game.setDebugMode(a.debug);

  if (a.once) return runOnce(a);

  loadHighScore();

  ds::aa::Converter conv;
  conv.init(a.aa);

  if (!g_term.open(a.term)) return 1;
  std::signal(SIGTERM, onSignal);
  std::signal(SIGHUP, onSignal);
  std::signal(SIGINT, onSignal);

  const int64_t frameUs = 1000000 / a.fps;
  int64_t last = nowUs(), accum = 0, lastFrame = last - frameUs, lastBeep = 0;
  int64_t lastSave = last, statsAt = last;
  int64_t renderUs = 0, convertUs = 0, writeUs = 0, outBytes = 0;
  int frames = 0, ticks = 0, dropped = 0;
  int fps = 0, tps = 0, drops = 0;
  char statsLine[160] = "";
  bool running = true;

  while (running) {
    int64_t now = nowUs();
    accum += now - last;
    last = now;
    g_term.poll(now / 1000);

    ds::term::KeyPress presses[16];
    const int np = g_term.takePresses(presses, 16);
    for (int i = 0; i < np; i++) {
      if (presses[i].repeat) continue;
      const uint32_t k = presses[i].key;
      if (k == 'q' || k == ds::term::KEY_CTRL_C)
        running = false;
      else if (k == ds::term::KEY_CTRL_L)
        conv.forceRedraw();
      else if (k >= '1' && k <= '8')
        debugKey((int)(k - '0'));
    }

    int n = 0;
    while (accum >= TICK_US && n < MAX_CATCHUP) {
      const bool wasMuted = g_game.muted();
      g_game.tick(buttonsNow(now / 1000));
      g_renderer.pollEffects(g_game);
      const uint32_t snd = g_game.sounds();
      const bool unmuted = wasMuted && !g_game.muted();
      if (a.beep && ((snd & BEEP_MASK) || unmuted) &&
          now - lastBeep >= BEEP_GAP_US) {
        g_term.write("\a", 1);
        lastBeep = now;
      }
      accum -= TICK_US;
      n++;
      ticks++;
    }
    if (accum >= TICK_US) {  // the terminal is too slow: drop the backlog
      dropped += (int)(accum / TICK_US);
      accum %= TICK_US;
    }

    if (now - lastFrame >= frameUs) {
      const float dt = (float)(now - lastFrame) / 1e6f;
      lastFrame = now;
      int cols, rows;
      g_term.size(&cols, &rows);
      conv.setScreen(cols, rows, FB_W, FB_H);
      if (g_term
              .focusLost()) { /* keys already released by the terminal layer */
      }

      const int64_t t0 = nowUs();
      renderFrame(dt);
      const int64_t t1 = nowUs();
      conv.convert(g_fb, FB_W, FB_H, FB_W * 2);
      if (a.stats) conv.overlay(0, 0, statsLine);
      const std::string &out = conv.emit();
      const int64_t t2 = nowUs();
      g_term.write(out);
      const int64_t t3 = nowUs();
      renderUs += t1 - t0;
      convertUs += t2 - t1;
      writeUs += t3 - t2;
      outBytes += (int64_t)out.size();
      frames++;
    }

    if (now - statsAt >= 1000000) {
      fps = frames;
      tps = ticks;
      drops = dropped;
      const int f = frames ? frames : 1;
      std::snprintf(
          statsLine, sizeof(statsLine),
          "%dfps %dtps drop%d %s %dx%d render%.1f conv%.1f write%.1fms %dKB/f",
          fps, tps, drops,
          g_term.keys() == ds::term::Keys::KITTY ? "kitty" : "legacy",
          conv.gridW(), conv.gridH(), renderUs / 1e3 / f, convertUs / 1e3 / f,
          writeUs / 1e3 / f, (int)(outBytes / f / 1024));
      frames = ticks = dropped = 0;
      renderUs = convertUs = writeUs = outBytes = 0;
      statsAt = now;
    }
    if (now - lastSave >= 1000000) {
      saveHighScore();
      lastSave = now;
    }

    // Sleep until the next tick or frame, whichever is first, watching stdin
    const int64_t toTick = TICK_US - accum;
    const int64_t toFrame = frameUs - (nowUs() - lastFrame);
    int64_t waitUs = toTick < toFrame ? toTick : toFrame;
    if (waitUs < 0) waitUs = 0;
    g_term.wait((int)(waitUs / 1000));
  }

  saveHighScore();
  g_term.close();
  if (a.stats) std::fprintf(stderr, "%s\n", statsLine);
  return 0;
}
