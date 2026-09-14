#ifndef DEVOURSPHERE_SIM_CONFIG_HPP
#define DEVOURSPHERE_SIM_CONFIG_HPP

// Tunable constants of the simulation. All lengths are in world "units";
// PU (part unit) is the width of a size-1 part.

#include <cstdint>

#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

constexpr int TICK_RATE = 30;  // simulation ticks per second

// --- World scale ------------------------------------------------------------
constexpr int32_t PU = 256;              // units per part unit
constexpr int PLANET_RADIUS_SHIFT = 17;  // R = 2^17 units = 512 PU
constexpr int32_t PLANET_RADIUS = 1 << PLANET_RADIUS_SHIFT;

// --- Capacities -------------------------------------------------------------
constexpr int MAX_CREATURES = 256;
constexpr int MAX_FLOATING_PARTS = 1024;
constexpr int MAX_BULLETS = 256;
constexpr int MAX_PARTICLES = 512;
constexpr int MAX_PARTS_PER_CREATURE = 16;
constexpr int MAX_SIZE_LOG2 = 20;  // largest part exponent handled

// --- Altitude ---------------------------------------------------------------
// altitude(size) = ALT_MIN + ALT_FACTOR * (halfSize(ALT_MAX_LOG2) -
// halfSize(log2 size)) with ALT_FACTOR = ALT_FACTOR_NUM / ALT_FACTOR_DEN, so
// that the spacing between size classes grows with the body size
constexpr int ALT_MAX_LOG2 = 14;
constexpr int32_t ALT_FACTOR_NUM = 1, ALT_FACTOR_DEN = 1;
constexpr int32_t ALT_MIN = 6 * PU;
constexpr int32_t ALT_APPROACH_SHIFT = 5;  // altitude eases 1/32 per tick

// --- Movement ---------------------------------------------------------------
constexpr int32_t SPEED_BASE = PU / 2;       // units per tick for size 1
constexpr int32_t SPEED_PER_LOG2 = PU / 12;  // extra speed per doubling
constexpr int32_t DASH_SPEED_NUM = 9, DASH_SPEED_DEN = 4;  // dash: x2.25
constexpr int32_t BRAKE_DECEL_SHIFT =
    4;  // speed eases towards 0 by 1/16 per tick
constexpr int32_t SPEED_ACCEL_SHIFT =
    3;  // speed eases towards cruise by 1/8 per tick
constexpr uint16_t TURN_RATE = degToBrad(3);        // per tick
constexpr uint16_t TURN_RATE_BRAKE = degToBrad(6);  // per tick while braking
constexpr uint16_t TURN_RATE_DASH = degToBrad(2);   // per tick while dashing

// --- Health -----------------------------------------------------------------
constexpr int32_t HP_PER_SIZE = 32;  // hpMax = HP_PER_SIZE * size
constexpr int32_t DASH_HP_COST_SHIFT =
    10;                                 // hpMax >> 10 per tick while dashing
constexpr int32_t HP_REGEN_SHIFT = 12;  // hpMax >> 12 per tick passive regen

// --- Weapons ----------------------------------------------------------------
enum class Weapon : uint8_t { VULCAN = 0, LASER = 1, MISSILE = 2 };
constexpr int WEAPON_COUNT = 3;

struct WeaponSpec {
  int32_t speed;         // units per tick
  int16_t lifetime;      // ticks
  int16_t cooldown;      // ticks between shots
  int32_t powerPerSize;  // damage = powerPerSize * ownerSize / 8
  int32_t hpCostShift;   // hpMax >> shift per shot
  uint16_t spread;       // random spread half-angle (brad)
  uint16_t homing;       // turn rate per tick (brad); 0 = none
  int32_t radiusPU8;     // hit radius in PU/8 for a size-1 owner
};

constexpr WeaponSpec WEAPON_SPECS[WEAPON_COUNT] = {
    // speed        life  cd  power  cost  spread          homing        radius
    {PU * 5 / 2, 45, 4, 8, 11, degToBrad(5), 0, 6},                // VULCAN
    {PU * 8, 28, 14, 40, 8, 0, 0, 4},                              // LASER
    {PU * 7 / 4, 100, 12, 16, 9, degToBrad(20), degToBrad(4), 8},  // MISSILE
};

// --- Sizes and combat rules -------------------------------------------------
// Attackable: 2/3 <= other / self <= 3/2
static inline bool canAttack(uint32_t self, uint32_t other) {
  return (uint64_t)other * 3 >= (uint64_t)self * 2 &&
         (uint64_t)other * 2 <= (uint64_t)self * 3;
}

// --- Parts inside a creature ------------------------------------------------
constexpr int32_t PART_MERGE_DIST_NUM = 5,
                  PART_MERGE_DIST_DEN = 8;  // fraction of kite half-size
constexpr int32_t LAYOUT_NEAR_PU =
    90;  // full-rate part physics within this distance of the player

// --- Floating parts and particles ------------------------------------------
constexpr int32_t PARTICLE_LIFETIME = 12 * TICK_RATE;  // ticks
constexpr int32_t PARTICLE_IMMUNE_TICKS =
    TICK_RATE;  // cannot be absorbed right after spawning
constexpr int32_t FLOATING_DRIFT_TICKS =
    3 * TICK_RATE;  // initial scatter duration

// --- Planet population ------------------------------------------------------
constexpr int INITIAL_CREATURES = 200;
constexpr int PLAYER_START_SIZE_LOG2 = 2;  // size 4
constexpr int RESPAWN_INTERVAL = TICK_RATE / 2;
// Free food: parts that condense out of the cyber space
constexpr int INITIAL_FOOD_PARTS = 320;
constexpr int FOOD_TARGET = 400;  // keep at least this many floating parts
constexpr int FOOD_SPAWN_INTERVAL = 4;  // ticks
constexpr int FOOD_MAX_SIZE_LOG2 = 3;

// --- Colors (hues 0..255; the renderer maps them to its palette) ----------
constexpr uint8_t ENEMY_HUES[] = {0, 21, 42, 64, 85, 170, 190, 213, 235};
constexpr int ENEMY_HUE_COUNT = 9;
constexpr uint8_t PLAYER_HUE = 135;
constexpr uint8_t PART_HUE = 120;  // floating parts (teal)

// --- Enemy AI ---------------------------------------------------------------
constexpr int AI_THINK_INTERVAL = 8;  // ticks between decisions (staggered)
constexpr int32_t AI_SIGHT_PU = 120;  // detection range in PU

}  // namespace devoursphere::sim

#endif
