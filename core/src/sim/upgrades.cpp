// Upgrades (Shield, Overdrive, Thruster, Extra Core), spare cores, the
// Charge / Lance / Ram state machine and the respawn after losing a core.

#include "devoursphere/sim/game.hpp"

namespace devoursphere::sim {

void Game::resetUpgrades() {
  for (int i = 0; i < UPGRADE_KINDS; i++) upgradeLevels_[i] = 0;
  cores_ = CORES_START;
  chargeState_ = ChargeState::IDLE;
  chargeGauge_ = 0;
  chargeTimer_ = 0;
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
  events_ |= Event::PLAYER_UPGRADED;
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

int32_t Game::lanceLength() const {
  const Entity &p = entities[playerIndex_];
  return fragmentHalfSize(log2Floor(p.size)) * LANCE_LENGTH_MUL +
         LANCE_LENGTH_FU * FU;
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

// Enemy bullet damage grows with the player's upgrades (difficulty)
int32_t Game::enemyDamagePct() const {
  return 100 + DIFF_DAMAGE_PCT_PER_LEVEL * totalUpgradeLevel();
}

// The Charge / Lance / Ram state machine. Called with the player's buttons
// before the controls are applied; it may override them.
void Game::updateCharge(uint8_t buttons) {
  Entity &p = entities[playerIndex_];
  bool a = (buttons & Button::A) != 0;
  bool down = (buttons & Button::DOWN) != 0;
  bool up = (buttons & Button::UP) != 0;
  bool lanceOk = upgradeLevel(UpgradeKind::OVERDRIVE) >= UPGRADE_MAX_LEVEL;
  bool ramOk = upgradeLevel(UpgradeKind::THRUSTER) >= UPGRADE_MAX_LEVEL;
  int fill = 256 / CHARGE_TICKS + 1;
  int32_t cruise = cruiseSpeedForSize(p.size);

  switch (chargeState_) {
    case ChargeState::IDLE:
      chargeGauge_ = 0;
      if (lanceOk && a && down) {
        chargeState_ = ChargeState::CHARGING_LANCE;
      } else if (ramOk && down && !a && !up && p.speed < cruise / 8) {
        chargeState_ = ChargeState::CHARGING_RAM;
      }
      break;
    case ChargeState::CHARGING_LANCE:
      if (!(a && down)) {
        chargeState_ = ChargeState::IDLE;
        chargeGauge_ = 0;
        break;
      }
      chargeGauge_ += fill;
      if (chargeGauge_ >= 256) {
        chargeGauge_ = 256;
        chargeState_ = ChargeState::READY_LANCE;
      }
      break;
    case ChargeState::READY_LANCE:
      if (!a) {
        chargeState_ = ChargeState::IDLE;
        chargeGauge_ = 0;
      } else if (!down) {
        chargeState_ = ChargeState::LANCE;
        chargeTimer_ = LANCE_TICKS;
      }
      break;
    case ChargeState::LANCE:
      if (--chargeTimer_ <= 0) chargeState_ = ChargeState::COOLDOWN;
      break;
    case ChargeState::CHARGING_RAM:
      if (!down || a || up) {
        chargeState_ = ChargeState::IDLE;
        chargeGauge_ = 0;
        break;
      }
      chargeGauge_ += fill;
      if (chargeGauge_ >= 256) {
        chargeGauge_ = 256;
        chargeState_ = ChargeState::READY_RAM;
      }
      break;
    case ChargeState::READY_RAM:
      if (a || up) {
        chargeState_ = ChargeState::IDLE;
        chargeGauge_ = 0;
      } else if (!down) {
        chargeState_ = ChargeState::RAM_WINDOW;
        chargeTimer_ = RAM_TRIGGER_TICKS;
      }
      break;
    case ChargeState::RAM_WINDOW:
      if (up) {
        chargeState_ = ChargeState::RAM;
        chargeTimer_ = RAM_TICKS;
        for (int i = 0; i < MAX_ENTITIES / 8; i++) ramHit_[i] = 0;
      } else if (a || down || --chargeTimer_ <= 0) {
        chargeState_ = ChargeState::IDLE;
        chargeGauge_ = 0;
      }
      break;
    case ChargeState::RAM:
      if (--chargeTimer_ <= 0) chargeState_ = ChargeState::COOLDOWN;
      break;
    case ChargeState::COOLDOWN:
      chargeGauge_ -= 256 / COOLDOWN_TICKS + 1;
      if (chargeGauge_ <= 0) {
        chargeGauge_ = 0;
        chargeState_ = ChargeState::IDLE;
      }
      break;
  }

  // Controls implied by the state
  switch (chargeState_) {
    case ChargeState::CHARGING_LANCE:
    case ChargeState::READY_LANCE:
      p.firing = false;  // no normal shots while charging the Lance
      p.dashing = false;
      break;
    case ChargeState::LANCE:
      p.firing = false;
      p.dashing = false;
      break;
    case ChargeState::CHARGING_RAM:
    case ChargeState::READY_RAM: p.dashing = false; break;
    case ChargeState::RAM:
      p.turn = 0;
      p.firing = false;
      p.braking = false;
      p.dashing = true;
      break;
    case ChargeState::COOLDOWN: p.dashing = false; break;
    default: break;
  }
}

// Lance: damage everything in the beam in front of the player
void Game::updateLance() {
  Entity &p = entities[playerIndex_];
  if (!p.alive) return;
  int32_t len = lanceLength();
  int32_t width = p.bodyRadius;
  Vec3 start = worldPos(p.frame.n, p.r) + scaleToLength(p.frame.t, p.coreY);
  Vec3 seg = scaleToLength(p.frame.t, len);
  int64_t segLen2 = length2_64(seg);
  int32_t dmg = (int32_t)(((int64_t)LANCE_POWER * p.size) / 8);
  if (dmg < 1) dmg = 1;
  for (int j = 0; j < MAX_ENTITIES; j++) {
    Entity &o = entities[j];
    if (!o.alive || j == playerIndex_) continue;
    int32_t reach = o.bodyRadius + width;
    int64_t d2;
    if (!tangentialDist2(p.frame.n, o.frame.n, len + reach, d2)) continue;
    Vec3 rel = worldPos(o.frame.n, o.r) - start;
    int64_t t = segLen2 > 0 ? clampI64(0, segLen2, dot64(rel, seg)) : 0;
    Vec3 closest = start;
    if (segLen2 > 0) {
      closest = start + Vec3{(int32_t)((int64_t)seg.x * t / segLen2),
                             (int32_t)((int64_t)seg.y * t / segLen2),
                             (int32_t)((int64_t)seg.z * t / segLen2)};
    }
    Vec3 gap = worldPos(o.frame.n, o.r) - closest;
    int32_t along = dotQ30(gap, o.frame.n);
    gap = gap - scaleToLength(o.frame.n, along);
    if (length2_64(gap) >= (int64_t)reach * reach) continue;
    damageEntity(j, dmg, playerIndex_, false);
  }
}

// Ram: strike every enemy touched once; the player is invulnerable
void Game::updateRam() {
  Entity &p = entities[playerIndex_];
  if (!p.alive) return;
  p.speed = cruiseSpeedForSize(p.size) * DASH_SPEED_NUM / DASH_SPEED_DEN *
            RAM_SPEED_MUL;
  int32_t dmg = (int32_t)(((int64_t)RAM_POWER * p.size) / 8);
  for (int j = 0; j < MAX_ENTITIES; j++) {
    Entity &o = entities[j];
    if (!o.alive || j == playerIndex_) continue;
    if (ramHit_[j >> 3] & (1u << (j & 7))) continue;
    int32_t reach = (o.bodyRadius + p.bodyRadius) * 3 / 4;
    int64_t d2;
    if (!tangentialDist2(p.frame.n, o.frame.n, reach, d2)) continue;
    if (d2 >= (int64_t)reach * reach) continue;
    ramHit_[j >> 3] |= (uint8_t)(1u << (j & 7));
    damageEntity(j, dmg, playerIndex_, false);
    events_ |= Event::PLAYER_RAM_HIT;
  }
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
  chargeState_ = ChargeState::IDLE;
  chargeGauge_ = 0;
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
