#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

static const int32_t SIN_TABLE[1025] = {
#include "sin_table.inc"
};

int32_t sinQ30(uint16_t brad) {
  uint32_t a = brad;
  bool neg = (a & 0x8000) != 0;
  a &= 0x7FFF;                   // half turn: 0..32767
  if (a > 16384) a = 32768 - a;  // mirror the second quarter
  uint32_t i = a >> 4, f = a & 15;
  int32_t v = SIN_TABLE[i];
  if (f) v += (int32_t)(((int64_t)(SIN_TABLE[i + 1] - v) * (int32_t)f) >> 4);
  return neg ? -v : v;
}

int32_t divQ30(int32_t a, int32_t b) {
  if (b == 0) return a < 0 ? INT32_MIN : INT32_MAX;
  int64_t r = ((int64_t)a * Q30_ONE) / b;
  return (int32_t)clampI64(INT32_MIN, INT32_MAX, r);
}

// Newton iteration from an initial guess >= sqrt(v): the sequence decreases
// monotonically to floor(sqrt(v)). Integer only, hence deterministic.
uint32_t isqrt32(uint32_t v) {
  if (v < 2) return v;
  int bits = 32 - __builtin_clz(v);
  uint32_t r = 1u << ((bits + 1) >> 1);
  for (;;) {
    uint32_t nr = (r + v / r) >> 1;
    if (nr >= r) return r;
    r = nr;
  }
}

uint32_t isqrt64(uint64_t v) {
  if (v < ((uint64_t)1 << 32)) return isqrt32((uint32_t)v);
  int bits = 64 - __builtin_clzll(v);
  uint64_t r = (uint64_t)1 << ((bits + 1) >> 1);
  for (;;) {
    uint64_t nr = (r + v / r) >> 1;
    if (nr >= r) return (uint32_t)r;
    r = nr;
  }
}

// atan(x) for x in [0, 1] (Q15) in brad; polynomial approximation
static uint16_t atanUnitBrad(int32_t x) {
  // atan(x) ~= (pi/4) x - x (x - 1) (0.2447 + 0.0663 x), scaled to brad
  int32_t term1 = x >> 2;                                   // 8192 * x
  int32_t u = (int32_t)(((int64_t)x * (x - 32768)) >> 15);  // x (x - 1), <= 0
  int32_t coef = 2552 + (int32_t)(((int64_t)691 * x) >> 15);
  int32_t term2 = (int32_t)(((int64_t)u * coef) >> 15);
  return (uint16_t)(term1 - term2);
}

uint16_t atan2Brad(int32_t y, int32_t x) {
  if (x == 0 && y == 0) return 0;
  int64_t ax = x < 0 ? -(int64_t)x : x, ay = y < 0 ? -(int64_t)y : y;
  uint16_t a;
  if (ax >= ay) {
    a = atanUnitBrad((int32_t)((ay << 15) / ax));
  } else {
    a = (uint16_t)(BRAD_QUARTER - atanUnitBrad((int32_t)((ax << 15) / ay)));
  }
  if (x < 0) a = (uint16_t)(BRAD_HALF - a);
  if (y < 0) a = (uint16_t)(0 - a);
  return a;
}

Vec3 normalizeQ30(const Vec3 &in) {
  Vec3 a = in;
  // Keep the squared length representable in 64 bits
  for (;;) {
    int32_t m = absI32(a.x);
    if (absI32(a.y) > m) m = absI32(a.y);
    if (absI32(a.z) > m) m = absI32(a.z);
    if (m <= 0x5FFFFFFF) break;
    a.x >>= 1, a.y >>= 1, a.z >>= 1;
  }
  uint64_t len2 = (uint64_t)dot64(a, a);
  uint32_t len = isqrt64(len2);
  if (len == 0) return {0, 0, Q30_ONE};
  return {
      (int32_t)(((int64_t)a.x * Q30_ONE) / (int64_t)len),
      (int32_t)(((int64_t)a.y * Q30_ONE) / (int64_t)len),
      (int32_t)(((int64_t)a.z * Q30_ONE) / (int64_t)len),
  };
}

Vec3 rotateAroundQ30(const Vec3 &t, const Vec3 &axis, uint16_t angle) {
  int32_t c = cosQ30(angle), s = sinQ30(angle);
  Vec3 k = crossQ30(axis, t);
  int32_t d = dotQ30(axis, t);
  int32_t ic = Q30_ONE - c;
  Vec3 r = scaleQ30(t, c) + scaleQ30(k, s) + scaleQ30(axis, mulQ30(d, ic));
  return normalizeQ30(r);
}

Vec3 orthonormalizeQ30(const Vec3 &t, const Vec3 &n) {
  int32_t d = dotQ30(t, n);
  Vec3 r = t - scaleQ30(n, d);
  return normalizeQ30(r);
}

}  // namespace devoursphere::sim
