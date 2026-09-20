// Game state machine, sphere setup, spawning and bookkeeping.

#include <cstring>

#include "devoursphere/sim/game.hpp"

namespace devoursphere::sim {

const int LAUNCH_TICKS = 5 * TICK_RATE;
const int ARRIVE_TICKS = 5 * TICK_RATE;
const int ARRIVE_SWITCH_TICKS = TICK_RATE;
static constexpr int DEAD_WAIT_TICKS = 2 * TICK_RATE;
// Ticks without a bounty out before the sphere counts as cleared
static constexpr int CLEAR_GRACE_TICKS = 4 * TICK_RATE;

Game::Game() { reset(1); }

void Game::reset(uint32_t seed) {
  seed_ = seed;
  rng_.reset(seed);
  tickCount_ = 0;
  events_ = 0;
  sphereLevel_ = 1;
  spheresCleared_ = 0;
  selectedWeapon_ = 0;
  displayScaleLog2_ = 0;
  prevButtons_ = 0xFF;  // buttons held during reset do not count as presses
  paused_ = false;      // muted_ is a setting and stays
  stats_ = {};
  // Everything a game leaves behind, so that a Game reset in place behaves
  // exactly like a fresh one (the high score is the one thing meant to
  // survive; autoPlayer_ is a debug switch the caller owns)
  scoreQ8_ = 0;
  resetUpgrades();
  lastUpgradeKind_ = UpgradeKind::NONE;
  respawnDelay_ = 0;
  timeUp_ = false;
  lastSphereScoreQ8_ = lastClearBonusQ8_ = 0;
  lastClearTicks_ = 0;
  resetPlayerTimers();
  playerClimb_ = 0;
  switchPending_ = false;
  effectCount_ = 0;
  sounds_ = 0;
  for (int i = 0; i < SOUND_KINDS; i++) soundGap_[i] = 0;
  aliveEntities_ = 0;
  maxBodyRadius_ = 0;
  maxCoreReach_ = 0;
  // The state first: spawnPlayer() rolls a random weapon on the title
  // screen and takes the chosen one otherwise, so the sphere built here
  // must not see the state of the game that just ended
  setState(GameState::TITLE);
  startSphere(false);
}

void Game::debugStartSphere(int level, int weapon) {
  sphereLevel_ = level < 1 ? 1 : level;
  spheresCleared_ = level - 1;
  selectedWeapon_ = weapon % WEAPON_COUNT;
  displayScaleLog2_ = 0;
  scoreQ8_ = 0;
  resetUpgrades();
  setState(GameState::PLAYING);
  startSphere(false);
}

void Game::setState(GameState s) {
  state_ = s;
  stateTimer_ = 0;
}

// The descent of the arrival: (time left)^2, reaching the cruising altitude
// a second before the state ends. Continued backwards before the switch
// tick it starts higher, which is where the first sphere of a game begins.
int32_t Game::descentTarget(int timer) const {
  int64_t left = ARRIVE_TICKS - TICK_RATE - timer;
  if (left < 0) left = 0;
  const int64_t span = ARRIVE_TICKS - TICK_RATE - ARRIVE_SWITCH_TICKS;
  return SPHERE_RADIUS + ALTITUDE +
         (int32_t)((int64_t)ARRIVE_ALTITUDE * left * left / (span * span));
}
int32_t Game::descentSlope(int timer) const {
  int64_t left = ARRIVE_TICKS - TICK_RATE - timer;
  if (left < 0) left = 0;
  const int64_t span = ARRIVE_TICKS - TICK_RATE - ARRIVE_SWITCH_TICKS;
  return (int32_t)(2 * (int64_t)ARRIVE_ALTITUDE * left / (span * span));
}

// The first sphere of a game: the whole arrival flight, diving from the
// start, but there is no sphere to leave, so nothing is switched
void Game::beginArrival() {
  Entity &p = entities[playerIndex_];
  p.invincible = 0;  // frozen instead; the protection starts on landing
  // The altitude filter (1/8 per tick) is given its steady-state lead, so
  // the descent runs at the profile's slope from the first tick
  p.r = descentTarget(0) + (descentSlope(0) << 3);
  switchPending_ = false;
}

// Midway through the arrival: the old sphere is behind the player and off
// screen. The player shrinks back to a small entity, and the next sphere
// is put where the player is already heading: its frame is turned around
// its right axis so that the descent (speed along t, the profile's slope
// down n) is the very velocity it had while climbing. Seen from the
// player nothing changes: it flies straight on, and the new sphere is
// ahead where the old one was behind.
void Game::switchSphere() {
  Entity &p = entities[playerIndex_];
  switchPending_ = false;
  spheresCleared_++;
  sphereLevel_++;
  // The velocity before (scaled up: normalizeQ30 keeps the precision of
  // a long vector)
  const Vec3 vOld = normalizeQ30(scaleToLength(p.frame.t, p.speed << 8) +
                                 scaleToLength(p.frame.n, playerClimb_ << 8));
  rescalePlayerForNextSphere();  // p.speed is the new cruising speed now
  const int32_t slope = descentSlope(stateTimer_);
  const uint32_t mag =
      isqrt32((uint32_t)((int64_t)p.speed * p.speed + (int64_t)slope * slope));
  const int32_t c = (int32_t)(((int64_t)p.speed << Q30_SHIFT) / mag);
  const int32_t s = (int32_t)(((int64_t)(-slope) << Q30_SHIFT) / mag);
  // w is vOld turned a quarter turn towards n (nose up); the new frame has
  // t' cos + n' sin = vOld for the descent angle (cos, sin) = (c, s)
  const Vec3 right = crossQ30(p.frame.t, p.frame.n);
  const Vec3 w = crossQ30(right, vOld);
  p.frame.n = normalizeQ30(scaleQ30(vOld, s) + scaleQ30(w, c));
  p.frame.t = orthonormalizeQ30(scaleQ30(vOld, c) - scaleQ30(w, s), p.frame.n);
  startSphere(true);
  playerClimb_ = -slope;  // what the renderer pitches the body by this tick
}

void Game::debugTakeUpgrade(UpgradeKind k) {
  debugMode_ = true;
  takeUpgrade(k);
}

void Game::debugScaleSize(bool bigger) {
  debugMode_ = true;
  Entity &p = entities[playerIndex_];
  if (!p.alive) return;
  uint32_t size = bigger ? p.size * 2 : p.size / 2;
  if (size < 1) size = 1;
  if (size > (1u << MAX_SIZE_LOG2)) size = 1u << MAX_SIZE_LOG2;
  setEntitySize(p, size);
  syncFragments(p, 0, 0);
  updateLayout(p);
}

void Game::debugHeal(int pct) {
  debugMode_ = true;
  Entity &p = entities[playerIndex_];
  if (!p.alive) return;
  int32_t hp = p.hp + (int32_t)((int64_t)p.hpMax * pct / 100);
  if (hp < 1) hp = 1;
  if (hp > p.hpMax) hp = p.hpMax;
  p.hp = hp;
}

int Game::findFreeEntity() const {
  for (int i = 0; i < MAX_ENTITIES; i++) {
    // The player's slot is never reused (a dead player is a ghost that the
    // camera follows until the respawn or the game over)
    if (i == playerIndex_) continue;
    if (!entities[i].alive) return i;
  }
  return -1;
}

void Game::startSphere(bool keepPlayer) {
  sphereSeed_ = rng_.next();
  sphereTicks_ = 0;
  clearGrace_ = 0;
  sphereScoreStartQ8_ = scoreQ8_;
  Entity saved;
  if (keepPlayer) saved = entities[playerIndex_];

  std::memset(entities, 0, sizeof(entities));
  std::memset(floatingFragments, 0, sizeof(floatingFragments));
  std::memset(bullets, 0, sizeof(bullets));
  std::memset(floatingUpgrades, 0, sizeof(floatingUpgrades));
  entityOrderCount_ = 0;
  fragmentOrderCount_ = 0;

  playerIndex_ = 0;
  if (keepPlayer) {
    // The player arrives from above, keeping its heading (the stars must
    // not jump when the sphere is switched under it); it is frozen until
    // it lands, so no spawn protection is needed yet
    entities[0] = saved;
    Entity &p = entities[0];
    p.alive = true;
    p.invincible = 0;
    p.hp = p.hpMax;
    p.r = descentTarget(stateTimer_) + (descentSlope(stateTimer_) << 3);
  } else {
    spawnPlayer();
  }

  int maxExp = 8 + sphereLevel_;
  if (maxExp > 16) maxExp = 16;
  for (int i = 1; i < INITIAL_ENTITIES; i++) {
    // Two rolls, keep the smaller: many small entities, few giants
    int a = (int)rng_.below((uint32_t)maxExp + 1);
    int b = (int)rng_.below((uint32_t)maxExp + 1);
    int e = a < b ? a : b;
    if (i < 4) e = maxExp;  // guarantee a few giants
    spawnEnemy(i, e, true);
  }
  for (int i = 0; i < INITIAL_FOOD_FRAGMENTS; i++) spawnFood(false);
  respawnTimer_ = RESPAWN_INTERVAL;
  foodTimer_ = FOOD_SPAWN_INTERVAL;
  assignUpgrades();
  updateRanks();
  rebuildOrders();
}

// A free fragment somewhere on the sphere; small sizes are the most common
void Game::spawnFood(bool farFromPlayer) {
  Frame f;
  int32_t r;
  int a = (int)rng_.below(FOOD_MAX_SIZE_LOG2 + 1);
  int b = (int)rng_.below(FOOD_MAX_SIZE_LOG2 + 1);
  int k = a < b ? a : b;
  placeRandom(f, r, 4u << k, farFromPlayer);
  spawnFloatingFragment(f.n, r, k, {0, 0, 0});
}

void Game::initEntity(Entity &c, int sizeLog2, Weapon w) {
  std::memset(&c, 0, sizeof(c));
  c.alive = true;
  c.weapon = w;
  c.aiTarget = -1;
  c.evadeFrom = NO_ENTITY;
  c.fragmentCount = 0;
  c.size = 0;
  // Decompose 2^k into a few fragments for a more interesting body
  if (sizeLog2 <= 0) {
    addFragmentToEntity(c, 0, FU / 2, -FU / 2);
  } else if (sizeLog2 == 1) {
    addFragmentToEntity(c, 0, FU / 2, -FU / 2);
    addFragmentToEntity(c, 0, FU, -FU);
  } else {
    int32_t h = fragmentHalfSize(sizeLog2 - 1);
    addFragmentToEntity(c, sizeLog2 - 1, h, -h);
    addFragmentToEntity(c, sizeLog2 - 2, h * 2, -h * 2);
    addFragmentToEntity(c, sizeLog2 - 2, h / 2, -h * 3);
  }
  c.hpMax = HP_PER_SIZE * (int32_t)c.size;
  c.hp = c.hpMax;
  c.speed = cruiseSpeedForSize(c.size);
  c.frame.n = {0, 0, Q30_ONE};
  c.frame.t = {0, Q30_ONE, 0};
  c.r = SPHERE_RADIUS + altitudeForSize(c.size);
  c.aiWanderAngle = rng_.brad();
  updateLayout(c);
}

void Game::spawnPlayer() {
  Entity &p = entities[playerIndex_];
  Weapon w = (Weapon)selectedWeapon_;
  if (state_ == GameState::TITLE) w = (Weapon)rng_.below(WEAPON_COUNT);
  initEntity(p, PLAYER_START_SIZE_LOG2, w);
  p.isPlayer = true;
  p.hue = PLAYER_HUE;
  p.invincible = 2 * TICK_RATE;
  placeRandom(p.frame, p.r, p.size, false);
}

void Game::spawnEnemy(int index, int sizeLog2, bool farFromPlayer) {
  Entity &c = entities[index];
  // The weapon mix of the sphere's AI tier
  const AiTier &tier = aiTier();
  uint32_t roll = rng_.below(100);
  Weapon w = roll < tier.laserPct                    ? Weapon::LASER
             : roll < (uint32_t)tier.laserPct + tier.missilePct ? Weapon::MISSILE
                                                     : Weapon::VULCAN;
  initEntity(c, sizeLog2, w);
  c.hue = ENEMY_HUES[rng_.below(ENEMY_HUE_COUNT)];
  c.invincible = TICK_RATE;
  placeRandom(c.frame, c.r, c.size, farFromPlayer);
}

void Game::placeRandom(Frame &f, int32_t &r, uint32_t size,
                       bool farFromPlayer) {
  const Entity &p = entities[playerIndex_];
  // cos(9 degrees) in Q30: entities spawn at least ~160 FU away
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
  r = SPHERE_RADIUS + altitudeForSize(size);
}

uint32_t Game::playerSizeAfterSwitch() const {
  const Entity &p = entities[playerIndex_];
  int shift = log2Floor(p.size) - PLAYER_START_SIZE_LOG2 - 1;
  if (shift <= 0) return p.size;
  uint32_t total = 0;
  for (int i = 0; i < p.fragmentCount; i++) {
    int e = (int)p.fragments[i].sizeLog2 - shift;
    total += 1u << (e < 0 ? 0 : e);
  }
  return total;
}

void Game::rescalePlayerForNextSphere() {
  Entity &p = entities[playerIndex_];
  int k = log2Floor(p.size);
  int shift = k - PLAYER_START_SIZE_LOG2 - 1;
  if (shift <= 0) return;
  displayScaleLog2_ += shift;
  // Shrink every fragment; positions shrink with the kite size
  int32_t oldHalf = fragmentHalfSize(shift), newHalf = fragmentHalfSize(0);
  uint32_t total = 0;
  for (int i = 0; i < p.fragmentCount; i++) {
    Fragment &pt = p.fragments[i];
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

// Ranks go by size alone (not by the health-weighted effective size).
//
// Bounties: an enemy that is the largest on the sphere (bigger than the
// player) gets one, and keeps it until it dies or drops out of the top three
// -- except that the last bounty is never lifted that way, so the sphere is
// only cleared by a kill (an enemy chipped down by critical hits would
// otherwise hand over the clear while the player is still shooting). While
// the player is not on top there is always a bounty out, so bountyCount_ == 0
// means the player is the largest and every one-time boss is dead.
//
// One pass over the entities (rank, largest enemy, the three largest sizes,
// the bounties alive) plus a second one only when a bounty could be lifted
void Game::updateRanks() {
  Entity &p = entities[playerIndex_];
  int rank = 1;
  aliveEntities_ = 0;
  largestEnemy_ = -1;
  uint32_t largest = 0;
  uint32_t top[3] = {0, 0, 0};  // the three largest sizes, player included
  int bounties = 0;
  for (int i = 0; i < MAX_ENTITIES; i++) {
    const Entity &c = entities[i];
    if (!c.alive) continue;
    aliveEntities_++;
    const uint32_t s = c.size;
    if (s > top[0]) {
      top[2] = top[1], top[1] = top[0], top[0] = s;
    } else if (s > top[1]) {
      top[2] = top[1], top[1] = s;
    } else if (s > top[2]) {
      top[2] = s;
    }
    if (i == playerIndex_) continue;
    if (s > p.size) rank++;
    if (s > largest) largest = s, largestEnemy_ = i;
    bounties += c.bounty;
  }
  if (largestEnemy_ >= 0 && largest > p.size &&
      !entities[largestEnemy_].bounty) {
    entities[largestEnemy_].bounty = true;
    bounties++;
  }
  if (bounties > 1) {
    // Lift the bounties of the holders that fell out of the top three (ties
    // count as in), but never the last one
    for (int i = 0; i < MAX_ENTITIES && bounties > 1; i++) {
      Entity &c = entities[i];
      if (c.alive && c.bounty && !c.isPlayer && c.size < top[2]) {
        c.bounty = false;
        bounties--;
      }
    }
  }
  bountyCount_ = bounties;
  // The rank is frozen once the sphere is cleared
  if (state_ != GameState::LAUNCH) p.rank = (uint8_t)rank;
}

void Game::updateRespawns() {
  if (--foodTimer_ <= 0) {
    foodTimer_ = FOOD_SPAWN_INTERVAL;
    int n = 0;
    for (int i = 0; i < MAX_FLOATING_FRAGMENTS; i++)
      n += floatingFragments[i].alive;
    if (n < FOOD_TARGET) spawnFood(true);
  }
  if (aliveEntities_ >= INITIAL_ENTITIES) return;
  if (--respawnTimer_ > 0) return;
  respawnTimer_ = RESPAWN_INTERVAL;
  int idx = findFreeEntity();
  if (idx < 0) return;
  int maxExp = 4 + sphereLevel_;
  int a = (int)rng_.below((uint32_t)maxExp + 1);
  int b = (int)rng_.below((uint32_t)maxExp + 1);
  spawnEnemy(idx, a < b ? a : b, true);
}

void Game::checkTransitions() {
  Entity &p = entities[playerIndex_];
  switch (state_) {
    case GameState::TITLE:
    case GameState::WEAPON_SELECT:
      // Attract mode: keep the AI-driven player alive
      if (!p.alive) spawnPlayer();
      break;
    case GameState::PLAYING:
      if (!p.alive) {
        // Watch the wreck for a while, then come back (or game over)
        if (cores_ <= 0) {
          respawnDelay_ = 0;
          setState(GameState::DEAD);
        } else if (respawnDelay_ == 0) {
          respawnDelay_ = RESPAWN_DELAY_TICKS;
        } else if (--respawnDelay_ == 0) {
          if (timeUp_) {
            restartSphereAfterTimeUp();
          } else {
            respawnPlayer();
          }
        }
      } else if (sphereTicks_ >= SPHERE_TIME_LIMIT_TICKS) {
        // Time up: the player breaks apart like a kill, and the wreck is
        // watched like one; then the sphere starts over (or game over)
        timeUp_ = true;
        events_ |= Event::SPHERE_TIME_UP;
        killEntity(playerIndex_, -1);
      } else if (bountyCount_ > 0) {
        clearGrace_ = 0;
      } else if (++clearGrace_ > CLEAR_GRACE_TICKS) {
        // Every bounty claimed (and so the player on top, see updateRanks)
        // for a moment: the kill has been seen and heard
        events_ |= Event::SPHERE_CLEARED;
        // The sphere's own score, then the clear bonus: a base plus a share
        // of the time bonus for the time still left
        lastSphereScoreQ8_ = scoreQ8_ - sphereScoreStartQ8_;
        lastClearTicks_ = sphereTicks_;
        int64_t left = sphereTimeLeft();
        int64_t bonus = (int64_t)SCORE_CLEAR_BASE * 256 +
                        (int64_t)SCORE_CLEAR_TIME_BONUS * 256 * left /
                            SPHERE_TIME_LIMIT_TICKS;
        uint64_t before = scoreQ8_;
        addScore(bonus);
        lastClearBonusQ8_ = scoreQ8_ - before;
        setState(GameState::LAUNCH);
        pushSound(SoundKind::LAUNCH);
      }
      break;
    case GameState::LAUNCH:
      if (stateTimer_ >= LAUNCH_TICKS) {
        setState(GameState::ARRIVE);
        pushSound(SoundKind::ARRIVE);
        switchPending_ = true;
      }
      break;
    case GameState::ARRIVE:
      if (stateTimer_ == ARRIVE_SWITCH_TICKS && switchPending_) {
        switchSphere();
      } else if (stateTimer_ >= ARRIVE_TICKS) {
        // Landed: the spawn protection starts now
        p.invincible = RESPAWN_INVINCIBLE_TICKS;
        setState(GameState::PLAYING);
      }
      break;
    case GameState::DEAD: break;
  }
}

void Game::tick(uint8_t buttons) {
  tickProfile_.begin();
  uint8_t pressed = buttons & (uint8_t)~prevButtons_;
  prevButtons_ = buttons;
  events_ = 0;
  effectCount_ = 0;
  sounds_ = 0;
  // Paused: only the pause menu runs. Nothing else moves, not even the
  // sound gaps or the tick count, so that resuming continues the very same
  // game (a run with a pause in it hashes like one without)
  if (paused_) {
    if (pressed & Button::PAUSE) {
      paused_ = false;
      pushSound(SoundKind::MENU_SELECT);
    } else if (pressed & Button::DOWN) {
      toggleMute();
    }
    tickProfile_.stamp(TP_OTHER);
    return;
  }
  if ((pressed & Button::PAUSE) &&
      (state_ == GameState::PLAYING || state_ == GameState::LAUNCH ||
       state_ == GameState::ARRIVE)) {
    paused_ = true;
    pushSound(SoundKind::MENU_SELECT);
    tickProfile_.stamp(TP_OTHER);
    return;
  }
  for (int i = 0; i < SOUND_KINDS; i++) {
    if (soundGap_[i] > 0) soundGap_[i]--;
  }
  tickCount_++;
  stateTimer_++;
  if (state_ == GameState::PLAYING && entities[playerIndex_].alive) {
    // The clock of the sphere: not while dead, in flight or paused
    sphereTicks_++;
    int left = sphereTimeLeft();
    if (left > 0 && left <= TIME_ALARM_TICKS && left % TICK_RATE == 0) {
      pushSound(SoundKind::TIME_ALARM);
    }
  }

  // Menu handling
  switch (state_) {
    case GameState::TITLE:
      if (pressed & Button::A) {
        pushSound(SoundKind::MENU_START);
        setState(GameState::WEAPON_SELECT);
      } else if (pressed & Button::DOWN) {
        toggleMute();
      }
      break;
    case GameState::WEAPON_SELECT:
      // Either axis moves the cursor, so the keys match the layout whether
      // the choices sit side by side or stack on a narrow screen
      if (pressed & (Button::LEFT | Button::UP)) {
        selectedWeapon_ = (selectedWeapon_ + WEAPON_COUNT - 1) % WEAPON_COUNT;
        pushSound(SoundKind::MENU_SELECT);
      }
      if (pressed & (Button::RIGHT | Button::DOWN)) {
        selectedWeapon_ = (selectedWeapon_ + 1) % WEAPON_COUNT;
        pushSound(SoundKind::MENU_SELECT);
      }
      if (pressed & Button::A) {
        pushSound(SoundKind::MENU_START);
        sphereLevel_ = 1;
        spheresCleared_ = 0;
        displayScaleLog2_ = 0;
        scoreQ8_ = 0;
        resetUpgrades();
        setState(GameState::ARRIVE);
        pushSound(SoundKind::ARRIVE);
        startSphere(false);  // the state is set first: the chosen weapon
        beginArrival();
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

  Entity &p = entities[playerIndex_];
  if ((state_ == GameState::DEAD || state_ == GameState::PLAYING) && !p.alive) {
    // The camera keeps following the ghost of the player, cruising ahead
    int32_t ang = (int32_t)(((int64_t)cruiseSpeedForSize(p.size ? p.size : 1)
                             << Q30_SHIFT) /
                            p.r);
    p.frame.n = normalizeQ30(p.frame.n + scaleQ30(p.frame.t, ang));
    p.frame.t = orthonormalizeQ30(p.frame.t, p.frame.n);
    p.bank = (int16_t)(p.bank - (p.bank >> 3));
  }
  if (playerMercy_ > 0) playerMercy_--;
  if (dodgeTicks_ > 0) dodgeTicks_--;
  if (dodgeCooldown_ > 0) dodgeCooldown_--;
  if (state_ == GameState::PLAYING && p.alive && !autoPlayer_) {
    updatePlayerControls(buttons, pressed);
  } else if (state_ == GameState::LAUNCH || state_ == GameState::ARRIVE) {
    // Dashing through the flight; the dash ramps down over the last second
    // of the arrival so that the player lands at cruising speed and the
    // camera does not start in its dash position
    p.turn = 0;
    p.dashing = !(state_ == GameState::ARRIVE &&
                  stateTimer_ >= ARRIVE_TICKS - DASH_RAMP_DOWN_TICKS);
    p.braking = false;
    p.firing = false;
  }
  playerClimb_ = 0;

  tickProfile_.stamp(TP_OTHER);

  // The neighbor queries of the AI use the orders of the previous tick
  for (int i = 0; i < MAX_ENTITIES; i++) {
    Entity &c = entities[i];
    if (!c.alive) continue;
    bool aiDriven = !c.isPlayer || state_ == GameState::TITLE ||
                    state_ == GameState::WEAPON_SELECT ||
                    (autoPlayer_ && state_ == GameState::PLAYING);
    if (aiDriven && ((tickCount_ + (uint32_t)i) % AI_THINK_INTERVAL) == 0) {
      updateAi(i);
      tickProfile_.stamp(TP_AI);
    }
    moveEntity(c);
    // The fragment physics of entities far from the player runs at a lower rate
    int64_t d2;
    bool near = c.isPlayer ||
                tangentialDist2(p.frame.n, c.frame.n, LAYOUT_NEAR_FU * FU, d2);
    tickProfile_.stamp(TP_MOVE);
    if (near || ((tickCount_ + (uint32_t)i) & 3) == 0) {
      updateLayout(c);
      mergeFragments(c);
      tickProfile_.stamp(TP_LAYOUT);
    }
    if (c.firing) {
      fireWeapon(i);
      tickProfile_.stamp(TP_FIRE);
    }
  }
  tickProfile_.stamp(TP_OTHER);

  if (state_ == GameState::PLAYING && p.alive) updateShieldRegen();
  updateBullets();
  tickProfile_.stamp(TP_BULLETS);
  updateFloatingFragments();
  updateFloatingUpgrades();
  tickProfile_.stamp(TP_FRAGMENTS);
  rebuildOrders();
  tickProfile_.stamp(TP_ORDERS);
  handleEating();
  tickProfile_.stamp(TP_EATING);
  handleEntityCollisions();
  tickProfile_.stamp(TP_COLLISIONS);
  updateRanks();
  updateRespawns();
  checkTransitions();
  tickProfile_.stamp(TP_OTHER);
}

// 1.1^(level - 1) in Q8, by repeated integer multiplication (deterministic)
int32_t Game::levelMultQ8() const {
  int32_t m = 256;
  for (int i = 1; i < sphereLevel_ && i < 64; i++) {
    m = m * (100 + SCORE_LEVEL_GROWTH_PCT) / 100;
  }
  return m;
}

// baseQ8: base points in Q8; multiplied by the sphere multiplier
// (saturating)
void Game::addScore(int64_t baseQ8) {
  if (baseQ8 <= 0) return;
  uint64_t gain = ((uint64_t)baseQ8 * (uint64_t)levelMultQ8()) >> 8;
  uint64_t limit = (uint64_t)0xFFFFFFFFu << 8;
  scoreQ8_ = (scoreQ8_ + gain > limit) ? limit : scoreQ8_ + gain;
}

// The time ran out and the wreck has been watched: a core and a level of
// every upgrade are lost, and the same sphere is built anew, entered from
// the arrival flight like the first sphere of a game (the camera ahead of
// the player, no switch midway). The player comes back at the size it
// would have arrived with; the display scale stays, so it looks smaller
void Game::restartSphereAfterTimeUp() {
  cores_--;
  int lost[UPGRADE_KINDS] = {0, 0, 0};
  for (int i = 0; i < UPGRADE_KINDS; i++) {
    if (upgradeLevels_[i] > 0) upgradeLevels_[i]--, lost[i] = 1;
  }
  timeUp_ = false;
  respawnDelay_ = 0;
  resetPlayerTimers();
  setState(GameState::ARRIVE);
  pushSound(SoundKind::ARRIVE);
  startSphere(false);
  Entity &p = entities[playerIndex_];
  if (sphereLevel_ > 1) {
    Frame f = p.frame;
    uint8_t hue = p.hue;
    initEntity(p, PLAYER_START_SIZE_LOG2 + 1, p.weapon);
    p.isPlayer = true;
    p.hue = hue;
    p.frame = f;
  }
  beginArrival();
  // The lost levels lie around the landing point, like after a death
  scatterLostUpgrades(lost, p.frame.n, SPHERE_RADIUS + ALTITUDE);
  updateRanks();
}

void Game::pushEffect(EffectKind kind, int entity, const Vec3 &n, int32_t r,
                      int32_t size) {
  if (effectCount_ >= MAX_EFFECTS) return;
  effects_[effectCount_++] = {kind, (int16_t)entity, n, r, size};
}

// The mute toggle: heard when it turns the sound back on
void Game::toggleMute() {
  muted_ = !muted_;
  if (!muted_) pushSound(SoundKind::MENU_SELECT);
}

// Request a sound for this tick (the platform reads sounds() after the
// tick); kinds with a minimum gap are dropped while the gap runs
void Game::pushSound(SoundKind k) {
  int i = (int)k;
  if (soundGap_[i] > 0) return;
  soundGap_[i] = (uint8_t)SOUND_MIN_GAP_TICKS[i];
  sounds_ |= 1u << i;
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
  h = fnv(h, entities, sizeof(entities));
  h = fnv(h, floatingFragments, sizeof(floatingFragments));
  h = fnv(h, bullets, sizeof(bullets));
  h = fnv(h, floatingUpgrades, sizeof(floatingUpgrades));
  h = fnv(h, upgradeLevels_, sizeof(upgradeLevels_));
  uint32_t scalars[] = {
      (uint32_t)state_,       tickCount_,         (uint32_t)stateTimer_,
      (uint32_t)sphereLevel_, rng_.state(),       (uint32_t)playerIndex_,
      displayScaleLog2_,      (uint32_t)scoreQ8_, (uint32_t)(scoreQ8_ >> 32),
      (uint32_t)sphereTicks_, (uint32_t)cores_,   (uint32_t)switchPending_,
      (uint32_t)playerMercy_, (uint32_t)dodgeTicks_,
      (uint32_t)dodgeCooldown_, (uint32_t)dodgeDir_, (uint32_t)timeUp_,
      (uint32_t)bountyCount_,   (uint32_t)clearGrace_};
  h = fnv(h, scalars, sizeof(scalars));
  return h;
}

}  // namespace devoursphere::sim
