// Sound effects: one voice with priorities, fed from a pack of PCM in flash.
// Shared by the Xiamocon (RP2350 and ESP32S3) and the PicoSystem builds; the
// way the samples reach the speaker differs per chip (see se_player.cpp and
// each SPEC.md, "効果音"):
//   RP2040 / RP2350  DMA from the pack straight into a PWM compare register,
//                    no CPU while a sound plays
//   ESP32S3          I2S in PDM mode; a small task copies the samples into
//                    the driver's DMA ring
#pragma once

#include <cstdint>

namespace audio {

// RP2 only: how the DMA is paced to the sample rate
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
  // RP2 only: where the output rests between sounds: 0 (a piezo: no
  // switching) or the middle level (an AC-coupled amplifier: no step when a
  // sound starts)
  bool idleAtMiddle;
};

// Bring up the output and parse the pack (g_sePack, linked in by the build
// from core/tools/pack_se.py). Call once, before the core that will call
// request() starts.
void init(const Config &cfg);

// The sounds one simulation tick asked for (sim::Game::sounds(), one bit per
// sim::SoundKind). At most one of them starts: the highest priority, and
// only if it may take the voice from whatever is playing. Called from the
// core that runs the simulation, right after each tick; no other core
// touches the selection state.
void request(uint32_t bits);

// Muted: stops what plays and ignores requests (the game raises none while
// muted anyway; this cuts a sound already playing)
void setMuted(bool muted);

// Whether a sound is playing (for the overlay, and for the flash write)
bool playing();

// Cut the sound that plays, if any; the next request starts one as usual.
// For the flash write: on the RP2 chips the DMA reads the pack straight from
// the flash, which the write takes away (high_score_store.hpp)
void stop();

}  // namespace audio
