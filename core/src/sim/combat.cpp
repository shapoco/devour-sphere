// Weapons, damage, death, eating, collisions and floating fragments.

#include "devoursphere/sim/game.hpp"

namespace devoursphere::sim {

static constexpr int Z_SHIFT =
    Q30_SHIFT - SPHERE_RADIUS_SHIFT;  // units -> Q30 angle

// Tangential (chord) distance between two unit normals in world units.
// Returns false (and leaves d2 untouched) when any component differs by
// more than maxUnits, which is a cheap early rejection.
bool Game::tangentialDist2(const Vec3 &a, const Vec3 &b, int32_t maxUnits,
                           int64_t &d2) const {
  int64_t lim = (int64_t)maxUnits << Z_SHIFT;
  int64_t dx = (int64_t)a.x - b.x, dy = (int64_t)a.y - b.y,
          dz = (int64_t)a.z - b.z;
  if (dx > lim || dx < -lim || dy > lim || dy < -lim || dz > lim || dz < -lim) {
    return false;
  }
  int64_t ux = dx >> Z_SHIFT, uy = dy >> Z_SHIFT, uz = dz >> Z_SHIFT;
  d2 = ux * ux + uy * uy + uz * uz;
  return true;
}

// Bullets fly faster the bigger the owner: proportional to the kite size
// (the table speed applies to the player's starting size)
int32_t bulletSpeed(const WeaponSpec &ws, uint32_t ownerSize) {
  int64_t k = fragmentHalfSize(log2Floor(ownerSize));
  return (int32_t)((int64_t)ws.speed * k /
                   fragmentHalfSize(PLAYER_START_SIZE_LOG2));
}

static int32_t bulletRadius(const Bullet &b) {
  const WeaponSpec &ws = WEAPON_SPECS[(int)b.kind];
  return fragmentHalfSize(log2Floor(b.ownerSize)) * ws.radiusFU8 / 4;
}

void Game::fireWeapon(int idx) {
  Entity &c = entities[idx];
  if (c.fireCooldown > 0) return;
  const WeaponSpec &ws = WEAPON_SPECS[(int)c.weapon];

  int slot = -1;
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].alive) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return;

  int32_t cooldown = ws.cooldown;
  if (c.isPlayer) {
    cooldown = cooldown *
               OVERDRIVE_COOLDOWN_PCT[upgradeLevel(UpgradeKind::OVERDRIVE)] /
               100;
  }
  if (cooldown < 1) cooldown = 1;
  c.fireCooldown = (int16_t)cooldown;
  Bullet &b = bullets[slot];
  b.alive = true;
  Vec3 muzzle = worldPos(c.frame.n, c.r) + scaleToLength(c.frame.t, c.coreY);
  b.frame.n = normalizeQ30(muzzle);
  b.prevN = b.frame.n;
  uint16_t spread = 0;
  if (ws.spread) spread = (uint16_t)rng_.range(-(int32_t)ws.spread, ws.spread);
  b.frame.t = orthonormalizeQ30(rotateAroundQ30(c.frame.t, c.frame.n, spread),
                                b.frame.n);
  b.r = c.r;
  b.speed = bulletSpeed(ws, c.size);
  b.life = ws.lifetime;
  b.owner = (int16_t)idx;
  b.kind = c.weapon;
  b.ownerSize = c.size;
  b.power = (int32_t)(((int64_t)ws.powerPerSize * c.size) / 8);
  if (b.power < 1) b.power = 1;
  b.fromPlayer = c.isPlayer;
  b.target = -1;
  if (ws.homing) {
    // Nearest attackable entity in front
    int64_t best = INT64_MAX;
    int64_t range = (int64_t)bulletSpeed(ws, c.size) * ws.lifetime;
    for (int j = 0; j < MAX_ENTITIES; j++) {
      const Entity &o = entities[j];
      if (j == idx || !o.alive) continue;
      int64_t d2;
      if (!tangentialDist2(c.frame.n, o.frame.n, (int32_t)range, d2)) continue;
      Vec3 d = {(o.frame.n.x - c.frame.n.x) >> 2,
                (o.frame.n.y - c.frame.n.y) >> 2,
                (o.frame.n.z - c.frame.n.z) >> 2};
      if (dotQ30(c.frame.t, d) <= 0) continue;
      if (d2 < best) best = d2, b.target = (int16_t)j;
    }
  }
  if (c.isPlayer) {
    events_ |= Event::PLAYER_FIRED;
    pushSound(c.weapon == Weapon::VULCAN  ? SoundKind::SHOT_VULCAN
              : c.weapon == Weapon::LASER ? SoundKind::SHOT_LASER
                                          : SoundKind::SHOT_MISSILE);
  }
  stats_.shots++;
}

void Game::updateBullets() {
  for (int i = 0; i < MAX_BULLETS; i++) {
    Bullet &b = bullets[i];
    if (!b.alive) continue;
    if (--b.life <= 0) {
      b.alive = false;
      continue;
    }
    const WeaponSpec &ws = WEAPON_SPECS[(int)b.kind];
    if (ws.homing && b.target >= 0) {
      const Entity &o = entities[b.target];
      if (!o.alive) {
        b.target = -1;
      } else {
        Vec3 d = {(o.frame.n.x - b.frame.n.x) >> 2,
                  (o.frame.n.y - b.frame.n.y) >> 2,
                  (o.frame.n.z - b.frame.n.z) >> 2};
        d = d - scaleQ30(b.frame.n, dotQ30(d, b.frame.n));
        int32_t s = dotQ30(crossQ30(b.frame.t, d), b.frame.n);
        int32_t c = dotQ30(b.frame.t, d);
        int16_t err = (int16_t)atan2Brad(s, c);
        int16_t lim = (int16_t)ws.homing;
        if (err > lim) err = lim;
        if (err < -lim) err = -lim;
        b.frame.t = rotateAroundQ30(b.frame.t, b.frame.n, (uint16_t)err);
        // Approach the target altitude as well
        int32_t dr = o.r - b.r;
        b.r += dr >> (3 + RATE_SHIFT);
      }
    }
    int32_t ang = (int32_t)(((int64_t)b.speed << Q30_SHIFT) / b.r);
    b.prevN = b.frame.n;
    b.frame.n = normalizeQ30(b.frame.n + scaleQ30(b.frame.t, ang));
    b.frame.t = orthonormalizeQ30(b.frame.t, b.frame.n);

    // Hit test against the path flown during this tick (fast bullets would
    // otherwise jump over small targets)
    int32_t br = bulletRadius(b);
    int64_t dz = (int64_t)(maxBodyRadius_ + br + b.speed) << Z_SHIFT;
    Vec3 pPrev = worldPos(b.prevN, b.r), pCur = worldPos(b.frame.n, b.r);
    Vec3 seg = pCur - pPrev;
    int64_t segLen2 = length2_64(seg);
    for (int k = entityLowerBound(b.frame.n.z - dz); k < entityOrderCount_;
         k++) {
      int j = entityOrder_[k];
      Entity &o = entities[j];
      if (o.frame.n.z > b.frame.n.z + dz) break;
      if (!o.alive || j == b.owner) continue;
      if (o.isPlayer && playerFrozen()) continue;
      int32_t reach = o.bodyRadius + br;
      int64_t d2;
      if (!tangentialDist2(b.frame.n, o.frame.n, reach + b.speed, d2)) continue;
      // Closest point of the segment to the target center
      Vec3 rel = worldPos(o.frame.n, o.r) - pPrev;
      int64_t t = segLen2 > 0 ? clampI64(0, segLen2, dot64(rel, seg)) : 0;
      Vec3 closest = pPrev;
      if (segLen2 > 0) {
        closest = pPrev + Vec3{(int32_t)((int64_t)seg.x * t / segLen2),
                               (int32_t)((int64_t)seg.y * t / segLen2),
                               (int32_t)((int64_t)seg.z * t / segLen2)};
      }
      Vec3 gap = worldPos(o.frame.n, o.r) - closest;
      // ignore the altitude component: only the tangential distance counts
      int32_t along = dotQ30(gap, o.frame.n);
      gap = gap - scaleToLength(o.frame.n, along);
      if (length2_64(gap) >= (int64_t)reach * reach) continue;
      int32_t power = b.power;
      if (o.isPlayer && !b.fromPlayer) {
        // A giant's bullet hurts the player like one from an enemy at most
        // hitSizeRatioMax (of the sphere's AI tier) times its size
        uint32_t lim = o.size * aiTier().hitSizeRatioMax;
        if (b.ownerSize > lim) {
          power = (int32_t)(((int64_t)ws.powerPerSize * lim) / 8);
        }
      }
      damageEntity(j, power, b.owner);
      stats_.hits++;
      b.alive = false;
      break;
    }
  }
}

// Critical hit: a fragment of about a tenth of the body breaks off and
// flies away from `from` (the shooter's position)
void Game::criticalHit(int idx, const Vec3 &from) {
  Entity &c = entities[idx];
  uint32_t target = c.size / CRIT_FRACTION_DIV;
  if (target < 1) target = 1;
  int k = log2Floor(target);
  uint32_t piece = 1u << k;
  if (piece >= c.size) return;  // would be the whole body
  // Eject: the fragment leaves from the far side of the body
  Vec3 center = worldPos(c.frame.n, c.r);
  Vec3 away = center - worldPos(from, c.r);
  Vec3 dir = normalizeQ30(away - scaleQ30(c.frame.n, dotQ30(away, c.frame.n)));
  if (dotQ30(dir, dir) < (Q30_ONE >> 2)) dir = c.frame.right();
  Vec3 start = center + scaleToLength(dir, c.bodyRadius + FU);
  spawnFloatingFragment(normalizeQ30(start), c.r, k,
                        scaleToLength(dir, CRIT_EJECT_SPEED), idx);
  setEntitySize(c, c.size - piece);
  syncFragments(c, 0, 0);
  stats_.crits++;
}

void Game::damageEntity(int idx, int32_t dmg, int attacker, bool allowCrit) {
  Entity &c = entities[idx];
  if (!c.alive || c.invincible > 0) return;
  if (c.isPlayer && playerFrozen()) return;
  bool enemyAttacker = attacker >= 0 && attacker < MAX_ENTITIES &&
                       !entities[attacker].isPlayer;
  if (c.isPlayer && enemyAttacker) {
    // Just hit (mercy) or rolling through the dodge: the bullet passes
    if (playerMercy_ > 0 || dodgeTicks_ > 0) return;
    playerMercy_ = (int16_t)PLAYER_MERCY_TICKS;
  }
  if (c.isPlayer) {
    // Enemy fire scales with the sphere level and the player's upgrades
    // (difficulty), then the shield reduces it; the per-hit cap comes last
    if (enemyAttacker) dmg = (int32_t)((int64_t)dmg * enemyDamagePct() / 100);
    dmg = (int32_t)((int64_t)dmg *
                    SHIELD_DAMAGE_PCT[upgradeLevel(UpgradeKind::SHIELD)] / 100);
  }
  if (dmg < 1) dmg = 1;
  // Hits by the player build a grudge (see GRUDGE_* and updateAi); once it
  // reaches GRUDGE_ON it is full, so the pursuit outlasts the last hit by
  // GRUDGE_MAX - GRUDGE_ON
  if (!c.isPlayer && attacker == playerIndex_) {
    int grudge = c.grudge + GRUDGE_PER_HIT;
    c.grudge = (int16_t)(grudge >= GRUDGE_ON ? GRUDGE_MAX : grudge);
  }
  // Critical hit: knocks a fragment out instead of taking health (the
  // largest enemy included: without that, a runner-up far below the top
  // had no way to close the gap)
  if (allowCrit && rng_.below(CRIT_CHANCE_DEN) == 0 && c.size > 1) {
    Vec3 from = attacker >= 0 && attacker < MAX_ENTITIES
                    ? entities[attacker].frame.n
                    : c.frame.n;
    criticalHit(idx, from);
    if (c.isPlayer) {
      events_ |= Event::PLAYER_HIT;
      pushSound(SoundKind::HIT_PLAYER);
      pushEffect(EffectKind::PLAYER_HIT, idx, c.frame.n, c.r, dmg * 4);
    } else if (attacker == playerIndex_) {
      pushSound(SoundKind::HIT_ENEMY);
      pushEffect(EffectKind::ENEMY_HIT, idx, c.frame.n, c.r, dmg * 4);
    }
    return;
  }
  // The player never loses more than PLAYER_MAX_HIT_PERCENT of the gauge
  // from one hit (no one-shot kills by huge enemies); applied after every
  // multiplier, right before the health drops
  if (c.isPlayer) {
    int32_t cap = c.hpMax * PLAYER_MAX_HIT_PERCENT / 100;
    if (cap < 1) cap = 1;
    if (dmg > cap) dmg = cap;
  }
  if (dmg > c.hp) dmg = c.hp;
  c.hp -= dmg;
  if (c.isPlayer) {
    if (enemyAttacker) {
      stats_.playerHits++;
      stats_.playerDamageQ8 +=
          (uint32_t)((int64_t)dmg * 256 / (c.hpMax > 0 ? c.hpMax : 1));
    }
    events_ |= Event::PLAYER_HIT;
    pushSound(SoundKind::HIT_PLAYER);
    pushEffect(EffectKind::PLAYER_HIT, idx, c.frame.n, c.r, dmg);
  } else if (attacker == playerIndex_) {
    pushSound(SoundKind::HIT_ENEMY);
    pushEffect(EffectKind::ENEMY_HIT, idx, c.frame.n, c.r, dmg);
  }
  // Enemies that keep getting hit break off, out of the shooter's line of
  // fire (see updateAi); how many hits it takes, and whether they fight
  // back, is the sphere's AI tier (nothing at all on the first sphere)
  // (the grudge does not stop this: the break-off or the counterattack
  // runs first, the pursuit follows when the grudge is still there)
  const AiTier &tier = aiTier();
  if (!c.isPlayer && tier.evadeHits > 0) {
    if (c.evadeTicks == 0) {
      if (c.hitStreak < 255) c.hitStreak++;
      if (c.hitStreak >= tier.evadeHits) {
        c.hitStreak = 0;
        c.evadeTicks =
            (int16_t)(AI_EVADE_TICKS + rng_.range(0, AI_EVADE_TICKS / 2));
        bool known = attacker >= 0 && attacker < MAX_ENTITIES;
        c.evadeFrom = (uint8_t)(known ? attacker : NO_ENTITY);
        c.evadeDir = (int8_t)(rng_.below(2) ? 1 : -1);
        c.evadeFlipAt = (int16_t)(c.evadeTicks / 2);
        c.evadeMode = (uint8_t)EvadeMode::BREAK_PICK_SIDE;
        // A shooter no bigger than about oneself is fought back at (when
        // the health allows), otherwise (or by chance) the enemy breaks off
        if (known && c.hp * AI_COUNTER_MIN_HP_DIV > c.hpMax &&
            effectiveSizeQ8(entities[attacker]) * 100 <=
                effectiveSizeQ8(c) * AI_COUNTER_MAX_RATIO_PCT &&
            (int32_t)rng_.below(100) < tier.counterPct) {
          c.evadeMode = (uint8_t)EvadeMode::COUNTER;
        }
      }
    } else {
      // Still under fire: keep evading (from the latest shooter)
      if (attacker >= 0 && attacker < MAX_ENTITIES)
        c.evadeFrom = (uint8_t)attacker;
      if (c.evadeTicks < AI_EVADE_TICKS / 2) c.evadeTicks = AI_EVADE_TICKS / 2;
    }
  }
  if (c.hp <= 0) {
    if (!c.isPlayer && !c.noScore && attacker == playerIndex_) {
      // Kill score grows with the square of the size ratio
      const Entity &p = entities[playerIndex_];
      int64_t pct = p.size > 0 ? (int64_t)c.size * 100 / p.size : 100;
      pct = clampI64(SCORE_RATIO_MIN_PCT, SCORE_RATIO_MAX_PCT, pct);
      addScore((int64_t)SCORE_KILL_BASE * 256 * pct * pct / 10000);
    }
    killEntity(idx, attacker);
  }
}

void Game::killEntity(int idx, int killer) {
  Entity &c = entities[idx];
  if (!c.alive) return;
  // The sound: the player's own death, or a kill by the player (shot down
  // or drained dry). Big when the enemy is bigger now, or was when the
  // player started absorbing it
  if (c.isPlayer) {
    pushSound(SoundKind::PLAYER_KILLED);
  } else if (killer == playerIndex_) {
    pushSound(c.absorbedBig || c.size > entities[playerIndex_].size
                  ? SoundKind::ENEMY_KILLED_BIG
                  : SoundKind::ENEMY_KILLED_SMALL);
  }
  // Death effect when it happens within the player's surroundings
  {
    const Entity &p = entities[playerIndex_];
    int64_t d2;
    if (c.isPlayer) {
      pushEffect(EffectKind::PLAYER_KILLED, idx, c.frame.n, c.r,
                 (int32_t)c.size);
    } else if (tangentialDist2(c.frame.n, p.frame.n, EFFECT_RANGE_FU * FU,
                               d2)) {
      pushEffect(EffectKind::ENTITY_KILLED, idx, c.frame.n, c.r,
                 (int32_t)c.size);
    }
  }
  // The player's wreck leaves nothing behind: it can never take its own
  // fragments back (they are guarded for 3 s and it respawns elsewhere), so
  // they only ever fed the enemies that just killed it
  if (!c.isPlayer) {
    Vec3 center = worldPos(c.frame.n, c.r);
    Vec3 right = c.frame.right();
    for (int i = 0; i < c.fragmentCount; i++) {
      const Fragment &p = c.fragments[i];
      for (int side = -1; side <= 1; side += 2) {
        int32_t x = p.x * side;
        Vec3 off = scaleToLength(right, x) + scaleToLength(c.frame.t, p.y);
        Vec3 dir = off;
        if (dir.x == 0 && dir.y == 0 && dir.z == 0) dir = right;
        dir = normalizeQ30(dir);
        // Scatter briskly, bigger bodies burst wider
        int32_t sp =
            (FU / 4 + rng_.range(0, FU / 4)) * (8 + log2Floor(c.size)) / 8;
        Vec3 drift = scaleToLength(dir, sp);
        spawnFloatingFragment(normalizeQ30(center + off), c.r, p.sizeLog2,
                              drift);
      }
    }
  }
  releaseUpgrade(idx);
  c.alive = false;
  c.hp = 0;
  stats_.kills++;
  if (c.isPlayer) events_ |= Event::PLAYER_DIED;
}

// One tick of contact: size flows from the smaller entity to the bigger one
void Game::transferSize(int from, int to) {
  Entity &S = entities[from];
  Entity &B = entities[to];
  if (!S.alive || !B.alive) return;
  // Who gets absorbed is decided by size weighted by health, so the player
  // can absorb a bigger enemy whose gauge is low. Its size shrinks from here
  // on, so remember now for the kill sound
  if (B.isPlayer && S.size > B.size) S.absorbedBig = 1;
  // Health flows too: a share of the smaller one's gauge, healing the
  // bigger one by the same amount
  if ((tickCount_ % ABSORB_HP_INTERVAL) == 0) {
    int32_t drain = S.hpMax * ABSORB_HP_PCT / 100;
    if (drain < 1) drain = 1;
    if (drain > S.hp) drain = S.hp;
    S.hp -= drain;
    B.hp += drain;
    if (B.hp > B.hpMax) B.hp = B.hpMax;
    if (S.isPlayer) events_ |= Event::PLAYER_HIT;
    if (S.hp <= 0) {
      // Drained dry: devoured (the body bursts into fragments)
      if (B.isPlayer && !S.noScore) addScore((int64_t)SCORE_DEVOUR_BASE * 256);
      killEntity(from, to);
      stats_.absorbs++;
      return;
    }
  }
  uint32_t t = S.size >> ABSORB_RATE_SHIFT;
  if (t == 0) {
    if ((tickCount_ % ABSORB_MIN_INTERVAL) != 0) return;
    t = 1;
  }
  if (t > S.size) t = S.size;
  Vec3 rel = worldPos(S.frame.n, S.r) - worldPos(B.frame.n, B.r);
  setEntitySize(B, B.size + t);
  syncFragments(B, dotQ30(rel, B.frame.right()), dotQ30(rel, B.frame.t));
  healBySize(B, t);  // the absorber heals by what it took
  setEntitySize(S, S.size - t);
  if (S.size == 0) {
    releaseUpgrade(from);
    // Devoured: the same burst of debris as a kill (when near the player)
    {
      const Entity &p = entities[playerIndex_];
      int64_t d2;
      if (S.isPlayer) {
        pushEffect(EffectKind::PLAYER_KILLED, from, S.frame.n, S.r, (int32_t)t);
      } else if (tangentialDist2(S.frame.n, p.frame.n, EFFECT_RANGE_FU * FU,
                                 d2)) {
        pushEffect(EffectKind::ENTITY_KILLED, from, S.frame.n, S.r,
                   (int32_t)B.size / 8);
      }
    }
    S.alive = false;
    S.hp = 0;
    stats_.absorbs++;
    if (S.isPlayer) events_ |= Event::PLAYER_DIED;
    // Devoured whole (nothing of it is left to compare: the flag decides)
    if (S.isPlayer) {
      pushSound(SoundKind::PLAYER_KILLED);
    } else if (B.isPlayer) {
      pushSound(S.absorbedBig ? SoundKind::ENEMY_KILLED_BIG
                              : SoundKind::ENEMY_KILLED_SMALL);
    }
    if (B.isPlayer && !S.noScore) addScore((int64_t)SCORE_DEVOUR_BASE * 256);
    return;
  }
  syncFragments(S, 0, 0);
  // Being drained sounds like being hit, draining like eating fragments;
  // the minimum gap of the kinds (SOUND_MIN_GAP_TICKS) paces them
  if (S.isPlayer) {
    events_ |= Event::PLAYER_HIT;
    pushSound(SoundKind::HIT_PLAYER);
  }
  if (B.isPlayer) {
    events_ |= Event::PLAYER_ATE_FRAGMENT;
    pushSound(SoundKind::GET_FRAGMENT);
  }
  if ((tickCount_ % ticks30(5)) == 0) {
    if (S.isPlayer)
      pushEffect(EffectKind::PLAYER_DRAINED, from, S.frame.n, S.r, (int32_t)t);
    if (B.isPlayer)
      pushEffect(EffectKind::ENEMY_DRAINED, from, S.frame.n, S.r, (int32_t)t);
  }
}

void Game::spawnFloatingFragment(const Vec3 &n, int32_t r, int sizeLog2,
                                 const Vec3 &drift, int owner) {
  int slot = -1, oldest = -1;
  for (int i = 0; i < MAX_FLOATING_FRAGMENTS; i++) {
    if (!floatingFragments[i].alive) {
      slot = i;
      break;
    }
    if (oldest < 0 || floatingFragments[i].age > floatingFragments[oldest].age)
      oldest = i;
  }
  if (slot < 0) slot = oldest;
  FloatingFragment &fp = floatingFragments[slot];
  fp.alive = true;
  fp.n = n;
  fp.r = r;
  fp.drift = drift;
  fp.sizeLog2 = (uint8_t)sizeLog2;
  fp.age = 0;
  fp.owner = (int16_t)owner;
  fp.ownerGuard = (int16_t)(owner >= 0 ? FRAGMENT_OWNER_GUARD_TICKS : 0);
  fp.spin = rng_.brad();
}

// Move a point on the sphere by a tangential drift (units per tick)
static void applyDrift(Vec3 &n, int32_t r, Vec3 &drift, int decayShift) {
  if (drift.x == 0 && drift.y == 0 && drift.z == 0) return;
  Vec3 ang = {(int32_t)(((int64_t)drift.x * Q30_ONE) / r),
              (int32_t)(((int64_t)drift.y * Q30_ONE) / r),
              (int32_t)(((int64_t)drift.z * Q30_ONE) / r)};
  n = normalizeQ30(n + ang);
  drift.x -=
      (drift.x >> decayShift) + (drift.x > 0 ? 1 : (drift.x < 0 ? -1 : 0));
  drift.y -=
      (drift.y >> decayShift) + (drift.y > 0 ? 1 : (drift.y < 0 ? -1 : 0));
  drift.z -=
      (drift.z >> decayShift) + (drift.z > 0 ? 1 : (drift.z < 0 ? -1 : 0));
}

void Game::updateFloatingFragments() {
  const Entity &p = entities[playerIndex_];
  bool attract = p.alive && state_ == GameState::PLAYING;
  int32_t attractRange = p.bodyRadius + ATTRACT_RANGE_FU * FU;
  // The pull grows with the player (see ATTRACT_ACCEL)
  const int32_t attractScale = attractScaleQ8(p.size);
  const int32_t attractAccel =
      (int32_t)(((int64_t)ATTRACT_ACCEL * attractScale) >> 8);
  const int32_t attractMaxSpeed =
      p.bodyRadius > ATTRACT_MAX_SPEED ? p.bodyRadius : ATTRACT_MAX_SPEED;
  Vec3 playerPos = worldPos(p.frame.n, p.r);
  for (int i = 0; i < MAX_FLOATING_FRAGMENTS; i++) {
    FloatingFragment &fp = floatingFragments[i];
    if (!fp.alive) continue;
    if (fp.age < INT16_MAX) fp.age++;
    if (fp.ownerGuard > 0) fp.ownerGuard--;
    // Fragments near the player are drawn towards it (not ones it just lost)
    int64_t d2;
    bool guarded = fp.ownerGuard > 0 && fp.owner == playerIndex_;
    if (attract && !guarded &&
        tangentialDist2(fp.n, p.frame.n, attractRange, d2) &&
        d2 < (int64_t)attractRange * attractRange) {
      Vec3 dir = normalizeQ30(playerPos - worldPos(fp.n, fp.r));
      fp.drift = fp.drift + scaleToLength(dir, attractAccel);
      int64_t v2 = length2_64(fp.drift);
      if (v2 > (int64_t)attractMaxSpeed * attractMaxSpeed) {
        fp.drift = scaleToLength(normalizeQ30(fp.drift), attractMaxSpeed);
      }
    }
    applyDrift(fp.n, fp.r, fp.drift, 5 + RATE_SHIFT);
    fp.spin = (uint16_t)(fp.spin + 300 * 30 / TICK_RATE);
    int32_t rTarget = SPHERE_RADIUS + altitudeForSize(4u << fp.sizeLog2);
    int32_t dr = rTarget - fp.r;
    int32_t rs = dr >> (6 + RATE_SHIFT);
    if (rs == 0 && dr != 0) rs = dr > 0 ? 1 : -1;
    fp.r += rs;
  }
}

// Keep an index sorted by n.z for the entities and the floating fragments.
// The previous order is reused (insertion sort is nearly linear then).
template <typename T, typename ZFn>
static void rebuildOrder(const T *items, int count, int16_t *order,
                         int &orderCount, ZFn zOf) {
  bool listed[MAX_FLOATING_FRAGMENTS] = {};
  int n = 0;
  for (int i = 0; i < orderCount; i++) {
    int16_t idx = order[i];
    if (items[idx].alive) {
      order[n++] = idx;
      listed[idx] = true;
    }
  }
  for (int i = 0; i < count; i++) {
    if (items[i].alive && !listed[i]) order[n++] = (int16_t)i;
  }
  orderCount = n;
  for (int i = 1; i < n; i++) {
    int16_t v = order[i];
    int32_t z = zOf(items[v]);
    int j = i - 1;
    while (j >= 0 && zOf(items[order[j]]) > z) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = v;
  }
}

void Game::rebuildOrders() {
  rebuildOrder(entities, MAX_ENTITIES, entityOrder_, entityOrderCount_,
               [](const Entity &c) { return c.frame.n.z; });
  rebuildOrder(floatingFragments, MAX_FLOATING_FRAGMENTS, fragmentOrder_,
               fragmentOrderCount_,
               [](const FloatingFragment &p) { return p.n.z; });
  maxBodyRadius_ = 0;
  maxCoreReach_ = 0;
  for (int i = 0; i < MAX_ENTITIES; i++) {
    const Entity &c = entities[i];
    if (!c.alive) continue;
    if (c.bodyRadius > maxBodyRadius_) maxBodyRadius_ = c.bodyRadius;
    int32_t reach = c.coreHalf + absI32(c.coreY);
    if (reach > maxCoreReach_) maxCoreReach_ = reach;
  }
}

// First position in the entity order whose n.z >= z
int Game::entityLowerBound(int64_t z) const {
  int lo = 0, hi = entityOrderCount_;
  while (lo < hi) {
    int mid = (lo + hi) >> 1;
    if (entities[entityOrder_[mid]].frame.n.z < z)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

template <typename T>
static int lowerBoundZ(const T *items, const int16_t *order, int count,
                       int64_t z) {
  int lo = 0, hi = count;
  while (lo < hi) {
    int mid = (lo + hi) >> 1;
    if (items[order[mid]].n.z < z)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

void Game::handleEating() {
  for (int i = 0; i < MAX_ENTITIES; i++) {
    Entity &c = entities[i];
    if (!c.alive) continue;
    if (c.isPlayer && playerFrozen()) continue;
    Vec3 center = worldPos(c.frame.n, c.r);
    Vec3 right = c.frame.right();

    // Floating fragments
    int32_t reach = c.bodyRadius + 32 * FU;
    int64_t dz = (int64_t)reach << Z_SHIFT;
    int k = lowerBoundZ(floatingFragments, fragmentOrder_, fragmentOrderCount_,
                        c.frame.n.z - dz);
    for (; k < fragmentOrderCount_; k++) {
      FloatingFragment &fp = floatingFragments[fragmentOrder_[k]];
      if (fp.n.z > c.frame.n.z + dz) break;
      if (!fp.alive) continue;
      if (fp.ownerGuard > 0 && fp.owner == i) continue;  // just lost it
      int32_t lim = c.bodyRadius + fragmentHalfSize(fp.sizeLog2);
      int64_t d2;
      if (!tangentialDist2(c.frame.n, fp.n, lim, d2)) continue;
      if (d2 >= (int64_t)lim * lim) continue;
      if ((1u << fp.sizeLog2) * FOOD_NOTICE_RATIO < c.size) {
        // Too small to become part of the body: consumed for its health
        healByFragment(c, fp.sizeLog2);
        fp.alive = false;
        stats_.fragmentsHealed++;
        if (c.isPlayer) {
          events_ |= Event::PLAYER_HEALED;
          pushSound(SoundKind::GET_FRAGMENT);
        }
        continue;
      }
      Vec3 rel = worldPos(fp.n, fp.r) - center;
      addFragmentToEntity(c, fp.sizeLog2, dotQ30(rel, right),
                          dotQ30(rel, c.frame.t));
      c.absorbGuard = ABSORB_GUARD_TICKS;
      fp.alive = false;
      stats_.fragmentsEaten++;
      if (c.isPlayer) {
        events_ |= Event::PLAYER_ATE_FRAGMENT;
        pushSound(SoundKind::GET_FRAGMENT);
        addScore((int64_t)SCORE_FRAGMENT_BASE * 256);
      }
    }
  }
}

void Game::handleEntityCollisions() {
  int64_t dz = (int64_t)(maxBodyRadius_ * 2) << Z_SHIFT;
  for (int ka = 0; ka < entityOrderCount_; ka++) {
    int i = entityOrder_[ka];
    Entity &a = entities[i];
    if (!a.alive) continue;
    int64_t zHi = a.frame.n.z + dz;
    for (int kb = ka + 1; kb < entityOrderCount_; kb++) {
      int j = entityOrder_[kb];
      Entity &b = entities[j];
      if (b.frame.n.z > zHi) break;
      if (!b.alive || !a.alive) continue;
      if ((a.isPlayer || b.isPlayer) && playerFrozen()) continue;
      // Who devours whom: size weighted by the health gauge
      int64_t ea = effectiveSizeQ8(a), eb = effectiveSizeQ8(b);
      if (ea == eb) continue;
      int big = ea > eb ? i : j, small = big == i ? j : i;
      const Entity &B = entities[big];
      const Entity &S = entities[small];
      if (S.invincible > 0 || S.absorbGuard > 0) continue;
      // Bodies overlapping (in 3D, so a much higher entity is out of reach)
      int32_t reach = (B.bodyRadius + S.bodyRadius) >> 1;
      int64_t d2;
      if (!tangentialDist2(B.frame.n, S.frame.n, reach, d2)) continue;
      Vec3 rel = worldPos(B.frame.n, B.r) - worldPos(S.frame.n, S.r);
      if (length2_64(rel) >= (int64_t)reach * reach) continue;
      transferSize(small, big);
    }
  }
}

}  // namespace devoursphere::sim
