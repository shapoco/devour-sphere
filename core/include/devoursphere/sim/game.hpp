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
  TITLE,          // attract mode: the player creature is driven by the AI
  WEAPON_SELECT,  // choose a weapon with left/right, confirm with A
  PLAYING,
  LAUNCH,  // the player became the largest: leaving the planet
  DEAD,    // game over screen
};

// Event flags raised during the last tick (for effects; cleared every tick)
namespace Event {
constexpr uint32_t PLAYER_HIT = 1 << 0;
constexpr uint32_t PLAYER_ATE_PART = 1 << 1;
constexpr uint32_t PLAYER_ATE_ENERGY = 1 << 2;
constexpr uint32_t PLAYER_DIED = 1 << 3;
constexpr uint32_t PLANET_CLEARED = 1 << 4;
constexpr uint32_t PLAYER_FIRED = 1 << 5;
constexpr uint32_t PLAYER_MERGED = 1 << 6;
}  // namespace Event

// Counters for tuning and tests (never reset except by reset())
struct DebugStats {
  uint32_t shots, hits, kills, absorbs, partsEaten, particlesEaten;
};

class Game {
 public:
  Game();

  // Reset everything and enter the title screen
  void reset(uint32_t seed);

  // Advance the simulation by one tick with the given button state
  void tick(uint8_t buttons);

  // Debug: skip the menus and start playing on a planet of the given level
  void debugStartPlanet(int level, int weapon);

  // --- Read-only access for the renderer ------------------------------------
  GameState state() const { return state_; }
  uint32_t tickCount() const { return tickCount_; }
  uint32_t events() const { return events_; }
  int planetLevel() const { return planetLevel_; }
  int planetsCleared() const { return planetsCleared_; }
  int selectedWeapon() const { return selectedWeapon_; }
  int stateTimer() const { return stateTimer_; }  // ticks in the current state
  uint32_t planetSeed() const { return planetSeed_; }

  int playerIndex() const { return playerIndex_; }
  const Creature &player() const { return creatures[playerIndex_]; }
  int playerRank() const { return creatures[playerIndex_].rank; }
  int aliveCreatures() const { return aliveCreatures_; }
  uint32_t playerDisplayScaleLog2() const { return displayScaleLog2_; }

  // Hash of the whole state (for determinism tests)
  uint32_t stateHash() const;

  const DebugStats &debugStats() const { return stats_; }

  Creature creatures[MAX_CREATURES];
  FloatingPart floatingParts[MAX_FLOATING_PARTS];
  Bullet bullets[MAX_BULLETS];
  Particle particles[MAX_PARTICLES];

 private:
  GameState state_ = GameState::TITLE;
  uint32_t tickCount_ = 0;
  uint32_t events_ = 0;
  int stateTimer_ = 0;
  int planetLevel_ = 1;
  int planetsCleared_ = 0;
  int selectedWeapon_ = 0;
  uint32_t seed_ = 1;
  uint32_t planetSeed_ = 1;
  Random rng_;
  int playerIndex_ = 0;
  int aliveCreatures_ = 0;
  int respawnTimer_ = 0;
  uint32_t displayScaleLog2_ = 0;
  uint8_t prevButtons_ = 0;
  DebugStats stats_ = {};

  // Sorted index of floating parts by n.z (for neighbor queries)
  int16_t partOrder_[MAX_FLOATING_PARTS];
  int partOrderCount_ = 0;
  int16_t particleOrder_[MAX_PARTICLES];
  int particleOrderCount_ = 0;

  void setState(GameState s);
  void startPlanet(bool keepPlayer);
  void spawnPlayer();
  void spawnEnemy(int index, int sizeLog2, bool farFromPlayer);
  void placeRandom(Frame &f, int32_t &r, uint32_t size, bool farFromPlayer);
  void initCreature(Creature &c, int sizeLog2, Weapon w);
  void addPartToCreature(Creature &c, int sizeLog2, int32_t lx, int32_t ly);
  void enforcePartLimit(Creature &c);

  void updatePlayerControls(uint8_t buttons);
  void updateAi(int idx);
  void moveCreature(Creature &c);
  void updateLayout(Creature &c);
  void mergeParts(Creature &c);
  void fireWeapon(int idx);
  void updateBullets();
  void updateFloatingParts();
  void updateParticles();
  void rebuildOrders();
  void handleEating();
  void handleCreatureCollisions();
  void damageCreature(int idx, int32_t dmg, int attacker);
  void killCreature(int idx);
  void absorbCreature(int eater, int eaten);
  void spawnFloatingPart(const Vec3 &n, int32_t r, int sizeLog2,
                         const Vec3 &drift);
  void spawnParticle(const Vec3 &n, int32_t r, int32_t energy,
                     const Vec3 &drift);
  void updateRanks();
  void updateRespawns();
  void checkTransitions();
  void rescalePlayerForNextPlanet();
  int findFreeCreature() const;
  bool tangentialDist2(const Vec3 &a, const Vec3 &b, int32_t maxUnits,
                       int64_t &d2) const;
  Vec3 worldPos(const Vec3 &n, int32_t r) const { return scaleToLength(n, r); }
};

}  // namespace devoursphere::sim

#endif
