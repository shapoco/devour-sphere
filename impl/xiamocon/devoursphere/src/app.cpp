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
#include "high_score_store.hpp"
#include "profiler.hpp"
#include "se_player.hpp"
#include "xmc/input.hpp"
#include "xmc/ioex.hpp"
#include "xmc/multicore.hpp"
#include "xmc/pins.hpp"
#include "xmc/speaker.hpp"
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
ds::HighScoreStore g_store;  // the high score in flash
uint8_t g_arena[ds::ARENA_SIZE];
ds::BandWriter g_bands;
ds::Profiler g_prof;

// Frame pacing: the simulation steps at a fixed rate, the frame rate is
// whatever the device manages.
uint64_t g_lastUs = 0;
uint32_t g_accUs = 0;

#if DS_SIM_ON_CORE1

// core1 runs the ticks; core0 keeps the display and rasterizes. They overlap:
// core0 asks for a batch and then draws the frame the previous batch left
// behind, so the ticks happen while the bands go out.
//
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

// One word, read and written with explicit acquire/release. Not std::atomic:
// on the Xtensa toolchain std::atomic<uint32_t> is not guaranteed lock free,
// and a lock would be wrong here -- the two sides are different cores, not
// threads. A 32-bit aligned load or store is atomic on both targets anyway;
// the builtins add the barrier and stop the compiler moving the payload
// across the handover.
uint32_t g_sim = (uint32_t)Sim::IDLE;
int g_simWanted = 0;       // written before RUN is published
uint8_t g_simButtons = 0;  // ... same
int g_simRan = 0;          // written before DONE is published
uint32_t g_simTickUs = 0;  // ... same

volatile bool g_core1Ran = false;  // set the first time core1's task runs
uint64_t g_waitFromUs = 0;         // when core0 first found the batch running
bool g_waiting = false;

Sim simLoad() { return (Sim)__atomic_load_n(&g_sim, __ATOMIC_ACQUIRE); }
void simStore(Sim s) {
  __atomic_store_n(&g_sim, (uint32_t)s, __ATOMIC_RELEASE);
}

bool core1Task() {
  if (!g_core1Ran) {
    g_core1Ran = true;
    ds::stackWatchInitCore1();
  }
  if (simLoad() != Sim::RUN) {
    ds::frameIdle();
    return true;
  }
  const uint32_t t0 = (uint32_t)xmc::getTimeUs();
  for (int i = 0; i < g_simWanted; i++) {
    g_game->tick(g_simButtons);
    // The events of a tick are cleared by the next one, so each tick has to
    // be polled or the frame would only show the last one's explosions
    g_renderer.pollEffects(*g_game);
    audio::setMuted(g_game->muted());
    audio::request(g_game->sounds());
  }
  g_simTickUs = (uint32_t)xmc::getTimeUs() - t0;
  g_simRan = g_simWanted;
  simStore(Sim::DONE);
  return true;
}

#endif  // DS_SIM_ON_CORE1

// The buttons Xiamocon has, in the bits the simulation takes: A and Y fire,
// B is the emergency dodge, X pauses.
uint8_t mapButtons(xmc::input::Button b) {
  using B = xmc::input::Button;
  auto down = [&](B m) { return (b & m) != B::NONE; };
  uint8_t out = 0;
  if (down(B::LEFT)) out |= sim::Button::LEFT;
  if (down(B::RIGHT)) out |= sim::Button::RIGHT;
  if (down(B::UP)) out |= sim::Button::UP;
  if (down(B::DOWN)) out |= sim::Button::DOWN;
  if (down(B::A) || down(B::Y)) out |= sim::Button::A;
  if (down(B::B)) out |= sim::Button::B;
  if (down(B::X)) out |= sim::Button::PAUSE;
  return out;
}

// --- The amplifier's mute ----------------------------------------------------
// The SDK's own audio is off (speakerEnabled = false), so its mute pin on the
// IO expander (an I2C write: core0 only) is ours. Muted through the boot,
// unmuted a moment later unless the game is muted.
constexpr uint32_t AMP_UNMUTE_DELAY_US = 300 * 1000;
bool g_ampMuted = true;
uint64_t g_bootUs = 0;

void initAmp() {
  xmc::ioex::write(xmc::ioex::Pin::SPEAKER_MUTE, true);
  xmc::ioex::setDir(xmc::ioex::Pin::SPEAKER_MUTE, true);
  g_ampMuted = true;
  g_bootUs = xmc::getTimeUs();
}

void serviceAmp(bool gameMuted, uint64_t nowUs) {
  bool want = gameMuted || nowUs - g_bootUs < AMP_UNMUTE_DELAY_US;
  if (want == g_ampMuted) return;
  g_ampMuted = want;
  xmc::speaker::setMuted(want);
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

// The Game decides whether the score counts (not the title demo's); the
// store decides when it reaches the flash (once per run, on the game over
// screen, after the death sound). Only safe while the Game belongs to this
// core.
void keepHighScore() {
  g_game->keepHighScore();
  g_store.poll(*g_game);
}

// Draw the state the simulation has reached, and push it a band at a time
void drawFrame(int ticks) {
  const uint32_t begin0 = (uint32_t)xmc::getTimeUs();
  // Simulation time, not wall time: the renderer advances the camera
  // smoothing, the debris and the score roll-up by dt, and those have to stay
  // in step with the ticks that actually ran.
  g_renderer.beginFrame(*g_game, ticks * (1.0f / sim::TICK_RATE));
  g_prof.beginUs = (uint32_t)xmc::getTimeUs() - begin0;
}

void presentFrame() {
  g_bands.present(g_renderer, g_prof);
  g_renderer.endFrame();
  g_prof.endFrame(xmc::getTimeUs(), g_renderer.stats());
}

}  // namespace

xmc::AppConfig xmcAppGetConfig(void) {
  xmc::AppConfig cfg = xmc::getDefaultAppConfig();
  // RGB565 on this display is big-endian in memory, which is bit for bit what
  // ShapoGFX's PixelFormat::RGB565_SWAPPED produces. No conversion anywhere.
  cfg.displayPixelFormat = xmc::PixelFormat::RGB565;
  cfg.speakerEnabled = false;  // no audio yet
  return cfg;
  // Careful: this runs before display::init(), so nothing here may draw.
}

void xmcAppSetup(void) {
  g_game = ds::allocGame();
  if (!g_game) {
    ds::trace("no memory for the simulation", 0);
    return;  // xmcAppLoop() bails out too
  }
  g_game->reset(ds::randomSeed());
  g_store.load(*g_game);  // before core1, which has no business in the flash
  g_renderer.setControlHints(render::ControlHints{
      "MOVE: D-PAD   A/Y: FIRE   B: DODGE   X: PAUSE",
      "D-PAD: MOVE  A: FIRE  B: DODGE",
      // The dash line already describes this device correctly
      render::ControlHints{}.dash,
      render::ControlHints{}.dashAlt,
      "X: RESUME    DOWN: SOUND    UP / FUNC: STATS",
      "X: RESUME   DOWN: SOUND",
  });
  initAmp();
  // Before core1, which is the one that plays. RP2350: the speaker sits
  // behind a buffer, a low-pass and an amplifier, so the PWM runs fast (the
  // pack's wrap at the system clock), a DMA timer paces the samples, and the
  // output rests at the middle level (the amplifier is AC coupled). ESP32S3:
  // I2S PDM on the same pin; the other two fields are not used.
  audio::init(audio::Config{XMC_PIN_AUDIO_OUT, audio::Pacing::DMA_TIMER, true});
  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena),
                  ds::SPAN_CAPACITY);
  if (!g_bands.init()) {
    ds::trace("no memory for the band buffers, free", ds::freeInternalRam());
    g_game = nullptr;  // xmcAppLoop() bails out
    return;
  }
  // Start owing one tick, so the first loop has something to do instead of
  // waiting for the clock to move.
  g_lastUs = xmc::getTimeUs();
  g_accUs = ds::TICK_US;
  ds::stackWatchInitCore0();
  // Both cores are otherwise idle here, so this is the transfer on its own
  g_prof.xferUs = g_bands.measureTransfer();
#if DS_SIM_ON_CORE1
  xmc::startCore1(core1Task);
  // startCore1() reports XMC_OK whether or not the task was created -- it
  // does not check what xTaskCreatePinnedToCore() returns -- and the stack it
  // asks for comes out of the same pool as the band buffers. So wait and see
  // rather than trusting the status.
  xmc::sleepMs(50);
  if (!g_core1Ran) {
    ds::trace("core1 did not start, free", ds::freeInternalRam());
  }
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
  // The timing overlay: FUNC (only the SDK's while the board boots, free
  // here), or UP on the title or the pause screen. The HUD's snapshot says
  // which screen: the Game itself may be core1's right now.
  const render::HudState &hud = g_renderer.hud();
  if (xmc::input::wasPressed(xmc::input::Button::FUNC) ||
      (xmc::input::wasPressed(xmc::input::Button::UP) &&
       (hud.state == sim::GameState::TITLE || hud.paused))) {
    g_prof.toggle();
  }
  serviceAmp(hud.muted, nowUs);

#if DS_SIM_ON_CORE1
  // If core1 is still ticking, come back on the next libLoop() rather than
  // spinning: this is the only core that runs system::service(), which polls
  // the power button and services the IO expander.
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
    drawFrame(ran);
  }

  // Ask for the next batch before rasterizing, so core1 ticks while we do.
  // This is what costs a frame of latency: these buttons reach the screen one
  // frame later.
  const int want = ticksDue();
  if (want > 0) {
    g_simWanted = want;
    g_simButtons = buttons;
    simStore(Sim::RUN);
  }

  if (ran > 0) presentFrame();
#else
  // Everything on this core. Nothing overlaps the ticks, so this is slower.
  const uint32_t tick0 = (uint32_t)xmc::getTimeUs();
  const int ticks = ticksDue();
  for (int i = 0; i < ticks; i++) {
    g_game->tick(buttons);
    g_renderer.pollEffects(*g_game);
    audio::setMuted(g_game->muted());
    audio::request(g_game->sounds());
  }
  g_prof.tickUs = (uint32_t)xmc::getTimeUs() - tick0;
  g_prof.ticks = ticks;
  g_prof.core1WaitUs = 0;
  if (ticks == 0) return;  // ahead of the simulation: nothing new to show
  keepHighScore();
  drawFrame(ticks);
  presentFrame();
#endif
}

XmcStatus xmcAppTerminate(xmc::system::ShutdownReason reason) {
  (void)reason;
  // requestShutdown() calls us before it deinits anything and then draws its
  // power-off message through the same SPI bus, so the band left in flight by
  // present() has to be collected before we return. core1 only touches the
  // Game, but let its batch finish rather than have it tick into a shutdown.
#if DS_SIM_ON_CORE1
  while (simLoad() == Sim::RUN) xmc::tightLoopContents();
#endif
  g_bands.drain();
  // A run cut short by the power button still keeps its record (the Game is
  // ours now, and the band is out of the DMA)
  if (g_game) g_store.flush(*g_game);
  return XMC_OK;
}
