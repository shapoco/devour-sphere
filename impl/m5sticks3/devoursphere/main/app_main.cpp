// Devour Sphere on M5StickS3 (ESP32-S3, 135x240 ST7789, BMI270, ES8311).
//
// The game core (core/) is shared with the WASM front end, the two handhelds
// and the Tab5; everything here is the platform layer: bringing the board
// up, working out which way the stick has been tipped, the frame loop, the
// tilt controls and the memory the core is given. None of M5GFX's drawing
// is used once the game starts -- the frame is rasterized by core/render
// into band buffers and pushed by PanelOut (panel_out.hpp) -- but M5Unified
// is what brings up the panel, the PMIC, the codec, the IMU and the
// buttons, which is the same bargain the Xiamocon front end strikes with
// its SDK.
//
// Cores, as everywhere else: core1 runs the simulation while core0 builds
// the scene, rasterizes and pushes the bands. core0 asks for a batch of
// ticks and then draws the frame the previous batch left behind, so the
// ticks happen while the frame goes out. It costs one frame of input
// latency, which is inherent to overlapping them.
//
// The controls, all of them:
//
//   tip the stick        LEFT / RIGHT, and UP (dash) / DOWN (brake) by
//                        tipping its far edge away or near
//   KEY1 (BtnA, GPIO11)  fire, and confirm on the menus
//   KEY2 (BtnB, GPIO12)  emergency dodge
//   shake it             pause, and shake again to resume
//   both keys            the timing overlay, on the title or pause screen
//
// The stick has no third button, which is why the pause is a shake: the
// game wants five controls and there are two. The tilt is held clear while
// the device is being shaken, so a shake cannot also be a turn.

#include <M5Unified.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <cstdio>

#include "attitude.hpp"
#include "devoursphere/devoursphere.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"
#include "high_score_store.hpp"
#include "panel_out.hpp"
#include "profiler.hpp"
#include "se_player.hpp"

namespace sim = devoursphere::sim;
namespace render = devoursphere::render;

namespace {

sim::Game *g_game = nullptr;
render::Renderer g_renderer;
ds::HighScoreStore g_store;
alignas(8) uint8_t g_arena[ds::ARENA_SIZE];
ds::PanelOut g_panel;
ds::Attitude g_att;
ds::Profiler g_prof;
bool g_haveImu = false;

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

// --- The boot screen --------------------------------------------------------
// Drawn with M5GFX's own text, in the panel's native portrait orientation,
// before the renderer exists -- the one place in this front end where the
// display is used as a display rather than as somewhere to push bands.
//
// It asks for the one thing the software cannot work out for itself: which
// way round the stick is going to be held. It also shows the gravity the
// axis mapping in attitude.cpp produces, as a line from the middle of the
// screen, because that mapping is the first thing to check on a new board
// and this makes it a glance rather than a session with the serial log.
void drawBootScreen(ds::Attitude::Side side, bool still) {
  auto &d = M5.Display;
  const int w = d.width(), h = d.height();
  d.startWrite();
  d.fillScreen(0x0000);
  d.setTextColor(0xFFFF, 0x0000);
  d.setTextDatum(textdatum_t::top_center);
  d.setTextSize(1);
  d.drawString("DEVOUR", w / 2, 8);
  d.drawString("SPHERE", w / 2, 20);

  d.setTextDatum(textdatum_t::middle_center);
  if (side == ds::Attitude::Side::UNKNOWN) {
    d.drawString("TIP THE STICK", w / 2, h / 2 - 46);
    d.drawString("ONTO ITS SIDE", w / 2, h / 2 - 34);
  } else if (!still) {
    d.drawString("HOLD IT STILL", w / 2, h / 2 - 40);
  } else {
    d.drawString("READY", w / 2, h / 2 - 40);
  }

  // Gravity, as a labelled needle from the centre: with the axis mapping
  // right the DOWN end points at the floor however the stick is held, and
  // it is the label rather than the needle that says so -- a needle alone
  // is read as an attitude indicator, which turns the wrong way round on
  // purpose (the picture turns with the device, gravity does not).
  const ds::Vec3f g = g_att.gravity();
  const int cx = w / 2, cy = h / 2 + 20, r = 30;
  d.drawCircle(cx, cy, r, 0x4208);
  const int ex = cx + (int)(g.x * r), ey = cy + (int)(g.y * r);
  d.drawLine(cx, cy, ex, ey, 0x07E0);
  d.fillCircle(ex, ey, 4, 0x07E0);
  d.setTextColor(0x07E0, 0x0000);
  d.drawString("DOWN", cx + (int)(g.x * (r + 14)), cy + (int)(g.y * (r + 14)));
  d.setTextColor(0xFFFF, 0x0000);
  d.drawString("THIS END", w / 2, cy + r + 32);
  d.drawString("AT THE FLOOR", w / 2, cy + r + 44);

  // ... and the numbers behind it, for the serial-less case
  char line[32];
  d.setTextDatum(textdatum_t::bottom_center);
  std::snprintf(line, sizeof(line), "%+.2f %+.2f %+.2f", g.x, g.y, g.z);
  d.drawString(line, w / 2, h - 8);
  d.endWrite();
}

// Hold here until the stick has been tipped onto a side and held still. The
// attitude it settles in becomes the neutral one, and which side it is
// chooses the landscape rotation. Returns that rotation.
int waitForAttitude() {
  uint64_t lastUs = (uint64_t)esp_timer_get_time();
  uint32_t lastDrawMs = 0;
  for (;;) {
    const uint64_t nowUs = (uint64_t)esp_timer_get_time();
    const float dt = (float)(nowUs - lastUs) * 1e-6f;
    lastUs = nowUs;
    M5.update();
    g_att.sample(dt);
    const ds::Attitude::Side side = g_att.side();
    const bool still = g_att.settled();
    if (side != ds::Attitude::Side::UNKNOWN && still) break;
    // The prompt is a whole-screen redraw over SPI, so it goes at about
    // 20 Hz while the sampling goes as fast as the loop does.
    const uint32_t nowMs = (uint32_t)(nowUs / 1000);
    if (nowMs - lastDrawMs >= 50) {
      lastDrawMs = nowMs;
      drawBootScreen(side, still);
    }
    vTaskDelay(1);
  }
  const int rotation = g_att.confirm();
  return rotation ? rotation : ds::ROTATION_TOP_RIGHT;
}

// --- Input ------------------------------------------------------------------
// The two buttons, the tilt and the shake, plus the two things this front
// end does with them that the game does not: the timing overlay, and
// keeping the tilt off the menus.
uint8_t readInput(float dtSec) {
  M5.update();
  uint8_t buttons = 0;
  if (M5.BtnA.isPressed()) buttons |= sim::Button::A;
  if (M5.BtnB.isPressed()) buttons |= sim::Button::B;

  // Which screen it is has to come from the HUD's snapshot: the Game itself
  // may belong to the other core right now.
  const render::HudState &hud = g_renderer.hud();
  const bool menu = hud.state == sim::GameState::TITLE ||
                    hud.state == sim::GameState::WEAPON_SELECT ||
                    hud.state == sim::GameState::DEAD || hud.paused;

  if (g_haveImu) {
    g_att.sample(dtSec);
    uint8_t tilt = g_att.keys();
    // On the menus the up/down axis is dropped. The game reads DOWN there
    // as the mute and UP (on other platforms) as the overlay, and a stick
    // that is merely being held at a slight angle would work them both.
    // Left and right stay: that is how a weapon is chosen.
    if (menu) tilt &= (uint8_t)~(sim::Button::UP | sim::Button::DOWN);
    buttons |= tilt;
    // A shake is the pause button this board does not have. The game takes
    // it as an edge, so it is offered for exactly one frame.
    if (g_att.takeShake()) buttons |= sim::Button::PAUSE;
  }

  // The overlay: both keys together, on the title or pause screen. It has
  // to be a chord because the stick's own two buttons are fire and dodge,
  // and UP -- which every other front end uses here -- is a tilt now.
  static bool prevBoth = false;
  const bool both = M5.BtnA.isPressed() && M5.BtnB.isPressed();
  if (both && !prevBoth &&
      (hud.state == sim::GameState::TITLE || hud.paused)) {
    g_prof.toggle();
    buttons &= (uint8_t)~(sim::Button::A | sim::Button::B);  // not also a fire
  }
  prevBoth = both;
  return buttons;
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = true;
  cfg.internal_mic = false;
  cfg.internal_imu = true;
  cfg.internal_rtc = false;
  M5.begin(cfg);
  M5.Display.setBrightness(255);
  nvs_flash_init();

  g_mainTask = xTaskGetCurrentTaskHandle();
  ds::stackWatchInitCore0();
  ds::trace("internal free", ds::freeInternalRam());
  ds::trace("internal largest block", ds::largestInternalBlock());
  ds::trace("psram free", ds::freeSpiRam());

  // The mixer runs in its own task; keep it off the core that rasterizes.
  // M5.begin() has already configured and started the speaker for this
  // board, and Speaker_Class::begin() returns early once it has begun, so
  // the task only moves if it is stopped first. Nothing is playing yet.
  {
    M5.Speaker.end();
    auto spk = M5.Speaker.config();
    spk.task_pinned_core = 1;
    M5.Speaker.config(spk);
    M5.Speaker.begin();
  }

  // Which way round the stick is held, before anything else is sized: the
  // rotation is a MADCTL bit, so this costs nothing but has to happen
  // before the renderer is told how big the screen is.
  g_haveImu = g_att.begin();
  const int rotation = g_haveImu ? waitForAttitude() : ds::ROTATION_TOP_RIGHT;
  M5.Display.setRotation(rotation);
  M5.Display.fillScreen(0x0000);
  ds::trace("rotation", (uint32_t)rotation);

  g_game = ds::allocGame();
  if (!g_game) return;  // loop() bails out
  g_game->reset(ds::randomSeed());
  g_store.load(*g_game);  // before core1, which has no business in the NVS

  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena),
                  ds::SPAN_CAPACITY);
  // Two lines each, because 240 pixels of a 480 pixel reference layout
  // leave room for about twenty characters.
  g_renderer.setControlHints(render::ControlHints{
      "TILT: STEER   KEY1: FIRE   KEY2: DODGE   SHAKE: PAUSE",
      "TILT: STEER  KEY1: FIRE",
      "TILT AWAY / NEAR: DASH / BRAKE",
      "TILT AWAY: DASH",
      "SHAKE AGAIN TO RESUME    TILT DOWN: SOUND    KEY1+KEY2: STATS",
      "SHAKE: RESUME   KEY1+2: STATS",
  });

  audio::init(audio::Config{-1, audio::Pacing::DMA_TIMER, true});

  if (!g_panel.init(&M5.Display)) {
    ds::trace("display pipeline failed", 0);
    g_game = nullptr;
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
  // default, and on an ESP32S3 the display answers to CPU0 alone). It
  // blocks on its notification when idle, so the priority only decides how
  // quickly it picks a batch up.
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
  const float dtSec = (float)deltaUs * 1e-6f;

#if DS_SIM_ON_CORE1
  // Collect the batch core1 is running. Blocking here is the point: this
  // core has nothing of its own to do until the simulation has caught up.
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

  // Read the controls as late as possible -- after the wait and after the
  // scene is built -- because they go to the batch that runs while this
  // frame rasterizes, and reach the screen one frame later. That last frame
  // of latency is inherent to overlapping the two; anything before it is not.
  const uint8_t buttons = readInput(dtSec);

  // Dispatch before rasterizing, so core1 ticks while we draw.
  const int want = ticksDue();
  if (want > 0) {
    g_simWanted = want;
    g_simButtons = buttons;
    g_batchOut = true;
    xTaskNotifyGive(g_simTask);
  }

  if (ran > 0) {
    g_panel.present(g_renderer, g_prof);
    g_prof.endFrame((uint64_t)esp_timer_get_time(), g_renderer.stats());
  } else if (!g_batchOut) {
    ds::frameIdle();  // ahead of the simulation with nothing dispatched
  }
#else
  // Everything on this core. Nothing overlaps the ticks, so this is slower.
  const uint8_t buttons = readInput(dtSec);
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
  g_panel.present(g_renderer, g_prof);
  g_prof.endFrame((uint64_t)esp_timer_get_time(), g_renderer.stats());
#endif
}

}  // namespace

extern "C" void app_main(void) {
  setup();
  for (;;) loop();
}
