#ifndef DEVOURSPHERE_SIM_ENTITIES_HPP
#define DEVOURSPHERE_SIM_ENTITIES_HPP

#include <cstdint>

#include "devoursphere/sim/config.hpp"
#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

// Button state for one tick (bit set = pressed). A, PAUSE and B are buttons;
// the four directions are the axes of Input read as a digital pad (what the
// menus go by, see directionBits()), and how a digital pad is written
// (Input's constructor from bits).
namespace Button {
constexpr uint8_t LEFT = 1 << 0;
constexpr uint8_t RIGHT = 1 << 1;
constexpr uint8_t UP = 1 << 2;    // dash
constexpr uint8_t DOWN = 1 << 3;  // brake
constexpr uint8_t A = 1 << 4;      // fire / confirm
constexpr uint8_t PAUSE = 1 << 5;  // pause / resume (PLAYING, LAUNCH, ARRIVE)
constexpr uint8_t B = 1 << 6;      // emergency dodge (PLAYING)
constexpr uint8_t DIRECTIONS = LEFT | RIGHT | UP | DOWN;
}  // namespace Button

// Full strength of an axis of Input (and of Entity::turn / dash / brake)
constexpr int INPUT_MAX = 127;
// An axis counts as a direction button on the menus from this strength on,
// and until it drops below DIRECTION_OFF (hysteresis)
constexpr int DIRECTION_ON = 64;
constexpr int DIRECTION_OFF = 40;

// The input for one tick: the direction as two axes, and the buttons.
// x: -127 full left .. +127 full right (turn). y: -127 full up (dash) ..
// +127 full down (brake). The magnitude is the strength, linear from 0: the
// dead zone and the saturation belong to the platform, which knows its
// device (a digital pad gives -127, 0 or +127). -128 counts as -127.
struct Input {
  int8_t x = 0, y = 0;
  uint8_t buttons = 0;  // Button::A | PAUSE | B (the direction bits are unused)

  constexpr Input() = default;
  constexpr Input(int8_t x_, int8_t y_, uint8_t buttons_)
      : x(x_), y(y_), buttons(buttons_) {}
  // A digital pad: the direction bits of Button become full-strength axes
  // (implicit, so that `tick(Button::A)` and `tick(0)` read naturally)
  constexpr Input(uint8_t bits)
      : x((int8_t)(((bits & Button::RIGHT) ? INPUT_MAX : 0) -
                   ((bits & Button::LEFT) ? INPUT_MAX : 0))),
        y((int8_t)(((bits & Button::DOWN) ? INPUT_MAX : 0) -
                   ((bits & Button::UP) ? INPUT_MAX : 0))),
        buttons((uint8_t)(bits & ~Button::DIRECTIONS)) {}
};

// The axes of `in` as direction bits of Button, with hysteresis: `held` is
// what the previous call returned (a direction that was on stays on down to
// DIRECTION_OFF)
constexpr uint8_t directionBits(const Input &in, uint8_t held) {
  const int on = DIRECTION_ON, off = DIRECTION_OFF;
  uint8_t d = 0;
  if (-in.x >= ((held & Button::LEFT) ? off : on)) d |= Button::LEFT;
  if (in.x >= ((held & Button::RIGHT) ? off : on)) d |= Button::RIGHT;
  if (-in.y >= ((held & Button::UP) ? off : on)) d |= Button::UP;
  if (in.y >= ((held & Button::DOWN) ? off : on)) d |= Button::DOWN;
  return d;
}

// Kite half-size (units) of a fragment of size 2^sizeLog2 (area grows with
// size)
int32_t fragmentHalfSize(int sizeLog2);

// Altitude above the sphere surface for an entity of the given size
int32_t altitudeForSize(uint32_t size);

// How much the pull on nearby floating fragments is scaled (Q8) for a player
// of the given size: the body's visual scale relative to the starting player
int32_t attractScaleQ8(uint32_t size);

// Cruise speed (units per tick) of an entity of the given size
int32_t cruiseSpeedForSize(uint32_t size);

// Speed (units per tick) of a bullet fired by an entity of the given size
int32_t bulletSpeed(const WeaponSpec &ws, uint32_t ownerSize);

// A fragment inside an entity: local coordinates (x = right, y = forward) in
// units. x >= 0; a mirrored copy at (-x, y) is implied.
struct Fragment {
  int32_t x, y;    // position
  int16_t vx, vy;  // velocity (units per tick; updateLayout clamps it to a
                   // few thousand, see FRAGMENT_V_MAX)
  uint8_t sizeLog2;
};  // 16 bytes

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

// An entity index that means "none" (Entity::evadeFrom)
constexpr uint8_t NO_ENTITY = 0xFF;

// Ordered by alignment (4-byte members, then 2-byte, then bytes) so that
// nothing but the tail is padding: 224 bytes, checked in entities.cpp.
// The flags share one byte as bitfields; the indices that used to be
// int16_t with -1 for "none" are uint8_t with NO_ENTITY where the range
// allows (an entity index is < MAX_ENTITIES = 224; a floating fragment index
// is not, so aiTarget stays 16 bits)
struct Entity {
  Frame frame;
  int32_t r;      // distance from the sphere center (units)
  int32_t speed;  // current speed (units per tick)
  uint32_t size;  // sum of fragment sizes (authoritative)
  int32_t hp, hpMax;
  Fragment fragments[MAX_FRAGMENTS_PER_ENTITY];
  int32_t coreY;         // core position on the local Y axis
  int32_t coreHalf;      // core kite half-size
  int32_t bodyRadius;    // tangential extent (units) for collisions
  int32_t layoutFocusY;  // focus point for fragment orientation (render hint)

  int16_t bank;         // roll around the heading (brad, positive = right wing down)
  int16_t turnLevel;    // -256..256: how far the turn has built up
  int16_t dashLevel;    // 0..256: how far the dash has built up
  int16_t fireCooldown;
  int16_t invincible;   // ticks of spawn protection
  int16_t absorbGuard;  // ticks during which the entity cannot be absorbed
  int16_t evadeTicks;   // AI: ticks left of an evasive maneuver
  int16_t evadeFlipAt;  // AI: flip the escape side when evadeTicks gets here
  int16_t aiTarget;  // index into entities or floating fragments (-1 = none)
  int16_t grudge;    // AI: ticks of grudge against the player (GRUDGE_*)
  uint16_t aiWanderAngle;

  bool alive : 1;
  bool isPlayer : 1;
  bool firing : 1;
  bool absorbedBig : 1;  // was bigger than the player when it started
                         // absorbing it (the kill sound; the size shrinks
                         // while absorbed)
  bool bounty : 1;       // has been the largest on the sphere: must be killed
                         // for the sphere to be cleared (see updateRanks)
  bool noScore : 1;      // spawned to help the player back after a death:
                         // worth no score (see RESPAWN_PACK_MAX)
  Weapon weapon;
  uint8_t hue;          // 0..255 color hue (render hint)
  int8_t turn;          // -127 left .. +127 right (current input, INPUT_MAX)
  uint8_t evadeFrom;    // AI: entity whose fire triggered it (NO_ENTITY)
  int8_t evadeDir;      // AI: escape side (+1 / -1; see EvadeMode)
  uint8_t evadeMode;    // AI: EvadeMode
  uint8_t hitStreak;    // AI: recent hits taken (decays)
  uint8_t fragmentCount;
  AiMode aiMode;
  uint8_t upgrade;  // UpgradeKind carried (released when the entity dies)
  uint8_t rank;     // 1 = largest on the sphere (the player's is maintained)
  // Current input, 0..INPUT_MAX: how hard the dash / the brake is held
  // (never both; the AI uses 0 or INPUT_MAX)
  uint8_t dash, brake;
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
