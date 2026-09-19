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
constexpr uint8_t A = 1 << 4;      // fire / confirm
constexpr uint8_t PAUSE = 1 << 5;  // pause / resume (PLAYING, LAUNCH, ARRIVE)
constexpr uint8_t B = 1 << 6;      // emergency dodge (PLAYING)
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

// What an enemy under fire does (Entity::evadeMode)
enum class EvadeMode : uint8_t {
  BREAK_PICK_SIDE = 0,  // break off; the side is chosen at the next think
  BREAK,                // break off sideways (evadeDir is the side)
  COUNTER               // quick turn towards the shooter and fight back
};

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
  int16_t evadeFrom;    // AI: entity whose fire triggered it (-1 = unknown)
  int16_t evadeFlipAt;  // AI: flip the escape side when evadeTicks gets here
  int8_t evadeDir;      // AI: escape side (+1 / -1; see EvadeMode)
  uint8_t evadeMode;    // AI: EvadeMode
  uint8_t hitStreak;    // AI: recent hits taken (decays)
  uint8_t absorbedBig;  // was bigger than the player when it started absorbing
                        // it (the kill sound; the size shrinks while absorbed).
                        // Here it fills the padding before the fragments

  Fragment fragments[MAX_FRAGMENTS_PER_ENTITY];
  uint8_t fragmentCount;
  int32_t coreY;         // core position on the local Y axis
  int32_t coreHalf;      // core kite half-size
  int32_t bodyRadius;    // tangential extent (units) for collisions
  int32_t layoutFocusY;  // focus point for fragment orientation (render hint)

  // AI
  AiMode aiMode;
  int16_t aiTarget;  // index into entities or floating fragments (-1 = none)
  int16_t grudge;  // AI: ticks of grudge against the player (GRUDGE_*)
  uint16_t aiWanderAngle;

  uint8_t upgrade;  // UpgradeKind carried (released when the entity dies)

  // Bookkeeping
  uint16_t rank;  // 1 = largest on the sphere (updated periodically)
  uint32_t seed;  // per-entity random seed (visual variation)
};

// Effective size for absorption and color: size * (0.25 + 0.75 * hp / hpMax)
// in Q8, so a weakened entity can be devoured by a smaller one: a 1.5x
// opponent once it is below 5/9 of its health, a 2x one below 1/3
static inline int64_t effectiveSizeQ8(const Entity &e) {
  int64_t hpTerm = e.hpMax > 0 ? (int64_t)e.hp * 256 / e.hpMax : 256;
  if (hpTerm < 0) hpTerm = 0;
  return (int64_t)e.size * (64 + hpTerm * 3 / 4);
}

struct FloatingFragment {
  bool alive;
  Vec3 n;      // unit normal (Q30)
  int32_t r;   // distance from the sphere center
  Vec3 drift;  // tangential velocity (units per tick, decays)
  uint8_t sizeLog2;
  int16_t age;
  uint16_t spin;       // render hint (brad)
  int16_t owner;       // entity that lost it (-1 = none)
  int16_t ownerGuard;  // ticks during which the owner cannot take it back
};

// A drifting upgrade released by a dead enemy (never expires, only the
// player can take it)
struct FloatingUpgrade {
  bool alive;
  uint8_t kind;  // UpgradeKind
  Vec3 n;
  int32_t r;
  Vec3 drift;
  uint16_t spin;
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
