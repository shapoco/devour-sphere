#ifndef DEVOURSPHERE_SIM_GAME_HPP
#define DEVOURSPHERE_SIM_GAME_HPP

// The whole game state and its deterministic update. The renderer reads the
// public members; the platform layer feeds `tick()` with the button state.

#include <cstdint>

#include "devoursphere/sim/config.hpp"
#include "devoursphere/sim/entities.hpp"
#include "devoursphere/sim/fixed.hpp"

namespace devoursphere::sim {

enum class GameState : uint8_t {
  TITLE,          // attract mode: the player entity is driven by the AI
  WEAPON_SELECT,  // choose a weapon with left/right, confirm with A
  PLAYING,
  LAUNCH,  // the player became the largest: leaving the sphere
  DEAD,    // game over screen
};

// Event flags raised during the last tick (for effects; cleared every tick)
namespace Event {
constexpr uint32_t PLAYER_HIT = 1 << 0;
constexpr uint32_t PLAYER_ATE_FRAGMENT = 1 << 1;
constexpr uint32_t PLAYER_ATE_SPARK = 1 << 2;
constexpr uint32_t PLAYER_DIED = 1 << 3;
constexpr uint32_t SPHERE_CLEARED = 1 << 4;
constexpr uint32_t PLAYER_FIRED = 1 << 5;
constexpr uint32_t PLAYER_MERGED = 1 << 6;
}  // namespace Event

// Positions of things worth an effect during the last tick (cleared every
// tick); the renderer turns them into debris
enum class EffectKind : uint8_t {
  PLAYER_HIT,      // the player was hit by a bullet
  PLAYER_DRAINED,  // the player is being absorbed
  ENEMY_HIT,       // an enemy was hit by the player's bullet
  ENEMY_DRAINED,   // an enemy is being absorbed by the player
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
  uint32_t shots, hits, kills, absorbs, fragmentsEaten, sparksEaten;
};

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

  // --- Read-only access for the renderer ------------------------------------
  GameState state() const { return state_; }
  uint32_t tickCount() const { return tickCount_; }
  uint32_t events() const { return events_; }
  int sphereLevel() const { return sphereLevel_; }
  int spheresCleared() const { return spheresCleared_; }
  int selectedWeapon() const { return selectedWeapon_; }
  int stateTimer() const { return stateTimer_; }  // ticks in the current state
  uint32_t sphereSeed() const { return sphereSeed_; }

  int playerIndex() const { return playerIndex_; }
  const Entity &player() const { return entities[playerIndex_]; }
  int playerRank() const { return entities[playerIndex_].rank; }
  int aliveEntities() const { return aliveEntities_; }
  uint32_t playerDisplayScaleLog2() const { return displayScaleLog2_; }

  // Hash of the whole state (for determinism tests)
  uint32_t stateHash() const;

  const DebugStats &debugStats() const { return stats_; }

  static constexpr int MAX_EFFECTS = 16;
  const EffectEvent *effects() const { return effects_; }
  int effectCount() const { return effectCount_; }

  Entity entities[MAX_ENTITIES];
  FloatingFragment floatingFragments[MAX_FLOATING_FRAGMENTS];
  Bullet bullets[MAX_BULLETS];
  Spark sparks[MAX_SPARKS];

 private:
  GameState state_ = GameState::TITLE;
  uint32_t tickCount_ = 0;
  uint32_t events_ = 0;
  int stateTimer_ = 0;
  int sphereLevel_ = 1;
  int spheresCleared_ = 0;
  int selectedWeapon_ = 0;
  uint32_t seed_ = 1;
  uint32_t sphereSeed_ = 1;
  Random rng_;
  int playerIndex_ = 0;
  int aliveEntities_ = 0;
  int respawnTimer_ = 0;
  int foodTimer_ = 0;
  uint32_t displayScaleLog2_ = 0;
  uint8_t prevButtons_ = 0;
  bool autoPlayer_ = false;
  DebugStats stats_ = {};
  EffectEvent effects_[MAX_EFFECTS];
  int effectCount_ = 0;
  void pushEffect(EffectKind kind, int entity, const Vec3 &n, int32_t r,
                  int32_t size);

  // Indices sorted by n.z (for neighbor queries)
  int16_t entityOrder_[MAX_ENTITIES];
  int entityOrderCount_ = 0;
  int16_t fragmentOrder_[MAX_FLOATING_FRAGMENTS];
  int fragmentOrderCount_ = 0;
  int16_t sparkOrder_[MAX_SPARKS];
  int sparkOrderCount_ = 0;
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

  void updatePlayerControls(uint8_t buttons);
  void updateAi(int idx);
  void moveEntity(Entity &c);
  void updateLayout(Entity &c);
  void mergeFragments(Entity &c);
  void fireWeapon(int idx);
  void updateBullets();
  void updateFloatingFragments();
  void updateSparks();
  void rebuildOrders();
  int entityLowerBound(int64_t z) const;
  void handleEating();
  void handleEntityCollisions();
  void damageEntity(int idx, int32_t dmg, int attacker);
  void killEntity(int idx);
  void transferSize(int from, int to);
  void setEntitySize(Entity &e, uint32_t size);
  void pushFragment(Entity &e, int sizeLog2, int32_t lx, int32_t ly);
  void syncFragments(Entity &e, int32_t lx, int32_t ly);
  void spawnFloatingFragment(const Vec3 &n, int32_t r, int sizeLog2,
                             const Vec3 &drift);
  void spawnSpark(const Vec3 &n, int32_t r, int32_t energy, const Vec3 &drift);
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
