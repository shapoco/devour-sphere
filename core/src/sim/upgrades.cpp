// Upgrades (Shield, Overdrive, Thruster, Extra Core), spare cores and the
// respawn after losing a core.

#include "devoursphere/sim/game.hpp"

namespace devoursphere::sim {

void Game::resetUpgrades() {
  for (int i = 0; i < UPGRADE_KINDS; i++) upgradeLevels_[i] = 0;
  cores_ = CORES_START;
  regenAccQ8_ = 0;
}

// Give one upgrade of each kind (and sometimes an Extra Core) to distinct
// random enemies; nothing shows who has them
void Game::assignUpgrades() {
  for (int i = 0; i < MAX_FLOATING_UPGRADES; i++)
    floatingUpgrades[i].alive = false;
  UpgradeKind kinds[4] = {UpgradeKind::SHIELD, UpgradeKind::OVERDRIVE,
                          UpgradeKind::THRUSTER, UpgradeKind::NONE};
  int count = 3;
  if (rng_.below(EXTRA_CORE_CHANCE_DEN) == 0)
    kinds[count++] = UpgradeKind::EXTRA_CORE;
  for (int k = 0; k < count; k++) {
    for (int attempt = 0; attempt < 64; attempt++) {
      int idx = (int)rng_.below(MAX_ENTITIES);
      Entity &e = entities[idx];
      if (!e.alive || e.isPlayer || e.upgrade != (uint8_t)UpgradeKind::NONE)
        continue;
      e.upgrade = (uint8_t)kinds[k];
      break;
    }
  }
}

void Game::spawnFloatingUpgrade(UpgradeKind k, const Vec3 &n, int32_t r) {
  int slot = -1;
  for (int i = 0; i < MAX_FLOATING_UPGRADES; i++) {
    if (!floatingUpgrades[i].alive) {
      slot = i;
      break;
    }
  }
  if (slot < 0) slot = 0;
  FloatingUpgrade &u = floatingUpgrades[slot];
  u.alive = true;
  u.kind = (uint8_t)k;
  u.n = n;
  u.r = r;
  uint16_t a = rng_.brad();
  Vec3 helper =
      absI32(n.x) < absI32(n.y) ? Vec3{Q30_ONE, 0, 0} : Vec3{0, Q30_ONE, 0};
  Vec3 t1 = normalizeQ30(crossQ30(n, helper));
  Vec3 t2 = crossQ30(n, t1);
  Vec3 dir = scaleQ30(t1, cosQ30(a)) + scaleQ30(t2, sinQ30(a));
  u.drift = scaleToLength(dir, fuPerSec(6));
  u.spin = rng_.brad();
}

// A dead enemy lets go of its upgrade
void Game::releaseUpgrade(int idx) {
  Entity &e = entities[idx];
  if (e.upgrade == (uint8_t)UpgradeKind::NONE) return;
  spawnFloatingUpgrade((UpgradeKind)e.upgrade, e.frame.n, e.r);
  e.upgrade = (uint8_t)UpgradeKind::NONE;
}

void Game::takeUpgrade(UpgradeKind k) {
  bool bonus = false;
  if (k == UpgradeKind::EXTRA_CORE) {
    if (cores_ < CORES_MAX)
      cores_++;
    else
      bonus = true;
  } else {
    int i = (int)k - 1;
    if (upgradeLevels_[i] < UPGRADE_MAX_LEVEL)
      upgradeLevels_[i]++;
    else
      bonus = true;
  }
  if (bonus) addScore((int64_t)SCORE_UPGRADE_BONUS_BASE * 256);
  lastUpgradeKind_ = k;
  events_ |= Event::PLAYER_UPGRADED;
  pushSound(SoundKind::GET_UPGRADE);
}

// Drift, and pick up by the player (enemies cannot take upgrades, and the
// player's fragment attraction does not pull them)
void Game::updateFloatingUpgrades() {
  Entity &p = entities[playerIndex_];
  bool canTake = p.alive && state_ == GameState::PLAYING;
  for (int i = 0; i < MAX_FLOATING_UPGRADES; i++) {
    FloatingUpgrade &u = floatingUpgrades[i];
    if (!u.alive) continue;
    if (u.drift.x || u.drift.y || u.drift.z) {
      Vec3 ang = {(int32_t)(((int64_t)u.drift.x * Q30_ONE) / u.r),
                  (int32_t)(((int64_t)u.drift.y * Q30_ONE) / u.r),
                  (int32_t)(((int64_t)u.drift.z * Q30_ONE) / u.r)};
      u.n = normalizeQ30(u.n + ang);
      u.drift.x -= (u.drift.x >> (6 + RATE_SHIFT)) +
                   (u.drift.x > 0 ? 1 : (u.drift.x < 0 ? -1 : 0));
      u.drift.y -= (u.drift.y >> (6 + RATE_SHIFT)) +
                   (u.drift.y > 0 ? 1 : (u.drift.y < 0 ? -1 : 0));
      u.drift.z -= (u.drift.z >> (6 + RATE_SHIFT)) +
                   (u.drift.z > 0 ? 1 : (u.drift.z < 0 ? -1 : 0));
    }
    u.spin = (uint16_t)(u.spin + 200 * 30 / TICK_RATE);
    int32_t dr = (SPHERE_RADIUS + ALTITUDE) - u.r;
    if (dr) u.r += (dr >> (5 + RATE_SHIFT)) + (dr > 0 ? 1 : -1);
    if (!canTake) continue;
    int32_t reach = p.bodyRadius + 2 * FU;
    int64_t d2;
    if (!tangentialDist2(p.frame.n, u.n, reach, d2)) continue;
    if (d2 >= (int64_t)reach * reach) continue;
    takeUpgrade((UpgradeKind)u.kind);
    u.alive = false;
  }
}

// Shield level 3: slow regeneration
void Game::updateShieldRegen() {
  Entity &p = entities[playerIndex_];
  if (!p.alive || upgradeLevel(UpgradeKind::SHIELD) < UPGRADE_MAX_LEVEL) return;
  if (p.hp >= p.hpMax) return;
  regenAccQ8_ += (int32_t)((int64_t)p.hpMax * SHIELD_REGEN_PCT_PER_SEC * 256 /
                           (100 * TICK_RATE));
  int32_t add = regenAccQ8_ >> 8;
  if (add > 0) {
    regenAccQ8_ -= add << 8;
    p.hp += add;
    if (p.hp > p.hpMax) p.hp = p.hpMax;
  }
}

// Enemy bullet damage against the player grows with the sphere level and
// the player's upgrades (difficulty); the two factors multiply
int32_t Game::enemyDamagePct() const {
  int lv = sphereLevel_ > 1 ? sphereLevel_ - 1 : 0;
  int64_t spherePct = 100 + (int64_t)DIFF_DAMAGE_PCT_PER_SPHERE * lv;
  if (spherePct > DIFF_DAMAGE_PCT_SPHERE_MAX) spherePct = DIFF_DAMAGE_PCT_SPHERE_MAX;
  int64_t upgradePct = 100 + DIFF_DAMAGE_PCT_PER_LEVEL * totalUpgradeLevel();
  return (int32_t)(spherePct * upgradePct / 100);
}

// Lost a core: come back smaller and weaker, where the enemies are sparse
bool Game::respawnPlayer() {
  if (cores_ <= 0) return false;
  cores_--;
  for (int i = 0; i < UPGRADE_KINDS; i++) {
    if (upgradeLevels_[i] > 0) upgradeLevels_[i]--;
  }
  Entity &p = entities[playerIndex_];
  uint32_t size = p.size / RESPAWN_SIZE_DIV;
  uint32_t minSize = 1u << PLAYER_START_SIZE_LOG2;
  if (size < minSize) size = minSize;
  Weapon w = p.weapon;
  uint8_t hue = p.hue;
  initEntity(p, log2Floor(size), w);
  p.isPlayer = true;
  p.hue = hue;
  setEntitySize(p, size);
  syncFragments(p, 0, 0);
  p.hp = p.hpMax;
  p.invincible = RESPAWN_INVINCIBLE_TICKS;
  resetPlayerTimers();
  // Best of several random spots: the one farthest from the nearest enemy
  Frame best = p.frame;
  int32_t bestR = p.r;
  int64_t bestD2 = -1;
  for (int c = 0; c < RESPAWN_CANDIDATES; c++) {
    Frame f;
    int32_t r;
    placeRandom(f, r, size, false);
    int64_t nearest = (int64_t)(400 * FU) * (400 * FU);
    for (int j = 0; j < MAX_ENTITIES; j++) {
      const Entity &o = entities[j];
      if (!o.alive || j == playerIndex_) continue;
      int64_t d2;
      if (tangentialDist2(f.n, o.frame.n, 400 * FU, d2) && d2 < nearest)
        nearest = d2;
    }
    if (nearest > bestD2) {
      bestD2 = nearest;
      best = f;
      bestR = r;
    }
  }
  p.frame = best;
  p.r = bestR;
  events_ |= Event::PLAYER_RESPAWNED;
  return true;
}

}  // namespace devoursphere::sim
