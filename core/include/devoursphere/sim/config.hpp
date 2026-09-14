#ifndef DEVOURSPHERE_SIM_CONFIG_HPP
#define DEVOURSPHERE_SIM_CONFIG_HPP

// Tunable constants of the simulation. All lengths are in world "units";
// FU (fragment unit) is the width of a size-1 fragment.

#include <cstdint>

#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

constexpr int TICK_RATE = 30;  // simulation ticks per second

// --- World scale ------------------------------------------------------------
constexpr int32_t FU = 256;              // units per fragment unit
constexpr int SPHERE_RADIUS_SHIFT = 17;  // R = 2^17 units = 512 FU
constexpr int32_t SPHERE_RADIUS = 1 << SPHERE_RADIUS_SHIFT;

// --- Capacities -------------------------------------------------------------
constexpr int MAX_ENTITIES = 256;
constexpr int MAX_FLOATING_FRAGMENTS = 1024;
constexpr int MAX_BULLETS = 256;
constexpr int MAX_FRAGMENTS_PER_ENTITY = 16;
constexpr int MAX_SIZE_LOG2 = 20;  // largest fragment exponent handled

// --- Altitude ---------------------------------------------------------------
// Every entity and floating fragment flies at the same height above the
// surface
constexpr int32_t ALTITUDE = 12 * FU;
constexpr int32_t ALT_APPROACH_SHIFT = 5;  // altitude eases 1/32 per tick

// --- Movement ---------------------------------------------------------------
constexpr int32_t SPEED_BASE = FU / 2;       // units per tick for size 1
constexpr int32_t SPEED_PER_LOG2 = FU / 12;  // extra speed per doubling
constexpr int32_t DASH_SPEED_NUM = 9, DASH_SPEED_DEN = 4;  // full dash: x2.25
// Dash builds up over DASH_RAMP_UP_TICKS and fades over DASH_RAMP_DOWN_TICKS
// (dashLevel is 0..256)
constexpr int DASH_RAMP_UP_TICKS = 2 * TICK_RATE;
constexpr int DASH_RAMP_DOWN_TICKS = TICK_RATE;
constexpr int32_t BRAKE_DECEL_SHIFT =
    4;  // speed eases towards 0 by 1/16 per tick
constexpr int32_t SPEED_ACCEL_SHIFT =
    3;  // speed eases towards cruise by 1/8 per tick
constexpr uint16_t TURN_RATE = degToBrad(3);        // per tick
constexpr uint16_t TURN_RATE_BRAKE = degToBrad(6);  // per tick while braking
constexpr uint16_t TURN_RATE_DASH = degToBrad(2);   // per tick while dashing
// A turn builds up (and stops) over TURN_RAMP_TICKS (turnLevel is -256..256)
constexpr int TURN_RAMP_TICKS = 9;            // 0.3 s
constexpr uint16_t BANK_MAX = degToBrad(28);  // roll while turning
constexpr uint16_t BANK_MAX_BRAKE =
    degToBrad(40);                      // roll while turning under brake
constexpr int BANK_APPROACH_SHIFT = 3;  // bank eases 1/8 per tick

// --- Health -----------------------------------------------------------------
constexpr int32_t HP_PER_SIZE = 32;  // hpMax = HP_PER_SIZE * size
// Health only drops from enemy fire (dashing and firing are free, no passive
// regen) and returns by touching fragments: HP_PER_SIZE * HEAL_PER_FRAGMENT_MUL
// per unit of fragment size, i.e. a fragment of 1/32 of the body heals ~9%.
// Fragments too small to be eaten (< size / FOOD_NOTICE_RATIO) are consumed
// for their health only.
constexpr int32_t HEAL_PER_FRAGMENT_MUL = 3;
constexpr int ABSORB_GUARD_TICKS =
    TICK_RATE;  // no absorption right after eating

// --- Weapons ----------------------------------------------------------------
enum class Weapon : uint8_t { VULCAN = 0, LASER = 1, MISSILE = 2 };
constexpr int WEAPON_COUNT = 3;

struct WeaponSpec {
  int32_t speed;         // units per tick
  int16_t lifetime;      // ticks
  int16_t cooldown;      // ticks between shots
  int32_t powerPerSize;  // damage = powerPerSize * ownerSize / 8
  uint16_t spread;       // random spread half-angle (brad)
  uint16_t homing;       // turn rate per tick (brad); 0 = none
  int32_t radiusFU8;     // hit radius in FU/8 for a size-1 owner
};

constexpr WeaponSpec WEAPON_SPECS[WEAPON_COUNT] = {
    // damage per hit = power * ownerSize / 8; hpMax = 32 * size, so an equal
    // opponent dies after ~26 vulcan hits, ~6 laser hits or ~14 missile hits
    // speed        life  cd  power  spread          homing        radius
    {FU * 5 / 2, 45, 4, 10, degToBrad(5), 0, 6},                // VULCAN
    {FU * 8, 28, 14, 40, 0, 0, 4},                              // LASER
    {FU * 7 / 4, 100, 12, 18, degToBrad(20), degToBrad(4), 8},  // MISSILE
};

// --- Sizes and combat rules -------------------------------------------------
// Every entity can attack every other entity. When two entities of different
// sizes touch, size flows from the smaller one to the bigger one: per tick
// size >> ABSORB_RATE_SHIFT (at least 1 every ABSORB_MIN_INTERVAL ticks).
constexpr int ABSORB_RATE_SHIFT = 7;
constexpr int ABSORB_MIN_INTERVAL = 4;

// --- Fragments inside an entity
// ------------------------------------------------
constexpr int32_t FRAGMENT_MERGE_DIST_NUM = 5,
                  FRAGMENT_MERGE_DIST_DEN = 8;  // fraction of kite half-size
constexpr int32_t LAYOUT_NEAR_FU =
    90;  // full-rate fragment physics within this distance of the player

// A fragment smaller than size / FOOD_NOTICE_RATIO cannot be eaten
constexpr uint32_t FOOD_NOTICE_RATIO = 32;

// --- Floating fragments ------------------------------------------------------
constexpr int32_t FLOATING_DRIFT_TICKS =
    3 * TICK_RATE;  // initial scatter duration

// --- Sphere population ------------------------------------------------------
constexpr int INITIAL_ENTITIES = 200;
constexpr int PLAYER_START_SIZE_LOG2 = 2;  // size 4
constexpr int RESPAWN_INTERVAL = TICK_RATE / 2;
// Free food: fragments that condense out of the cyber space
constexpr int INITIAL_FOOD_FRAGMENTS = 320;
constexpr int FOOD_TARGET = 400;  // keep at least this many floating fragments
constexpr int FOOD_SPAWN_INTERVAL = 4;  // ticks
constexpr int FOOD_MAX_SIZE_LOG2 = 3;

// --- Colors (hues 0..255; the renderer maps them to its palette) ----------
constexpr uint8_t ENEMY_HUES[] = {0, 21, 42, 64, 85, 170, 190, 213, 235};
constexpr int ENEMY_HUE_COUNT = 9;
constexpr uint8_t PLAYER_HUE = 135;
constexpr uint8_t FRAGMENT_HUE = 120;  // floating fragments (teal)

// --- Enemy AI ---------------------------------------------------------------
constexpr int AI_THINK_INTERVAL = 8;  // ticks between decisions (staggered)
// Chance (out of 256) that an enemy hunting a target fires during a think
// interval, per sphere level (index 0 = level 1); the last entry applies
// beyond
constexpr uint8_t AI_FIRE_CHANCE[] = {40, 110, 200, 255};
constexpr int AI_FIRE_CHANCE_LEVELS = 4;
constexpr int32_t AI_SIGHT_FU = 120;  // detection range in FU

}  // namespace devoursphere::sim

#endif
