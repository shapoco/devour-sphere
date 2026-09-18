// Devour Sphere on the PicoSystem (Pimoroni, RP2040).
//
// The game core (core/) is shared with the WASM and Xiamocon front ends;
// everything here is the platform layer: the clock, the buttons, the frame
// loop, the band buffers and the memory the core is given. The display is in
// display.cpp. The frame loop is the Xiamocon one (impl/xiamocon/): core1
// runs the simulation in batches while core0 builds the scene, rasterizes
// the bands and pushes them.
//
// Nothing here is allocated on a stack. The Game is 95 KB, and the RP2040
// gives each core 4 KB in SCRATCH_X / SCRATCH_Y.

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/vreg.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>

#include "devoursphere/devoursphere.hpp"
#include "devoursphere/profile.hpp"
#include "display.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"
#include "profiler.hpp"

namespace sim = devoursphere::sim;
namespace render = devoursphere::render;
namespace g2 = shapoco::gfx2d;

namespace {

sim::Game g_game;
render::Renderer g_renderer;
uint8_t g_arena[ds::ARENA_SIZE];
ds::Display g_display;
ds::Profiler g_prof;

// Two band buffers, RGB565BE. Alignment for the 16-bit DMA reads; static
// because every byte of SRAM is DMA capable on this chip.
alignas(4) uint16_t g_bands[2][ds::SCREEN_W * ds::BAND_H];
int g_bandCur = 0;  // free running across frames (see present())

// Frame pacing: the simulation steps at a fixed rate, the frame rate is
// whatever the device manages.
uint64_t g_lastUs = 0;
uint32_t g_accUs = 0;

// --- Buttons -------------------------------------------------------------
// Active low with the internal pull-ups, as the SDK wires them. Y is not a
// game button here: it toggles the timing overlay, the job FUNC does on
// Xiamocon.
constexpr uint BTN_PINS[] = {
    PICOSYSTEM_SW_UP_PIN,    PICOSYSTEM_SW_DOWN_PIN, PICOSYSTEM_SW_LEFT_PIN,
    PICOSYSTEM_SW_RIGHT_PIN, PICOSYSTEM_SW_A_PIN,    PICOSYSTEM_SW_B_PIN,
    PICOSYSTEM_SW_X_PIN,     PICOSYSTEM_SW_Y_PIN,
};
uint32_t g_prevGpio = ~0u;

void initButtons() {
  for (uint pin : BTN_PINS) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
  }
}

bool down(uint32_t gpio, uint pin) { return (gpio & (1u << pin)) == 0; }

// The buttons the PicoSystem has, in the five bits the simulation takes. The
// three remaining face buttons fire, so the thumb does not have to find a
// particular one.
uint8_t mapButtons(uint32_t gpio) {
  uint8_t out = 0;
  if (down(gpio, PICOSYSTEM_SW_LEFT_PIN)) out |= sim::Button::LEFT;
  if (down(gpio, PICOSYSTEM_SW_RIGHT_PIN)) out |= sim::Button::RIGHT;
  if (down(gpio, PICOSYSTEM_SW_UP_PIN)) out |= sim::Button::UP;
  if (down(gpio, PICOSYSTEM_SW_DOWN_PIN)) out |= sim::Button::DOWN;
  if (down(gpio, PICOSYSTEM_SW_A_PIN) || down(gpio, PICOSYSTEM_SW_B_PIN) ||
      down(gpio, PICOSYSTEM_SW_X_PIN)) {
    out |= sim::Button::A;
  }
  return out;
}

// The RGB LED is active high (the SDK drives it by PWM level) and floats at
// power-up: park it off
void initLed() {
  constexpr uint LED_PINS[] = {PICOSYSTEM_LED_R_PIN, PICOSYSTEM_LED_G_PIN,
                               PICOSYSTEM_LED_B_PIN};
  for (uint pin : LED_PINS) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
  }
}

// --- The two cores ---------------------------------------------------------
#if DS_SIM_ON_CORE1

// Which core touches what, while a batch is running:
//   core0  the arena and the scene built into it, the band buffers, the
//          display, and the Renderer's HUD snapshot / gauges / markers
//   core1  the Game, and the Renderer's effect state through pollEffects()
//          (debris, pickups, the effect rng) -- none of which renderBand()
//          reads, now that the HUD draws from the snapshot beginFrame takes.
//
// beginFrame() reads the whole Game, so it runs between collecting one batch
// and asking for the next, with core1 idle.
enum class Sim : uint32_t {
  IDLE,  // no batch outstanding; the Game is core0's
  RUN,   // core1 is ticking
  DONE,  // the batch finished and core0 has not collected it yet
};

// One word, read and written with explicit acquire/release: the payload
// below is written before the state is published and read after it is seen.
uint32_t g_sim = (uint32_t)Sim::IDLE;
int g_simWanted = 0;       // written before RUN is published
uint8_t g_simButtons = 0;  // ... same
int g_simRan = 0;          // written before DONE is published
uint32_t g_simTickUs = 0;  // ... same

uint64_t g_waitFromUs = 0;  // when core0 first found the batch running
bool g_waiting = false;

Sim simLoad() { return (Sim)__atomic_load_n(&g_sim, __ATOMIC_ACQUIRE); }
void simStore(Sim s) {
  __atomic_store_n(&g_sim, (uint32_t)s, __ATOMIC_RELEASE);
}

void core1Main() {
  ds::stackWatchInitCore1();
  for (;;) {
    if (simLoad() != Sim::RUN) {
      tight_loop_contents();
      continue;
    }
    const uint32_t t0 = (uint32_t)time_us_64();
    for (int i = 0; i < g_simWanted; i++) {
      g_game.tick(g_simButtons);
      // The events of a tick are cleared by the next one, so each tick has
      // to be polled or the frame would only show the last one's explosions
      g_renderer.pollEffects(g_game);
    }
    g_simTickUs = (uint32_t)time_us_64() - t0;
    g_simRan = g_simWanted;
    simStore(Sim::DONE);
  }
}

#endif  // DS_SIM_ON_CORE1

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

// Nothing is written to flash, but the high score still survives a restart
// for as long as the power is on, because Game::reset() leaves it alone.
// Only safe while the Game belongs to this core.
void keepHighScore() {
  if (g_game.score() > g_game.highScore()) {
    g_game.setHighScore(g_game.score());
  }
}

// --- Phase breakdown on the overlay ---------------------------------------
// Integer formatting only (see profiler.cpp for why not printf)
char *putStr(char *p, char *end, const char *s) {
  while (*s && p < end) *p++ = *s++;
  return p;
}

// Microseconds as "x.y" milliseconds
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

// Label + value pairs into one overlay line
void phaseLine(char *line, const char *head, const char *labels,
               const uint32_t *us, int count, uint32_t div) {
  char *p = line, *end = line + ds::Profiler::COLS;
  p = putStr(p, end, head);
  for (int i = 0; i < count; i++) {
    if (i > 0 && p < end) *p++ = ' ';
    if (p < end) *p++ = labels[i];
    p = putMs1(p, end, div ? us[i] / div : us[i]);
  }
  *p = '\0';
}

// The tick profile is per tick (the batch total divided by its ticks); the
// frame profile is per frame. Both are drawn one frame late, like everything
// on the overlay. Letters: see ../SPEC.md "デバッグ表示".
uint32_t g_overlayUs = 0;     // drawing the overlay itself, per frame
bool g_benchPending = false;  // run Renderer::benchPrimitives() next frame

void updatePhaseLines(int ticks) {
  if (!g_prof.on()) {
    for (auto &l : g_prof.extra) l[0] = '\0';
    return;
  }
  const uint32_t *tp = g_game.tickProfile().us;
  const uint32_t *fp = g_renderer.frameProfile().us;
  const uint32_t div = ticks > 0 ? (uint32_t)ticks : 1;
  const uint32_t tickRest[4] = {
      tp[sim::Game::TP_BULLETS], tp[sim::Game::TP_FRAGMENTS],
      tp[sim::Game::TP_EATING],
      tp[sim::Game::TP_COLLISIONS] + tp[sim::Game::TP_ORDERS] +
          tp[sim::Game::TP_OTHER]};
  const uint32_t rest[4] = {fp[render::FP_FRAGMENTS], fp[render::FP_BULLETS],
                            fp[render::FP_EFFECTS_DRAW],
                            fp[render::FP_OVERLAYS]};
  const uint32_t scene[4] = {
      fp[render::FP_SPHERE], fp[render::FP_ENTITIES],
      rest[0] + rest[1] + rest[2] + rest[3],
      fp[render::FP_CAMERA] + fp[render::FP_EFFECTS] + fp[render::FP_SORT]};
  const uint32_t bands[3] = {fp[render::FP_BAND_3D], fp[render::FP_BAND_2D],
                             g_overlayUs};
  phaseLine(g_prof.extra[0], "", "AMLF", tp, 4, div);  // per tick
  phaseLine(g_prof.extra[1], "", "BFKX", tickRest, 4, div);
  phaseLine(g_prof.extra[2], "", "SOHX", scene, 4, 0);  // beginFrame
  phaseLine(g_prof.extra[3], "", "FBEM", rest, 4, 0);   // H's parts
  phaseLine(g_prof.extra[4], "", "DUV", bands, 3, 0);   // the bands
}

// The phase clock costs a timer read per phase and entity, so it is only
// installed while the overlay is on. Switched between batches, when the
// Game belongs to this core.
uint32_t clockUs() { return (uint32_t)time_us_64(); }
void updateProfileClock() {
  devoursphere::profileClockUs = g_prof.on() ? clockUs : nullptr;
}

// --- Frames ---------------------------------------------------------------
g2::Surface bandSurface(int i) {
  return {g2::PixelFormat::RGB565BE, (int16_t)ds::SCREEN_W, (int16_t)ds::BAND_H,
          (uint32_t)(ds::SCREEN_W * 2), g_bands[i]};
}

// Time a whole screen's worth of band transfers with nothing else running:
// 240x240x2 bytes at 62.5 MHz should take 14.75 ms; anything more is
// per-band command overhead or a starved DMA. Returns microseconds.
uint32_t measureTransfer() {
  g_display.complete();
  const uint32_t t0 = (uint32_t)time_us_64();
  for (int b = 0; b < ds::BAND_COUNT; b++) {
    g_display.writeStart(b * ds::BAND_H, ds::SCREEN_W, ds::BAND_H, g_bands[0]);
    g_display.complete();
  }
  return (uint32_t)time_us_64() - t0;
}

// Draw the state the simulation has reached, and push it a band at a time
void drawFrame(int ticks) {
  const uint32_t begin0 = (uint32_t)time_us_64();
  g_renderer.resetFrameProfile();
  // Simulation time, not wall time: the renderer advances the camera
  // smoothing, the debris and the score roll-up by dt, and those have to
  // stay in step with the ticks that actually ran.
  g_renderer.beginFrame(g_game, ticks * (1.0f / sim::TICK_RATE));
  g_prof.beginUs = (uint32_t)time_us_64() - begin0;
}

// Rasterize the frame band by band into the buffer the previous band is not
// using, so drawing band N+1 overlaps the transfer of band N. The buffer
// index runs free across frames: the last band of a frame is still in
// flight when the next frame starts.
void presentFrame() {
  g_overlayUs = 0;
  g_prof.rasterUs = 0;
  g_prof.dmaWaitUs = 0;
  g_prof.cmdUs = 0;
  for (int b = 0; b < ds::BAND_COUNT; b++) {
    const int y = b * ds::BAND_H;
    const int idx = g_bandCur;
    g_bandCur ^= 1;
    const uint32_t t = (uint32_t)time_us_64();
    g_renderer.renderBand(bandSurface(idx), y, ds::BAND_H, 0);
    const uint32_t t1 = (uint32_t)time_us_64();
    g_prof.drawOverlay(bandSurface(idx), y);
    const uint32_t t2 = (uint32_t)time_us_64();
    g_overlayUs += t2 - t1;
    g_prof.rasterUs += t2 - t;
    // Waits for the previous band if we outran the display, then queues
    // this one; the two halves are timed separately
    g_display.complete();
    const uint32_t t3 = (uint32_t)time_us_64();
    g_prof.dmaWaitUs += t3 - t2;
    g_display.writeStart(y, ds::SCREEN_W, ds::BAND_H, g_bands[idx]);
    g_prof.cmdUs += (uint32_t)time_us_64() - t3;
  }
  // The last band is deliberately left in flight: it overlaps the next
  // frame's ticks and beginFrame(). The next writeStart() collects it.
  g_renderer.endFrame();
  g_prof.endFrame(time_us_64(), g_renderer.stats());
}

void frame() {
  const uint64_t nowUs = time_us_64();
  uint64_t deltaUs = nowUs - g_lastUs;
  g_lastUs = nowUs;
  // A long stall must not become a burst of simulation, so the elapsed time
  // is clamped before it reaches the accumulator as well as after
  if (deltaUs > (uint64_t)ds::MAX_CATCHUP * ds::TICK_US) {
    deltaUs = (uint64_t)ds::MAX_CATCHUP * ds::TICK_US;
  }
  g_accUs += (uint32_t)deltaUs;

  // Read once per frame; the simulation derives press and release edges
  // from the held state
  const uint32_t gpio = gpio_get_all();
  const uint8_t buttons = mapButtons(gpio);
  if (down(gpio, PICOSYSTEM_SW_Y_PIN) &&
      !down(g_prevGpio, PICOSYSTEM_SW_Y_PIN)) {
    g_prof.toggle();
    g_benchPending = g_prof.on();
  }
  g_prevGpio = gpio;

#if DS_SIM_ON_CORE1
  // If core1 is still ticking, come back next time round rather than
  // spinning here, so the wait is visible in the profile
  if (simLoad() == Sim::RUN) {
    if (!g_waiting) {
      g_waiting = true;
      g_waitFromUs = nowUs;
    }
    return;
  }

  int ran = 0;
  if (simLoad() == Sim::DONE) {
    ran = g_simRan;
    g_prof.tickUs = g_simTickUs;
    g_prof.ticks = ran;
    g_prof.core1WaitUs = g_waiting ? (uint32_t)(nowUs - g_waitFromUs) : 0;
    g_waiting = false;
    simStore(Sim::IDLE);  // the Game is ours until we ask for the next batch
  }

  // Build the scene from the state that batch left behind. Nothing else may
  // touch the Game here.
  if (ran > 0) {
    keepHighScore();
    updatePhaseLines(ran);  // the batch just collected, and the last frame
    g_game.resetTickProfile();
    updateProfileClock();
    if (g_benchPending) {
      // Once per switch-on of the overlay, here because the clock is
      // installed and the Game is ours (nothing of the bench reaches it)
      g_renderer.benchPrimitives(g_prof.benchUs);
      g_benchPending = false;
    }
    drawFrame(ran);
  }

  // Ask for the next batch before rasterizing, so core1 ticks while we do.
  // This is what costs a frame of latency: these buttons reach the screen
  // one frame later.
  const int want = ticksDue();
  if (want > 0) {
    g_simWanted = want;
    g_simButtons = buttons;
    simStore(Sim::RUN);
  }

  if (ran > 0) presentFrame();
#else
  // Everything on this core. Nothing overlaps the ticks, so this is slower.
  const uint32_t tick0 = (uint32_t)time_us_64();
  const int ticks = ticksDue();
  for (int i = 0; i < ticks; i++) {
    g_game.tick(buttons);
    g_renderer.pollEffects(g_game);
  }
  g_prof.tickUs = (uint32_t)time_us_64() - tick0;
  g_prof.ticks = ticks;
  g_prof.core1WaitUs = 0;
  if (ticks == 0) return;  // ahead of the simulation: nothing new to show
  keepHighScore();
  updatePhaseLines(ticks);
  g_game.resetTickProfile();
  updateProfileClock();
  drawFrame(ticks);
  presentFrame();
#endif
}

}  // namespace

int main() {
#if DS_OVERCLOCK
  // What the PicoSystem SDK does: a modest overvolt, then 250 MHz. The board
  // file's flash divider (2) keeps the QSPI at 125 MHz, which the part
  // tolerates -- the SDK ships this way.
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(10);
  set_sys_clock_khz(ds::SYS_CLOCK_KHZ, true);
#endif
  // pico-sdk 2.x moves clk_peri to the 48 MHz USB PLL whenever the system
  // clock is changed (set_sys_clock_pll(), unless
  // PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK is set), so that the UART
  // keeps its baud rate. The SPI is clocked from clk_peri too, and 48 MHz
  // caps it at 24 MHz: the first board showed "P48 SPI24.0" and a 42.5 ms
  // full-screen transfer. Put clk_peri back on clk_sys before the display is
  // brought up; spi_init() derives its divider from clk_peri at that point.
  clock_configure_undivided(clk_peri, 0,
                            CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                            clock_get_hz(clk_sys));

  initLed();
  initButtons();
  g_display.init();

  g_game.reset(ds::randomSeed());
  g_renderer.setControlHints(render::ControlHints{
      "MOVE: D-PAD    A/B/X: FIRE",
      "D-PAD: MOVE   A: FIRE",
      render::ControlHints{}.dash,
      render::ControlHints{}.dashAlt,
  });
  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena),
                  ds::SPAN_CAPACITY);
  g_renderer.setDetailTriangles(ds::DETAIL_TRIANGLES);

  ds::stackWatchInitCore0();
  // Both cores are otherwise idle here, so this is the transfer on its own
  g_prof.xferUs = measureTransfer();

  // Start owing one tick, so the first loop has something to do instead of
  // waiting for the clock to move
  g_lastUs = time_us_64();
  g_accUs = ds::TICK_US;

#if DS_SIM_ON_CORE1
  multicore_launch_core1(core1Main);
#endif

  for (;;) frame();
}
