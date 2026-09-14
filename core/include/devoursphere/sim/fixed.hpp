#ifndef DEVOURSPHERE_SIM_FIXED_HPP
#define DEVOURSPHERE_SIM_FIXED_HPP

// Fixed-point arithmetic used by the simulation. Everything here is integer
// only so that the simulation produces bit-identical results on every
// platform (WASM, RP2350, native tests).
//
// Conventions:
// - "Q30": int32_t with 30 fractional bits (1.0 == 1 << 30). Used for unit
//   vectors, trigonometric values and dimensionless ratios in [-2, 2).
// - "brad": binary radians, uint16_t; 65536 == one full turn (2 pi).
// - "units": int32_t world lengths; see config.hpp for the scale.

#include <cstdint>

namespace devoursphere::sim {

constexpr int Q30_SHIFT = 30;
constexpr int32_t Q30_ONE = 1 << Q30_SHIFT;

constexpr uint16_t BRAD_QUARTER = 16384;  // 90 degrees
constexpr uint16_t BRAD_HALF = 32768;     // 180 degrees

// Degrees to brad (integer degrees; rounding to the nearest brad)
constexpr uint16_t degToBrad(int deg) {
  return (uint16_t)(((int64_t)deg * 65536 + 180) / 360);
}

// Multiply two Q30 values
static inline int32_t mulQ30(int32_t a, int32_t b) {
  return (int32_t)(((int64_t)a * b) >> Q30_SHIFT);
}

// Divide two Q30 values (b != 0); result saturates to int32_t
int32_t divQ30(int32_t a, int32_t b);

// Integer square root (floor)
uint32_t isqrt32(uint32_t v);
uint32_t isqrt64(uint64_t v);

// floor(log2(v)) for v >= 1 (0 for v == 0)
static inline int log2Floor(uint32_t v) {
  int r = 0;
  while (v >>= 1) r++;
  return r;
}

static inline int32_t absI32(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t clampI32(int32_t lo, int32_t hi, int32_t v) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline int64_t clampI64(int64_t lo, int64_t hi, int64_t v) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Trigonometry in Q30 from an angle in brad (table + linear interpolation)
int32_t sinQ30(uint16_t brad);
static inline int32_t cosQ30(uint16_t brad) {
  return sinQ30((uint16_t)(brad + BRAD_QUARTER));
}

// atan2 in brad (approximation, error < 0.1 degree); (0, 0) returns 0
uint16_t atan2Brad(int32_t y, int32_t x);

// ---------------------------------------------------------------------------
// Vec3: three int32_t components. Used both for Q30 unit vectors and for
// positions in world units.

struct Vec3 {
  int32_t x, y, z;
};

static inline Vec3 operator+(const Vec3 &a, const Vec3 &b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
static inline Vec3 operator-(const Vec3 &a, const Vec3 &b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
static inline Vec3 operator-(const Vec3 &a) { return {-a.x, -a.y, -a.z}; }
static inline bool operator==(const Vec3 &a, const Vec3 &b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Dot product of two Q30 vectors as a Q60 64-bit value
static inline int64_t dot64(const Vec3 &a, const Vec3 &b) {
  return (int64_t)a.x * b.x + (int64_t)a.y * b.y + (int64_t)a.z * b.z;
}
// Dot product of two Q30 vectors as Q30
static inline int32_t dotQ30(const Vec3 &a, const Vec3 &b) {
  return (int32_t)(dot64(a, b) >> Q30_SHIFT);
}
// Cross product of two Q30 vectors (Q30)
static inline Vec3 crossQ30(const Vec3 &a, const Vec3 &b) {
  return {
      (int32_t)(((int64_t)a.y * b.z - (int64_t)a.z * b.y) >> Q30_SHIFT),
      (int32_t)(((int64_t)a.z * b.x - (int64_t)a.x * b.z) >> Q30_SHIFT),
      (int32_t)(((int64_t)a.x * b.y - (int64_t)a.y * b.x) >> Q30_SHIFT),
  };
}
// Scale a vector by a Q30 factor
static inline Vec3 scaleQ30(const Vec3 &a, int32_t s) {
  return {mulQ30(a.x, s), mulQ30(a.y, s), mulQ30(a.z, s)};
}
// Scale a Q30 unit vector by an integer length: result in the same unit as
// `len` (e.g. world units)
static inline Vec3 scaleToLength(const Vec3 &unit, int32_t len) {
  return {
      (int32_t)(((int64_t)unit.x * len) >> Q30_SHIFT),
      (int32_t)(((int64_t)unit.y * len) >> Q30_SHIFT),
      (int32_t)(((int64_t)unit.z * len) >> Q30_SHIFT),
  };
}
// Normalize to a Q30 unit vector. The zero vector yields {0, 0, Q30_ONE}.
Vec3 normalizeQ30(const Vec3 &a);

// Squared length of an integer vector (64-bit)
static inline int64_t length2_64(const Vec3 &a) { return dot64(a, a); }

// Rotate the Q30 unit vector `t` around the Q30 unit vector `axis` by `angle`
// (right-handed; positive angles turn t towards axis x t)
Vec3 rotateAroundQ30(const Vec3 &t, const Vec3 &axis, uint16_t angle);

// Remove the component of `t` along the unit vector `n` and renormalize
Vec3 orthonormalizeQ30(const Vec3 &t, const Vec3 &n);

// ---------------------------------------------------------------------------
// Deterministic pseudo random generator (xorshift32)

class Random {
 public:
  explicit Random(uint32_t seed = 1) { reset(seed); }
  void reset(uint32_t seed) { s_ = seed ? seed : 0x9E3779B9u; }
  uint32_t next() {
    uint32_t x = s_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_ = x;
    return x;
  }
  // Uniform in [0, n)
  uint32_t below(uint32_t n) {
    return n ? (uint32_t)(((uint64_t)next() * n) >> 32) : 0;
  }
  // Uniform in [lo, hi]
  int32_t range(int32_t lo, int32_t hi) {
    return lo + (int32_t)below((uint32_t)(hi - lo + 1));
  }
  uint16_t brad() { return (uint16_t)next(); }
  uint32_t state() const { return s_; }

 private:
  uint32_t s_;
};

}  // namespace devoursphere::sim

#endif
