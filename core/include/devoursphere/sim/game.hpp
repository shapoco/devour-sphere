#ifndef DEVOURSPHERE_SIM_GAME_HPP
#define DEVOURSPHERE_SIM_GAME_HPP

// The whole game state and its deterministic update. The renderer reads the
// public members; the platform layer feeds `tick()` with the button state.

#include <cstdint>

#include "devoursphere/profile.hpp"
#include "devoursphere/sim/config.hpp"
#include "devoursphere/sim/entities.hpp"
#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

enum class GameState : uint8_t {
  TITLE,          // attract mode: the player entity is driven by the AI
  WEAPON_SELECT,  // choose a weapon with either axis, confirm with A
  PLAYING,
  LAUNCH,  // the player became the largest: leaving the sphere
  DEAD,    // game over screen
  ARRIVE,  // flying in to the next sphere (the sphere switches midway)
};

// Event flags raised during the last tick (for effects; cleared every tick)
namespace Event {
constexpr uint32_t PLAYER_HIT = 1 << 0;
constexpr uint32_t PLAYER_ATE_FRAGMENT = 1 << 1;  // a fragment joined the body
constexpr uint32_t PLAYER_DIED = 1 << 3;
constexpr uint32_t SPHERE_CLEARED = 1 << 4;
constexpr uint32_t PLAYER_FIRED = 1 << 5;
constexpr uint32_t PLAYER_MERGED = 1 << 6;
constexpr uint32_t PLAYER_HEALED = 1 << 7;     // a heal-only (white) fragment
constexpr uint32_t PLAYER_UPGRADED = 1 << 8;   // took an upgrade
constexpr uint32_t PLAYER_RESPAWNED = 1 << 9;  // lost a core and came back
constexpr uint32_t PLAYER_DODGED = 1 << 10;    // started an emergency dodge
constexpr uint32_t SPHERE_TIME_UP = 1 << 11;   // the time limit ran out
}  // namespace Event

// Sound effects requested during the last tick (cleared every tick), one bit
// per kind in Game::sounds(). The simulation only says what to play; the
// waveforms and the playback belong to the platform (impl/wasm/ for the
// browser). A kind is raised at most once per tick, and the kinds listed in
// SOUND_MIN_GAP_TICKS keep a minimum gap so that bursts (a shower of
// fragments after a kill) do not machine-gun
enum class SoundKind : uint8_t {
  SHOT_VULCAN,         // the player fired
  SHOT_LASER,
  SHOT_MISSILE,
  HIT_ENEMY,           // the player's bullet hit an enemy
  HIT_PLAYER,          // an enemy bullet hit the player
  ENEMY_KILLED_SMALL,  // the player killed an enemy no bigger than itself
  ENEMY_KILLED_BIG,    // the player killed a bigger enemy
  PLAYER_KILLED,       // the player was shot down (game over or a lost core)
  GET_FRAGMENT,        // the player took a floating fragment (food or heal)
  GET_UPGRADE,         // the player took an upgrade
  MENU_SELECT,         // the menu cursor moved
  MENU_START,          // a menu choice was confirmed
  LAUNCH,              // the LAUNCH cinematic began (leaving the sphere)
  ARRIVE,              // the ARRIVE cinematic began (flying in)
  TIME_ALARM,          // once a second during the last TIME_ALARM_TICKS
  DODGE,               // the emergency dodge started
  COUNT
};
static_assert((int)SoundKind::COUNT == SOUND_KINDS, "SOUND_MIN_GAP_TICKS");

// Positions of things worth an effect during the last tick (cleared every
// tick); the renderer turns them into debris
enum class EffectKind : uint8_t {
  PLAYER_HIT,      // the player was hit by a bullet
  PLAYER_DRAINED,  // the player is being absorbed
  ENEMY_HIT,       // an enemy was hit by the player's bullet
  ENEMY_DRAINED,   // an enemy is being absorbed by the player
  ENTITY_KILLED,   // an enemy near the player was shot down (many debris)
  PLAYER_KILLED,   // the player was shot down
};

struct EffectEvent {
  EffectKind kind;
  int16_t entity;  // the entity that was hit / drained (flashes)
  Vec3 n;          // unit normal of the position
  int32_t r;       // distance from the center
  int32_t size;    // magnitude hint (damage or size transferred)
};

// Counters for tuning and tests (never reset except by reset())
struct DebugStats {
  uint32_t shots, hits, kills, absorbs, fragmentsEaten, fragmentsHealed, crits;
  // Enemy fire that reached the player's gauge: hits, and the sum of the
  // fractions of the gauge they took (Q8: 256 = one full gauge)
  uint32_t playerHits, playerDamageQ8;
};

extern const int LAUNCH_TICKS;  // length of the LAUNCH state
extern const int ARRIVE_TICKS;  // length of the ARRIVE state
// Tick of ARRIVE at which the next sphere replaces the old one (the camera
// is beside the player, neither sphere on screen)
extern const int ARRIVE_SWITCH_TICKS;

class Game {
 public:
  Game();

  // Reset everything and enter the title screen
  void reset(uint32_t seed);

  // Advance the simulation by one tick with the given button state
  void tick(uint8_t buttons);

  // Debug: skip the menus and start playing on a sphere of the given level
  void debugStartSphere(int level, int weapon);
  // Debug: let the AI drive the player while playing (for balancing runs)
  void debugAutoPlayer(bool on) { autoPlayer_ = on; }
  // Debug mode (the WASM front end enters it from the URL): cheats for
  // reaching any part of the game quickly. Any of them, or setDebugMode(),
  // makes the HUD show DEBUG MODE over the score for the rest of the run
  // (reset() does not clear it), so a screenshot of a cheated game says so.
  void setDebugMode(bool on) { debugMode_ = on; }
  bool debugMode() const { return debugMode_; }
  void debugTakeUpgrade(UpgradeKind k);
  void debugScaleSize(bool bigger);  // player size x2 / x0.5
  void debugHeal(int pct);           // player health +- pct of the maximum

  // --- Read-only access for the renderer ------------------------------------
  GameState state() const { return state_; }
  uint32_t tickCount() const { return tickCount_; }
  uint32_t events() const { return events_; }
  // SoundKind bits of the last tick; nothing while muted
  uint32_t sounds() const { return muted_ ? 0 : sounds_; }
  // Paused (Button::PAUSE during PLAYING / LAUNCH / ARRIVE): the ticks do
  // nothing but read the pause menu (PAUSE resumes, DOWN toggles the mute),
  // so the game goes on exactly as if it had not been paused
  bool paused() const { return paused_; }
  // Mute: DOWN on the title or the pause screen, or the platform's own
  // control. A setting, so it survives reset()
  bool muted() const { return muted_; }
  void setMuted(bool m) { muted_ = m; }
  int sphereLevel() const { return sphereLevel_; }
  int spheresCleared() const { return spheresCleared_; }
  int selectedWeapon() const { return selectedWeapon_; }
  int stateTimer() const { return stateTimer_; }  // ticks in the current state
  uint32_t sphereSeed() const { return sphereSeed_; }

  int playerIndex() const { return playerIndex_; }
  // The largest enemy alive (by size alone, like the ranks; -1 when none)
  int largestEnemy() const { return largestEnemy_; }
  // Enemies alive with a bounty on them (Entity::bounty): the sphere is
  // cleared once this stays 0 for a moment (see updateRanks)
  int bountyCount() const { return bountyCount_; }
  const Entity &player() const { return entities[playerIndex_]; }
  int playerRank() const { return entities[playerIndex_].rank; }
  int aliveEntities() const { return aliveEntities_; }
  uint32_t playerDisplayScaleLog2() const { return displayScaleLog2_; }
  uint32_t score() const { return (uint32_t)(scoreQ8_ >> 8); }
  // Ticks played on this sphere (only while PLAYING and alive), and what is
  // left of the time limit
  int sphereTicks() const { return sphereTicks_; }
  // The time limit of the current sphere (longer on the first two)
  int sphereTimeLimit() const { return sphereTimeLimitTicks(sphereLevel_); }
  int sphereTimeLeft() const {
    int left = sphereTimeLimit() - sphereTicks_;
    return left < 0 ? 0 : left;
  }
  // The time ran out: the wreck is being watched before the sphere restarts
  bool timeUp() const { return timeUp_; }
  // The last clear, for the LAUNCH summary: points earned on that sphere
  // (before the bonus), the clear bonus, and the ticks it took
  uint32_t sphereScore() const { return (uint32_t)(lastSphereScoreQ8_ >> 8); }
  uint32_t clearBonus() const { return (uint32_t)(lastClearBonusQ8_ >> 8); }
  int clearTicks() const { return lastClearTicks_; }
  // Score multiplier of the current sphere (Q8): 1.1^(level - 1)
  int32_t levelMultQ8() const;
  // Radial movement of the player during the last tick (units, positive
  // away from the sphere): the renderer pitches the player along the
  // flight path from it. Zero on the surface.
  int32_t playerClimb() const { return playerClimb_; }
  // Whether the arrival will switch the sphere (and rescale the player)
  // at ARRIVE_SWITCH_TICKS; false on the first sphere of a game
  bool switchPending() const { return switchPending_; }
  // The player's size once rescaled for the next sphere (what
  // rescalePlayerForNextSphere() will make it; the size itself when no
  // rescale is due). The renderer matches the camera distance to it.
  uint32_t playerSizeAfterSwitch() const;
  // The high score lives outside the simulation (platform storage); it is
  // only kept here for display
  void setHighScore(uint32_t v, int sphere = 0) {
    highScore_ = v;
    highScoreSphere_ = sphere;
  }
  uint32_t highScore() const { return highScore_; }
  int highScoreSphere() const { return highScoreSphere_; }  // reached then
  // The score is the player's own only during a run. On the title and the
  // weapon select screen the attract demo (the AI-driven player) scores
  // too, and that must never become the high score
  bool scoreIsPlayers() const {
    return state_ != GameState::TITLE && state_ != GameState::WEAPON_SELECT;
  }
  // Take the score as the high score when it is the player's and beats it;
  // true when it did (the platform then stores it)
  bool keepHighScore() {
    if (!scoreIsPlayers() || score() <= highScore_) return false;
    setHighScore(score(), sphereLevel_);
    return true;
  }

  // Hash of the whole state (for determinism tests)
  uint32_t stateHash() const;

  const DebugStats &debugStats() const { return stats_; }

  static constexpr int MAX_EFFECTS = 16;
  const EffectEvent *effects() const { return effects_; }
  int effectCount() const { return effectCount_; }

  // Where the ticks spend their time, accumulated since the last reset and
  // only counted while devoursphere::profileClockUs is set. Slots:
  //   0 AI decisions   1 movement   2 fragment layout and merging
  //   3 firing         4 bullets    5 floating fragments and upgrades
  //   6 neighbor orders   7 eating   8 entity collisions
  //   9 everything else (menus, ranks, respawns, transitions)
  enum TickPhase {
    TP_AI = 0,
    TP_MOVE,
    TP_LAYOUT,
    TP_FIRE,
    TP_BULLETS,
    TP_FRAGMENTS,
    TP_ORDERS,
    TP_EATING,
    TP_COLLISIONS,
    TP_OTHER,
    TP_COUNT
  };
  const PhaseTimer &tickProfile() const { return tickProfile_; }
  void resetTickProfile() { tickProfile_.reset(); }

  Entity entities[MAX_ENTITIES];
  FloatingFragment floatingFragments[MAX_FLOATING_FRAGMENTS];
  Bullet bullets[MAX_BULLETS];
  FloatingUpgrade floatingUpgrades[MAX_FLOATING_UPGRADES];

  // --- Upgrades and cores (player) ---------------------------------------
  int upgradeLevel(UpgradeKind k) const {
    int i = (int)k - 1;
    return (i >= 0 && i < UPGRADE_KINDS) ? upgradeLevels_[i] : 0;
  }
  int totalUpgradeLevel() const {
    return upgradeLevels_[0] + upgradeLevels_[1] + upgradeLevels_[2];
  }
  int maxUpgradeLevel() const {
    int m = upgradeLevels_[0];
    if (upgradeLevels_[1] > m) m = upgradeLevels_[1];
    if (upgradeLevels_[2] > m) m = upgradeLevels_[2];
    return m;
  }
  int cores() const { return cores_; }
  // Upgrades of a kind on the sphere: floating or carried by an enemy
  int countUpgradeKind(UpgradeKind k) const;
  UpgradeKind lastUpgradeKind() const { return lastUpgradeKind_; }
  // Emergency dodge: ticks left of the roll, and how ready the next one is
  // (Q8: 256 = ready now, for the HUD)
  int dodgeTicks() const { return dodgeTicks_; }
  int dodgeReadyQ8() const {
    return dodgeCooldown_ <= 0
               ? 256
               : 256 - (int)((int64_t)dodgeCooldown_ * 256 / DODGE_COOLDOWN_TICKS);
  }
  // The enemies' behaviour on this sphere
  const AiTier &aiTier() const {
    int i = sphereLevel_ - 1;
    if (i < 0) i = 0;
    if (i >= AI_TIER_LEVELS) i = AI_TIER_LEVELS - 1;
    return AI_TIERS[i];
  }
  // Ticks until the player respawns (0 when alive or game over)
  int respawnDelay() const { return respawnDelay_; }

 private:
  GameState state_ = GameState::TITLE;
  uint32_t tickCount_ = 0;
  uint32_t events_ = 0;
  uint32_t sounds_ = 0;
  uint8_t soundGap_[SOUND_KINDS] = {};  // ticks until the kind may play again
  void pushSound(SoundKind k);
  bool paused_ = false;
  bool muted_ = false;
  void toggleMute();
  int stateTimer_ = 0;
  int sphereLevel_ = 1;
  int spheresCleared_ = 0;
  int selectedWeapon_ = 0;
  uint32_t seed_ = 1;
  uint32_t sphereSeed_ = 1;
  Random rng_;
  int playerIndex_ = 0;
  int aliveEntities_ = 0;
  int largestEnemy_ = -1;
  int bountyCount_ = 0;
  int clearGrace_ = 0;  // ticks in a row without a bounty out (PLAYING)
  int respawnTimer_ = 0;
  int foodTimer_ = 0;
  uint32_t displayScaleLog2_ = 0;
  uint8_t prevButtons_ = 0;
  bool autoPlayer_ = false;
  uint64_t scoreQ8_ = 0;
  int upgradeLevels_[UPGRADE_KINDS] = {0, 0, 0};
  int cores_ = CORES_START;
  int32_t regenAccQ8_ = 0;
  UpgradeKind lastUpgradeKind_ = UpgradeKind::NONE;
  int respawnDelay_ = 0;
  // Player only (so the Entity does not grow): mercy ticks after a hit,
  // and the emergency dodge
  int16_t playerMercy_ = 0;
  int16_t dodgeTicks_ = 0;
  int16_t dodgeCooldown_ = 0;
  int8_t dodgeDir_ = 1;  // +1 right, -1 left
  void startDodge(uint8_t buttons);
  void resetPlayerTimers() {
    playerMercy_ = 0;
    dodgeTicks_ = 0;
    dodgeCooldown_ = 0;
  }
  void resetUpgrades();
  void assignUpgrades();
  void releaseUpgrade(int idx);
  void spawnFloatingUpgrade(UpgradeKind k, const Vec3 &n, int32_t r);
  void updateFloatingUpgrades();
  void takeUpgrade(UpgradeKind k);
  // The levels lost on a death, one floating upgrade each on a circle of
  // UPGRADE_SCATTER_FU around `center` (up to the per-kind cap)
  void scatterLostUpgrades(const int lost[UPGRADE_KINDS], const Vec3 &center,
                           int32_t r);
  void updateShieldRegen();
  bool respawnPlayer();
  // The enemies placed around the respawn point for the player to grow back
  // on (see RESPAWN_PACK_MAX). `lost` is the size the death cost
  void spawnRespawnPack(uint32_t lost);
  // In flight (after clearing a sphere and until landing on the next) the
  // player is frozen: no hits, no eating, no absorption in either direction,
  // so the body keeps its shape; the AI ignores it too
  bool playerFrozen() const {
    return state_ == GameState::LAUNCH || state_ == GameState::ARRIVE;
  }
  int32_t playerClimb_ = 0;
  // ARRIVE from a launch switches the sphere at ARRIVE_SWITCH_TICKS; the
  // first sphere of a game has nothing to leave and does not
  bool switchPending_ = false;
  bool debugMode_ = false;
  void beginArrival();
  void switchSphere();
  // The descent of the arrival: the target radius at a tick of ARRIVE and
  // the profile's slope there (units per tick, positive)
  int32_t descentTarget(int timer) const;
  int32_t descentSlope(int timer) const;
  int32_t enemyDamagePct() const;
  int sphereTicks_ = 0;
  bool timeUp_ = false;
  void restartSphereAfterTimeUp();
  uint64_t sphereScoreStartQ8_ = 0;  // the score when this sphere began
  uint64_t lastSphereScoreQ8_ = 0, lastClearBonusQ8_ = 0;
  int lastClearTicks_ = 0;
  uint32_t highScore_ = 0;
  int highScoreSphere_ = 0;
  void addScore(int64_t baseQ8);
  DebugStats stats_ = {};
  PhaseTimer tickProfile_;
  EffectEvent effects_[MAX_EFFECTS];
  int effectCount_ = 0;
  void pushEffect(EffectKind kind, int entity, const Vec3 &n, int32_t r,
                  int32_t size);

  // Indices sorted by n.z (for neighbor queries)
  int16_t entityOrder_[MAX_ENTITIES];
  int entityOrderCount_ = 0;
  int16_t fragmentOrder_[MAX_FLOATING_FRAGMENTS];
  int fragmentOrderCount_ = 0;
  int32_t maxBodyRadius_ = 0;  // over all living entities (this tick)
  int32_t maxCoreReach_ = 0;   // max coreHalf + |coreY|

  void setState(GameState s);
  void startSphere(bool keepPlayer);
  void spawnPlayer();
  void spawnEnemy(int index, int sizeLog2, bool farFromPlayer);
  void placeRandom(Frame &f, int32_t &r, uint32_t size, bool farFromPlayer);
  void initEntity(Entity &c, int sizeLog2, Weapon w);
  void addFragmentToEntity(Entity &c, int sizeLog2, int32_t lx, int32_t ly);
  void enforceFragmentLimit(Entity &c);

  void updatePlayerControls(uint8_t buttons, uint8_t pressed);
  void updateAi(int idx);
  void moveEntity(Entity &c);
  void updateLayout(Entity &c);
  void mergeFragments(Entity &c);
  void fireWeapon(int idx);
  void updateBullets();
  void updateFloatingFragments();
  void rebuildOrders();
  int entityLowerBound(int64_t z) const;
  void handleEating();
  void handleEntityCollisions();
  void damageEntity(int idx, int32_t dmg, int attacker, bool allowCrit = true);
  void killEntity(int idx, int killer);  // killer: entity index or -1
  void transferSize(int from, int to);
  void setEntitySize(Entity &e, uint32_t size);
  void pushFragment(Entity &e, int sizeLog2, int32_t lx, int32_t ly);
  void healByFragment(Entity &e, int sizeLog2);
  void healBySize(Entity &e, uint32_t sizeUnits);
  void syncFragments(Entity &e, int32_t lx, int32_t ly);
  void spawnFloatingFragment(const Vec3 &n, int32_t r, int sizeLog2,
                             const Vec3 &drift, int owner = -1);
  void criticalHit(int idx, const Vec3 &from);
  void updateRanks();
  void updateRespawns();
  void spawnFood(bool farFromPlayer);
  void checkTransitions();
  void rescalePlayerForNextSphere();
  int findFreeEntity() const;
  bool tangentialDist2(const Vec3 &a, const Vec3 &b, int32_t maxUnits,
                       int64_t &d2) const;
  Vec3 worldPos(const Vec3 &n, int32_t r) const { return scaleToLength(n, r); }
};

}  // namespace devoursphere::sim

#endif
