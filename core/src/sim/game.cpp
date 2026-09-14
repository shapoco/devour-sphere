// Game state machine, planet setup, spawning and bookkeeping.

#include <cstring>

#include "devoursphere/sim/game.hpp"

namespace devoursphere::sim {

static constexpr int LAUNCH_TICKS = 3 * TICK_RATE;
static constexpr int DEAD_WAIT_TICKS = 2 * TICK_RATE;
static constexpr int CLEAR_GRACE_TICKS = 4 * TICK_RATE;

Game::Game() { reset(1); }

void Game::reset(uint32_t seed) {
  seed_ = seed;
  rng_.reset(seed);
  tickCount_ = 0;
  events_ = 0;
  planetLevel_ = 1;
  planetsCleared_ = 0;
  selectedWeapon_ = 0;
  displayScaleLog2_ = 0;
  prevButtons_ = 0xFF;  // buttons held during reset do not count as presses
  stats_ = {};
  startPlanet(false);
  setState(GameState::TITLE);
}

void Game::debugStartPlanet(int level, int weapon) {
  planetLevel_ = level < 1 ? 1 : level;
  planetsCleared_ = level - 1;
  selectedWeapon_ = weapon % WEAPON_COUNT;
  displayScaleLog2_ = 0;
  setState(GameState::PLAYING);
  startPlanet(false);
}

void Game::setState(GameState s) {
  state_ = s;
  stateTimer_ = 0;
}

int Game::findFreeCreature() const {
  for (int i = 0; i < MAX_CREATURES; i++) {
    if (!creatures[i].alive) return i;
  }
  return -1;
}

void Game::startPlanet(bool keepPlayer) {
  planetSeed_ = rng_.next();
  Creature saved;
  if (keepPlayer) saved = creatures[playerIndex_];

  std::memset(creatures, 0, sizeof(creatures));
  std::memset(floatingParts, 0, sizeof(floatingParts));
  std::memset(bullets, 0, sizeof(bullets));
  std::memset(particles, 0, sizeof(particles));
  creatureOrderCount_ = 0;
  partOrderCount_ = 0;
  particleOrderCount_ = 0;

  playerIndex_ = 0;
  if (keepPlayer) {
    creatures[0] = saved;
    Creature &p = creatures[0];
    p.alive = true;
    p.invincible = 2 * TICK_RATE;
    p.hp = p.hpMax;
    placeRandom(p.frame, p.r, p.size, false);
  } else {
    spawnPlayer();
  }

  int maxExp = 8 + planetLevel_;
  if (maxExp > 16) maxExp = 16;
  for (int i = 1; i < INITIAL_CREATURES; i++) {
    // Two rolls, keep the smaller: many small creatures, few giants
    int a = (int)rng_.below((uint32_t)maxExp + 1);
    int b = (int)rng_.below((uint32_t)maxExp + 1);
    int e = a < b ? a : b;
    if (i < 4) e = maxExp;  // guarantee a few giants
    spawnEnemy(i, e, true);
  }
  for (int i = 0; i < INITIAL_FOOD_PARTS; i++) spawnFood(false);
  respawnTimer_ = RESPAWN_INTERVAL;
  foodTimer_ = FOOD_SPAWN_INTERVAL;
  updateRanks();
  rebuildOrders();
}

// A free part somewhere on the planet; small sizes are the most common
void Game::spawnFood(bool farFromPlayer) {
  Frame f;
  int32_t r;
  int a = (int)rng_.below(FOOD_MAX_SIZE_LOG2 + 1);
  int b = (int)rng_.below(FOOD_MAX_SIZE_LOG2 + 1);
  int k = a < b ? a : b;
  placeRandom(f, r, 4u << k, farFromPlayer);
  spawnFloatingPart(f.n, r, k, {0, 0, 0});
}

void Game::initCreature(Creature &c, int sizeLog2, Weapon w) {
  std::memset(&c, 0, sizeof(c));
  c.alive = true;
  c.weapon = w;
  c.aiTarget = -1;
  c.seed = rng_.next();
  c.partCount = 0;
  c.size = 0;
  // Decompose 2^k into a few parts for a more interesting body
  if (sizeLog2 <= 0) {
    addPartToCreature(c, 0, PU / 2, -PU / 2);
  } else if (sizeLog2 == 1) {
    addPartToCreature(c, 0, PU / 2, -PU / 2);
    addPartToCreature(c, 0, PU, -PU);
  } else {
    int32_t h = partHalfSize(sizeLog2 - 1);
    addPartToCreature(c, sizeLog2 - 1, h, -h);
    addPartToCreature(c, sizeLog2 - 2, h * 2, -h * 2);
    addPartToCreature(c, sizeLog2 - 2, h / 2, -h * 3);
  }
  c.hpMax = HP_PER_SIZE * (int32_t)c.size;
  c.hp = c.hpMax;
  c.speed = cruiseSpeedForSize(c.size);
  c.frame.n = {0, 0, Q30_ONE};
  c.frame.t = {0, Q30_ONE, 0};
  c.r = PLANET_RADIUS + altitudeForSize(c.size);
  c.aiWanderAngle = rng_.brad();
  updateLayout(c);
}

void Game::spawnPlayer() {
  Creature &p = creatures[playerIndex_];
  Weapon w = (Weapon)selectedWeapon_;
  if (state_ == GameState::TITLE) w = (Weapon)rng_.below(WEAPON_COUNT);
  initCreature(p, PLAYER_START_SIZE_LOG2, w);
  p.isPlayer = true;
  p.hue = PLAYER_HUE;
  p.invincible = 2 * TICK_RATE;
  placeRandom(p.frame, p.r, p.size, false);
}

void Game::spawnEnemy(int index, int sizeLog2, bool farFromPlayer) {
  Creature &c = creatures[index];
  // Weapons available to enemies depend on the planet level
  Weapon w = Weapon::VULCAN;
  if (planetLevel_ >= 4) {
    w = (Weapon)rng_.below(WEAPON_COUNT);
  } else if (planetLevel_ >= 3) {
    w = rng_.below(2) ? Weapon::MISSILE : Weapon::VULCAN;
  }
  initCreature(c, sizeLog2, w);
  c.hue = ENEMY_HUES[rng_.below(ENEMY_HUE_COUNT)];
  c.invincible = TICK_RATE;
  placeRandom(c.frame, c.r, c.size, farFromPlayer);
}

void Game::placeRandom(Frame &f, int32_t &r, uint32_t size,
                       bool farFromPlayer) {
  const Creature &p = creatures[playerIndex_];
  // cos(9 degrees) in Q30: creatures spawn at least ~160 PU away
  constexpr int32_t COS_NEAR = (int32_t)(0.9877 * Q30_ONE);
  Vec3 n;
  for (int attempt = 0; attempt < 16; attempt++) {
    Vec3 v = {(int32_t)rng_.next() >> 1, (int32_t)rng_.next() >> 1,
              (int32_t)rng_.next() >> 1};
    n = normalizeQ30(v);
    if (!farFromPlayer || !p.alive) break;
    if (dotQ30(n, p.frame.n) < COS_NEAR) break;
  }
  Vec3 v = {(int32_t)rng_.next() >> 1, (int32_t)rng_.next() >> 1,
            (int32_t)rng_.next() >> 1};
  Vec3 t = orthonormalizeQ30(v, n);
  if (absI32(dotQ30(t, n)) > (Q30_ONE >> 8)) {
    // v was (nearly) parallel to n; pick any perpendicular vector
    Vec3 helper =
        absI32(n.x) < absI32(n.y) ? Vec3{Q30_ONE, 0, 0} : Vec3{0, Q30_ONE, 0};
    t = normalizeQ30(crossQ30(n, helper));
  }
  f.n = n;
  f.t = t;
  r = PLANET_RADIUS + altitudeForSize(size);
}

void Game::rescalePlayerForNextPlanet() {
  Creature &p = creatures[playerIndex_];
  int k = log2Floor(p.size);
  int shift = k - PLAYER_START_SIZE_LOG2 - 1;
  if (shift <= 0) return;
  displayScaleLog2_ += shift;
  // Shrink every part; positions shrink with the kite size
  int32_t oldHalf = partHalfSize(shift), newHalf = partHalfSize(0);
  uint32_t total = 0;
  for (int i = 0; i < p.partCount; i++) {
    Part &pt = p.parts[i];
    int e = (int)pt.sizeLog2 - shift;
    pt.sizeLog2 = (uint8_t)(e < 0 ? 0 : e);
    pt.x = (int32_t)(((int64_t)pt.x * newHalf) / oldHalf);
    pt.y = (int32_t)(((int64_t)pt.y * newHalf) / oldHalf);
    pt.vx = pt.vy = 0;
    total += 1u << pt.sizeLog2;
  }
  p.size = total;
  p.hpMax = HP_PER_SIZE * (int32_t)p.size;
  p.hp = p.hpMax;
  p.speed = cruiseSpeedForSize(p.size);
  updateLayout(p);
}

void Game::updateRanks() {
  Creature &p = creatures[playerIndex_];
  int rank = 1;
  aliveCreatures_ = 0;
  for (int i = 0; i < MAX_CREATURES; i++) {
    const Creature &c = creatures[i];
    if (!c.alive) continue;
    aliveCreatures_++;
    if (i != playerIndex_ && c.size > p.size) rank++;
  }
  p.rank = (uint16_t)rank;
}

void Game::updateRespawns() {
  if (--foodTimer_ <= 0) {
    foodTimer_ = FOOD_SPAWN_INTERVAL;
    int n = 0;
    for (int i = 0; i < MAX_FLOATING_PARTS; i++) n += floatingParts[i].alive;
    if (n < FOOD_TARGET) spawnFood(true);
  }
  if (aliveCreatures_ >= INITIAL_CREATURES) return;
  if (--respawnTimer_ > 0) return;
  respawnTimer_ = RESPAWN_INTERVAL;
  int idx = findFreeCreature();
  if (idx < 0) return;
  int maxExp = 4 + planetLevel_;
  int a = (int)rng_.below((uint32_t)maxExp + 1);
  int b = (int)rng_.below((uint32_t)maxExp + 1);
  spawnEnemy(idx, a < b ? a : b, true);
}

void Game::checkTransitions() {
  Creature &p = creatures[playerIndex_];
  switch (state_) {
    case GameState::TITLE:
    case GameState::WEAPON_SELECT:
      // Attract mode: keep the AI-driven player alive
      if (!p.alive) spawnPlayer();
      break;
    case GameState::PLAYING:
      if (!p.alive) {
        setState(GameState::DEAD);
      } else if (p.rank == 1 && stateTimer_ > CLEAR_GRACE_TICKS) {
        events_ |= Event::PLANET_CLEARED;
        setState(GameState::LAUNCH);
      }
      break;
    case GameState::LAUNCH:
      if (stateTimer_ >= LAUNCH_TICKS) {
        planetsCleared_++;
        planetLevel_++;
        rescalePlayerForNextPlanet();
        startPlanet(true);
        setState(GameState::PLAYING);
      }
      break;
    case GameState::DEAD: break;
  }
}

void Game::tick(uint8_t buttons) {
  uint8_t pressed = buttons & (uint8_t)~prevButtons_;
  prevButtons_ = buttons;
  events_ = 0;
  tickCount_++;
  stateTimer_++;

  // Menu handling
  switch (state_) {
    case GameState::TITLE:
      if (pressed & Button::A) setState(GameState::WEAPON_SELECT);
      break;
    case GameState::WEAPON_SELECT:
      if (pressed & Button::LEFT) {
        selectedWeapon_ = (selectedWeapon_ + WEAPON_COUNT - 1) % WEAPON_COUNT;
      }
      if (pressed & Button::RIGHT) {
        selectedWeapon_ = (selectedWeapon_ + 1) % WEAPON_COUNT;
      }
      if (pressed & Button::A) {
        planetLevel_ = 1;
        planetsCleared_ = 0;
        displayScaleLog2_ = 0;
        setState(GameState::PLAYING);
        startPlanet(false);
      }
      break;
    case GameState::DEAD:
      if (stateTimer_ > DEAD_WAIT_TICKS && (pressed & Button::A)) {
        uint32_t s = rng_.next();
        reset(s);
        return;
      }
      break;
    default: break;
  }

  Creature &p = creatures[playerIndex_];
  if (state_ == GameState::PLAYING && p.alive && !autoPlayer_) {
    updatePlayerControls(buttons);
  } else if (state_ == GameState::LAUNCH) {
    p.turn = 0;
    p.dashing = true;
    p.braking = false;
    p.firing = false;
  }

  // The neighbor queries of the AI use the orders of the previous tick
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = creatures[i];
    if (!c.alive) continue;
    bool aiDriven = !c.isPlayer || state_ == GameState::TITLE ||
                    state_ == GameState::WEAPON_SELECT ||
                    (autoPlayer_ && state_ == GameState::PLAYING);
    if (aiDriven && ((tickCount_ + (uint32_t)i) % AI_THINK_INTERVAL) == 0) {
      updateAi(i);
    }
    moveCreature(c);
    // The part physics of creatures far from the player runs at a lower rate
    int64_t d2;
    bool near = c.isPlayer ||
                tangentialDist2(p.frame.n, c.frame.n, LAYOUT_NEAR_PU * PU, d2);
    if (near || ((tickCount_ + (uint32_t)i) & 3) == 0) {
      updateLayout(c);
      mergeParts(c);
    }
    if (c.firing) fireWeapon(i);
  }

  updateBullets();
  updateFloatingParts();
  updateParticles();
  rebuildOrders();
  handleEating();
  handleCreatureCollisions();
  updateRanks();
  updateRespawns();
  checkTransitions();
}

// FNV-1a over the raw state (all arrays are zeroed before use, so padding is
// deterministic)
static uint32_t fnv(uint32_t h, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  for (size_t i = 0; i < len; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

uint32_t Game::stateHash() const {
  uint32_t h = 2166136261u;
  h = fnv(h, creatures, sizeof(creatures));
  h = fnv(h, floatingParts, sizeof(floatingParts));
  h = fnv(h, bullets, sizeof(bullets));
  h = fnv(h, particles, sizeof(particles));
  uint32_t scalars[] = {(uint32_t)state_,      tickCount_,
                        (uint32_t)stateTimer_, (uint32_t)planetLevel_,
                        rng_.state(),          (uint32_t)playerIndex_,
                        displayScaleLog2_};
  h = fnv(h, scalars, sizeof(scalars));
  return h;
}

}  // namespace devoursphere::sim
