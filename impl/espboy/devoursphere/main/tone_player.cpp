// A one-voice square-wave player on GPIO0 (the ESPboy's speaker), driven by
// the ESP8266's FRC1 timer: the interrupt toggles the pin and counts the
// toggles of the current note, and moves the timer to the next note's
// half period itself. Nothing in the interrupt leaves IRAM, which is what
// lets it run through a flash write (the high score).
//
// The sounds are short tunes of up to four notes, one per sim::SoundKind
// (the PCM pack the other front ends play needs a DAC or a PWM fed by DMA,
// and this chip has neither on this pin). The priorities and the hold are
// the RP2 player's (impl/picosystem/SPEC.md "効果音"): one voice, the
// highest priority of a tick wins, and a lower one waits HOLD_US into the
// sound that plays.

#include "tone_player.hpp"

#include <driver/gpio.h>
#include <driver/hw_timer.h>
#include <esp8266/gpio_register.h>
#include <esp8266/timer_struct.h>
#include <esp_attr.h>
#include <esp_timer.h>

#include "devoursphere/sim/game.hpp"
#include "ds_config.hpp"

namespace audio {

namespace {

using devoursphere::sim::SoundKind;

constexpr int MAX_NOTES = 4;
struct Note {
  uint16_t hz;  // 0: a rest
  uint16_t ms;
};
struct Tune {
  uint8_t priority;
  Note notes[MAX_NOTES];
};

// The half period of each note, in timer ticks (the FRC1 at 80 MHz / 16 =
// 5 MHz), and how many half periods (pin toggles) the note lasts
constexpr uint32_t TICKS_PER_US = 5;
constexpr uint32_t REST_HALF_US = 1000;  // a silent note still counts time

constexpr Tune TUNES[(int)SoundKind::COUNT] = {
    /* SHOT_VULCAN */ {0, {{880, 15}, {660, 15}}},
    /* SHOT_LASER */ {0, {{1800, 20}, {2400, 25}}},
    /* SHOT_MISSILE */ {0, {{300, 40}, {420, 40}}},
    /* HIT_ENEMY */ {0, {{1200, 20}}},
    /* HIT_PLAYER */ {1, {{200, 60}, {150, 60}}},
    /* ENEMY_KILLED_SMALL */ {2, {{600, 40}, {900, 40}, {1200, 60}}},
    /* ENEMY_KILLED_BIG */ {3, {{500, 80}, {750, 80}, {1000, 160}}},
    /* PLAYER_KILLED */ {5, {{400, 150}, {300, 150}, {200, 300}, {120, 400}}},
    /* GET_FRAGMENT */ {1, {{1400, 25}}},
    /* GET_UPGRADE */ {3, {{900, 60}, {1200, 60}, {1800, 120}}},
    /* MENU_SELECT */ {0, {{1000, 20}}},
    /* MENU_START */ {2, {{800, 60}, {1600, 120}}},
    /* LAUNCH */ {4, {{300, 200}, {500, 200}, {800, 400}}},
    /* ARRIVE */ {4, {{800, 200}, {500, 200}, {300, 400}}},
    /* TIME_ALARM */ {1, {{2000, 60}, {0, 60}, {2000, 60}}},
    /* DODGE */ {1, {{1500, 30}, {1000, 30}}},
};

// A lower priority may take the voice once the playing sound is this old
constexpr int64_t HOLD_US = 400000;

// What the interrupt reads and writes. The main task only touches it with
// the timer disarmed.
struct Voice {
  volatile bool active = false;
  volatile int note = 0;         // index into notes
  volatile uint32_t left = 0;    // toggles left in the current note
  volatile uint32_t half[MAX_NOTES] = {};   // half period, ticks (0: rest)
  volatile uint32_t count[MAX_NOTES] = {};  // toggles per note
  volatile int notes = 0;
};
Voice g_voice;
bool g_timerArmed = false;
bool g_muted = false;
int g_priority = -1;
int64_t g_startUs = 0;

inline void pinHigh() { GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, 1u << ds::PIN_SPEAKER); }
inline void pinLow() { GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, 1u << ds::PIN_SPEAKER); }

// One half period: toggle, count, and step to the next note. With the
// timer reloading, writing load.data sets the next period.
void IRAM_ATTR onTimer(void *) {
  Voice &v = g_voice;
  if (!v.active) return;
  if (v.left > 0) {
    v.left--;
    if (v.half[v.note] != 0) {
      if (v.left & 1) pinHigh(); else pinLow();
    }
    if (v.left > 0) return;
  }
  // Next note
  v.note = v.note + 1;
  if (v.note >= v.notes) {
    pinLow();
    v.active = false;
    return;
  }
  v.left = v.count[v.note];
  const uint32_t h = v.half[v.note];
  frc1.load.data = h != 0 ? h : REST_HALF_US * TICKS_PER_US;
}

void disarm() {
  if (!g_timerArmed) return;
  hw_timer_disarm();
  g_timerArmed = false;
  pinLow();
}

void start(int kind) {
  disarm();
  const Tune &t = TUNES[kind];
  Voice &v = g_voice;
  int n = 0;
  for (; n < MAX_NOTES && t.notes[n].ms > 0; n++) {
    const Note &nt = t.notes[n];
    if (nt.hz > 0) {
      const uint32_t halfUs = 500000u / nt.hz;
      v.half[n] = halfUs * TICKS_PER_US;
      v.count[n] = (uint32_t)nt.ms * 1000u / halfUs;
    } else {
      v.half[n] = 0;
      v.count[n] = (uint32_t)nt.ms * 1000u / REST_HALF_US;
    }
    if (v.count[n] == 0) v.count[n] = 1;
  }
  v.notes = n;
  v.note = 0;
  v.left = v.count[0];
  v.active = true;
  const uint32_t h0 = v.half[0] != 0 ? v.half[0] : REST_HALF_US * TICKS_PER_US;
  // hw_timer_alarm_us() programs the divider (16), the reload and the
  // first period; the interrupt rewrites load.data for the notes after
  hw_timer_alarm_us(h0 / TICKS_PER_US, true);
  g_timerArmed = true;
  g_priority = t.priority;
  g_startUs = esp_timer_get_time();
}

}  // namespace

void init(const Config &) {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ull << ds::PIN_SPEAKER;
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  pinLow();
  hw_timer_init(onTimer, nullptr);
}

void request(uint32_t bits) {
  if (g_muted || bits == 0) return;
  int best = -1;
  for (int k = 0; k < (int)SoundKind::COUNT; k++) {
    if (!(bits & (1u << k))) continue;
    if (best < 0 || TUNES[k].priority > TUNES[best].priority) best = k;
  }
  if (best < 0) return;
  if (playing() && TUNES[best].priority < g_priority &&
      esp_timer_get_time() - g_startUs < HOLD_US) {
    return;  // the sound that plays keeps the voice for now
  }
  start(best);
}

void setMuted(bool muted) {
  g_muted = muted;
  if (muted) stop();
}

bool playing() { return g_voice.active; }

void stop() {
  g_voice.active = false;
  disarm();
}

void poll() {
  if (g_timerArmed && !g_voice.active) disarm();
}

}  // namespace audio
