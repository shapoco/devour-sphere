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
// monotonically to floor(sqrt(v)). Integer only, hence deterministic. The
// division is 32-bit, which every target does in hardware (the RP2040 through
// its SIO divider).
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

// Digit by digit, two bits of the argument per step, and exact: the same
// floor(sqrt(v)) the Newton form gave. No division at all. Newton on a 64-bit
// value needs a 64-bit division per step, and that is a library call on
// every embedded target (some hundred cycles on a Cortex-M0+); this is called
// about two thousand times a tick (the fragment layout forces and every
// normalize), so it was most of the tick's division cost.
uint32_t isqrt64(uint64_t v) {
  if (v < ((uint64_t)1 << 32)) return isqrt32((uint32_t)v);
  // Start at the highest even bit position at or below the top set bit
  int top = 63 - __builtin_clzll(v);
  uint64_t bit = (uint64_t)1 << (top & ~1);
  uint64_t r = 0;
  while (bit) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return (uint32_t)r;
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

// One 64-bit division (the reciprocal of the length) and three multiplies,
// instead of the three exact divisions this used to do. The result can differ
// from the exact quotient by a couple of Q30 steps, which is far below what
// the simulation resolves; it is not bit-identical to the older form.
Vec3 normalizeQ30(const Vec3 &in) {
  // 64-bit working copies: the scaling below multiplies and shifts signed
  // values, and 64 bits keeps every step in range (and away from the
  // undefined left shift of a negative int32)
  int64_t x = in.x, y = in.y, z = in.z;
  uint64_t m = (uint64_t)(x < 0 ? -x : x);
  uint64_t my = (uint64_t)(y < 0 ? -y : y), mz = (uint64_t)(z < 0 ? -z : z);
  if (my > m) m = my;
  if (mz > m) m = mz;
  if (m == 0) return {0, 0, Q30_ONE};
  // Bring the largest magnitude into [2^29, 2^30): keeps the squared length
  // in 64 bits, and gives a short vector the same precision as a long one
  const int top = 63 - __builtin_clzll(m);  // 0..31
  if (top < 29) {
    const int64_t k = (int64_t)1 << (29 - top);
    x *= k, y *= k, z *= k;
  } else if (top > 29) {
    const int sh = top - 29;
    x >>= sh, y >>= sh, z >>= sh;
  }
  const uint64_t len2 = (uint64_t)(x * x + y * y + z * z);  // [2^58, 3 * 2^60)
  const uint32_t len = isqrt64(len2);                       // [2^29, 2^31)
  // inv = 2^61 / len is in (2^30, 2^32]; a component is below 2^30 in
  // magnitude, so the product stays below 2^62. Then
  // x / len * 2^30 = (x * inv) >> 31.
  const int64_t inv = (int64_t)(((uint64_t)1 << 61) / len);
  return {
      (int32_t)((x * inv) >> 31),
      (int32_t)((y * inv) >> 31),
      (int32_t)((z * inv) >> 31),
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
