// Devour Sphere on Xiamocon (XIAO RP2350 and XIAO ESP32S3).
//
// The game core (core/) is shared with the WASM front end; everything here is
// the platform layer: the SDK entry points, the frame loop, the input mapping
// and the memory the core is given. None of the SDK's graphics API is used --
// the frame is drawn by core/render into band buffers and pushed by
// BandWriter (see band_writer.hpp).
//
// Nothing here is allocated on a stack. Game alone is 136 KB, and RP2350
// gives each core 4 KB in SCRATCH_X / SCRATCH_Y.

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

// The handover word between the cores, read and written with explicit
// acquire/release. Not std::atomic: on the Xtensa toolchain
// std::atomic<uint32_t> is not guaranteed lock free, and a lock would be
// wrong here -- the two sides are different cores, not threads. A 32-bit
// aligned load or store is atomic on both targets anyway; the builtins add
// the barrier and stop the compiler moving the payload past the handover.
template <typename T>
T stateLoad(const uint32_t &w) {
  return (T)__atomic_load_n(&w, __ATOMIC_ACQUIRE);
}
template <typename T>
void stateStore(uint32_t &w, T v) {
  __atomic_store_n(&w, (uint32_t)v, __ATOMIC_RELEASE);
}

#if DS_SPLIT == DS_SPLIT_RENDER

// core1 renders: beginFrame(), then the bands and their transfers. core0
// simulates and hands frames over, so its ticks for the next frame overlap
// core1's rasterization of this one.
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
uint32_t g_frame = (uint32_t)Frame::IDLE;
float g_frameDt = 0;  // written before BUILD is published, read after it

Frame frameLoad() { return stateLoad<Frame>(g_frame); }
void frameStore(Frame f) { stateStore(g_frame, f); }
void waitFrame(Frame want) {
  while (frameLoad() != want) xmc::tightLoopContents();
}

bool core1Task() {
  static bool started = false;
  if (!started) {
    started = true;
    ds::stackWatchInitCore1();
  }
  if (frameLoad() != Frame::BUILD) {
    ds::frameIdle();
    return true;
  }
  g_renderer.beginFrame(*g_game, g_frameDt);
  frameStore(Frame::SCENE_READY);  // the Game belongs to core0 again
  g_bands.present(g_renderer, g_prof);
  g_renderer.endFrame();
  frameStore(Frame::DONE);
  return true;
}

#elif DS_SPLIT == DS_SPLIT_SIM

// core1 simulates: core0 asks for a batch of ticks and rasterizes the frame
// it already has while that batch runs. The display stays core0's throughout,
// which on ESP32S3 is the whole reason for this arrangement.
//
// Which core touches what, while a batch is running:
//   core0  the arena and the scene, the band buffers, the display, and the
//          Renderer's HUD snapshot / gauges / markers
//   core1  the Game, and the Renderer's effect state through pollEffects()
//
// The same field split as the other arrangement with the cores swapped.
// beginFrame() reads the whole Game, so it runs while core1 is idle, between
// collecting one batch and asking for the next.
enum class Sim : uint32_t {
  IDLE,  // no batch outstanding; the Game is core0's
  RUN,   // core1 is ticking
  DONE,  // the batch finished and core0 has not collected it yet
};
uint32_t g_sim = (uint32_t)Sim::IDLE;
int g_simWanted = 0;       // written before RUN is published
uint8_t g_simButtons = 0;  // ... same
int g_simRan = 0;          // written before DONE is published
uint32_t g_simTickUs = 0;  // ... same

Sim simLoad() { return stateLoad<Sim>(g_sim); }
void simStore(Sim s) { stateStore(g_sim, s); }

bool core1Task() {
  static bool started = false;
  if (!started) {
    started = true;
    ds::stackWatchInitCore1();
  }
  static bool sawIdle = false;
  if (!sawIdle) {
    sawIdle = true;
    ds::trace("core1 alive", 0);
  }
  if (simLoad() != Sim::RUN) {
    ds::frameIdle();
    return true;
  }
  static int batches = 0;
  if (batches < 3) ds::trace("core1 batch in", g_simWanted);
  const uint32_t t0 = (uint32_t)xmc::getTimeUs();
  for (int i = 0; i < g_simWanted; i++) {
    g_game->tick(g_simButtons);
    // The events of a tick are cleared by the next one, so each tick has to
    // be polled or the frame would only show the last one's explosions
    g_renderer.pollEffects(*g_game);
  }
  g_simTickUs = (uint32_t)xmc::getTimeUs() - t0;
  g_simRan = g_simWanted;
  simStore(Sim::DONE);
  if (batches < 3) {
    batches++;
    ds::trace("core1 batch out", g_simTickUs);
  }
  return true;
}

#endif

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
  if (g_game->score() > g_game->highScore()) {
    g_game->setHighScore(g_game->score());
  }
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
  // Start owing one tick, so the first loop has something to do instead of
  // waiting for the clock to move.
  g_lastUs = xmc::getTimeUs();
  g_accUs = ds::TICK_US;
  ds::stackWatchInitCore0();
  ds::trace("renderer ready", (uint32_t)sizeof(g_arena));
  // Both cores are otherwise idle here, so this is the transfer on its own
  g_prof.xferUs = g_bands.measureTransfer();
  ds::trace("transfer us", g_prof.xferUs);
#if DS_SPLIT == DS_SPLIT_NONE
  ds::trace("one core", 0);
#else
  ds::trace("core1", (uint32_t)xmc::startCore1(core1Task));
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

#if DS_SPLIT == DS_SPLIT_SIM
  // Collect the batch core1 ran while we rasterized the previous frame. If it
  // is still going, come back on the next libLoop() rather than spinning
  // here: this is the only core that runs system::service(), which is what
  // polls the power button and services the IO expander.
  if (simLoad() == Sim::RUN) {
    g_prof.core1WaitUs += (uint32_t)(xmc::getTimeUs() - nowUs);
    return;
  }
  g_prof.core1WaitUs = 0;

  int ran = 0;
  if (simLoad() == Sim::DONE) {
    ran = g_simRan;
    g_prof.tickUs = g_simTickUs;
    g_prof.ticks = ran;
    simStore(Sim::IDLE);  // the Game is ours until we ask for the next batch
  }

  // Build the scene from the state that batch left behind. Nothing else may
  // touch the Game here.
  if (ran > 0) {
    keepHighScore();
    const uint32_t begin0 = (uint32_t)xmc::getTimeUs();
    // Simulation time, not wall time: the renderer advances the camera
    // smoothing, the debris and the score roll-up by dt, and those have to
    // stay in step with the ticks that actually ran.
    g_renderer.beginFrame(*g_game, ran * (1.0f / sim::TICK_RATE));
    g_prof.beginUs = (uint32_t)xmc::getTimeUs() - begin0;
  }

  // Ask for the next batch before rasterizing, so core1 ticks while we do.
  // This is what costs a frame of latency: these buttons reach the screen one
  // frame later.
  const int want = ticksDue();
  static int loops = 0;
  if (loops < 3) {
    loops++;
    ds::trace("loop ran", (uint32_t)ran);
    ds::trace("loop want", (uint32_t)want);
  }
  if (want > 0) {
    g_simWanted = want;
    g_simButtons = buttons;
    simStore(Sim::RUN);
  }

  if (ran > 0) {
    g_bands.present(g_renderer, g_prof);
    g_renderer.endFrame();
    g_prof.endFrame(xmc::getTimeUs(), g_renderer.stats());
  }
#else
  const uint32_t tick0 = (uint32_t)xmc::getTimeUs();
  const int ticks = ticksDue();
  for (int i = 0; i < ticks; i++) {
    g_game->tick(buttons);
    // The events of a tick are cleared by the next one, so each tick has to
    // be polled or the frame would only show the last one's explosions
    g_renderer.pollEffects(*g_game);
  }
  g_prof.tickUs = (uint32_t)xmc::getTimeUs() - tick0;
  g_prof.ticks = ticks;
  if (ticks == 0) return;  // ahead of the simulation: nothing new to show
  keepHighScore();
  const float dt = ticks * (1.0f / sim::TICK_RATE);

#if DS_SPLIT == DS_SPLIT_RENDER
  // Collect the frame core1 has been working on while we ticked. On entry
  // core1 is always past beginFrame(), so the ticks above could not have
  // raced with it.
  const uint32_t wait0 = (uint32_t)xmc::getTimeUs();
  if (frameLoad() != Frame::IDLE) waitFrame(Frame::DONE);
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
  // Everything on this core. Nothing overlaps the ticks, so this is slower.
  const uint32_t begin0 = (uint32_t)xmc::getTimeUs();
  g_renderer.beginFrame(*g_game, dt);
  g_prof.beginUs = (uint32_t)xmc::getTimeUs() - begin0;
  g_bands.present(g_renderer, g_prof);
  g_renderer.endFrame();
  g_prof.core1WaitUs = 0;
  g_prof.endFrame(xmc::getTimeUs(), g_renderer.stats());
#endif
#endif
}

XmcStatus xmcAppTerminate(xmc::system::ShutdownReason reason) {
  (void)reason;
  // requestShutdown() calls us before it deinits anything and then draws its
  // power-off message through the same SPI bus. Core1 has to be off whatever
  // it shares with us, and the band left in flight by present() collected,
  // both before we return.
#if DS_SPLIT == DS_SPLIT_RENDER
  if (frameLoad() != Frame::IDLE) waitFrame(Frame::DONE);
#elif DS_SPLIT == DS_SPLIT_SIM
  // The batch will finish on its own; core1 never blocks
  while (simLoad() == Sim::RUN) xmc::tightLoopContents();
#endif
  g_bands.drain();
  return XMC_OK;
}
