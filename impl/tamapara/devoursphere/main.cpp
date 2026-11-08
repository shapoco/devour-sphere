// Devour Sphere on the Tamagotchi Paradise (Sonix SNC73410, Cortex-M3).
//
// A guest of the stock firmware: the launcher (hw/stub_tpl.bin, installed by
// tools/mkrom.js) copies the first 64 KB of this program into PRAM and
// resets into it when A and C are held on the home screen; the rest runs
// from the cached flash window. Everything here is the platform layer: the
// vectors and the start-up, the millisecond tick (buttons, dial, tunes),
// the frame loop, the band push and the way back. The game is core/.
//
// Nothing is allocated. The Game and the Renderer are statics in .bss (the
// SRAM); the stack is the top of the SRAM.
//
// GPL-3.0-or-later (this directory, see ../hw/NOTICE.md).

#include <cstdint>

#include "devoursphere/devoursphere.hpp"
#include "devoursphere/profile.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"
#include "high_score_store.hpp"
#include "profiler.hpp"
#include "tone_player.hpp"

extern "C" {
#include "hw.h"
void ds_para_mark(void);
int ds_para_back_fix(u8 *buf);
void ds_uart_init(void);
void ds_uart_puts(const char *s);
void ds_fault_c(u32 *fr);
void ds_frame_mark(void);
}

namespace sim = devoursphere::sim;
namespace render = devoursphere::render;
namespace g2 = shapoco::gfx2d;

#define PRAM_TEXT __attribute__((section(".pram_text.main"), noinline))

// ---- The blocks the launcher and mkrom.js read ------------------------------
// "PMXC" at 0x140 of the image (right after the 80 vectors): the launcher
// only checks it is there. Then ours, "DSPH": where the flash part and the
// high score sector sit, so mkrom.js can place the image and check the room.
struct PmxCfg {
  uint32_t magic, version, guest_off, guest_size, save_off, device;
  uint8_t map[128];
};
__attribute__((section(".pmxcfg"), used)) volatile const PmxCfg g_pmx = {
    0x43584D50u, 1, 0, 0, 0, 0, {0}};

extern "C" uint32_t __xip_size;  // linker: SIZEOF(.xip) and what follows it
#ifndef DS_XIP_FLASH_OFF
#error "DS_XIP_FLASH_OFF (flash offset of the .xip part) must be defined"
#endif
#ifndef DS_HISCORE_OFF
#error "DS_HISCORE_OFF (flash offset of the high score sector) must be defined"
#endif
struct DsCfg {
  uint32_t magic, version, xip_off, xip_len, hiscore_off, detent, flags, reserved;
};
__attribute__((section(".dscfg"), used)) volatile const DsCfg g_dscfg = {
    0x48505344u, 1, DS_XIP_FLASH_OFF, (uint32_t)&__xip_size, DS_HISCORE_OFF,
    (uint32_t)ds::DETENT_STEPS, 0, 0};
extern "C" const volatile uint32_t ds_hiscore_off = DS_HISCORE_OFF;

// ---- The millisecond tick ---------------------------------------------------
extern "C" volatile uint32_t g_ms;
volatile uint32_t g_ms = 0;
namespace {
volatile uint32_t g_encLast = 0;
volatile int g_encAcc = 0;      // quadrature steps towards the next detent
volatile int g_encDetents = 0;  // detents since the last frame read them
// Gray code transitions of P2.0/P2.1; clockwise is positive on the device
__attribute__((section(".pram_rodata.main"))) const int8_t QD[16] = {
    0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
}  // namespace

extern "C" PRAM_TEXT void systick_c(void) {
  g_ms++;
  hw_wdt_feed();
  const uint32_t n = hw_encoder();
  if (n != g_encLast) {
    g_encAcc += QD[(g_encLast << 2) | n];
    g_encLast = n;
    while (g_encAcc >= ds::DETENT_STEPS) { g_encAcc -= ds::DETENT_STEPS; g_encDetents++; }
    while (g_encAcc <= -ds::DETENT_STEPS) { g_encAcc += ds::DETENT_STEPS; g_encDetents--; }
  }
  audio::tick1ms();
}
extern "C" __attribute__((naked, section(".pram_text.main"))) void SysTick_Handler(void) {
  __asm__ volatile("b systick_c\n");
}

namespace {

inline void irqOff() { __asm__ volatile("cpsid i" ::: "memory"); }
inline void irqOn() { __asm__ volatile("cpsie i" ::: "memory"); }

// ---- The game and its memory ------------------------------------------------
sim::Game g_game;
render::Renderer g_renderer;
ds::HighScoreStore g_store;
alignas(8) uint8_t g_arena[ds::ARENA_SIZE];
ds::Profiler g_prof;
render::Benchmark g_bench;

// Two band buffers, RGB565_SWAPPED: the bytes are in panel order already, so
// the DMA sends them as they are. One is being drawn while the other goes out.
// Between frames the DMA is idle and the first one is also the 4 KB buffer of
// the flash write and of the way back.
alignas(4) uint16_t g_bands[2][ds::SCREEN_W * ds::BAND_H];
int g_drawBand = 0;

// Frame pacing
uint32_t g_lastUs = 0;
uint32_t g_accUs = 0;

// ---- The buttons and the dial --------------------------------------------------
// The dial turned is the steering lever: counterclockwise left, clockwise
// right, LEVER_MAX clicks to the stop, back to the middle for straight
// ahead. On the menus one click is one press of left or right. A fires /
// confirms, B accelerates, C brakes, the dial pressed dodges. B and C
// pressed together (within PAUSE_PAIR_MS of each other) pause. On the title
// and the pause screen a short press of C toggles the sound (the core's
// DOWN), A with the dial held cycles the timing overlay, the dial held 2 s
// goes back to the Paradise, and B and C held 3 s on the title start the
// benchmark.
constexpr uint32_t BTN_FIRE = BTN_A, BTN_ACCEL = BTN_B, BTN_BRAKE = BTN_C;
struct Pad {
  uint32_t accelSince = 0;   // ms when B was first seen pressed (0: not)
  uint32_t brakeSince = 0;   // ms when C was first seen pressed (0: not)
  bool brakeJoined = false;  // B was pressed during this hold of C
  bool pairHeld = false;     // B and C are down as a pair (pause / benchmark)
  uint32_t wheelSince = 0;   // ms when the dial was first seen pressed
  int lever = 0;             // -LEVER_MAX .. LEVER_MAX clicks
  int clicks = 0;            // menu clicks not yet given to a tick (sign = side)
  bool mutePending = false;  // a short press of C not yet given to a tick
  bool pulseOn = false;      // the previous tick carried a press: this one is its release
  uint32_t prev = 0;         // raw buttons of the previous frame
  bool overlayDone = false;  // the overlay toggle fired for this press of A
};
Pad g_pad;

void leave();

sim::Input readInput(const render::HudState &hud) {
  const uint32_t raw = hw_buttons();
  const uint32_t now = ds::nowMs();
  int det;
  irqOff();
  det = g_encDetents;
  g_encDetents = 0;
  irqOn();

  const bool title = hud.state == sim::GameState::TITLE;
  const bool menu = title || hud.paused;  // where the extras live
  const bool inRun = hud.state == sim::GameState::PLAYING ||
                     hud.state == sim::GameState::LAUNCH || hud.state == sim::GameState::ARRIVE;
  const bool playing = inRun && !hud.paused;
  const bool fire = raw & BTN_FIRE, accel = raw & BTN_ACCEL, brake = raw & BTN_BRAKE,
             w = raw & BTN_WHEEL;

  uint8_t bits = 0;
  int8_t x = 0, y = 0;

  // The dial turned: the lever in play, clicks for the menus (kept until a
  // tick takes them, see tickInput)
  if (!inRun) g_pad.lever = 0;  // a new run starts straight ahead
  if (det != 0 && !g_bench.active()) {
    if (playing) {
      g_pad.lever += det;
      if (g_pad.lever > ds::LEVER_MAX) g_pad.lever = ds::LEVER_MAX;
      if (g_pad.lever < -ds::LEVER_MAX) g_pad.lever = -ds::LEVER_MAX;
    } else {
      g_pad.clicks += det;
    }
  }
  if (playing) x = (int8_t)(g_pad.lever * sim::INPUT_MAX / ds::LEVER_MAX);

  // When B and C went down
  if (accel) {
    if (!g_pad.accelSince) g_pad.accelSince = now | 1u;
  } else {
    g_pad.accelSince = 0;
  }
  if (brake) {
    if (!g_pad.brakeSince) {
      g_pad.brakeSince = now | 1u;
      g_pad.brakeJoined = accel;
    }
    if (accel) g_pad.brakeJoined = true;
  }

  // B and C down together, pressed within PAUSE_PAIR_MS of each other: the
  // pause (the core acts on the press); the pair stays a pair until one is
  // released. On the title a long hold of the pair is the benchmark (the
  // core wants B for three seconds). B over C otherwise cancel out.
  if (accel && brake) {
    if (!g_pad.pairHeld) {
      const int32_t gap = (int32_t)(g_pad.accelSince - g_pad.brakeSince);
      g_pad.pairHeld = (gap < 0 ? -gap : gap) <= (int32_t)ds::PAUSE_PAIR_MS;
    }
    if (g_pad.pairHeld) {
      if (!title) bits |= sim::Button::PAUSE;
      const uint32_t since = g_pad.accelSince > g_pad.brakeSince ? g_pad.accelSince : g_pad.brakeSince;
      if (title && now - since >= ds::BENCH_HOLD_MS) bits |= sim::Button::B;
    }
  } else {
    g_pad.pairHeld = false;
    if (playing && accel) y = -sim::INPUT_MAX;  // dash is y < 0
    if (playing && brake) y = sim::INPUT_MAX;   // brake is y > 0
  }

  // C alone, pressed briefly on the title or the pause screen: the sound
  if (!brake && g_pad.brakeSince) {
    if (menu && !g_pad.brakeJoined && now - g_pad.brakeSince < ds::MUTE_PRESS_MS) {
      g_pad.mutePending = true;
    }
    g_pad.brakeSince = 0;
  }

  // The dial pressed: the dodge; held on the title or the pause screen: back
  if (w) {
    if (!g_pad.wheelSince) g_pad.wheelSince = now | 1u;
    if (menu && (int)(now - g_pad.wheelSince) >= (int)ds::LEAVE_HOLD_MS) leave();
    if (!menu || !g_bench.active()) bits |= sim::Button::B;
  } else {
    g_pad.wheelSince = 0;
  }

  // A: fire / confirm. With the dial held on the title or pause screen it
  // cycles the overlay instead (and the press is not the core's).
  if (fire) {
    if (menu && w && !(g_pad.prev & BTN_FIRE)) {
      g_prof.toggle();
      g_pad.overlayDone = true;
    }
    if (!g_pad.overlayDone) bits |= sim::Button::A;
  } else {
    g_pad.overlayDone = false;
  }

  g_pad.prev = raw;
  sim::Input in(bits);
  in.x = x;
  in.y = y;
  return in;
}

// The menu presses the dial's clicks and C's short press stand for, one per
// tick with a release tick between them, so none is lost on a frame without
// a tick and each click is its own press (the core reads the axes as a pad)
sim::Input tickInput(const sim::Input &base) {
  sim::Input in = base;
  if (g_bench.active()) return in;
  if (g_pad.pulseOn) {
    g_pad.pulseOn = false;
    return in;
  }
  if (g_pad.clicks != 0) {
    in.x = (int8_t)(g_pad.clicks < 0 ? -sim::INPUT_MAX : sim::INPUT_MAX);
    g_pad.clicks += g_pad.clicks < 0 ? 1 : -1;
    g_pad.pulseOn = true;
  } else if (g_pad.mutePending) {
    in.y = sim::INPUT_MAX;  // the core's DOWN
    g_pad.mutePending = false;
    g_pad.pulseOn = true;
  }
  return in;
}

// ---- Phase breakdown on the overlay (the ESPboy's three lines) ----------------
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
void phaseLine(char *line, const char *labels, const uint32_t *us, int count, uint32_t div) {
  char *p = line, *end = line + ds::Profiler::COLS;
  for (int i = 0; i < count; i++) {
    if (i > 0 && p < end) *p++ = ' ';
    if (p < end) *p++ = labels[i];
    p = putMs1(p, end, div ? us[i] / div : us[i]);
  }
  *p = '\0';
}

uint32_t g_overlayUs = 0;
uint32_t g_xferUs = 0;

void updatePhaseLines(int ticks) {
  if (!g_prof.full()) {
    for (auto &l : g_prof.extra) l[0] = '\0';
    return;
  }
  const uint32_t *tp = g_game.tickProfile().us;
  const uint32_t *fp = g_renderer.frameProfile().us;
  const uint32_t div = ticks > 0 ? (uint32_t)ticks : 1;
  const uint32_t tick[4] = {
      tp[sim::Game::TP_AI], tp[sim::Game::TP_MOVE], tp[sim::Game::TP_LAYOUT],
      tp[sim::Game::TP_FIRE] + tp[sim::Game::TP_BULLETS] + tp[sim::Game::TP_FRAGMENTS] +
          tp[sim::Game::TP_EATING] + tp[sim::Game::TP_COLLISIONS] + tp[sim::Game::TP_ORDERS] +
          tp[sim::Game::TP_OTHER]};
  const uint32_t scene[4] = {
      fp[render::FP_SPHERE], fp[render::FP_ENTITIES],
      fp[render::FP_FRAGMENTS] + fp[render::FP_BULLETS] + fp[render::FP_EFFECTS_DRAW] +
          fp[render::FP_OVERLAYS],
      fp[render::FP_CAMERA] + fp[render::FP_EFFECTS] + fp[render::FP_SORT]};
  const uint32_t bands[4] = {fp[render::FP_BAND_3D], fp[render::FP_BAND_2D], g_overlayUs,
                             g_xferUs};
  phaseLine(g_prof.extra[0], "AMLX", tick, 4, div);
  phaseLine(g_prof.extra[1], "SOHX", scene, 4, 0);
  phaseLine(g_prof.extra[2], "DUVT", bands, 4, 0);
  g_prof.extra[3][0] = '\0';
  g_prof.extra[4][0] = '\0';
}

// The benchmark's lines go out of UART1 (the dongle's contacts, 460800 baud)
void benchLog(const char *line) {
  ds_uart_puts(line);
  ds_uart_puts("\r\n");
}

uint32_t clockUs() { return (uint32_t)ds::nowUs(); }
void updateProfileClock() {
  devoursphere::profileClockUs = g_prof.full() || g_bench.active() ? clockUs : nullptr;
}

// ---- Frames -----------------------------------------------------------------
g2::Surface bandSurface() {
  return {g2::PixelFormat::RGB565_SWAPPED, (int16_t)ds::SCREEN_W, (int16_t)ds::BAND_H,
          (uint32_t)(ds::SCREEN_W * 2), g_bands[g_drawBand]};
}

// Start sending the band just drawn and hand the other buffer to the renderer.
// Waits only if the previous band is still going out.
void pushBand(int y) {
  hw_px_dma_wait();
  hw_px_begin(0, (u32)y, ds::SCREEN_W, (u32)(y + ds::BAND_H));
  hw_px_dma_start((const u8 *)g_bands[g_drawBand], sizeof(g_bands[0]));
  g_drawBand ^= 1;
}

// A whole screen of band pushes with nothing else running, in microseconds
uint32_t measureTransfer() {
  const uint32_t t0 = clockUs();
  for (int b = 0; b < ds::BAND_COUNT; b++) pushBand(b * ds::BAND_H);
  hw_px_dma_wait();
  return clockUs() - t0;
}

int ticksDue() {
  int n = 0;
  while (g_accUs >= ds::TICK_US && n < ds::MAX_CATCHUP) {
    g_accUs -= ds::TICK_US;
    n++;
  }
  if (g_accUs >= ds::TICK_US) g_accUs = ds::TICK_US - 1;
  return n;
}

void drawFrame(int ticks) {
  const uint32_t t0 = clockUs();
  g_renderer.resetFrameProfile();
  g_renderer.beginFrame(g_game, g_bench.dt(ticks));
  g_prof.beginUs = clockUs() - t0;
}

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
    pushBand(y);
    g_xferUs += clockUs() - t2;
  }
  const uint32_t t3 = clockUs();
  hw_px_dma_wait();  // the last band: the DMA is idle between frames
  g_xferUs += clockUs() - t3;
  g_prof.dmaWaitUs = g_xferUs;  // the time the CPU waited, not the time on the wire
  g_renderer.endFrame();
  g_prof.endFrame(ds::nowUs(), g_renderer.stats());
  ds_frame_mark();
}

void frame() {
  const uint32_t nowUs = (uint32_t)ds::nowUs();
  uint32_t deltaUs = nowUs - g_lastUs;
  g_lastUs = nowUs;
  if (deltaUs > (uint32_t)ds::MAX_CATCHUP * ds::TICK_US) deltaUs = (uint32_t)ds::MAX_CATCHUP * ds::TICK_US;
  g_accUs += deltaUs;

  const sim::Input input = g_bench.input(readInput(g_renderer.hud()));

  const uint32_t tick0 = clockUs();
  const int ticks = g_bench.ticks(ticksDue());
  for (int i = 0; i < ticks; i++) {
    g_game.tick(tickInput(input));
    g_renderer.pollEffects(g_game);
    audio::setMuted(g_game.muted());
    audio::request(g_game.sounds());
  }
  audio::poll();
  g_prof.tickUs = clockUs() - tick0;
  g_prof.ticks = ticks;
  g_prof.core1WaitUs = 0;
  if (ticks == 0) return;
  g_game.keepHighScore();
  g_store.poll(g_game);
  updatePhaseLines(ticks);
  g_bench.beforeFrame(g_game, g_renderer, ds::benchSample(g_prof));
  g_game.resetTickProfile();
  updateProfileClock();
  drawFrame(ticks);
  presentFrame();
}

// ---- The way back --------------------------------------------------------------
// Runs from PRAM: it ends by jumping into the firmware's own restart.
PRAM_TEXT void leave() {
  audio::stop();
  g_store.flush(g_game);
  hw_tone(0);
  irqOff();
  *(volatile uint32_t *)0xE000E010u = 0;  // SysTick off
  *(volatile uint32_t *)0x20000FFCu = 0x4C584D50u;  // "PMXL": back on purpose
  ds_para_back_fix((u8 *)g_bands[0]);  // the Paradise's clock moves on by our time
  hw_bl_pwm_off();
  for (uint32_t t = 0; hw_buttons() && t < 20000000u; t++) {
    if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  }
  hw_back_to_paradise();
}

}  // namespace

// The simulator marks a presented frame by this symbol
extern "C" __attribute__((noinline, used)) void ds_frame_mark(void) { __asm__ volatile("" ::: "memory"); }

// The 4 KB scratch the flash write borrows (the band buffer, idle between frames)
extern "C" uint8_t *ds_sector_buffer() { return (uint8_t *)g_bands[0]; }

// ---- Start-up -------------------------------------------------------------------
extern "C" {
extern uint32_t __data_load, __data_start, __data_end, __bss_start, __bss_end;
extern void (*__init_array_start[])(void), (*__init_array_end[])(void);
}

namespace {

void gameMain() {
  // Constructors of the statics (the Game, the Renderer, ...)
  for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) (*f)();

  ds_uart_init();
  ds_uart_puts("\r\nDevour Sphere on the Paradise\r\n");
  audio::init(audio::Config{0, audio::Pacing::PWM_WRAP, false});

  g_game.reset(ds::randomSeed());
  g_store.load(g_game);
  g_renderer.setControlHints(render::ControlHints{
      "DIAL: TURN   A: FIRE   PUSH DIAL: DODGE",
      "DIAL: TURN  A: FIRE  PUSH: DODGE",
      "B: DASH   C: BRAKE",
      "B / C: DASH / BRAKE",
      "B+C: RESUME    C: SOUND",
      "B+C: RESUME   C: SOUND",
  });
  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena), ds::SPAN_CAPACITY);
  g_renderer.setDetailTriangles(ds::DETAIL_TRIANGLES);
  g_bench.setPlatform("TAMAPARA", benchLog);

  ds::stackWatchInit();
  for (uint32_t i = 0; i < ds::SCREEN_W * ds::BAND_H; i++) g_bands[0][i] = g_bands[1][i] = 0;
  g_prof.xferUs = measureTransfer();

  g_lastUs = (uint32_t)ds::nowUs();
  g_accUs = ds::TICK_US;
  for (;;) frame();
}

}  // namespace

extern "C" __attribute__((section(".pram_text.main"), noinline, used)) void Reset_Handler(void) {
  irqOff();
  ds_para_mark();  // the Paradise's clock, before the SRAM is overwritten
  *(volatile uint32_t *)0xE000ED14u &= ~(1u << 3);  // UNALIGN_TRP off
  hw_init();
  {
    const uint32_t *s = &__data_load;
    for (uint32_t *p = &__data_start; p < &__data_end; p++) *p = *s++;
  }
  for (uint32_t *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  __asm__ volatile("dsb\n isb");
  for (uint32_t i = 0; i < 4; i++) {  // all IRQs off, nothing pending
    *(volatile uint32_t *)(0xE000E180u + 4u * i) = 0xFFFFFFFFu;
    *(volatile uint32_t *)(0xE000E280u + 4u * i) = 0xFFFFFFFFu;
  }
  *(volatile uint32_t *)(0x4001A000u + 0x04) &= ~3u;  // P2.0, P2.1 inputs
  g_encLast = hw_encoder();
  *(volatile uint32_t *)0xE000E014u = ds::SYSTICK_RELOAD;
  *(volatile uint32_t *)0xE000E018u = 0;
  *(volatile uint32_t *)0xE000E010u = 7;
  irqOn();
  // wait until the start keys (A+C) are released
  for (uint32_t t0 = g_ms; (hw_buttons() & (BTN_A | BTN_C)) && g_ms - t0 < 3000u;) {}
  gameMain();
}

// A fault: its own stack, then the red screen (plat_c.c)
__attribute__((section(".bss"), aligned(8))) uint32_t g_faultStack[256];
extern "C" __attribute__((naked, section(".pram_text.main"))) void Fault_Handler(void) {
  __asm__ volatile("mrs r0, msp\n ldr r2, =g_faultStack+1024\n mov sp, r2\n b ds_fault_c\n .ltorg\n");
}
extern "C" __attribute__((naked, section(".pram_text.main"))) void Exc_Off(void) { __asm__ volatile("bx lr\n"); }

// The vector table itself is in plat_c.c (C allows the ranged designated
// initializer; in C++ the integer cast of the stack top became a run-time
// initializer and the table was left empty)
