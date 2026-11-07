// A one-voice tune player on the Paradise's buzzer. The buzzer hangs on
// timer CT32B7 as a PWM (hw_tone starts and stops it), so the CPU only
// changes the pitch at the note boundaries, from the millisecond tick.
//
// The tunes, the priorities and the hold are the ESPboy build's
// (impl/espboy/devoursphere/main/tone_player.cpp): one voice, the highest
// priority of a tick wins, and a lower one waits HOLD_MS into the sound
// that plays.

#include "tone_player.hpp"

#include "devoursphere/sim/game.hpp"
#include "ds_config.hpp"
#include "ds_platform.hpp"

extern "C" {
#include "hw.h"
}

#define PRAM_TEXT __attribute__((section(".pram_text.tone")))

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

// Read from the tick too, so it lives in PRAM
__attribute__((section(".pram_rodata.tone"))) constexpr Tune TUNES[(int)SoundKind::COUNT] = {
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
constexpr uint32_t HOLD_MS = 400;

// What the tick reads and writes. The main loop changes it with the
// interrupts off.
struct Voice {
  volatile bool active = false;
  volatile int kind = 0;
  volatile int note = 0;
  volatile uint32_t left = 0;  // ms left in the current note
};
Voice g_voice;
bool g_muted = false;
int g_priority = -1;
uint32_t g_startMs = 0;

inline void irqOff() { __asm__ volatile("cpsid i" ::: "memory"); }
inline void irqOn() { __asm__ volatile("cpsie i" ::: "memory"); }

void start(int kind) {
  const Tune &t = TUNES[kind];
  irqOff();
  g_voice.kind = kind;
  g_voice.note = 0;
  g_voice.left = t.notes[0].ms;
  g_voice.active = true;
  hw_tone(t.notes[0].hz);
  irqOn();
  g_priority = t.priority;
  g_startMs = ds::nowMs();
}

}  // namespace

// The tick: count the note down, step to the next one, stop at the end
PRAM_TEXT void tick1ms() {
  Voice &v = g_voice;
  if (!v.active) return;
  if (v.left > 0 && --v.left > 0) return;
  const Tune &t = TUNES[v.kind];
  const int n = v.note + 1;
  if (n >= MAX_NOTES || t.notes[n].ms == 0) {
    hw_tone(0);
    v.active = false;
    return;
  }
  v.note = n;
  v.left = t.notes[n].ms;
  hw_tone(t.notes[n].hz);
}

void init(const Config &) { hw_tone(0); }

void request(uint32_t bits) {
  if (g_muted || bits == 0) return;
  int best = -1;
  for (int k = 0; k < (int)SoundKind::COUNT; k++) {
    if (!(bits & (1u << k))) continue;
    if (best < 0 || TUNES[k].priority > TUNES[best].priority) best = k;
  }
  if (best < 0) return;
  if (playing() && TUNES[best].priority < g_priority &&
      ds::nowMs() - g_startMs < HOLD_MS) {
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
  irqOff();
  g_voice.active = false;
  hw_tone(0);
  irqOn();
}

void poll() {}

}  // namespace audio
