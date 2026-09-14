#include "devoursphere/sim/entities.hpp"

namespace devoursphere::sim {

static constexpr int32_t SQRT2_Q16 = 92682;

int32_t fragmentHalfSize(int sizeLog2) {
  if (sizeLog2 < 0) sizeLog2 = 0;
  if (sizeLog2 > MAX_SIZE_LOG2) sizeLog2 = MAX_SIZE_LOG2;
  int32_t base = (FU / 2) << (sizeLog2 >> 1);
  if (sizeLog2 & 1) base = (int32_t)(((int64_t)base * SQRT2_Q16) >> 16);
  return base;
}

int32_t altitudeForSize(uint32_t size) {
  int k = log2Floor(size);
  if (k > ALT_MAX_LOG2) k = ALT_MAX_LOG2;
  return ALT_MIN + (fragmentHalfSize(ALT_MAX_LOG2) - fragmentHalfSize(k)) *
                       ALT_FACTOR_NUM / ALT_FACTOR_DEN;
}

int32_t cruiseSpeedForSize(uint32_t size) {
  return SPEED_BASE + SPEED_PER_LOG2 * log2Floor(size);
}

}  // namespace devoursphere::sim
