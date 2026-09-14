#ifndef DEVOURSPHERE_SIM_ENTITIES_HPP
#define DEVOURSPHERE_SIM_ENTITIES_HPP

#include <cstdint>

#include "devoursphere/sim/config.hpp"
#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

// Button state for one tick (bit set = pressed)
namespace Button {
constexpr uint8_t LEFT = 1 << 0;
constexpr uint8_t RIGHT = 1 << 1;
constexpr uint8_t UP = 1 << 2;    // dash
constexpr uint8_t DOWN = 1 << 3;  // brake
constexpr uint8_t A = 1 << 4;     // fire / confirm
}  // namespace Button

// Kite half-size (units) of a fragment of size 2^sizeLog2 (area grows with
// size)
int32_t fragmentHalfSize(int sizeLog2);

// Altitude above the sphere surface for an entity of the given size
int32_t altitudeForSize(uint32_t size);

// Cruise speed (units per tick) of an entity of the given size
int32_t cruiseSpeedForSize(uint32_t size);

// Speed (units per tick) of a bullet fired by an entity of the given size
int32_t bulletSpeed(const WeaponSpec &ws, uint32_t ownerSize);

// A fragment inside an entity: local coordinates (x = right, y = forward) in
// units. x >= 0; a mirrored copy at (-x, y) is implied.
struct Fragment {
  int32_t x, y;    // position
  int32_t vx, vy;  // velocity (units per tick)
  uint8_t sizeLog2;
};

// Orientation frame on the sphere: n (up, unit normal), t (forward, unit
// tangent), right = t x n. All Q30.
struct Frame {
  Vec3 n, t;
  Vec3 right() const { return crossQ30(t, n); }
};

enum class AiMode : uint8_t { WANDER, HUNT_FRAGMENT, HUNT_ENTITY, FLEE };

struct Entity {
  bool alive;
  bool isPlayer;
  Frame frame;
  int32_t r;      // distance from the sphere center (units)
  int32_t speed;  // current speed (units per tick)
  uint32_t size;  // sum of fragment sizes (authoritative)
  int32_t hp, hpMax;
  Weapon weapon;
  uint8_t hue;   // 0..255 color hue (render hint)
  int8_t turn;   // -1 left, 0, +1 right (current input)
  int16_t bank;  // roll around the heading (brad, positive = right wing down)
  int16_t turnLevel;  // -256..256: how far the turn has built up
  bool dashing, braking, firing;
  int16_t dashLevel;  // 0..256: how far the dash has built up
  int16_t fireCooldown;
  int16_t invincible;   // ticks of spawn protection
  int16_t absorbGuard;  // ticks during which the entity cannot be absorbed
  int16_t evadeTicks;   // AI: ticks left of an evasive maneuver
  int8_t evadeDir;      // AI: turn direction while evading
  uint8_t hitStreak;    // AI: recent hits taken (decays)

  Fragment fragments[MAX_FRAGMENTS_PER_ENTITY];
  uint8_t fragmentCount;
  int32_t coreY;         // core position on the local Y axis
  int32_t coreHalf;      // core kite half-size
  int32_t bodyRadius;    // tangential extent (units) for collisions
  int32_t layoutFocusY;  // focus point for fragment orientation (render hint)

  // AI
  AiMode aiMode;
  int16_t aiTarget;  // index into entities or floating fragments (-1 = none)
  int16_t aiTimer;
  uint16_t aiWanderAngle;

  // Bookkeeping
  uint16_t rank;  // 1 = largest on the sphere (updated periodically)
  uint32_t seed;  // per-entity random seed (visual variation)
};

struct FloatingFragment {
  bool alive;
  Vec3 n;      // unit normal (Q30)
  int32_t r;   // distance from the sphere center
  Vec3 drift;  // tangential velocity (units per tick, decays)
  uint8_t sizeLog2;
  int16_t age;
  uint16_t spin;  // render hint (brad)
};

struct Bullet {
  bool alive;
  Frame frame;  // n: position, t: direction of travel
  Vec3 prevN;   // position at the previous tick (the hit test sweeps the path)
  int32_t r;
  int32_t speed;
  int16_t life;
  int16_t owner;  // entity index
  Weapon kind;
  uint32_t ownerSize;  // size of the owner when fired (hit rule and scale)
  int32_t power;       // damage
  int16_t target;      // homing target entity (-1 = none)
  bool fromPlayer;
};

}  // namespace devoursphere::sim

#endif
