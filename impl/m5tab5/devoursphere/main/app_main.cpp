// Devour Sphere on M5Stack Tab5 (ESP32-P4).
//
// The game core (core/) is shared with the WASM front end and the two
// handhelds; everything here is the platform layer: bringing the board up,
// the frame loop, the touch pad and the memory the core is given. None of
// M5GFX's drawing is used -- the frame is rasterized by core/render into
// band buffers and pushed by PanelOut (panel_out.hpp) -- but M5Unified is
// what brings up the DSI panel, the touch controller and the speaker, which
// is the same bargain the Xiamocon front end strikes with its SDK.
//
// Cores, as on both handhelds: core1 runs the simulation while core0 builds
// the scene, rasterizes and feeds the PPA. core0 asks for a batch of ticks
// and then draws the frame the previous batch left behind, so the ticks
// happen while the frame goes out. It costs one frame of input latency,
// which is inherent to overlapping them.

#include <M5Unified.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "devoursphere/devoursphere.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"
#include "high_score_store.hpp"
#include "panel_out.hpp"
#include "profiler.hpp"
#include "se_player.hpp"
#include "touch_pad.hpp"

namespace sim = devoursphere::sim;
namespace render = devoursphere::render;

namespace {

sim::Game *g_game = nullptr;
render::Renderer g_renderer;
ds::HighScoreStore g_store;
alignas(8) uint8_t g_arena[ds::ARENA_SIZE];
ds::PanelOut g_panel;
ds::TouchPad g_pad;
ds::Profiler g_prof;

uint64_t g_lastUs = 0;
uint32_t g_accUs = 0;

// --- The handover between the cores -----------------------------------------
// A pair of task notifications rather than a polled flag: core0 hands core1
// a batch of ticks and then has nothing to do until it comes back, and a
// poll would either spin (starving core0's own callbacks) or sleep on the
// scheduler tick, which at 1 kHz is a sizeable slice of a frame to give
// away twice over. The notification also carries the barrier the payload
// below needs -- it is written before the give and read after the take, and
// never touched while the other core owns it.
TaskHandle_t g_mainTask = nullptr;
TaskHandle_t g_simTask = nullptr;
int g_simWanted = 0;       // core0 -> core1
uint8_t g_simButtons = 0;  // ... same
int g_simRan = 0;          // core1 -> core0
uint32_t g_simTickUs = 0;  // ... same
bool g_batchOut = false;   // core0 only: a batch is with core1
volatile bool g_core1Ran = false;

void simTask(void *) {
  ds::stackWatchInitCore1();
  g_core1Ran = true;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < g_simWanted; i++) {
      g_game->tick(g_simButtons);
      // A tick's events are cleared by the next one, so each has to be
      // polled: otherwise a frame that catches up two ticks shows only the
      // last one's explosions.
      g_renderer.pollEffects(*g_game);
      audio::setMuted(g_game->muted());
      audio::request(g_game->sounds());
    }
    g_simTickUs = (uint32_t)(esp_timer_get_time() - t0);
    g_simRan = g_simWanted;
    xTaskNotifyGive(g_mainTask);
  }
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
// store decides when it reaches the flash. Only safe while the Game is ours.
void keepHighScore() {
  g_game->keepHighScore();
  g_store.poll(*g_game);
}

void drawPad(const shapoco::gfx2d::Surface &band, int bandY, void *) {
  g_pad.draw(band, bandY);
}

// The pad, plus the two things this front end does with it that the game
// does not: the timing overlay on UP (as on the handhelds; the mute is DOWN
// and the game handles that itself).
uint8_t readInput() {
  M5.update();
  const uint8_t buttons = g_pad.poll();
  static uint8_t prevButtons = 0;
  const uint8_t pressed = (uint8_t)(buttons & ~prevButtons);
  prevButtons = buttons;
  // Which screen it is has to come from the HUD's snapshot: the Game itself
  // may belong to the other core right now.
  const render::HudState &hud = g_renderer.hud();
  if ((pressed & sim::Button::UP) &&
      (hud.state == sim::GameState::TITLE || hud.paused)) {
    g_prof.toggle();
  }
  return buttons;
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = true;
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  M5.begin(cfg);
  // Landscape. panel_out.cpp turns the frame by the same quarter, so the
  // touch coordinates M5 reports land on the pad we drew; nothing of M5GFX's
  // own drawing is used.
  M5.Display.setRotation(ds::PANEL_ROTATION);
  M5.Display.setBrightness(255);

  nvs_flash_init();
  g_mainTask = xTaskGetCurrentTaskHandle();
  ds::stackWatchInitCore0();
  ds::trace("internal free", ds::freeInternalRam());
  ds::trace("internal largest block", ds::largestInternalBlock());
  ds::trace("psram free", ds::freeSpiRam());

  g_game = ds::allocGame();
  g_game->reset(ds::randomSeed());
  g_store.load(*g_game);  // before core1, which has no business in the NVS

  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena),
                  ds::SPAN_CAPACITY);
  g_renderer.setHudInsets(render::HudInsets{ds::HUD_INSET_LEFT,
                                            ds::HUD_INSET_RIGHT,
                                            ds::HUD_INSET_TOP,
                                            ds::HUD_INSET_BOTTOM});
  g_renderer.setControlHints(render::ControlHints{
      "DISC: STEER   A: FIRE   B: DODGE   TOP RIGHT: PAUSE",
      "DISC: STEER  A: FIRE  B: DODGE",
      "DISC UP / DOWN: DASH / BRAKE",
      "UP / DOWN: DASH / BRAKE",
      "PAUSE AGAIN TO RESUME    DOWN: SOUND    UP: STATS",
      "PAUSE: RESUME   DOWN: SOUND",
  });

  audio::init(audio::Config{-1, audio::Pacing::DMA_TIMER, true});

  if (!g_panel.init(&M5.Display)) {
    ds::trace("display pipeline failed", 0);
    g_game = nullptr;  // loop() bails out
    return;
  }
  // Both cores are otherwise idle here, so this is the panel write on its
  // own -- the one cost of a frame that does not depend on what is in it.
  g_prof.xferUs = g_panel.measureTransfer();
  ds::trace("panel write us", g_prof.xferUs);

  // Start owing one tick, so the first loop has something to do.
  g_lastUs = (uint64_t)esp_timer_get_time();
  g_accUs = ds::TICK_US;

#if DS_SIM_ON_CORE1
  // Pinned to the core app_main does not run on (app_main is CPU0 by
  // default). It blocks on its notification when idle, so the priority only
  // decides how quickly it picks a batch up.
  if (xTaskCreatePinnedToCore(simTask, "ds_sim", ds::STACK_BYTES_CORE1,
                              nullptr, 5, &g_simTask, 1) != pdPASS) {
    ds::trace("sim task not created, internal free", ds::freeInternalRam());
    g_game = nullptr;
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  if (!g_core1Ran) ds::trace("sim task did not run", ds::freeInternalRam());
#endif
  ds::trace("internal free after setup", ds::freeInternalRam());
}

void loop() {
  if (!g_game) {
    vTaskDelay(pdMS_TO_TICKS(500));
    return;
  }
  const uint64_t nowUs = (uint64_t)esp_timer_get_time();
  uint64_t deltaUs = nowUs - g_lastUs;
  g_lastUs = nowUs;
  // A long stall must not become a burst of simulation, so the elapsed time
  // is clamped before it reaches the accumulator as well as after.
  if (deltaUs > (uint64_t)ds::MAX_CATCHUP * ds::TICK_US) {
    deltaUs = (uint64_t)ds::MAX_CATCHUP * ds::TICK_US;
  }
  g_accUs += (uint32_t)deltaUs;

#if DS_SIM_ON_CORE1
  // Collect the batch core1 is running. Blocking here is the point: this
  // core has nothing of its own to do until the simulation has caught up,
  // and the scheduler can run the touch and the PPA callbacks meanwhile.
  int ran = 0;
  if (g_batchOut) {
    const int64_t w0 = esp_timer_get_time();
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    g_prof.core1WaitUs = (uint32_t)(esp_timer_get_time() - w0);
    g_batchOut = false;
    ran = g_simRan;
    g_prof.tickUs = g_simTickUs;
    g_prof.ticks = ran;
  }

  // Build the scene from the state that batch left behind. The Game is ours
  // from here until the next dispatch, and the simulation must not advance
  // between beginFrame() and the last band: renderBand() draws the HUD.
  if (ran > 0) {
    keepHighScore();
    const int64_t b0 = esp_timer_get_time();
    // Simulation time, not wall time: the camera smoothing, the debris and
    // the score roll-up advance by this, and have to stay in step with the
    // ticks that actually ran.
    g_renderer.beginFrame(*g_game, ran * (1.0f / sim::TICK_RATE));
    g_prof.beginUs = (uint32_t)(esp_timer_get_time() - b0);
  }

  // Read the pad as late as possible -- after the wait and after the scene
  // is built -- because these buttons go to the batch that runs while this
  // frame rasterizes, and reach the screen one frame later. That last frame
  // of latency is inherent to overlapping the two; anything before it is not.
  const uint8_t buttons = readInput();

  // Dispatch before rasterizing, so core1 ticks while we draw.
  const int want = ticksDue();
  if (want > 0) {
    g_simWanted = want;
    g_simButtons = buttons;
    g_batchOut = true;
    xTaskNotifyGive(g_simTask);
  }

  if (ran > 0) {
    g_panel.present(g_renderer, g_prof, drawPad, nullptr);
    g_prof.endFrame((uint64_t)esp_timer_get_time(), g_renderer.stats());
  } else if (!g_batchOut) {
    ds::frameIdle();  // ahead of the simulation with nothing dispatched
  }
#else
  // Everything on this core. Nothing overlaps the ticks, so this is slower.
  const uint8_t buttons = readInput();
  const int64_t t0 = esp_timer_get_time();
  const int ticks = ticksDue();
  for (int i = 0; i < ticks; i++) {
    g_game->tick(buttons);
    g_renderer.pollEffects(*g_game);
    audio::setMuted(g_game->muted());
    audio::request(g_game->sounds());
  }
  g_prof.tickUs = (uint32_t)(esp_timer_get_time() - t0);
  g_prof.ticks = ticks;
  g_prof.core1WaitUs = 0;
  if (ticks == 0) {
    ds::frameIdle();
    return;
  }
  keepHighScore();
  const int64_t b0 = esp_timer_get_time();
  g_renderer.beginFrame(*g_game, ticks * (1.0f / sim::TICK_RATE));
  g_prof.beginUs = (uint32_t)(esp_timer_get_time() - b0);
  g_panel.present(g_renderer, g_prof, drawPad, nullptr);
  g_prof.endFrame((uint64_t)esp_timer_get_time(), g_renderer.stats());
#endif
}

}  // namespace

extern "C" void app_main(void) {
  setup();
  for (;;) loop();
}
