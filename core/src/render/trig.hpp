#ifndef DEVOURSPHERE_RENDER_TRIG_HPP
#define DEVOURSPHERE_RENDER_TRIG_HPP

// Sine and cosine for the float parts of the render layer. sinf / cosf are
// library calls on a core without an FPU and cost a few thousand cycles
// each; these cost a multiply and a lookup in the simulation's table
// (1025 points, linear interpolation, error ~1e-6).

#include <cstdint>

#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::render {

static inline uint16_t radToBrad(float rad) {
  return (uint16_t)(int32_t)(rad * (65536.0f / 6.28318530717959f));
}
static inline float fastSin(float rad) {
  return sim::sinQ30(radToBrad(rad)) * (1.0f / (float)(1 << 30));
}
static inline float fastCos(float rad) {
  return sim::cosQ30(radToBrad(rad)) * (1.0f / (float)(1 << 30));
}

// The three corners of an equilateral triangle at angle a, a + 120 and
// a + 240 degrees, from one sine / cosine pair: (c, s) rotated by 120
// degrees is (-c/2 - s*r, -s/2 + c*r) with r = sqrt(3)/2.
static inline void triangleAngles(float a, float c[3], float s[3]) {
  constexpr float R = 0.86602540378f;
  c[0] = fastCos(a);
  s[0] = fastSin(a);
  c[1] = -0.5f * c[0] - R * s[0];
  s[1] = -0.5f * s[0] + R * c[0];
  c[2] = -0.5f * c[0] + R * s[0];
  s[2] = -0.5f * s[0] - R * c[0];
}

}  // namespace devoursphere::render

#endif
