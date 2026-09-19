// Devour Sphere: WASM front end (and a native build for quick checks).
//
// Built with Emscripten (see Makefile) it exports a small C API driven by
// docs/play/play.js. Built natively (CMake) it runs the simulation for a
// number of ticks and writes one frame as a PPM file.

#include <cstdint>
#include <cstring>

#include "devoursphere/devoursphere.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define DS_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define DS_EXPORT
#endif

namespace g2 = shapoco::gfx2d;
using namespace devoursphere;

// The frame buffer size is chosen at run time (ds_set_screen); the buffer
// itself is a static array big enough for the largest size allowed, so that
// nothing is ever allocated dynamically. The renderer derives the whole HUD
// layout from the size it is given, so any of these works.
static constexpr int DEFAULT_W = 480;
static constexpr int DEFAULT_H = 320;
static constexpr int MIN_DIM = 64;
static constexpr int MAX_DIM = 1280;
static constexpr int MAX_PIXELS = 1280 * 720;

static uint16_t fb[MAX_PIXELS];  // RGB565BE
static int screenW = DEFAULT_W;
static int screenH = DEFAULT_H;
static g2::Surface fbSurface = {g2::PixelFormat::RGB565BE, DEFAULT_W, DEFAULT_H,
                                DEFAULT_W * 2, fb};
static uint8_t arena[256 * 1024];
static sim::Game game;
static render::Renderer renderer;

static void applyScreen() {
  fbSurface = {g2::PixelFormat::RGB565BE, (int16_t)screenW, (int16_t)screenH,
               (uint32_t)(screenW * 2), fb};
  renderer.init(screenW, screenH, arena, sizeof(arena));
}

extern "C" {

DS_EXPORT uint16_t *ds_get_fb() { return fb; }
DS_EXPORT int ds_get_width() { return screenW; }
DS_EXPORT int ds_get_height() { return screenH; }
DS_EXPORT int ds_get_max_pixels() { return MAX_PIXELS; }

// Choose the frame buffer size. Returns 1 when the size was taken, 0 when it
// was rejected (too small, too large, or more pixels than the buffer holds);
// the previous size stays in effect either way. Call it before ds_init().
DS_EXPORT int ds_set_screen(int w, int h) {
  if (w < MIN_DIM || h < MIN_DIM || w > MAX_DIM || h > MAX_DIM) return 0;
  if ((long)w * h > MAX_PIXELS) return 0;
  screenW = w;
  screenH = h;
  applyScreen();
  return 1;
}

DS_EXPORT void ds_init(uint32_t seed) {
  game.reset(seed);
  applyScreen();
}

// Debug: skip the menus (level >= 1)
DS_EXPORT void ds_debug_start(int level, int weapon) {
  game.debugStartSphere(level, weapon);
}

// Debug: let the AI drive the player (attract-mode style demo)
DS_EXPORT void ds_debug_auto(int on) { game.debugAutoPlayer(on != 0); }

// Debug mode (play.js enters it with ?debug in the URL): the HUD shows
// DEBUG MODE from here on, and the number keys cheat
DS_EXPORT void ds_set_debug(int on) { game.setDebugMode(on != 0); }
// 1 Shield  2 Overdrive  3 Thruster  4 Extra Core  5 size x2  6 size x0.5
// 7 health -25%  8 health +25%
DS_EXPORT void ds_debug_key(int key) {
  if (!game.debugMode()) return;
  switch (key) {
    case 1: game.debugTakeUpgrade(sim::UpgradeKind::SHIELD); break;
    case 2: game.debugTakeUpgrade(sim::UpgradeKind::OVERDRIVE); break;
    case 3: game.debugTakeUpgrade(sim::UpgradeKind::THRUSTER); break;
    case 4: game.debugTakeUpgrade(sim::UpgradeKind::EXTRA_CORE); break;
    case 5: game.debugScaleSize(true); break;
    case 6: game.debugScaleSize(false); break;
    case 7: game.debugHeal(-25); break;
    case 8: game.debugHeal(25); break;
    default: break;
  }
}

// One simulation tick with the button bits of sim::Button (LEFT=1, RIGHT=2,
// UP=4, DOWN=8, A=16, PAUSE=32)
DS_EXPORT void ds_tick(uint32_t buttons) {
  game.tick((uint8_t)buttons);
  // This front end renders after every tick, so ds_render() would pick the
  // effects up anyway; doing it here keeps both front ends on the same rule
  // (every tick is polled, whether or not a frame follows it).
  renderer.pollEffects(game);
}

// Render the current state into the frame buffer; dt = seconds since the
// previous render (camera smoothing)
DS_EXPORT void ds_render(float dt) {
  renderer.beginFrame(game, dt);
  renderer.renderBand(fbSurface, 0, screenH, 0);
  renderer.endFrame();
}

DS_EXPORT int ds_get_state() { return (int)game.state(); }
// Sound effects requested by the last tick, one bit per sim::SoundKind
// (cleared every tick: read it right after ds_tick()). The waveforms and
// the playback are the platform's (play.js, docs/play/se.bin)
DS_EXPORT uint32_t ds_get_sounds() { return game.sounds(); }
// The mute is a setting of the game (DOWN on the title or the pause screen
// toggles it too); the platform keeps it across sessions
DS_EXPORT void ds_set_muted(int on) { game.setMuted(on != 0); }
DS_EXPORT int ds_get_muted() { return game.muted() ? 1 : 0; }
DS_EXPORT int ds_get_paused() { return game.paused() ? 1 : 0; }
DS_EXPORT uint32_t ds_get_score() { return game.score(); }
DS_EXPORT int ds_get_sphere_level() { return game.sphereLevel(); }
// The high score (and the sphere reached in that run) is stored by the
// platform (browser: localStorage) together with the major version; a
// record from another major is dropped
DS_EXPORT void ds_set_high_score(uint32_t v, int sphere) {
  game.setHighScore(v, sphere);
}
// The game decides whether the score counts (the title demo's does not):
// 1 when the high score just changed and the platform should store it
DS_EXPORT int ds_keep_high_score() { return game.keepHighScore() ? 1 : 0; }
DS_EXPORT uint32_t ds_get_high_score() { return game.highScore(); }
DS_EXPORT int ds_get_high_score_sphere() { return game.highScoreSphere(); }
DS_EXPORT int ds_get_version_major() { return sim::VERSION_MAJOR; }
DS_EXPORT int ds_get_tick_rate() { return sim::TICK_RATE; }

}  // extern "C"

// ---------------------------------------------------------------------------
// Native entry point

#ifndef __EMSCRIPTEN__

#include <chrono>
#include <cstdio>
#include <cstdlib>

static void writePpm(const char *path) {
  FILE *fp = std::fopen(path, "wb");
  if (!fp) {
    std::perror(path);
    return;
  }
  std::fprintf(fp, "P6\n%d %d\n255\n", screenW, screenH);
  for (int i = 0; i < screenW * screenH; i++) {
    uint16_t p = g2::bswap16(fb[i]);
    uint8_t rgb[3] = {
        (uint8_t)(((p >> 11) & 31) * 255 / 31),
        (uint8_t)(((p >> 5) & 63) * 255 / 63),
        (uint8_t)((p & 31) * 255 / 31),
    };
    std::fwrite(rgb, 1, 3, fp);
  }
  std::fclose(fp);
  std::printf("wrote %s\n", path);
}

// Usage: devoursphere_native [level] [script] [out.ppm] [auto] [seed] [WxH]
//   level:  0 = title screen, >= 1 = play on that sphere level
//   script: comma separated "COUNTxBUTTONS" items, e.g. "5x0,1x16,300x2"
//           (BUTTONS = sim::Button bits held for COUNT ticks); a plain number
//           means that many ticks without input
//   auto:   1 = the AI drives the player
//   WxH:    frame buffer size (default 480x320)
int main(int argc, char **argv) {
  int level = argc > 1 ? std::atoi(argv[1]) : 0;
  const char *script = argc > 2 ? argv[2] : "60";
  const char *path = argc > 3 ? argv[3] : "devoursphere.ppm";
  int autoPlay = argc > 4 ? std::atoi(argv[4]) : 0;
  uint32_t seed = argc > 5 ? (uint32_t)std::atoi(argv[5]) : 12345u;
  if (argc > 6) {
    int w = std::atoi(argv[6]);
    const char *x = std::strchr(argv[6], 'x');
    int h = x ? std::atoi(x + 1) : 0;
    if (!ds_set_screen(w, h)) {
      std::printf("bad size \"%s\" (%d..%d per axis, %d pixels at most)\n",
                  argv[6], MIN_DIM, MAX_DIM, MAX_PIXELS);
      return 1;
    }
  }

  ds_init(seed);
  if (level > 0) ds_debug_start(level, 0);
  ds_debug_auto(autoPlay);

  // Expand the script into ticks, counting the sound requests per kind
  static const char *const SOUND_NAMES[sim::SOUND_KINDS] = {
      "shot_vulcan", "shot_laser",   "shot_missile",       "hit_enemy",
      "hit_player",  "enemy_killed_small", "enemy_killed_big", "player_killed",
      "get_fragment", "get_upgrade", "menu_select",        "menu_start",
      "launch",       "arrive"};
  int soundCounts[sim::SOUND_KINDS] = {};
  int ticks = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (const char *p = script; *p;) {
    int count = std::atoi(p);
    uint32_t buttons = 0;
    while (*p && *p != ',' && *p != 'x') p++;
    if (*p == 'x') buttons = (uint32_t)std::atoi(++p);
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    for (int i = 0; i < count; i++) {
      ds_tick(buttons);
      uint32_t s = ds_get_sounds();
      for (int k = 0; k < sim::SOUND_KINDS; k++) soundCounts[k] += (s >> k) & 1;
    }
    ticks += count;
  }
  auto t1 = std::chrono::steady_clock::now();
  const int RENDERS = 20;
  for (int i = 0; i < RENDERS; i++) ds_render(1.0f / 30);
  auto t2 = std::chrono::steady_clock::now();
  writePpm(path);

  double tickMs = std::chrono::duration<double, std::milli>(t1 - t0).count() /
                  (ticks ? ticks : 1);
  double renderMs =
      std::chrono::duration<double, std::milli>(t2 - t1).count() / RENDERS;
  render::RenderStats st = renderer.stats();
  std::printf("%dx%d state=%d tick %.3f ms, render %.3f ms\n", screenW, screenH,
              ds_get_state(), tickMs, renderMs);
  std::printf("lines=%d points=%d entities=%d kites=%d\n", st.lines, st.points,
              st.entitiesDrawn, st.kites);
  std::printf("sounds:");
  for (int k = 0; k < sim::SOUND_KINDS; k++) {
    if (soundCounts[k]) std::printf(" %s=%d", SOUND_NAMES[k], soundCounts[k]);
  }
  std::printf("\n");
  std::printf(
      "tris %d in %zu/%zu B (dropped %d), layers %d (dropped %d), "
      "spans peak %d/%d (dropped %d), arena %zu/%zu\n",
      st.gfx.triCount, st.gfx.triBytes, st.gfx.triBytesTotal, st.gfx.triDropped,
      st.gfx.layerCount, st.gfx.layersDropped, st.gfx.spanPeak,
      st.gfx.spanCapacity, st.gfx.spanDropped, st.gfx.arenaUsed,
      st.gfx.arenaSize);
  return 0;
}

#endif
