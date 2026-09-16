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

#include "xmc/app.hpp"
#include "band_writer.hpp"
#include "devoursphere/devoursphere.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"
#include "profiler.hpp"
#include "xmc/input.hpp"
#include "xmc/multicore.hpp"
#include "xmc/system.hpp"
#include "xmc/timer.hpp"
#include "xmc/xmc_common.hpp"

// Deliberately no `using namespace`: xmc and shapoco::gfx2d both have a
// PixelFormat, a Color and a Graphics2D.
namespace sim = devoursphere::sim;
namespace render = devoursphere::render;

namespace {

sim::Game *g_game = nullptr;  // see ds::allocGame()
render::Renderer g_renderer;
uint8_t g_arena[ds::ARENA_SIZE];
ds::BandWriter g_bands;
ds::Profiler g_prof;

// Frame pacing: the simulation steps at a fixed rate, the frame rate is
// whatever the device manages.
uint64_t g_lastUs = 0;
uint32_t g_accUs = 0;

// --- The two cores -------------------------------------------------------
#if DS_RENDER_ON_CORE1
//
// core1 renders: beginFrame(), then the bands and their transfers. core0
// runs the simulation and hands frames over. The point of the split is that
// core0's ticks for the next frame overlap core1's rasterization of this
// one; beginFrame() cannot overlap anything, because it reads the whole
// Game.
//
// Which core touches what, while core1 is past SCENE_READY:
//   core1  the arena and the scene built into it, the band buffers, the
//          display, and the Renderer's HUD snapshot / gauges / markers
//   core0  the Game, and the Renderer's effect state through pollEffects()
//          (debris, pickups, the effect rng) -- none of which renderBand()
//          reads, now that the HUD draws from the snapshot beginFrame takes.
enum class Frame : uint32_t {
  IDLE,         // nothing in flight
  BUILD,        // core0 asked for a frame; core1 is inside beginFrame()
  SCENE_READY,  // the scene is built; core1 is rasterizing and transferring
  DONE,         // the frame is out; core0 may hand over the next one
};
// A plain word with explicit acquire/release rather than std::atomic: on the
// Xtensa toolchain std::atomic<uint32_t> is not guaranteed lock free, and a
// lock would be wrong here -- the two sides are different cores, not
// threads. A 32-bit aligned load or store is atomic on both targets anyway;
// the builtins add the barrier and stop the compiler reordering the renderer
// writes past the handover.
uint32_t g_frame = (uint32_t)Frame::IDLE;
float g_frameDt = 0;  // written before BUILD is published, read after it

Frame frameLoad() { return (Frame)__atomic_load_n(&g_frame, __ATOMIC_ACQUIRE); }
void frameStore(Frame f) {
  __atomic_store_n(&g_frame, (uint32_t)f, __ATOMIC_RELEASE);
}

bool core1Task() {
  static bool painted = false;
  if (!painted) {
    painted = true;
    ds::stackWatchInitCore1();
  }
  if (frameLoad() != Frame::BUILD) {
    ds::frameIdle();
    return true;
  }
  g_renderer.beginFrame(*g_game, g_frameDt);
  // From here the Game belongs to core0 again
  frameStore(Frame::SCENE_READY);
  g_bands.present(g_renderer, g_prof);
  g_renderer.endFrame();
  frameStore(Frame::DONE);
  return true;
}

void waitFrame(Frame want) {
  while (frameLoad() != want) {
    xmc::tightLoopContents();
  }
}

#endif  // DS_RENDER_ON_CORE1

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
  ds::trace("setup", 0);
  g_game = ds::allocGame();
  if (!g_game) {
    ds::trace("no memory for the simulation", 0);
    return;  // xmcAppLoop() bails out too
  }
  g_game->reset(ds::randomSeed());
  g_renderer.setControlHints(render::ControlHints{
      "MOVE: D-PAD    A/B/X/Y: FIRE",
      "D-PAD: MOVE   A: FIRE",
      // The dash line already describes this device correctly
      render::ControlHints{}.dash,
      render::ControlHints{}.dashAlt,
  });
  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena));
  if (!g_bands.init()) {
    ds::trace("no memory for the band buffers", 0);
    g_game = nullptr;  // xmcAppLoop() bails out
    return;
  }
  // Start owing one tick, so the very first loop draws instead of leaving the
  // screen blank until the clock has moved.
  g_lastUs = xmc::getTimeUs();
  g_accUs = ds::TICK_US;
  ds::stackWatchInitCore0();
  ds::trace("renderer ready", (uint32_t)sizeof(g_arena));
  // Both cores are otherwise idle here, so this is the transfer on its own
  g_prof.xferUs = g_bands.measureTransfer();
  ds::trace("transfer us", g_prof.xferUs);
#if DS_RENDER_ON_CORE1
  ds::trace("core1", (uint32_t)xmc::startCore1(core1Task));
#else
  ds::trace("rendering on core0", 0);
#endif
}

void xmcAppLoop(void) {
  if (!g_game) return;
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
  // FUNC only means something to the SDK while the board is booting, so it is
  // free to use here.
  if (xmc::input::wasPressed(xmc::input::Button::FUNC)) g_prof.toggle();

  int ticks = 0;
  const uint32_t tick0 = (uint32_t)xmc::getTimeUs();
  while (g_accUs >= ds::TICK_US && ticks < ds::MAX_CATCHUP) {
    g_game->tick(buttons);
    // The events of a tick are cleared by the next one, so each tick has to
    // be polled or the frame would only show the last one's explosions
    g_renderer.pollEffects(*g_game);
    g_accUs -= ds::TICK_US;
    ticks++;
  }
  g_prof.tickUs = (uint32_t)xmc::getTimeUs() - tick0;
  g_prof.ticks = ticks;
  if (g_accUs >= ds::TICK_US) g_accUs = ds::TICK_US - 1;  // drop the surplus
  if (ticks == 0) return;  // ahead of the simulation: nothing new to show

  // Nothing is written to flash, but the high score still survives a restart
  // for as long as the power is on, because Game::reset() leaves it alone.
  if (g_game->score() > g_game->highScore())
    g_game->setHighScore(g_game->score());

  // Simulation time, not wall time: the renderer advances the camera
  // smoothing, the debris and the score roll-up by dt, and those have to stay
  // in step with the ticks that actually ran.
  const float dt = ticks * (1.0f / sim::TICK_RATE);

#if DS_RENDER_ON_CORE1
  // Collect the frame core1 has been working on while we ticked. On entry
  // core1 is always past beginFrame(), so the ticks above could not have
  // raced with it.
  const uint32_t wait0 = (uint32_t)xmc::getTimeUs();
  if (frameLoad() != Frame::IDLE) {
    waitFrame(Frame::DONE);
  }
  g_prof.core1WaitUs = (uint32_t)xmc::getTimeUs() - wait0;
  g_prof.endFrame(xmc::getTimeUs(), g_renderer.stats());

  // Hand the state we just ticked to core1 and wait only until it has built
  // the scene; it keeps rasterizing while we return and tick the next frame.
  g_frameDt = dt;
  const uint32_t begin0 = (uint32_t)xmc::getTimeUs();
  frameStore(Frame::BUILD);
  waitFrame(Frame::SCENE_READY);
  g_prof.beginUs = (uint32_t)xmc::getTimeUs() - begin0;
#else
  // Everything on this core: build the scene, then rasterize and push each
  // band. Nothing overlaps the ticks, so this is slower.
  const uint32_t begin0 = (uint32_t)xmc::getTimeUs();
  g_renderer.beginFrame(*g_game, dt);
  g_prof.beginUs = (uint32_t)xmc::getTimeUs() - begin0;
  g_bands.present(g_renderer, g_prof);
  g_renderer.endFrame();
  g_prof.core1WaitUs = 0;
  g_prof.endFrame(xmc::getTimeUs(), g_renderer.stats());
#endif
}

XmcStatus xmcAppTerminate(xmc::system::ShutdownReason reason) {
  (void)reason;
  // requestShutdown() calls us before it deinits anything and then draws its
  // power-off message through the same SPI bus, so core1 has to be off the
  // display and the band left in flight by present() collected, both before
  // we return.
#if DS_RENDER_ON_CORE1
  if (frameLoad() != Frame::IDLE) {
    waitFrame(Frame::DONE);
  }
#endif
  g_bands.drain();
  return XMC_OK;
}
