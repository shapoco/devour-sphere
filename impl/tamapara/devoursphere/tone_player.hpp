#ifndef DS_TONE_PLAYER_HPP
#define DS_TONE_PLAYER_HPP

// The Paradise's side of the audio:: interface (impl/xiamocon/devoursphere/
// include/se_player.hpp, which high_score_store.hpp reads): the buzzer is a
// timer PWM that plays one square tone at a time (hw_tone), so each
// sim::SoundKind is a short tune of up to four notes, the ESPboy's tunes.
// The notes advance in the millisecond tick.

#include "se_player.hpp"

namespace audio {

// From the SysTick handler (runs from PRAM): advance the tune that plays
void tick1ms();

// Once a frame, from the main loop (nothing to do here; kept for symmetry
// with the ESPboy build)
void poll();

}  // namespace audio

#endif
