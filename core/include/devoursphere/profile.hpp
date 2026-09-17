#ifndef DEVOURSPHERE_PROFILE_HPP
#define DEVOURSPHERE_PROFILE_HPP

// Optional phase timers for the simulation tick and the frame build.
//
// A platform that wants the breakdown installs a microsecond clock here;
// Game::tick() and Renderer::beginFrame() then split their time between
// their phases (Game::tickProfile(), Renderer::frameProfile()). With the
// clock left null the cost is one null check per phase, so this is always
// compiled in. The values are wall-clock microseconds and mean nothing for
// determinism: they are never read by the simulation.

#include <cstdint>

namespace devoursphere {

inline uint32_t (*profileClockUs)() = nullptr;

// Accumulates the time between successive stamp() calls into slots
struct PhaseTimer {
  static constexpr int MAX = 8;
  uint32_t us[MAX] = {};
  uint32_t last = 0;

  void begin() {
    if (profileClockUs) last = profileClockUs();
  }
  // Charge the time since the previous stamp (or begin()) to slot i
  void stamp(int i) {
    if (!profileClockUs) return;
    const uint32_t now = profileClockUs();
    us[i] += now - last;
    last = now;
  }
  void reset() {
    for (uint32_t &v : us) v = 0;
  }
};

}  // namespace devoursphere

#endif
