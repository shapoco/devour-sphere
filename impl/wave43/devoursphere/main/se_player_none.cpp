// Sound effects on the wave_43: none yet (SPEC.md). The audio:: interface of
// impl/xiamocon/devoursphere/include/se_player.hpp is still implemented,
// because the frame loop and high_score_store.hpp call it; every request is
// dropped.
//
// playing() answers false, so the high score reaches the NVS as soon as the
// game over screen comes up instead of waiting for a death sound that is not
// playing.

#include "se_player.hpp"

namespace audio {

void init(const Config &) {}
void request(uint32_t) {}
void setMuted(bool) {}
bool playing() { return false; }
void stop() {}

}  // namespace audio
