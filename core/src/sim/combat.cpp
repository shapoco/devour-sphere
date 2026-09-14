// Weapons, damage, death, eating, collisions, floating parts and particles.

#include "devoursphere/sim/game.hpp"

namespace devoursphere::sim {

static constexpr int Z_SHIFT =
    Q30_SHIFT - PLANET_RADIUS_SHIFT;  // units -> Q30 angle

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
  int64_t k = partHalfSize(log2Floor(ownerSize));
  return (int32_t)((int64_t)ws.speed * k /
                   partHalfSize(PLAYER_START_SIZE_LOG2));
}

static int32_t bulletRadius(const Bullet &b) {
  const WeaponSpec &ws = WEAPON_SPECS[(int)b.kind];
  return partHalfSize(log2Floor(b.ownerSize)) * ws.radiusPU8 / 4;
}

void Game::fireWeapon(int idx) {
  Creature &c = creatures[idx];
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
    // Nearest attackable creature in front
    int64_t best = INT64_MAX;
    int64_t range = (int64_t)bulletSpeed(ws, c.size) * ws.lifetime;
    for (int j = 0; j < MAX_CREATURES; j++) {
      const Creature &o = creatures[j];
      if (j == idx || !o.alive || !canAttack(c.size, o.size)) continue;
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
      const Creature &o = creatures[b.target];
      if (!o.alive || !canAttack(b.ownerSize, o.size)) {
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
    for (int k = creatureLowerBound(b.frame.n.z - dz); k < creatureOrderCount_;
         k++) {
      int j = creatureOrder_[k];
      Creature &o = creatures[j];
      if (o.frame.n.z > b.frame.n.z + dz) break;
      if (!o.alive || j == b.owner) continue;
      if (!canAttack(b.ownerSize, o.size)) continue;
      int32_t reach = o.bodyRadius + br;
      int64_t d2;
      if (!tangentialDist2(b.frame.n, o.frame.n, reach, d2)) continue;
      if (d2 >= (int64_t)reach * reach) continue;
      damageCreature(j, b.power, b.owner);
      stats_.hits++;
      b.alive = false;
      break;
    }
  }
}

void Game::damageCreature(int idx, int32_t dmg, int attacker) {
  (void)attacker;
  Creature &c = creatures[idx];
  if (!c.alive || c.invincible > 0) return;
  if (dmg > c.hp) dmg = c.hp;
  c.hp -= dmg;
  if (c.isPlayer) events_ |= Event::PLAYER_HIT;
  // The lost health becomes energy particles flying out of the body
  int pieces = dmg >= 256 ? 3 : (dmg >= 64 ? 2 : 1);
  int32_t per = dmg / pieces;
  Vec3 right = c.frame.right();
  for (int i = 0; i < pieces; i++) {
    uint16_t a = rng_.brad();
    int32_t dist = c.bodyRadius + PU;
    Vec3 dir = scaleQ30(right, cosQ30(a)) + scaleQ30(c.frame.t, sinQ30(a));
    Vec3 p = worldPos(c.frame.n, c.r) + scaleToLength(dir, dist);
    Vec3 drift = scaleToLength(dir, PU / 3 + rng_.range(0, PU / 3));
    spawnParticle(normalizeQ30(p), c.r,
                  i == pieces - 1 ? dmg - per * (pieces - 1) : per, drift);
  }
  if (c.hp <= 0) killCreature(idx);
}

void Game::killCreature(int idx) {
  Creature &c = creatures[idx];
  if (!c.alive) return;
  Vec3 center = worldPos(c.frame.n, c.r);
  Vec3 right = c.frame.right();
  for (int i = 0; i < c.partCount; i++) {
    const Part &p = c.parts[i];
    for (int side = -1; side <= 1; side += 2) {
      int32_t x = p.x * side;
      Vec3 off = scaleToLength(right, x) + scaleToLength(c.frame.t, p.y);
      Vec3 dir = off;
      if (dir.x == 0 && dir.y == 0 && dir.z == 0) dir = right;
      dir = normalizeQ30(dir);
      int32_t sp = PU / 8 + rng_.range(0, PU / 8);
      Vec3 drift = scaleToLength(dir, sp);
      spawnFloatingPart(normalizeQ30(center + off), c.r, p.sizeLog2, drift);
    }
  }
  c.alive = false;
  c.hp = 0;
  stats_.kills++;
  if (c.isPlayer) events_ |= Event::PLAYER_DIED;
}

void Game::absorbCreature(int eater, int eaten) {
  Creature &e = creatures[eater];
  Creature &v = creatures[eaten];
  if (!e.alive || !v.alive) return;
  Vec3 rel = worldPos(v.frame.n, v.r) - worldPos(e.frame.n, e.r);
  int32_t lx = dotQ30(rel, e.frame.right());
  int32_t ly = dotQ30(rel, e.frame.t);
  for (int i = 0; i < v.partCount; i++) {
    const Part &p = v.parts[i];
    addPartToCreature(e, p.sizeLog2, lx + p.x, ly + p.y);
  }
  e.absorbGuard = ABSORB_GUARD_TICKS;
  v.alive = false;
  v.hp = 0;
  stats_.absorbs++;
  if (v.isPlayer) events_ |= Event::PLAYER_DIED;
}

void Game::spawnFloatingPart(const Vec3 &n, int32_t r, int sizeLog2,
                             const Vec3 &drift) {
  int slot = -1, oldest = -1;
  for (int i = 0; i < MAX_FLOATING_PARTS; i++) {
    if (!floatingParts[i].alive) {
      slot = i;
      break;
    }
    if (oldest < 0 || floatingParts[i].age > floatingParts[oldest].age)
      oldest = i;
  }
  if (slot < 0) slot = oldest;
  FloatingPart &fp = floatingParts[slot];
  fp.alive = true;
  fp.n = n;
  fp.r = r;
  fp.drift = drift;
  fp.sizeLog2 = (uint8_t)sizeLog2;
  fp.age = 0;
  fp.spin = rng_.brad();
}

void Game::spawnParticle(const Vec3 &n, int32_t r, int32_t energy,
                         const Vec3 &drift) {
  if (energy <= 0) return;
  int slot = -1, oldest = -1;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].alive) {
      slot = i;
      break;
    }
    if (oldest < 0 || particles[i].life < particles[oldest].life) oldest = i;
  }
  if (slot < 0) slot = oldest;
  Particle &p = particles[slot];
  p.alive = true;
  p.n = n;
  p.r = r;
  p.drift = drift;
  p.energy = energy;
  p.life = (int16_t)PARTICLE_LIFETIME;
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

void Game::updateFloatingParts() {
  for (int i = 0; i < MAX_FLOATING_PARTS; i++) {
    FloatingPart &fp = floatingParts[i];
    if (!fp.alive) continue;
    if (fp.age < INT16_MAX) fp.age++;
    applyDrift(fp.n, fp.r, fp.drift, 5);
    fp.spin = (uint16_t)(fp.spin + 300);
    int32_t rTarget = PLANET_RADIUS + altitudeForSize(4u << fp.sizeLog2);
    int32_t dr = rTarget - fp.r;
    int32_t rs = dr >> 6;
    if (rs == 0 && dr != 0) rs = dr > 0 ? 1 : -1;
    fp.r += rs;
  }
}

void Game::updateParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle &p = particles[i];
    if (!p.alive) continue;
    if (--p.life <= 0) {
      p.alive = false;
      continue;
    }
    applyDrift(p.n, p.r, p.drift, 6);
  }
}

// Keep an index sorted by n.z for the creatures, the floating parts and the
// particles. The previous order is reused (insertion sort is nearly linear
// then).
template <typename T, typename ZFn>
static void rebuildOrder(const T *items, int count, int16_t *order,
                         int &orderCount, ZFn zOf) {
  bool listed[MAX_FLOATING_PARTS] = {};
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
  rebuildOrder(creatures, MAX_CREATURES, creatureOrder_, creatureOrderCount_,
               [](const Creature &c) { return c.frame.n.z; });
  rebuildOrder(floatingParts, MAX_FLOATING_PARTS, partOrder_, partOrderCount_,
               [](const FloatingPart &p) { return p.n.z; });
  rebuildOrder(particles, MAX_PARTICLES, particleOrder_, particleOrderCount_,
               [](const Particle &p) { return p.n.z; });
  maxBodyRadius_ = 0;
  maxCoreReach_ = 0;
  for (int i = 0; i < MAX_CREATURES; i++) {
    const Creature &c = creatures[i];
    if (!c.alive) continue;
    if (c.bodyRadius > maxBodyRadius_) maxBodyRadius_ = c.bodyRadius;
    int32_t reach = c.coreHalf + absI32(c.coreY);
    if (reach > maxCoreReach_) maxCoreReach_ = reach;
  }
}

// First position in the creature order whose n.z >= z
int Game::creatureLowerBound(int64_t z) const {
  int lo = 0, hi = creatureOrderCount_;
  while (lo < hi) {
    int mid = (lo + hi) >> 1;
    if (creatures[creatureOrder_[mid]].frame.n.z < z)
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
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = creatures[i];
    if (!c.alive) continue;
    Vec3 center = worldPos(c.frame.n, c.r);
    Vec3 right = c.frame.right();

    // Floating parts
    int32_t reach = c.bodyRadius + 32 * PU;
    int64_t dz = (int64_t)reach << Z_SHIFT;
    int k = lowerBoundZ(floatingParts, partOrder_, partOrderCount_,
                        c.frame.n.z - dz);
    for (; k < partOrderCount_; k++) {
      FloatingPart &fp = floatingParts[partOrder_[k]];
      if (fp.n.z > c.frame.n.z + dz) break;
      if (!fp.alive) continue;
      // Parts smaller than 1/32 of the body are beneath notice
      if ((1u << fp.sizeLog2) * 32 < c.size) continue;
      int32_t lim = c.bodyRadius + partHalfSize(fp.sizeLog2);
      int64_t d2;
      if (!tangentialDist2(c.frame.n, fp.n, lim, d2)) continue;
      if (d2 >= (int64_t)lim * lim) continue;
      Vec3 rel = worldPos(fp.n, fp.r) - center;
      addPartToCreature(c, fp.sizeLog2, dotQ30(rel, right),
                        dotQ30(rel, c.frame.t));
      c.absorbGuard = ABSORB_GUARD_TICKS;
      fp.alive = false;
      stats_.partsEaten++;
      if (c.isPlayer) events_ |= Event::PLAYER_ATE_PART;
    }

    // Energy particles
    reach = c.bodyRadius;
    dz = (int64_t)reach << Z_SHIFT;
    k = lowerBoundZ(particles, particleOrder_, particleOrderCount_,
                    c.frame.n.z - dz);
    for (; k < particleOrderCount_; k++) {
      Particle &p = particles[particleOrder_[k]];
      if (p.n.z > c.frame.n.z + dz) break;
      if (!p.alive || p.life > PARTICLE_LIFETIME - PARTICLE_IMMUNE_TICKS)
        continue;
      int64_t d2;
      if (!tangentialDist2(c.frame.n, p.n, reach, d2)) continue;
      if (d2 >= (int64_t)reach * reach) continue;
      c.hp += p.energy;
      if (c.hp > c.hpMax) c.hp = c.hpMax;
      p.alive = false;
      stats_.particlesEaten++;
      if (c.isPlayer) events_ |= Event::PLAYER_ATE_ENERGY;
    }
  }
}

void Game::handleCreatureCollisions() {
  int64_t dz = (int64_t)(maxBodyRadius_ + maxCoreReach_) << Z_SHIFT;
  for (int ka = 0; ka < creatureOrderCount_; ka++) {
    int i = creatureOrder_[ka];
    Creature &a = creatures[i];
    if (!a.alive) continue;
    int64_t zHi = a.frame.n.z + dz;
    for (int kb = ka + 1; kb < creatureOrderCount_; kb++) {
      int j = creatureOrder_[kb];
      Creature &b = creatures[j];
      if (b.frame.n.z > zHi) break;
      if (!b.alive || !a.alive) continue;
      if (a.size == b.size) continue;
      int big = a.size > b.size ? i : j, small = big == i ? j : i;
      const Creature &B = creatures[big];
      const Creature &S = creatures[small];
      if (S.invincible > 0 || S.absorbGuard > 0) continue;
      // Only creatures that can fight each other collide; the bigger one
      // absorbs the smaller one when the smaller one's core enters its body
      if (!canAttack(S.size, B.size)) continue;
      int32_t reach = B.bodyRadius * 3 / 4 + S.coreHalf;
      int64_t d2;
      if (!tangentialDist2(B.frame.n, S.frame.n, reach + absI32(S.coreY), d2)) {
        continue;
      }
      Vec3 coreS = worldPos(S.frame.n, S.r) + scaleToLength(S.frame.t, S.coreY);
      Vec3 rel = worldPos(B.frame.n, B.r) - coreS;
      if (length2_64(rel) >= (int64_t)reach * reach) continue;
      absorbCreature(big, small);
    }
  }
}

}  // namespace devoursphere::sim
