#ifndef DS_TONE_PLAYER_HPP
#define DS_TONE_PLAYER_HPP

// The ESPboy's side of the audio:: interface (impl/xiamocon/devoursphere/
// include/se_player.hpp, which high_score_store.hpp reads): a square-wave
// tone player on the speaker pin, with a short tune per sim::SoundKind
// instead of the PCM pack. See ../../SPEC.md "効果音".

#include "se_player.hpp"

namespace audio {

// Once a frame, from the task that owns the game: hands the timer back
// when a tune has ended, so the pin stops interrupting between sounds
void poll();

}  // namespace audio

#endif
