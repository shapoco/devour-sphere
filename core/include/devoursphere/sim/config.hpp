#ifndef DEVOURSPHERE_SIM_CONFIG_HPP
#define DEVOURSPHERE_SIM_CONFIG_HPP

// Tunable constants of the simulation. All lengths are in world "units";
// FU (fragment unit) is the width of a size-1 fragment.

#include <cstdint>

#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

// --- Version ----------------------------------------------------------------
// The major version changes when the game system changes in a way that
// makes older scores incomparable (scoring, the time limit, the difficulty
// structure); a platform that stores the high score keeps the major with it
// and drops the score when the major differs. Anything else bumps the minor.
constexpr int VERSION_MAJOR = 1;
constexpr int VERSION_MINOR = 0;
constexpr const char *VERSION_STRING = "v1.0";

constexpr int32_t FU_UNITS = 256;  // units per fragment unit (see FU below)

// Simulation ticks per second. Everything below is derived from per-second
// values (ticks30(), fuPerSec(), turnPerTick(), RATE_SHIFT), so a platform
// that cannot afford 60 Hz may build the core at a lower rate -- the game
// then plays the same but is no longer bit-identical to a build at another
// rate. 30 Hz is what the constants were originally tuned at.
#ifndef DEVOURSPHERE_TICK_RATE
#define DEVOURSPHERE_TICK_RATE 60
#endif
constexpr int TICK_RATE = DEVOURSPHERE_TICK_RATE;
static_assert(TICK_RATE == 30 || TICK_RATE == 60,
              "only 30 and 60 Hz are tuned and tested");

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
// Sized from what the arrays really hold rather than from round numbers,
// because the whole Game is a static on the embedded targets and these three
// arrays are most of it. Measured over levels 1-7 x 3 seeds with the AI
// driving, 10 s and 3 min per run: at most 201 entities alive (INITIAL_ENTITIES
// plus the player; the respawn logic never goes above that), 336 floating
// fragments and 84 bullets. Each cap keeps at least 1.5x the peak. The arrays
// never overflow anyway -- a spawn into a full array is skipped or replaces the
// oldest -- so a cap only ever costs a spawn, not correctness. Keeping
// MAX_ENTITIES below 256 also leaves room to store an entity index in a byte.
constexpr int MAX_ENTITIES = 224;
constexpr int MAX_FLOATING_FRAGMENTS = 512;
constexpr int MAX_BULLETS = 128;
constexpr int MAX_FRAGMENTS_PER_ENTITY = 8;
constexpr int MAX_SIZE_LOG2 = 20;  // largest fragment exponent handled

// --- Altitude ---------------------------------------------------------------
// Every entity and floating fragment flies at the same height above the
// surface
constexpr int32_t ALTITUDE = 12 * FU;
constexpr int32_t ALT_APPROACH_SHIFT =
    5 + RATE_SHIFT;  // altitude eases 1/32 per tick at 30 Hz

// --- Flight between spheres (LAUNCH / ARRIVE) ------------------------------
// The player climbs LAUNCH_ALTITUDE above the cruising altitude during the
// launch (accelerating, t^2) and keeps climbing until the arrival switches
// the sphere; the descent starts ARRIVE_ALTITUDE above the new sphere
// (decelerating, mirror of the climb). Lower than the launch altitude so
// that the next sphere looks bigger than the one just left.
constexpr int32_t LAUNCH_ALTITUDE = 900 * FU;
constexpr int32_t ARRIVE_ALTITUDE = 500 * FU;
// One full roll around the heading, and when it starts in each state
constexpr int FLIGHT_ROLL_TICKS = 3 * TICK_RATE;
constexpr int LAUNCH_ROLL_START = TICK_RATE / 2;
constexpr int ARRIVE_ROLL_START = 3 * TICK_RATE / 2;

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
constexpr int32_t HEAL_PER_FRAGMENT_MUL = 1;
// One bullet hit takes at most this much of the player's gauge
constexpr int32_t PLAYER_MAX_HIT_PERCENT = 25;
// (the size clamp of a giant's bullet is AiTier::hitSizeRatioMax)
// After a hit the player ignores enemy bullets for this long (0.1 s:
// several enemies firing at once cannot stack their hits into one instant)
constexpr int PLAYER_MERCY_TICKS = TICK_RATE / 10;
// Emergency dodge (Button::B): a barrel roll sideways during which enemy
// bullets pass through the player. The sidestep is DODGE_SPEED_MUL times the
// cruising speed, so it scales with the body like everything else.
constexpr int DODGE_TICKS = TICK_RATE * 3 / 10;  // 0.3 s
constexpr int DODGE_COOLDOWN_TICKS = 3 * TICK_RATE;
constexpr int32_t DODGE_SPEED_MUL = 4;
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

// Sound effects: minimum gap between two requests of the same kind, indexed
// by SoundKind (game.hpp). The fragment pickup needs one because a kill
// scatters fragments that the player then swallows over a few ticks, and
// both it and the player's hit are raised every tick of a contact
// absorption (the gap paces the munching)
constexpr int SOUND_KINDS = 16;
constexpr int16_t SOUND_MIN_GAP_TICKS[SOUND_KINDS] = {
    0, 0, 0,                  // SHOT_VULCAN, SHOT_LASER, SHOT_MISSILE
    0, (int16_t)ticks30(3),   // HIT_ENEMY, HIT_PLAYER (0.1 s)
    0, 0, 0,                  // ENEMY_KILLED_SMALL, ENEMY_KILLED_BIG, PLAYER_KILLED
    (int16_t)ticks30(3),      // GET_FRAGMENT (0.1 s)
    0, 0, 0,                  // GET_UPGRADE, MENU_SELECT, MENU_START
    0, 0,                     // LAUNCH, ARRIVE
    0, 0,                     // TIME_ALARM (once a second by the sim), DODGE
};
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
    {fuPerSec(75), (int16_t)ticks30(45), (int16_t)ticks30(4), 5, degToBrad(5),
     0, 6},  // VULCAN
    {fuPerSec(240), (int16_t)ticks30(28), (int16_t)ticks30(14), 20, 0, 0,
     4},  // LASER
    {fuPerSec(52), (int16_t)ticks30(100), (int16_t)ticks30(12), 10,
     degToBrad(20), turnPerTick(120), 8},  // MISSILE
};

// --- Sizes and combat rules -------------------------------------------------
// Every entity can attack every other entity. When two entities of different
// sizes touch, size flows from the smaller one to the bigger one: per tick
// size >> ABSORB_RATE_SHIFT (at least 1 every ABSORB_MIN_INTERVAL ticks).
constexpr int ABSORB_RATE_SHIFT = 7 + RATE_SHIFT;
constexpr int ABSORB_MIN_INTERVAL = ticks30(4);
// Health is drained as well: every ABSORB_HP_INTERVAL ticks the smaller one
// loses ABSORB_HP_PCT percent of its gauge and the bigger one heals by the
// same amount (a small share of a big gauge, a large share of a small one).
// At zero health the drained one is devoured outright.
constexpr int ABSORB_HP_INTERVAL = ticks30(5);
constexpr int32_t ABSORB_HP_PCT = 5;

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
// Time limit of a sphere (the clock runs only while the player plays and is
// alive). Under TIME_WARN_TICKS the HUD turns yellow, under TIME_ALARM_TICKS
// it blinks and the sim asks for the alarm once a second. At zero the player
// breaks apart as if shot down and, with a spare core, the same sphere
// starts over from the arrival (see Game::restartSphereAfterTimeUp)
constexpr int SPHERE_TIME_LIMIT_TICKS = 5 * 60 * TICK_RATE;
constexpr int TIME_WARN_TICKS = 60 * TICK_RATE;
constexpr int TIME_ALARM_TICKS = 30 * TICK_RATE;

// --- Colors (hues 0..255; the renderer maps them to its palette) ----------
constexpr uint8_t ENEMY_HUES[] = {0, 21, 42, 64, 85, 170, 190, 213, 235};
constexpr int ENEMY_HUE_COUNT = 9;
constexpr uint8_t PLAYER_HUE = 135;
constexpr uint8_t FRAGMENT_HUE = 120;  // floating fragments (teal)

// --- Score ------------------------------------------------------------------
// Every gain is base * (1 + SCORE_LEVEL_GROWTH_PCT / 100)^(sphere level - 1).
// The score is kept in Q8 (1 point = 256). Clearing a sphere pays
// SCORE_CLEAR_BASE plus SCORE_CLEAR_TIME_BONUS times the share of the time
// limit still left, so a quick clear and a long hunt are both worth it.
constexpr int32_t SCORE_KILL_BASE = 100;    // x (enemy size / own size)^2
constexpr int32_t SCORE_DEVOUR_BASE = 30;   // enemy consumed by contact
constexpr int32_t SCORE_FRAGMENT_BASE = 1;  // fragment joined the body
constexpr int32_t SCORE_CLEAR_BASE = 2000;
constexpr int32_t SCORE_CLEAR_TIME_BONUS = 4000;
constexpr int32_t SCORE_LEVEL_GROWTH_PCT = 10;
constexpr int32_t SCORE_RATIO_MIN_PCT = 25, SCORE_RATIO_MAX_PCT = 300;

// --- Upgrades and cores ------------------------------------------------------
enum class UpgradeKind : uint8_t {
  NONE = 0,
  SHIELD,
  OVERDRIVE,
  THRUSTER,
  EXTRA_CORE
};
constexpr int UPGRADE_KINDS =
    3;  // leveled upgrades: SHIELD, OVERDRIVE, THRUSTER
constexpr int UPGRADE_MAX_LEVEL = 3;
constexpr int CORES_MAX = 3;  // spare cores (lives)
constexpr int CORES_START = 1;
constexpr int MAX_FLOATING_UPGRADES = 8;
constexpr uint32_t EXTRA_CORE_CHANCE_DEN = 3;  // 1 in 3 spheres carry one
// Shield: damage taken in percent per level, level 3 regenerates
constexpr int32_t SHIELD_DAMAGE_PCT[UPGRADE_MAX_LEVEL + 1] = {100, 75, 50, 50};
constexpr int32_t SHIELD_REGEN_PCT_PER_SEC = 5;
// Overdrive: fire rate x1.25 / x1.5 / x2 per level (cooldown in percent)
constexpr int32_t OVERDRIVE_COOLDOWN_PCT[UPGRADE_MAX_LEVEL + 1] = {100, 80, 67,
                                                                   50};
// Thruster: dash speed bonus x1.25 / x1.5 / x2 per level (in percent)
constexpr int32_t THRUSTER_DASH_PCT[UPGRADE_MAX_LEVEL + 1] = {100, 125, 150,
                                                              200};
// Score for an upgrade taken at max level
constexpr int32_t SCORE_UPGRADE_BONUS_BASE = 500;
// Respawn after losing a core: size divided, all upgrade levels -1
constexpr int RESPAWN_SIZE_DIV = 2;
constexpr int RESPAWN_CANDIDATES = 24;
constexpr int RESPAWN_INVINCIBLE_TICKS = 2 * TICK_RATE;
constexpr int RESPAWN_DELAY_TICKS = 3 * TICK_RATE;  // watch the wreck first
// Difficulty: enemies get a little stronger with the player's total upgrade
// level (the upgrades are the reward; the sphere level is the difficulty)
constexpr int32_t DIFF_FIRE_PCT_PER_LEVEL = 5;    // fire chance
constexpr int32_t DIFF_DAMAGE_PCT_PER_LEVEL = 5;  // bullet damage
// ... and enemy fire hurts the player more on every sphere: damage x
// (100 + DIFF_DAMAGE_PCT_PER_SPHERE * (sphere level - 1)) %, capped at
// DIFF_DAMAGE_PCT_SPHERE_MAX, multiplied with the upgrade factor above. The
// player's own bullets never scale.
constexpr int32_t DIFF_DAMAGE_PCT_PER_SPHERE = 15;
constexpr int32_t DIFF_DAMAGE_PCT_SPHERE_MAX = 250;

// Enemies hunt other enemies no smaller than 1 / AI_PREY_MIN_RATIO of
// themselves (the player's own ratio comes from the AI tier below)
constexpr uint32_t AI_PREY_MIN_RATIO = 4;

// --- Enemy AI ---------------------------------------------------------------
constexpr int AI_THINK_INTERVAL =
    ticks30(8);  // ticks between decisions (staggered)
// Which of the AI behaviours a tier switches on (AiTier::flags)
constexpr uint8_t AI_DASH = 1 << 0;   // dash when hunting far prey / fleeing
constexpr uint8_t AI_LEAD = 1 << 1;   // aim ahead of a moving prey
constexpr uint8_t AI_PACK = 1 << 2;   // join a neighbour hunting the player
constexpr uint8_t AI_FLANK = 1 << 3;  // approach the player from the side
// What the enemies do on a sphere, by level (index = level - 1; the last
// entry applies beyond). Everything about the enemies' aggression is keyed
// on the sphere level, nothing on the player's upgrades.
struct AiTier {
  uint8_t fireChance;  // out of 256 per think interval, prey in the cone
  uint8_t missilePct;  // weapons of the enemies spawned (the rest: vulcan)
  uint8_t laserPct;
  // A bigger enemy hunts the player only when the player is at least
  // 1 / preyMinRatio of its size (the giants ignore a small player)
  uint8_t preyMinRatio;
  // Bravery: a bigger entity within 60 FU is a threat (to flee from) only
  // when it is more than fleeFarPct percent of one's own effective size,
  // within 40 FU when more than fleeNearPct; and the player up to fleeFarPct
  // is hunted rather than avoided (a smaller enemy fights a bigger player)
  uint16_t fleeFarPct, fleeNearPct;
  uint8_t playerSightFU;  // range at which the player is noticed
  uint8_t playerBiasPct;  // the player's distance is divided by this percent
                          // when choosing prey (preferred over other prey)
  uint8_t evadeHits;      // hits in a row that start a break-off (0: never)
  uint8_t counterPct;     // chance to counterattack instead of breaking off
  uint8_t flags;          // AI_DASH | AI_LEAD | AI_PACK | AI_FLANK
  // An enemy bullet hurts the player as if its owner were at most this
  // many times the player's size (a giant's shot is a big hit, not a kill)
  uint8_t hitSizeRatioMax;
};
constexpr AiTier AI_TIERS[] = {
    // fire mis las prey far  near sight bias evade counter flags hit
    {48, 0, 0, 2, 150, 110, 130, 110, 0, 0, 0, 2},                       // 1
    {96, 0, 0, 2, 150, 110, 150, 120, 3, 25, 0, 2},                      // 2
    {140, 25, 10, 3, 200, 125, 170, 130, 2, 50, AI_DASH | AI_LEAD, 2},   // 3
    {180, 33, 20, 4, 200, 125, 190, 140, 2, 60,
     AI_DASH | AI_LEAD | AI_PACK, 3},  // 4
    {220, 33, 33, 4, 250, 150, 210, 150, 2, 75,
     AI_DASH | AI_LEAD | AI_PACK | AI_FLANK, 3},  // 5
    {255, 33, 40, 6, 250, 150, 230, 160, 1, 85,
     AI_DASH | AI_LEAD | AI_PACK | AI_FLANK, 3},  // 6
    {255, 33, 45, 6, 300, 175, 250, 175, 1, 100,
     AI_DASH | AI_LEAD | AI_PACK | AI_FLANK, 4},  // 7
    {255, 30, 50, 8, 300, 200, 250, 190, 1, 100,
     AI_DASH | AI_LEAD | AI_PACK | AI_FLANK, 4},  // 8
    {255, 25, 50, 8, 350, 200, 250, 200, 1, 100,
     AI_DASH | AI_LEAD | AI_PACK | AI_FLANK, 4},  // 9
    {255, 25, 50, 8, 400, 250, 250, 220, 1, 100,
     AI_DASH | AI_LEAD | AI_PACK | AI_FLANK, 4},  // 10 and beyond
};
constexpr int AI_TIER_LEVELS = 10;
// Threat distances of the bravery rule above
constexpr int32_t AI_FLEE_FAR_FU = 60, AI_FLEE_NEAR_FU = 40;
// Pack: an enemy within this distance that hunts the player calls the
// neighbours in
constexpr int32_t AI_PACK_CALL_FU = 90;
// Flank: an enemy in front of the player (within this half-angle of its
// heading) and farther than AI_FLANK_MIN_FU steers for a point
// AI_FLANK_OFFSET_FU beside the player instead of at it
constexpr uint16_t AI_FLANK_CONE = degToBrad(30);
constexpr int32_t AI_FLANK_MIN_FU = 30;
constexpr int32_t AI_FLANK_OFFSET_FU = 40;
// A break-off lasts AI_EVADE_TICKS (+ up to half as much at random): the
// enemy steers out of the shooter's line of fire (sideways, leaning away),
// with a quick turn under brake while the escape heading is more than
// AI_EVADE_TURN_ANGLE off and a dash once it points there. Hits taken
// meanwhile keep at least AI_EVADE_TICKS / 2 of the maneuver ahead.
constexpr int AI_EVADE_TICKS = 2 * TICK_RATE;
constexpr uint16_t AI_EVADE_TURN_ANGLE = degToBrad(45);
// Halfway through a break-off the enemy flips its escape side with another
// quick turn (a zigzag), so it never just runs in a straight line. When the
// shooter's effective size is at most AI_COUNTER_MAX_RATIO_PCT percent of
// the enemy's own and the enemy still has more than 1 / AI_COUNTER_MIN_HP_DIV
// of its health, it counterattacks instead with the tier's counterPct
// probability: a quick turn under brake towards the shooter, firing as soon
// as it is in the cone, then a charge.
constexpr int32_t AI_COUNTER_MAX_RATIO_PCT = 200;
constexpr int32_t AI_COUNTER_MIN_HP_DIV = 4;
// Steering: brake (quick turn) when the target is more than this far around
// and closer than AI_QUICK_TURN_FU
constexpr uint16_t AI_QUICK_TURN_ANGLE = degToBrad(60);
constexpr int32_t AI_QUICK_TURN_FU = 30;
// Death effects are reported within this distance of the player
constexpr int32_t EFFECT_RANGE_FU = 90;
constexpr int32_t AI_SIGHT_FU = 120;  // detection range in FU

}  // namespace devoursphere::sim

#endif
