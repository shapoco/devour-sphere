// Sound effects on an RP2040 / RP2350: one voice, streamed by DMA from a pack
// in flash into a PWM slice's compare register. Shared by the Xiamocon and
// the PicoSystem builds (see each SPEC.md, "効果音"); a stub on the ESP32S3.
#pragma once

#include <cstdint>

namespace audio {

// How the DMA is paced to the sample rate
enum class Pacing : uint8_t {
  PWM_WRAP,   // one sample per PWM period: the pack's wrap sets the rate
              // (the PicoSystem: a 22 kHz carrier is what its piezo wants)
  DMA_TIMER,  // a DMA pacing timer at the pack's sample rate; the PWM runs
              // at its own (higher) frequency (the Xiamocon: an amplifier
              // behind a low-pass)
};

struct Config {
  int pin;
  Pacing pacing;
  // Where the output rests between sounds: 0 (a piezo: no switching) or the
  // middle level (an AC-coupled amplifier: no step when a sound starts)
  bool idleAtMiddle;
};

// Bring up the PWM and the DMA channel and parse the pack (g_sePack, linked
// in by the build from core/tools/pack_se.py --pwm). Call once, before the
// core that will call request() starts.
void init(const Config &cfg);

// The sounds one simulation tick asked for (sim::Game::sounds(), one bit per
// sim::SoundKind). At most one of them starts: the highest priority, and
// only if it may take the voice from whatever is playing. Called from the
// core that runs the simulation, right after each tick; no other core
// touches the audio state.
void request(uint32_t bits);

// Muted: stops what plays and ignores requests (the game raises none while
// muted anyway; this cuts a sound already playing)
void setMuted(bool muted);

// Whether a sound is playing (for the overlay / debugging)
bool playing();

}  // namespace audio
