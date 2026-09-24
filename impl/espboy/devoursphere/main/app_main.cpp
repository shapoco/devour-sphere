// Devour Sphere on the ESPboy (ESP8266).
//
// The game core (core/) is shared with every other front end; everything
// here is the platform layer: the clock, the buttons, the frame loop, the
// band buffer and the memory the core is given. The panel is in
// display.cpp, the I2C devices in input.cpp, the speaker in
// tone_player.cpp. One core and one task: the ticks, the scene and the
// bands run in sequence, as the PicoSystem build does with
// DS_SIM_ON_CORE1=0.
//
// Nothing here is allocated on the stack. The Game and the Renderer are
// statics in .bss; the task's stack is 8 KB (sdkconfig.defaults).

#include <esp_heap_caps.h>
#include <esp_log.h>
extern "C" {
#include <esp_task_wdt.h>  // no extern "C" of its own in this SDK
}
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "devoursphere/devoursphere.hpp"
#include "devoursphere/profile.hpp"
#include "display.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"
#include "high_score_store.hpp"
#include "input.hpp"
#include "profiler.hpp"
#include "tone_player.hpp"

namespace sim = devoursphere::sim;
namespace render = devoursphere::render;
namespace g2 = shapoco::gfx2d;

namespace {

constexpr const char *TAG = "devoursphere";

sim::Game g_game;
render::Renderer g_renderer;
ds::HighScoreStore g_store;  // the high score in flash
alignas(8) uint8_t g_arena[ds::ARENA_SIZE];
ds::Display g_display;
ds::Profiler g_prof;

// One band buffer, RGB565_SWAPPED (see display.hpp for why not two)
alignas(4) uint16_t g_band[ds::SCREEN_W * ds::BAND_H];

// Frame pacing: the simulation steps at a fixed rate, the frame rate is
// whatever the device manages.
uint64_t g_lastUs = 0;
uint32_t g_accUs = 0;
uint8_t g_prevButtons = 0;

// The buttons the ESPboy has, in the bits the simulation takes: A (ACT)
// fires, B (ESC) is the emergency dodge, the left shoulder pauses. The
// right shoulder cycles the timing overlay on the title and pause screens
// (frame()).
uint8_t mapButtons(uint8_t b) {
  uint8_t out = 0;
  if (b & ds::input::LEFT) out |= sim::Button::LEFT;
  if (b & ds::input::RIGHT) out |= sim::Button::RIGHT;
  if (b & ds::input::UP) out |= sim::Button::UP;
  if (b & ds::input::DOWN) out |= sim::Button::DOWN;
  if (b & ds::input::ACT) out |= sim::Button::A;
  if (b & ds::input::ESC) out |= sim::Button::B;
  if (b & ds::input::LFT) out |= sim::Button::PAUSE;
  return out;
}

// How many ticks the clock owes us, taken out of the accumulator
int ticksDue() {
  int n = 0;
  while (g_accUs >= ds::TICK_US && n < ds::MAX_CATCHUP) {
    g_accUs -= ds::TICK_US;
    n++;
  }
  if (g_accUs >= ds::TICK_US) g_accUs = ds::TICK_US - 1;  // drop the surplus
  return n;
}

// --- Phase breakdown on the overlay (the PicoSystem's lines) --------------
char *putStr(char *p, char *end, const char *s) {
  while (*s && p < end) *p++ = *s++;
  return p;
}

char *putMs1(char *p, char *end, uint32_t us) {
  char tmp[12];
  uint32_t v = us / 1000;
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v && n < (int)sizeof(tmp));
  while (n > 0 && p < end) *p++ = tmp[--n];
  if (p < end) *p++ = '.';
  if (p < end) *p++ = (char)('0' + (us / 100) % 10);
  return p;
}

void phaseLine(char *line, const char *labels, const uint32_t *us, int count,
               uint32_t div) {
  char *p = line, *end = line + ds::Profiler::COLS;
  for (int i = 0; i < count; i++) {
    if (i > 0 && p < end) *p++ = ' ';
    if (p < end) *p++ = labels[i];
    p = putMs1(p, end, div ? us[i] / div : us[i]);
  }
  *p = '\0';
}

uint32_t g_overlayUs = 0;  // drawing the overlay itself, per frame
uint32_t g_xferUs = 0;     // pushing the bands, per frame

void updatePhaseLines(int ticks) {
  if (!g_prof.full()) {
    for (auto &l : g_prof.extra) l[0] = '\0';
    return;
  }
  const uint32_t *tp = g_game.tickProfile().us;
  const uint32_t *fp = g_renderer.frameProfile().us;
  const uint32_t div = ticks > 0 ? (uint32_t)ticks : 1;
  // Three lines, not the PicoSystem's five: with the panel's eight the
  // 128 px screen holds eleven (26 + 11 x 9 = 125), and the smaller
  // phases (bullets, fragments, effects, the collisions) are folded into
  // the last letter of each line
  const uint32_t tick[4] = {
      tp[sim::Game::TP_AI], tp[sim::Game::TP_MOVE], tp[sim::Game::TP_LAYOUT],
      tp[sim::Game::TP_FIRE] + tp[sim::Game::TP_BULLETS] +
          tp[sim::Game::TP_FRAGMENTS] + tp[sim::Game::TP_EATING] +
          tp[sim::Game::TP_COLLISIONS] + tp[sim::Game::TP_ORDERS] +
          tp[sim::Game::TP_OTHER]};
  const uint32_t scene[4] = {
      fp[render::FP_SPHERE], fp[render::FP_ENTITIES],
      fp[render::FP_FRAGMENTS] + fp[render::FP_BULLETS] +
          fp[render::FP_EFFECTS_DRAW] + fp[render::FP_OVERLAYS],
      fp[render::FP_CAMERA] + fp[render::FP_EFFECTS] + fp[render::FP_SORT]};
  const uint32_t bands[4] = {fp[render::FP_BAND_3D], fp[render::FP_BAND_2D],
                             g_overlayUs, g_xferUs};
  phaseLine(g_prof.extra[0], "AMLX", tick, 4, div);  // per tick
  phaseLine(g_prof.extra[1], "SOHX", scene, 4, 0);   // beginFrame
  phaseLine(g_prof.extra[2], "DUVT", bands, 4, 0);   // the bands, T the push
  g_prof.extra[3][0] = '\0';
  g_prof.extra[4][0] = '\0';
}

// B held for three seconds on the title (core/SPEC.md "ベンチマーク"): the
// results on the screen and on the serial console
render::Benchmark g_bench;
void benchLog(const char *line) { printf("%s\n", line); }

uint32_t clockUs() { return (uint32_t)ds::nowUs(); }
void updateProfileClock() {
  devoursphere::profileClockUs =
      g_prof.full() || g_bench.active() ? clockUs : nullptr;
}

// --- Frames ---------------------------------------------------------------
g2::Surface bandSurface() {
  return {g2::PixelFormat::RGB565_SWAPPED, (int16_t)ds::SCREEN_W, (int16_t)ds::BAND_H,
          (uint32_t)(ds::SCREEN_W * 2), g_band};
}

// A whole screen's worth of band pushes with nothing else running: 128x128x2
// bytes at 26.7 MHz should take 9.8 ms. Microseconds.
uint32_t measureTransfer() {
  const uint32_t t0 = clockUs();
  for (int b = 0; b < ds::BAND_COUNT; b++) {
    g_display.write(b * ds::BAND_H, ds::SCREEN_W, ds::BAND_H, g_band);
  }
  return clockUs() - t0;
}

void drawFrame(int ticks) {
  const uint32_t t0 = clockUs();
  g_renderer.resetFrameProfile();
  // Simulation time, not wall time: the camera smoothing, the debris and
  // the score roll-up stay in step with the ticks that actually ran
  g_renderer.beginFrame(g_game, g_bench.dt(ticks));
  g_prof.beginUs = clockUs() - t0;
}

// Rasterize the frame band by band into the one buffer and push each band
// before the next is drawn. Nothing overlaps: the push is the CPU's.
void presentFrame() {
  g_overlayUs = 0;
  g_xferUs = 0;
  g_prof.rasterUs = 0;
  g_prof.dmaWaitUs = 0;
  g_prof.cmdUs = 0;
  for (int b = 0; b < ds::BAND_COUNT; b++) {
    const int y = b * ds::BAND_H;
    const uint32_t t = clockUs();
    g_renderer.renderBand(bandSurface(), y, ds::BAND_H, 0);
    const uint32_t t1 = clockUs();
    g_prof.drawOverlay(bandSurface(), y);
    const uint32_t t2 = clockUs();
    g_overlayUs += t2 - t1;
    g_prof.rasterUs += t2 - t;
    g_display.write(y, ds::SCREEN_W, ds::BAND_H, g_band);
    g_xferUs += clockUs() - t2;
  }
  // The overlay's DMA line is the push here (there is no DMA)
  g_prof.dmaWaitUs = g_xferUs;
  g_renderer.endFrame();
  g_prof.endFrame(ds::nowUs(), g_renderer.stats());
}

void frame() {
  const uint64_t nowUs = ds::nowUs();
  uint64_t deltaUs = nowUs - g_lastUs;
  g_lastUs = nowUs;
  if (deltaUs > (uint64_t)ds::MAX_CATCHUP * ds::TICK_US) {
    deltaUs = (uint64_t)ds::MAX_CATCHUP * ds::TICK_US;
  }
  g_accUs += (uint32_t)deltaUs;

  // Read once per frame; the simulation derives press and release edges
  // from the held state. The right shoulder on the title or the pause
  // screen cycles the overlay.
  const uint8_t raw = ds::input::read();
  // What the game gets: nothing while the benchmark runs or shows its results
  const uint8_t buttons = g_bench.input(mapButtons(raw));
  const render::HudState &hud = g_renderer.hud();
  if ((raw & ds::input::RGT) && !(g_prevButtons & ds::input::RGT) &&
      (hud.state == sim::GameState::TITLE || hud.paused)) {
    g_prof.toggle();
  }
  g_prevButtons = raw;

  const uint32_t tick0 = clockUs();
  const int ticks = g_bench.ticks(ticksDue());
  for (int i = 0; i < ticks; i++) {
    g_game.tick(buttons);
    // The events of a tick are cleared by the next one, so each tick has
    // to be polled or the frame would only show the last one's explosions
    g_renderer.pollEffects(g_game);
    audio::setMuted(g_game.muted());
    audio::request(g_game.sounds());
  }
  audio::poll();
  g_prof.tickUs = clockUs() - tick0;
  g_prof.ticks = ticks;
  g_prof.core1WaitUs = 0;
  if (ticks == 0) return;  // ahead of the simulation: nothing new to show
  // The Game decides whether the score counts (not the title demo's); the
  // store decides when it reaches the flash (once per run, on the game
  // over screen, after the death sound)
  g_game.keepHighScore();
  g_store.poll(g_game);
  updatePhaseLines(ticks);
  g_bench.beforeFrame(g_game, g_renderer, ds::benchSample(g_prof));
  g_game.resetTickProfile();
  updateProfileClock();
  drawFrame(ticks);
  presentFrame();
}

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(TAG, "Devour Sphere: DRAM free %u, IRAM free %u (32-bit heap)",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_32BIT) -
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
  ESP_LOGI(TAG, "Game %u B, Renderer %u B, arena %u B, band %u B",
           (unsigned)sizeof(g_game), (unsigned)sizeof(g_renderer),
           (unsigned)sizeof(g_arena), (unsigned)sizeof(g_band));

  ds::input::init();  // pulls the panel's chip select low
  g_display.init();
  audio::init(audio::Config{ds::PIN_SPEAKER, audio::Pacing::PWM_WRAP, false});

  g_game.reset(ds::randomSeed());
  g_store.load(g_game);
  g_renderer.setControlHints(render::ControlHints{
      "MOVE: D-PAD   A: FIRE   B: DODGE   L: PAUSE",
      "D-PAD: MOVE  A: FIRE  B: DODGE",
      render::ControlHints{}.dash,
      render::ControlHints{}.dashAlt,
      "L: RESUME    DOWN: SOUND    R: STATS",
      "L: RESUME   DOWN: SOUND",
  });
  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena),
                  ds::SPAN_CAPACITY);
  g_renderer.setDetailTriangles(ds::DETAIL_TRIANGLES);
  g_bench.setPlatform("ESPBOY", benchLog);

  ds::stackWatchInit();
  g_prof.xferUs = measureTransfer();
  ESP_LOGI(TAG, "screen push %u us, DRAM free now %u", (unsigned)g_prof.xferUs,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
  ds::input::setBacklight(4095);

  // Start owing one tick, so the first loop has something to do instead of
  // waiting for the clock to move
  g_lastUs = ds::nowUs();
  g_accUs = ds::TICK_US;

  // The idle task never gets the core (the loop never blocks), so the task
  // watchdog is fed here, once a frame
  for (;;) {
    frame();
    esp_task_wdt_reset();
  }
}
