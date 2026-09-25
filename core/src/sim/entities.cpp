#include "devoursphere/sim/entities.hpp"

namespace devoursphere::sim {

// 2^(i/4) in Q16 for i = 0..3
static constexpr int32_t QUARTER_POW2_Q16[4] = {65536, 77936, 92682, 110218};

// The kite half-size grows with the fourth root of the size (doubling the
// size makes the fragment only 19% wider), so entities grow slowly on screen
static constexpr int32_t halfSizeOf(int sizeLog2) {
  int32_t base = (FU / 2) << (sizeLog2 >> 2);
  return (int32_t)(((int64_t)base * QUARTER_POW2_Q16[sizeLog2 & 3]) >> 16);
}

// Tabulated: this is asked about four thousand times a tick (every force in
// the fragment layout), and the 64-bit multiply is not free everywhere.
static constexpr int32_t HALF_SIZE[MAX_SIZE_LOG2 + 1] = {
    halfSizeOf(0),  halfSizeOf(1),  halfSizeOf(2),  halfSizeOf(3),
    halfSizeOf(4),  halfSizeOf(5),  halfSizeOf(6),  halfSizeOf(7),
    halfSizeOf(8),  halfSizeOf(9),  halfSizeOf(10), halfSizeOf(11),
    halfSizeOf(12), halfSizeOf(13), halfSizeOf(14), halfSizeOf(15),
    halfSizeOf(16), halfSizeOf(17), halfSizeOf(18), halfSizeOf(19),
    halfSizeOf(20),
};
static_assert(MAX_SIZE_LOG2 == 20, "HALF_SIZE lists one entry per exponent");
static_assert(sizeof(Fragment) == 16, "Fragment grew");
static_assert(sizeof(Entity) == 224, "Entity grew");
static_assert(sizeof(Entity) == 224, "Entity grew (see the layout note)");
static_assert(MAX_ENTITIES < NO_ENTITY, "evadeFrom is a byte");

int32_t fragmentHalfSize(int sizeLog2) {
  if (sizeLog2 < 0) sizeLog2 = 0;
  if (sizeLog2 > MAX_SIZE_LOG2) sizeLog2 = MAX_SIZE_LOG2;
  return HALF_SIZE[sizeLog2];
}

// The fragment attraction scales with the body, so a fragment reaches a
// huge player as readily as it reaches a new one (see ATTRACT_ACCEL)
int32_t attractScaleQ8(uint32_t size) {
  int32_t q8 = (int32_t)(((int64_t)fragmentHalfSize(log2Floor(size)) << 8) /
                         fragmentHalfSize(PLAYER_START_SIZE_LOG2));
  if (q8 < 256) q8 = 256;
  if (q8 > ATTRACT_SCALE_MAX_Q8) q8 = ATTRACT_SCALE_MAX_Q8;
  return q8;
}

int32_t altitudeForSize(uint32_t size) {
  (void)size;
  return ALTITUDE;
}

int32_t cruiseSpeedForSize(uint32_t size) {
  return SPEED_BASE + SPEED_PER_LOG2 * log2Floor(size);
}

}  // namespace devoursphere::sim
