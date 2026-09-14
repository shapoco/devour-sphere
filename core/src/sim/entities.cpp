#include "devoursphere/sim/entities.hpp"

namespace devoursphere::sim {

// 2^(i/4) in Q16 for i = 0..3
static constexpr int32_t QUARTER_POW2_Q16[4] = {65536, 77936, 92682, 110218};

// The kite half-size grows with the fourth root of the size (doubling the
// size makes the fragment only 19% wider), so entities grow slowly on screen
int32_t fragmentHalfSize(int sizeLog2) {
  if (sizeLog2 < 0) sizeLog2 = 0;
  if (sizeLog2 > MAX_SIZE_LOG2) sizeLog2 = MAX_SIZE_LOG2;
  int32_t base = (FU / 2) << (sizeLog2 >> 2);
  return (int32_t)(((int64_t)base * QUARTER_POW2_Q16[sizeLog2 & 3]) >> 16);
}

int32_t altitudeForSize(uint32_t size) {
  (void)size;
  return ALTITUDE;
}

int32_t cruiseSpeedForSize(uint32_t size) {
  return SPEED_BASE + SPEED_PER_LOG2 * log2Floor(size);
}

}  // namespace devoursphere::sim
