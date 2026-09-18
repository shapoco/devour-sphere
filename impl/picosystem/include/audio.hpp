// Sound effects on the PicoSystem's piezo: one voice, streamed by DMA from
// flash into the PWM on PICOSYSTEM_AUDIO_PIN. See ../SPEC.md "効果音".
#pragma once

#include <cstdint>

namespace audio {

// Bring up the PWM and the DMA channel and parse the pack. Call once, before
// the core that will call request() starts.
void init();

// The sounds one simulation tick asked for (sim::Game::sounds(), one bit per
// sim::SoundKind). At most one of them starts: the highest priority, and
// only if it may take the voice from whatever is playing. Called from the
// core that runs the simulation, right after each tick; no other core
// touches the audio state.
void request(uint32_t bits);

// Whether a sound is playing (for the overlay / debugging)
bool playing();

}  // namespace audio
