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

static constexpr int SCREEN_W = 480;
static constexpr int SCREEN_H = 320;

static uint16_t fb[SCREEN_W * SCREEN_H];  // RGB565BE
static const g2::Surface fbSurface = {g2::PixelFormat::RGB565BE, SCREEN_W,
                                      SCREEN_H, SCREEN_W * 2, fb};
static uint8_t arena[256 * 1024];
static sim::Game game;
static render::Renderer renderer;

extern "C" {

DS_EXPORT uint16_t *ds_get_fb() { return fb; }
DS_EXPORT int ds_get_width() { return SCREEN_W; }
DS_EXPORT int ds_get_height() { return SCREEN_H; }

DS_EXPORT void ds_init(uint32_t seed) {
  game.reset(seed);
  renderer.init(SCREEN_W, SCREEN_H, arena, sizeof(arena));
}

// Debug: skip the menus (level >= 1)
DS_EXPORT void ds_debug_start(int level, int weapon) {
  game.debugStartSphere(level, weapon);
}

// Debug: let the AI drive the player (attract-mode style demo)
DS_EXPORT void ds_debug_auto(int on) { game.debugAutoPlayer(on != 0); }

// One simulation tick (30 per second) with the button bits of sim::Button
DS_EXPORT void ds_tick(uint32_t buttons) { game.tick((uint8_t)buttons); }

// Render the current state into the frame buffer; dt = seconds since the
// previous render (camera smoothing)
DS_EXPORT void ds_render(float dt) {
  renderer.beginFrame(game, dt);
  renderer.renderBand(fbSurface, 0, SCREEN_H, 0);
  renderer.endFrame();
}

DS_EXPORT int ds_get_state() { return (int)game.state(); }
DS_EXPORT uint32_t ds_get_score() { return game.score(); }
// The high score is stored by the platform (browser: localStorage)
DS_EXPORT void ds_set_high_score(uint32_t v) { game.setHighScore(v); }
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
  std::fprintf(fp, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
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

// Usage: devoursphere_native [level] [script] [out.ppm] [auto] [seed]
//   level:  0 = title screen, >= 1 = play on that sphere level
//   script: comma separated "COUNTxBUTTONS" items, e.g. "5x0,1x16,300x2"
//           (BUTTONS = sim::Button bits held for COUNT ticks); a plain number
//           means that many ticks without input
//   auto:   1 = the AI drives the player
int main(int argc, char **argv) {
  int level = argc > 1 ? std::atoi(argv[1]) : 0;
  const char *script = argc > 2 ? argv[2] : "60";
  const char *path = argc > 3 ? argv[3] : "devoursphere.ppm";
  int autoPlay = argc > 4 ? std::atoi(argv[4]) : 0;
  uint32_t seed = argc > 5 ? (uint32_t)std::atoi(argv[5]) : 12345u;

  ds_init(seed);
  if (level > 0) ds_debug_start(level, 0);
  ds_debug_auto(autoPlay);

  // Expand the script into ticks
  int ticks = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (const char *p = script; *p;) {
    int count = std::atoi(p);
    uint32_t buttons = 0;
    while (*p && *p != ',' && *p != 'x') p++;
    if (*p == 'x') buttons = (uint32_t)std::atoi(++p);
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    for (int i = 0; i < count; i++) ds_tick(buttons);
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
  std::printf("state=%d tick %.3f ms, render %.3f ms\n", ds_get_state(), tickMs,
              renderMs);
  std::printf("lines=%d points=%d entities=%d kites=%d\n", st.lines, st.points,
              st.entitiesDrawn, st.kites);
  std::printf(
      "tris %d/%d (dropped %d), spans peak %d/%d (dropped %d), arena %zu/%zu\n",
      st.gfx.triCount, st.gfx.triCapacity, st.gfx.triDropped, st.gfx.spanPeak,
      st.gfx.spanCapacity, st.gfx.spanDropped, st.gfx.arenaUsed,
      st.gfx.arenaSize);
  return 0;
}

#endif
