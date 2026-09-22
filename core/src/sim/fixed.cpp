#include "devoursphere/sim/fixed.hpp"

#include "devoursphere/sim/config.hpp"  // DEVOURSPHERE_HOT_ATTR

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
DEVOURSPHERE_HOT_ATTR uint32_t isqrt32(uint32_t v) {
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
DEVOURSPHERE_HOT_ATTR uint32_t isqrt64(uint64_t v) {
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

// No 64-bit division and no 64-bit square root: the reciprocal of the
// length comes from a 32-bit square root of the top bits of the squared
// length, a 32-bit division and two Newton steps of the reciprocal square
// root, all in 32x32 -> 64 multiplies. On a Cortex-M0+ those are the
// operations that stay cheap. The result is within a few Q30 steps of the
// exact quotient (the harness measured 4 at worst), which is far below what
// the simulation resolves; it is not bit-identical to the older forms.
DEVOURSPHERE_HOT_ATTR Vec3 normalizeQ30(const Vec3 &in) {
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
  // q = len2 / 2^30 in 32 bits, [2^28, 3 * 2^30). We want g = 2^45 / sqrt(q)
  // = 2^60 / sqrt(len2), in (2^29, 2^31]; a component is then
  // c * 2^30 / sqrt(len2) = (c * g) >> 30.
  const uint32_t q = (uint32_t)(len2 >> 30);
  const uint32_t s = isqrt32(q);  // [2^14, 2^16): 32-bit Newton, hardware div
  uint64_t g = (uint64_t)(0xFFFFFFFFu / s) << 13;  // ~2^45 / s, 14 bits good
  // Newton for the reciprocal square root: g <- g (3 - q g^2 / 2^90) / 2,
  // with e = q g^2 / 2^90 kept in Q32. Each step doubles the good bits.
  for (int i = 0; i < 2; i++) {
    const uint64_t e = ((uint64_t)q * ((g * g) >> 32)) >> 26;  // Q32
    g = ((g >> 1) * ((3ull << 31) - (e >> 1))) >> 31;
  }
  const int64_t inv = (int64_t)g;
  return {
      (int32_t)((x * inv) >> 30),
      (int32_t)((y * inv) >> 30),
      (int32_t)((z * inv) >> 30),
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
