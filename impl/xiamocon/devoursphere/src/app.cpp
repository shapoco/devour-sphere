// Devour Sphere on Xiamocon (XIAO RP2350).
//
// The game core (core/) is shared with the WASM front end; everything here is
// the platform layer: the SDK entry points, the frame loop, the input mapping
// and the memory the core is given. None of the SDK's graphics API is used --
// the frame is drawn by core/render into band buffers and pushed by
// BandWriter (see band_writer.hpp).
//
// Nothing is allocated dynamically: Game, Renderer, the 3D arena and the band
// buffers are all statics. Game alone is 133 KB, so it must never go on a
// stack (both cores get 4 KB, in SCRATCH_X / SCRATCH_Y).

#include <pico/rand.h>

#include "band_writer.hpp"
#include "devoursphere/devoursphere.hpp"
#include "ds_config.hpp"
#include "xmc/app.hpp"
#include "xmc/input.hpp"
#include "xmc/system.hpp"
#include "xmc/timer.hpp"

// Deliberately no `using namespace`: xmc and shapoco::gfx2d both have a
// PixelFormat, a Color and a Graphics2D.
namespace sim = devoursphere::sim;
namespace render = devoursphere::render;

namespace {

sim::Game g_game;
render::Renderer g_renderer;
uint8_t g_arena[ds::ARENA_SIZE];
ds::BandWriter g_bands;

// Frame pacing: the simulation steps at a fixed 60 Hz, the frame rate is
// whatever the device manages.
uint64_t g_lastUs = 0;
uint32_t g_accUs = 0;

// The buttons Xiamocon has, in the five bits the simulation takes. All four
// face buttons fire, so the thumb does not have to find a particular one.
uint8_t mapButtons(xmc::input::Button b) {
  using B = xmc::input::Button;
  auto down = [&](B m) { return (b & m) != B::NONE; };
  uint8_t out = 0;
  if (down(B::LEFT)) out |= sim::Button::LEFT;
  if (down(B::RIGHT)) out |= sim::Button::RIGHT;
  if (down(B::UP)) out |= sim::Button::UP;
  if (down(B::DOWN)) out |= sim::Button::DOWN;
  if (down(B::A) || down(B::B) || down(B::X) || down(B::Y)) {
    out |= sim::Button::A;
  }
  return out;
}

}  // namespace

xmc::AppConfig xmcAppGetConfig(void) {
  xmc::AppConfig cfg = xmc::getDefaultAppConfig();
  // RGB565 on this display is big-endian in memory, which is bit for bit what
  // ShapoGFX's PixelFormat::RGB565BE produces. No conversion anywhere.
  cfg.displayPixelFormat = xmc::PixelFormat::RGB565;
  cfg.speakerEnabled = false;  // no audio yet
  return cfg;
  // Careful: this runs before display::init(), so nothing here may draw.
}

void xmcAppSetup(void) {
  // xmc::randomU32() is an unseeded newlib rand(), identical on every boot;
  // get_rand_32() is the SDK's ring-oscillator entropy.
  g_game.reset(get_rand_32());
  g_renderer.setControlHints(render::ControlHints{
      "MOVE: D-PAD    A/B/X/Y: FIRE",
      "D-PAD: MOVE   A: FIRE",
      // The dash line already describes this device correctly
      render::ControlHints{}.dash,
      render::ControlHints{}.dashAlt,
  });
  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena));
  // Start owing one tick, so the very first loop draws instead of leaving the
  // screen blank until the clock has moved.
  g_lastUs = xmc::getTimeUs();
  g_accUs = ds::TICK_US;
}

void xmcAppLoop(void) {
  const uint64_t nowUs = xmc::getTimeUs();
  uint64_t deltaUs = nowUs - g_lastUs;
  g_lastUs = nowUs;
  // A long stall must not become a burst of simulation, so the elapsed time
  // is clamped before it reaches the accumulator as well as after.
  if (deltaUs > (uint64_t)ds::MAX_CATCHUP * ds::TICK_US) {
    deltaUs = (uint64_t)ds::MAX_CATCHUP * ds::TICK_US;
  }
  g_accUs += (uint32_t)deltaUs;

  // Read once per frame. system::service() has already run input::service()
  // this time round libLoop(); calling it again would consume the press and
  // release edges the simulation derives from the held state.
  const uint8_t buttons = mapButtons(xmc::input::getState());

  int ticks = 0;
  while (g_accUs >= ds::TICK_US && ticks < ds::MAX_CATCHUP) {
    g_game.tick(buttons);
    // The events of a tick are cleared by the next one, so each tick has to
    // be polled or the frame would only show the last one's explosions
    g_renderer.pollEffects(g_game);
    g_accUs -= ds::TICK_US;
    ticks++;
  }
  if (g_accUs >= ds::TICK_US) g_accUs = ds::TICK_US - 1;  // drop the surplus
  if (ticks == 0) return;  // ahead of the simulation: nothing new to show

  // Nothing is written to flash, but the high score still survives a restart
  // for as long as the power is on, because Game::reset() leaves it alone.
  if (g_game.score() > g_game.highScore()) g_game.setHighScore(g_game.score());

  // Simulation time, not wall time: the renderer advances the camera
  // smoothing, the debris and the score roll-up by dt, and those have to stay
  // in step with the ticks that actually ran.
  const float dt = ticks * (1.0f / sim::TICK_RATE);
  g_renderer.beginFrame(g_game, dt);
  g_bands.present(g_renderer);
  g_renderer.endFrame();
}

XmcStatus xmcAppTerminate(xmc::system::ShutdownReason reason) {
  (void)reason;
  // requestShutdown() calls us before it deinits anything and then draws its
  // power-off message through the same SPI bus, so the band left in flight by
  // present() has to be collected here.
  g_bands.drain();
  return XMC_OK;
}
