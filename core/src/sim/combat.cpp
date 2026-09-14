// Weapons, damage, death, eating, collisions, floating fragments and sparks.

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

  c.fireCooldown = ws.cooldown;
  Bullet &b = bullets[slot];
  b.alive = true;
  Vec3 muzzle = worldPos(c.frame.n, c.r) + scaleToLength(c.frame.t, c.coreY);
  b.frame.n = normalizeQ30(muzzle);
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
  if (c.isPlayer) events_ |= Event::PLAYER_FIRED;
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
        b.r += dr >> 3;
      }
    }
    int32_t ang = (int32_t)(((int64_t)b.speed << Q30_SHIFT) / b.r);
    b.frame.n = normalizeQ30(b.frame.n + scaleQ30(b.frame.t, ang));
    b.frame.t = orthonormalizeQ30(b.frame.t, b.frame.n);

    int32_t br = bulletRadius(b);
    int64_t dz = (int64_t)(maxBodyRadius_ + br) << Z_SHIFT;
    for (int k = entityLowerBound(b.frame.n.z - dz); k < entityOrderCount_;
         k++) {
      int j = entityOrder_[k];
      Entity &o = entities[j];
      if (o.frame.n.z > b.frame.n.z + dz) break;
      if (!o.alive || j == b.owner) continue;
      int32_t reach = o.bodyRadius + br;
      int64_t d2;
      if (!tangentialDist2(b.frame.n, o.frame.n, reach, d2)) continue;
      if (d2 >= (int64_t)reach * reach) continue;
      damageEntity(j, b.power, b.owner);
      stats_.hits++;
      b.alive = false;
      break;
    }
  }
}

void Game::damageEntity(int idx, int32_t dmg, int attacker) {
  Entity &c = entities[idx];
  if (!c.alive || c.invincible > 0) return;
  if (dmg > c.hp) dmg = c.hp;
  c.hp -= dmg;
  if (c.isPlayer) {
    events_ |= Event::PLAYER_HIT;
    pushEffect(EffectKind::PLAYER_HIT, c.frame.n, c.r, dmg);
  } else if (attacker == playerIndex_) {
    pushEffect(EffectKind::ENEMY_HIT, c.frame.n, c.r, dmg);
  }
  // The lost health becomes sparks flying out of the body
  int pieces = dmg >= 256 ? 3 : (dmg >= 64 ? 2 : 1);
  int32_t per = dmg / pieces;
  Vec3 right = c.frame.right();
  for (int i = 0; i < pieces; i++) {
    uint16_t a = rng_.brad();
    int32_t dist = c.bodyRadius + FU;
    Vec3 dir = scaleQ30(right, cosQ30(a)) + scaleQ30(c.frame.t, sinQ30(a));
    Vec3 p = worldPos(c.frame.n, c.r) + scaleToLength(dir, dist);
    Vec3 drift = scaleToLength(dir, FU / 3 + rng_.range(0, FU / 3));
    spawnSpark(normalizeQ30(p), c.r,
               i == pieces - 1 ? dmg - per * (pieces - 1) : per, drift);
  }
  if (c.hp <= 0) killEntity(idx);
}

void Game::killEntity(int idx) {
  Entity &c = entities[idx];
  if (!c.alive) return;
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
      int32_t sp = FU / 8 + rng_.range(0, FU / 8);
      Vec3 drift = scaleToLength(dir, sp);
      spawnFloatingFragment(normalizeQ30(center + off), c.r, p.sizeLog2, drift);
    }
  }
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
  uint32_t t = S.size >> ABSORB_RATE_SHIFT;
  if (t == 0) {
    if ((tickCount_ % ABSORB_MIN_INTERVAL) != 0) return;
    t = 1;
  }
  if (t > S.size) t = S.size;
  Vec3 rel = worldPos(S.frame.n, S.r) - worldPos(B.frame.n, B.r);
  setEntitySize(B, B.size + t);
  syncFragments(B, dotQ30(rel, B.frame.right()), dotQ30(rel, B.frame.t));
  setEntitySize(S, S.size - t);
  if (S.size == 0) {
    S.alive = false;
    S.hp = 0;
    stats_.absorbs++;
    if (S.isPlayer) events_ |= Event::PLAYER_DIED;
    return;
  }
  syncFragments(S, 0, 0);
  if (S.isPlayer) events_ |= Event::PLAYER_HIT;
  if (B.isPlayer) events_ |= Event::PLAYER_ATE_FRAGMENT;
  if ((tickCount_ % 5) == 0) {
    if (S.isPlayer)
      pushEffect(EffectKind::PLAYER_DRAINED, S.frame.n, S.r, (int32_t)t);
    if (B.isPlayer)
      pushEffect(EffectKind::ENEMY_DRAINED, S.frame.n, S.r, (int32_t)t);
  }
}

void Game::spawnFloatingFragment(const Vec3 &n, int32_t r, int sizeLog2,
                                 const Vec3 &drift) {
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
  fp.spin = rng_.brad();
}

void Game::spawnSpark(const Vec3 &n, int32_t r, int32_t energy,
                      const Vec3 &drift) {
  if (energy <= 0) return;
  int slot = -1, oldest = -1;
  for (int i = 0; i < MAX_SPARKS; i++) {
    if (!sparks[i].alive) {
      slot = i;
      break;
    }
    if (oldest < 0 || sparks[i].life < sparks[oldest].life) oldest = i;
  }
  if (slot < 0) slot = oldest;
  Spark &p = sparks[slot];
  p.alive = true;
  p.n = n;
  p.r = r;
  p.drift = drift;
  p.energy = energy;
  p.life = (int16_t)SPARK_LIFETIME;
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
  for (int i = 0; i < MAX_FLOATING_FRAGMENTS; i++) {
    FloatingFragment &fp = floatingFragments[i];
    if (!fp.alive) continue;
    if (fp.age < INT16_MAX) fp.age++;
    applyDrift(fp.n, fp.r, fp.drift, 5);
    fp.spin = (uint16_t)(fp.spin + 300);
    int32_t rTarget = SPHERE_RADIUS + altitudeForSize(4u << fp.sizeLog2);
    int32_t dr = rTarget - fp.r;
    int32_t rs = dr >> 6;
    if (rs == 0 && dr != 0) rs = dr > 0 ? 1 : -1;
    fp.r += rs;
  }
}

void Game::updateSparks() {
  for (int i = 0; i < MAX_SPARKS; i++) {
    Spark &p = sparks[i];
    if (!p.alive) continue;
    if (--p.life <= 0) {
      p.alive = false;
      continue;
    }
    applyDrift(p.n, p.r, p.drift, 6);
  }
}

// Keep an index sorted by n.z for the entities, the floating fragments and the
// sparks. The previous order is reused (insertion sort is nearly linear
// then).
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
  rebuildOrder(sparks, MAX_SPARKS, sparkOrder_, sparkOrderCount_,
               [](const Spark &p) { return p.n.z; });
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
      // Fragments smaller than 1/32 of the body are beneath notice
      if ((1u << fp.sizeLog2) * 32 < c.size) continue;
      int32_t lim = c.bodyRadius + fragmentHalfSize(fp.sizeLog2);
      int64_t d2;
      if (!tangentialDist2(c.frame.n, fp.n, lim, d2)) continue;
      if (d2 >= (int64_t)lim * lim) continue;
      Vec3 rel = worldPos(fp.n, fp.r) - center;
      addFragmentToEntity(c, fp.sizeLog2, dotQ30(rel, right),
                          dotQ30(rel, c.frame.t));
      c.absorbGuard = ABSORB_GUARD_TICKS;
      fp.alive = false;
      stats_.fragmentsEaten++;
      if (c.isPlayer) events_ |= Event::PLAYER_ATE_FRAGMENT;
    }

    // Energy sparks
    reach = c.bodyRadius;
    dz = (int64_t)reach << Z_SHIFT;
    k = lowerBoundZ(sparks, sparkOrder_, sparkOrderCount_, c.frame.n.z - dz);
    for (; k < sparkOrderCount_; k++) {
      Spark &p = sparks[sparkOrder_[k]];
      if (p.n.z > c.frame.n.z + dz) break;
      if (!p.alive || p.life > SPARK_LIFETIME - SPARK_IMMUNE_TICKS) continue;
      int64_t d2;
      if (!tangentialDist2(c.frame.n, p.n, reach, d2)) continue;
      if (d2 >= (int64_t)reach * reach) continue;
      c.hp += p.energy;
      if (c.hp > c.hpMax) c.hp = c.hpMax;
      p.alive = false;
      stats_.sparksEaten++;
      if (c.isPlayer) events_ |= Event::PLAYER_ATE_SPARK;
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
      if (a.size == b.size) continue;
      int big = a.size > b.size ? i : j, small = big == i ? j : i;
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
