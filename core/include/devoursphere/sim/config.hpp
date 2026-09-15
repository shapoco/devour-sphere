#ifndef DEVOURSPHERE_SIM_CONFIG_HPP
#define DEVOURSPHERE_SIM_CONFIG_HPP

// Tunable constants of the simulation. All lengths are in world "units";
// FU (fragment unit) is the width of a size-1 fragment.

#include <cstdint>

#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

constexpr int32_t FU_UNITS = 256;  // units per fragment unit (see FU below)
constexpr int TICK_RATE = 60;      // simulation ticks per second

// Per-tick values are derived from per-second definitions so that TICK_RATE
// can change (both platforms must use the same value: the simulation is
// deterministic only for identical tick rates).
// Ticks for a duration given in 1/30 s units (the original tuning rate)
constexpr int ticks30(int n30) { return n30 * TICK_RATE / 30; }
// Units per tick for a speed given in FU per second
constexpr int32_t fuPerSec(int32_t fuPerSecond) {
  return fuPerSecond * FU_UNITS / TICK_RATE;
}
// Extra shift for "ease by 1/2^n per tick" rates tuned at 30 Hz
constexpr int RATE_SHIFT = (TICK_RATE >= 60) ? 1 : 0;

// --- World scale ------------------------------------------------------------
constexpr int32_t FU = FU_UNITS;         // units per fragment unit
constexpr int SPHERE_RADIUS_SHIFT = 17;  // R = 2^17 units = 512 FU
constexpr int32_t SPHERE_RADIUS = 1 << SPHERE_RADIUS_SHIFT;

// --- Capacities -------------------------------------------------------------
constexpr int MAX_ENTITIES = 256;
constexpr int MAX_FLOATING_FRAGMENTS = 1024;
constexpr int MAX_BULLETS = 256;
constexpr int MAX_FRAGMENTS_PER_ENTITY = 8;
constexpr int MAX_SIZE_LOG2 = 20;  // largest fragment exponent handled

// --- Altitude ---------------------------------------------------------------
// Every entity and floating fragment flies at the same height above the
// surface
constexpr int32_t ALTITUDE = 12 * FU;
constexpr int32_t ALT_APPROACH_SHIFT =
    5 + RATE_SHIFT;  // altitude eases 1/32 per tick at 30 Hz

// --- Movement ---------------------------------------------------------------
constexpr int32_t SPEED_BASE = fuPerSec(15);  // units per tick for size 1
constexpr int32_t SPEED_PER_LOG2 =
    FU * 5 / (2 * TICK_RATE);  // extra speed per doubling (2.5 FU/s)
constexpr int32_t DASH_SPEED_NUM = 9, DASH_SPEED_DEN = 4;  // full dash: x2.25
// Dash builds up over DASH_RAMP_UP_TICKS and fades over DASH_RAMP_DOWN_TICKS
// (dashLevel is 0..256)
constexpr int DASH_RAMP_UP_TICKS = 2 * TICK_RATE;
constexpr int DASH_RAMP_DOWN_TICKS = TICK_RATE;
constexpr int32_t BRAKE_DECEL_SHIFT =
    4 + RATE_SHIFT;  // speed eases towards 0 by 1/16 per tick at 30 Hz
constexpr int32_t SPEED_ACCEL_SHIFT =
    3 + RATE_SHIFT;  // speed eases towards cruise by 1/8 per tick at 30 Hz
// Turn rates: degrees per second, converted to brad per tick
constexpr uint16_t turnPerTick(int degPerSec) {
  return (uint16_t)(degToBrad(degPerSec) / TICK_RATE);
}
constexpr uint16_t TURN_RATE = turnPerTick(90);
constexpr uint16_t TURN_RATE_BRAKE = turnPerTick(180);  // while braking
constexpr uint16_t TURN_RATE_DASH = turnPerTick(60);    // while dashing
// A turn builds up (and stops) over TURN_RAMP_TICKS (turnLevel is -256..256)
constexpr int TURN_RAMP_TICKS = ticks30(9);   // 0.3 s
constexpr uint16_t BANK_MAX = degToBrad(28);  // roll while turning
constexpr uint16_t BANK_MAX_BRAKE =
    degToBrad(40);  // roll while turning under brake
constexpr int BANK_APPROACH_SHIFT =
    3 + RATE_SHIFT;  // bank eases 1/8 per tick at 30 Hz

// --- Health -----------------------------------------------------------------
constexpr int32_t HP_PER_SIZE = 32;  // hpMax = HP_PER_SIZE * size
// Health only drops from enemy fire (dashing and firing are free, no passive
// regen) and returns by touching fragments: HP_PER_SIZE * HEAL_PER_FRAGMENT_MUL
// per unit of fragment size, i.e. a fragment of 1/32 of the body heals ~9%.
// Fragments too small to be eaten (< size / FOOD_NOTICE_RATIO) are consumed
// for their health only.
constexpr int32_t HEAL_PER_FRAGMENT_MUL = 3;
// One bullet hit takes at most this much of the player's gauge
constexpr int32_t PLAYER_MAX_HIT_PERCENT = 30;
// Critical hit: one hit in CRIT_CHANCE_DEN knocks a fragment of about
// size / CRIT_FRACTION_DIV out of the body instead of taking health. The
// fragment flies off at CRIT_EJECT_SPEED and its former owner cannot take it
// back for FRAGMENT_OWNER_GUARD_TICKS.
constexpr uint32_t CRIT_CHANCE_DEN = 30;
constexpr uint32_t CRIT_FRACTION_DIV = 10;
constexpr int32_t CRIT_EJECT_SPEED = fuPerSec(90);
constexpr int FRAGMENT_OWNER_GUARD_TICKS = 3 * TICK_RATE;
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
    // speed (FU/s)  life (1/30 s)  cooldown  power  spread  homing (deg/s)
    // radius
    {fuPerSec(75), (int16_t)ticks30(45), (int16_t)ticks30(4), 10, degToBrad(5),
     0, 6},  // VULCAN
    {fuPerSec(240), (int16_t)ticks30(28), (int16_t)ticks30(14), 40, 0, 0,
     4},  // LASER
    {fuPerSec(52), (int16_t)ticks30(100), (int16_t)ticks30(12), 18,
     degToBrad(20), turnPerTick(120), 8},  // MISSILE
};

// --- Sizes and combat rules -------------------------------------------------
// Every entity can attack every other entity. When two entities of different
// sizes touch, size flows from the smaller one to the bigger one: per tick
// size >> ABSORB_RATE_SHIFT (at least 1 every ABSORB_MIN_INTERVAL ticks).
constexpr int ABSORB_RATE_SHIFT = 7 + RATE_SHIFT;
constexpr int ABSORB_MIN_INTERVAL = ticks30(4);

// --- Fragments inside an entity
// ------------------------------------------------
constexpr int32_t FRAGMENT_MERGE_DIST_NUM = 5,
                  FRAGMENT_MERGE_DIST_DEN =
                      8;  // fraction of the kite half-size
// ... when more than half the fragment limit is in use
constexpr int32_t FRAGMENT_MERGE_DIST_CROWDED_NUM = 20;
constexpr int32_t LAYOUT_NEAR_FU =
    90;  // full-rate fragment physics within this distance of the player

// A fragment smaller than size / FOOD_NOTICE_RATIO cannot be eaten
constexpr uint32_t FOOD_NOTICE_RATIO = 32;
// Floating fragments within bodyRadius + ATTRACT_RANGE_FU of the player are
// drawn towards it
constexpr int32_t ATTRACT_RANGE_FU = 32;
constexpr int32_t ATTRACT_ACCEL =
    FU * 128 / (TICK_RATE * TICK_RATE);  // 128 FU/s^2, per tick per tick
constexpr int32_t ATTRACT_MAX_SPEED = fuPerSec(480);

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
constexpr int FOOD_SPAWN_INTERVAL = ticks30(4);  // ticks
constexpr int FOOD_MAX_SIZE_LOG2 = 3;

// --- Colors (hues 0..255; the renderer maps them to its palette) ----------
constexpr uint8_t ENEMY_HUES[] = {0, 21, 42, 64, 85, 170, 190, 213, 235};
constexpr int ENEMY_HUE_COUNT = 9;
constexpr uint8_t PLAYER_HUE = 135;
constexpr uint8_t FRAGMENT_HUE = 120;  // floating fragments (teal)

// --- Score ------------------------------------------------------------------
// Every gain is base * 2^(sphere level - 1) * stay factor. The stay factor is
// 1.0 for the first SCORE_STAY_FULL_TICKS on a sphere and falls linearly to
// SCORE_STAY_MIN (Q8) at SCORE_STAY_MIN_TICKS, so lingering pays less and
// less. The score is kept in Q8 (1 point = 256).
constexpr int32_t SCORE_KILL_BASE = 100;    // x (enemy size / own size)^2
constexpr int32_t SCORE_DEVOUR_BASE = 30;   // enemy consumed by contact
constexpr int32_t SCORE_FRAGMENT_BASE = 1;  // fragment joined the body
constexpr int32_t SCORE_CLEAR_BASE = 2000;  // x speed factor
constexpr int SCORE_STAY_FULL_TICKS = 2 * 60 * TICK_RATE;
constexpr int SCORE_STAY_MIN_TICKS = 6 * 60 * TICK_RATE;
constexpr int32_t SCORE_STAY_MIN_Q8 = 51;  // 0.2
constexpr int SCORE_CLEAR_FAST_TICKS = 3 * 60 * TICK_RATE;
constexpr int SCORE_CLEAR_SLOW_TICKS = 8 * 60 * TICK_RATE;
constexpr int32_t SCORE_CLEAR_SLOW_Q8 = 128;  // 0.5
constexpr int32_t SCORE_RATIO_MIN_PCT = 25, SCORE_RATIO_MAX_PCT = 300;

// --- Enemy AI ---------------------------------------------------------------
constexpr int AI_THINK_INTERVAL =
    ticks30(8);  // ticks between decisions (staggered)
// Chance (out of 256) that an enemy hunting a target fires during a think
// interval, per sphere level (index 0 = level 1); the last entry applies
// beyond
constexpr uint8_t AI_FIRE_CHANCE[] = {40, 110, 200, 255};
constexpr int AI_FIRE_CHANCE_LEVELS = 4;
// After AI_EVADE_HITS hits in a short time an enemy breaks off for about
// AI_EVADE_TICKS (quick turn, then dash away on level 2 and up)
constexpr int AI_EVADE_HITS = 3;
constexpr int AI_EVADE_TICKS = ticks30(40);
// Steering: brake (quick turn) when the target is more than this far around
// and closer than AI_QUICK_TURN_FU
constexpr uint16_t AI_QUICK_TURN_ANGLE = degToBrad(60);
constexpr int32_t AI_QUICK_TURN_FU = 30;
// Death effects are reported within this distance of the player
constexpr int32_t EFFECT_RANGE_FU = 90;
constexpr int32_t AI_SIGHT_FU = 120;  // detection range in FU

}  // namespace devoursphere::sim

#endif
