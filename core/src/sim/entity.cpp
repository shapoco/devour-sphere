// Entity control, AI, movement and the physics of the fragments inside a body.

#include "devoursphere/sim/game.hpp"

namespace devoursphere::sim {

// cos(15 degrees) in Q30: firing cone
static constexpr int32_t COS_FIRE_CONE = (int32_t)(0.9659 * Q30_ONE);

void Game::updatePlayerControls(uint8_t buttons) {
  Entity &c = entities[playerIndex_];
  c.turn = 0;
  if (buttons & Button::LEFT) c.turn -= 1;
  if (buttons & Button::RIGHT) c.turn += 1;
  c.braking = (buttons & Button::DOWN) != 0;
  c.dashing =
      (buttons & Button::UP) != 0 && !c.braking && c.hp > (c.hpMax >> 3);
  c.firing = (buttons & Button::A) != 0;
}

// Direction from `from` towards `to` projected on the tangent plane at `from`
// (Q30, not normalized; scaled down to avoid overflow)
static Vec3 tangentTowards(const Vec3 &fromN, const Vec3 &toN) {
  Vec3 d = {(toN.x - fromN.x) >> 2, (toN.y - fromN.y) >> 2,
            (toN.z - fromN.z) >> 2};
  int32_t along = dotQ30(d, fromN);
  return d - scaleQ30(fromN, along);
}

// Signed angle (brad, positive = left) from the heading to direction `d`
static int16_t headingError(const Frame &f, const Vec3 &d) {
  int32_t s = dotQ30(crossQ30(f.t, d), f.n);
  int32_t c = dotQ30(f.t, d);
  return (int16_t)atan2Brad(s, c);
}

// errOut: the signed heading error (brad) to the (fled) target
static int8_t steerTowards(const Entity &c, const Vec3 &targetN, bool flee,
                           int16_t *errOut = nullptr) {
  Vec3 d = tangentTowards(c.frame.n, targetN);
  if (flee) d = -d;
  if (errOut) *errOut = 0;
  if (d.x == 0 && d.y == 0 && d.z == 0) return 0;
  int16_t err = headingError(c.frame, d);
  if (errOut) *errOut = err;
  constexpr int16_t DEAD = (int16_t)degToBrad(4);
  if (err > DEAD) return -1;  // target is on the left
  if (err < -DEAD) return 1;
  return 0;
}

// A target far around and close by: brake for a quick turn instead of
// circling with a wide turning radius
static bool wantsQuickTurn(int16_t err, int64_t d2) {
  int32_t a = err < 0 ? -err : err;
  return a > (int32_t)AI_QUICK_TURN_ANGLE &&
         d2 < (int64_t)(AI_QUICK_TURN_FU * FU) * (AI_QUICK_TURN_FU * FU);
}

void Game::updateAi(int idx) {
  Entity &c = entities[idx];
  const int32_t sight = AI_SIGHT_FU * FU;
  c.aiMode = AiMode::WANDER;
  c.aiTarget = -1;
  c.firing = false;
  c.dashing = false;
  c.braking = false;

  // Threats: bigger entities that can attack or absorb us
  int threat = -1;
  int64_t threatD2 = INT64_MAX;
  int prey = -1;
  int64_t preyD2 = INT64_MAX;
  int64_t sightDz = (int64_t)sight << (Q30_SHIFT - SPHERE_RADIUS_SHIFT);
  for (int k = entityLowerBound(c.frame.n.z - sightDz); k < entityOrderCount_;
       k++) {
    int j = entityOrder_[k];
    const Entity &o = entities[j];
    if (o.frame.n.z > c.frame.n.z + sightDz) break;
    if (j == idx || !o.alive) continue;
    int64_t d2;
    if (!tangentialDist2(c.frame.n, o.frame.n, sight, d2)) continue;
    if (effectiveSizeQ8(o) > effectiveSizeQ8(c)) {
      // Bigger attackers are a threat in sight (when they can attack);
      // absorbers only when close
      constexpr int64_t NEAR2 = (int64_t)(40 * FU) * (40 * FU);
      // Bigger entities are a threat only when close (everything is bigger
      // than somebody; fleeing from every giant in sight would paralyze the
      // small ones): 1.25x within 60 FU, anything bigger within 40 FU
      constexpr int64_t FLEE2 = (int64_t)(60 * FU) * (60 * FU);
      int64_t ec = effectiveSizeQ8(c), eo = effectiveSizeQ8(o);
      bool dangerous =
          (eo * 4 > ec * 5 && d2 < FLEE2) || (eo > ec && d2 < NEAR2);
      if (dangerous && d2 < threatD2) threat = j, threatD2 = d2;
    } else if (o.size * 4 >= c.size && d2 < preyD2) {
      prey = j, preyD2 = d2;  // equal or smaller (but not tiny): fair game
    }
  }
  // Food: nearest floating fragment in sight (z-band query)
  int food = -1;
  int64_t foodD2 = INT64_MAX;
  {
    int64_t dz = (int64_t)sight << (Q30_SHIFT - SPHERE_RADIUS_SHIFT);
    int64_t zLo = c.frame.n.z - dz, zHi = c.frame.n.z + dz;
    int lo = 0, hi = fragmentOrderCount_;
    while (lo < hi) {
      int mid = (lo + hi) >> 1;
      if (floatingFragments[fragmentOrder_[mid]].n.z < zLo)
        lo = mid + 1;
      else
        hi = mid;
    }
    for (int k = lo; k < fragmentOrderCount_; k++) {
      const FloatingFragment &fp = floatingFragments[fragmentOrder_[k]];
      if (fp.n.z > zHi) break;
      int64_t d2;
      // Fragments beneath notice (see handleEating) are not worth chasing
      if ((1u << fp.sizeLog2) * FOOD_NOTICE_RATIO < c.size) continue;
      if (!tangentialDist2(c.frame.n, fp.n, sight, d2)) continue;
      if (d2 < foodD2) food = fragmentOrder_[k], foodD2 = d2;
    }
  }

  if (c.hitStreak > 0) c.hitStreak--;
  if (c.evadeTicks > 0) {
    // Under sustained fire: break off in a fixed direction (quick turn
    // under brake at first, then dash away on higher levels)
    c.aiMode = AiMode::FLEE;
    c.turn = c.evadeDir;
    c.braking = c.evadeTicks > AI_EVADE_TICKS / 2;
    c.dashing = !c.braking && sphereLevel_ >= 2;
    return;
  }
  if (threat >= 0) {
    c.aiMode = AiMode::FLEE;
    c.aiTarget = (int16_t)threat;
    c.turn = steerTowards(c, entities[threat].frame.n, true);
    c.dashing = sphereLevel_ >= 3 && c.hp > (c.hpMax >> 1) &&
                threatD2 < (int64_t)(30 * FU) * (30 * FU);
    return;
  }
  // Food first unless the prey is much closer (everything is prey now)
  if (food >= 0 && (prey < 0 || foodD2 <= preyD2 * 4)) {
    c.aiMode = AiMode::HUNT_FRAGMENT;
    c.aiTarget = (int16_t)food;
    int16_t err;
    c.turn = steerTowards(c, floatingFragments[food].n, false, &err);
    c.braking = wantsQuickTurn(err, foodD2);
    return;
  }
  if (prey >= 0) {
    c.aiMode = AiMode::HUNT_ENTITY;
    c.aiTarget = (int16_t)prey;
    const Entity &o = entities[prey];
    int16_t err;
    c.turn = steerTowards(c, o.frame.n, false, &err);
    Vec3 d = tangentTowards(c.frame.n, o.frame.n);
    Vec3 dn = normalizeQ30(d);
    // Prey far around and close by: quick turn under brake
    c.braking = wantsQuickTurn(err, preyD2);
    const WeaponSpec &ws = WEAPON_SPECS[(int)c.weapon];
    int64_t range = (int64_t)bulletSpeed(ws, c.size) * ws.lifetime;
    if (dotQ30(c.frame.t, dn) > COS_FIRE_CONE && preyD2 < range * range) {
      // Enemies fire more eagerly on higher level spheres; the AI-driven
      // player always fires
      int lv = sphereLevel_ - 1;
      if (lv >= AI_FIRE_CHANCE_LEVELS) lv = AI_FIRE_CHANCE_LEVELS - 1;
      if (lv < 0) lv = 0;
      c.firing = c.isPlayer || rng_.below(256) < AI_FIRE_CHANCE[lv];
    }
    c.dashing = sphereLevel_ >= 3 && preyD2 > (int64_t)(40 * FU) * (40 * FU) &&
                c.hp > (c.hpMax >> 1);
    return;
  }
  // Wander: keep a slowly drifting heading
  c.aiWanderAngle = (uint16_t)(c.aiWanderAngle + rng_.range(-3000, 3000));
  int32_t r = rng_.range(0, 7);
  c.turn = (int8_t)(r == 0 ? -1 : (r == 1 ? 1 : 0));
}

void Game::moveEntity(Entity &c) {
  int32_t cruise = cruiseSpeedForSize(c.size);
  // The dash builds up slowly and fades faster
  if (c.dashing && !c.braking) {
    c.dashLevel = (int16_t)(c.dashLevel + 256 / DASH_RAMP_UP_TICKS + 1);
    if (c.dashLevel > 256) c.dashLevel = 256;
  } else {
    c.dashLevel = (int16_t)(c.dashLevel - 256 / DASH_RAMP_DOWN_TICKS - 1);
    if (c.dashLevel < 0) c.dashLevel = 0;
  }
  int32_t dashExtra =
      cruise * (DASH_SPEED_NUM - DASH_SPEED_DEN) / DASH_SPEED_DEN;
  int32_t target = cruise + (int32_t)(((int64_t)dashExtra * c.dashLevel) >> 8);
  uint16_t rate = TURN_RATE;
  if (c.braking) {
    target = 0;
    rate = TURN_RATE_BRAKE;
  } else if (c.dashLevel > 128) {
    rate = TURN_RATE_DASH;
  }
  int shift = c.braking ? BRAKE_DECEL_SHIFT : SPEED_ACCEL_SHIFT;
  int32_t delta = target - c.speed;
  int32_t step = delta >> shift;
  if (step == 0 && delta != 0) step = delta > 0 ? 1 : -1;
  c.speed += step;

  // The turn builds up and stops gradually
  {
    int32_t target = c.turn * 256;
    int32_t d = target - c.turnLevel;
    int32_t step = 256 / TURN_RAMP_TICKS + 1;
    if (d > step) d = step;
    if (d < -step) d = -step;
    c.turnLevel = (int16_t)(c.turnLevel + d);
  }
  if (c.turnLevel != 0) {
    // positive angles turn left; turnLevel > 0 means turning right
    int32_t angle = -((int32_t)rate * c.turnLevel) >> 8;
    c.frame.t = rotateAroundQ30(c.frame.t, c.frame.n, (uint16_t)angle);
  }
  // Bank into the turn (visual, but kept in the simulation so that every
  // platform shows the same attitude)
  int32_t bankTarget =
      ((int32_t)(c.braking ? BANK_MAX_BRAKE : BANK_MAX) * c.turnLevel) >> 8;
  int32_t db = bankTarget - c.bank;
  int32_t bs = db >> BANK_APPROACH_SHIFT;
  if (bs == 0 && db != 0) bs = db > 0 ? 1 : -1;
  c.bank = (int16_t)(c.bank + bs);

  if (c.speed > 0) {
    int32_t ang = (int32_t)(((int64_t)c.speed << Q30_SHIFT) / c.r);
    c.frame.n = normalizeQ30(c.frame.n + scaleQ30(c.frame.t, ang));
    c.frame.t = orthonormalizeQ30(c.frame.t, c.frame.n);
  }

  // Altitude
  int32_t rTarget = SPHERE_RADIUS + altitudeForSize(c.size);
  int approach = ALT_APPROACH_SHIFT;
  if (c.isPlayer && state_ == GameState::LAUNCH) {
    // Slow, accelerating ascent
    int64_t t = stateTimer_;
    rTarget =
        SPHERE_RADIUS + ALTITUDE +
        (int32_t)(900 * FU * t * t / ((int64_t)LAUNCH_TICKS * LAUNCH_TICKS));
    approach = 3;
  }
  int32_t dr = rTarget - c.r;
  int32_t rs = dr >> approach;
  if (rs == 0 && dr != 0) rs = dr > 0 ? 1 : -1;
  c.r += rs;

  if (c.fireCooldown > 0) c.fireCooldown--;
  if (c.invincible > 0) c.invincible--;
  if (c.absorbGuard > 0) c.absorbGuard--;
  if (c.evadeTicks > 0) c.evadeTicks--;
}

// Add the force between a fragment at (px, py) and a point (qx, qy) with the
// equilibrium distance d0 and strength A (units per tick)
static void addForce(int64_t &fx, int64_t &fy, int32_t px, int32_t py,
                     int32_t qx, int32_t qy, int32_t d0, int32_t A) {
  int64_t dx = (int64_t)qx - px, dy = (int64_t)qy - py;
  int64_t d2 = dx * dx + dy * dy;
  if (d2 == 0) {
    fx += A;  // coincident: push sideways
    return;
  }
  uint32_t d = isqrt64((uint64_t)d2);
  if (d == 0) d = 1;
  // inv = 2^24 / d (one 32-bit division; d < 2^24 in practice)
  int64_t inv = (d < (1u << 24)) ? (int64_t)((1u << 24) / d) : 1;
  int64_t q = ((int64_t)d0 * inv) >> 12;  // d0 / d in Q12
  if (q > (8 << 12)) q = 8 << 12;
  int64_t F = (A * q * q * (4096 - q)) >> 36;
  if (F < -4 * (int64_t)A) F = -4 * A;
  if (q < 4096 && F < A / 8) F = A / 8;  // far away: minimum attraction
  fx += (F * dx * inv) >> 24;
  fy += (F * dy * inv) >> 24;
}

void Game::updateLayout(Entity &c) {
  int k = log2Floor(c.size);
  int32_t coreHalf = fragmentHalfSize(k) * 5 / 8;
  if (coreHalf < FU / 2) coreHalf = FU / 2;
  c.coreHalf = coreHalf;
  c.coreY = coreHalf * 3 / 2;
  c.layoutFocusY = c.coreY + coreHalf;

  int32_t half[MAX_FRAGMENTS_PER_ENTITY];
  for (int i = 0; i < c.fragmentCount; i++)
    half[i] = fragmentHalfSize(c.fragments[i].sizeLog2);

  int32_t bodyR = c.coreY + 2 * coreHalf;
  for (int i = 0; i < c.fragmentCount; i++) {
    Fragment &p = c.fragments[i];
    int32_t s = half[i];
    int32_t A = s >> (4 + RATE_SHIFT);
    if (A < 1) A = 1;
    int64_t fx = 0, fy = 0;
    addForce(fx, fy, p.x, p.y, 0, c.coreY, (coreHalf + s) * 9 / 8, A);
    for (int j = 0; j < c.fragmentCount; j++) {
      const Fragment &o = c.fragments[j];
      int32_t d0 = (s + half[j]) * 9 / 8;
      if (j != i) addForce(fx, fy, p.x, p.y, o.x, o.y, d0, A);
      addForce(fx, fy, p.x, p.y, -o.x, o.y, d0, A);  // mirror images
    }
    // Weak spring towards the local origin: a fragment that got left behind is
    // pulled back into the body (the speed limit grows with the distance so
    // that far fragments return quickly)
    int32_t dist = absI32(p.x) > absI32(p.y) ? absI32(p.x) : absI32(p.y);
    fx -= p.x >> (5 + RATE_SHIFT);
    fy -= p.y >> (5 + RATE_SHIFT);

    // Heavily damped so that the layout settles instead of oscillating
    int64_t vx = ((int64_t)p.vx >> 2) + fx;
    int64_t vy = ((int64_t)p.vy >> 2) + fy;
    int32_t vmax = (s >> (3 + RATE_SHIFT)) + (dist >> (4 + RATE_SHIFT));
    p.vx = (int32_t)clampI64(-vmax, vmax, vx);
    p.vy = (int32_t)clampI64(-vmax, vmax, vy);
    // Below a small threshold the fragment is considered at rest
    int32_t rest = (s >> (6 + RATE_SHIFT)) + 1;
    if (absI32(p.vx) <= rest && absI32(p.vy) <= rest) p.vx = p.vy = 0;
    p.x += p.vx;
    p.y += p.vy;
    int32_t xmin = s >> 2;
    if (p.x < xmin) {
      p.x = xmin;
      if (p.vx < 0) p.vx = 0;
    }
    int32_t ext = p.x + s * 3 / 2;
    if (ext > bodyR) bodyR = ext;
    ext = absI32(p.y) + s * 2;
    if (ext > bodyR) bodyR = ext;
  }
  c.bodyRadius = bodyR;
}

// Merge fragments a and b (same size) into a
static void mergePair(Entity &c, int i, int j) {
  Fragment &a = c.fragments[i];
  const Fragment &b = c.fragments[j];
  a.x = (int32_t)(((int64_t)a.x + b.x) >> 1);
  a.y = (int32_t)(((int64_t)a.y + b.y) >> 1);
  a.vx = a.vy = 0;
  a.sizeLog2++;
  c.fragments[j] = c.fragments[c.fragmentCount - 1];
  c.fragmentCount--;
}

void Game::mergeFragments(Entity &c) {
  // Above half the fragment limit the body is crowded: merge same-sized
  // fragments from much farther apart, and if none are close, the closest
  // pair anyway
  bool crowded = c.fragmentCount > MAX_FRAGMENTS_PER_ENTITY / 2;
  int bestI = -1, bestJ = -1;
  int64_t bestD2 = INT64_MAX;
  for (int i = 0; i < c.fragmentCount; i++) {
    Fragment &a = c.fragments[i];
    if (a.sizeLog2 >= MAX_SIZE_LOG2) continue;
    int32_t limit =
        fragmentHalfSize(a.sizeLog2) *
        (crowded ? FRAGMENT_MERGE_DIST_CROWDED_NUM : FRAGMENT_MERGE_DIST_NUM) /
        FRAGMENT_MERGE_DIST_DEN;
    for (int j = i + 1; j < c.fragmentCount; j++) {
      Fragment &b = c.fragments[j];
      if (b.sizeLog2 != a.sizeLog2) continue;
      int64_t dx = (int64_t)a.x - b.x, dy = (int64_t)a.y - b.y;
      int64_t d2 = dx * dx + dy * dy;
      if (d2 < (int64_t)limit * limit) {
        mergePair(c, i, j);
        if (c.isPlayer) events_ |= Event::PLAYER_MERGED;
        return;  // one merge per tick keeps the layout stable
      }
      if (d2 < bestD2) bestD2 = d2, bestI = i, bestJ = j;
    }
  }
  if (crowded && bestI >= 0) {
    mergePair(c, bestI, bestJ);
    if (c.isPlayer) events_ |= Event::PLAYER_MERGED;
  }
}

void Game::enforceFragmentLimit(Entity &c) {
  if (c.fragmentCount < MAX_FRAGMENTS_PER_ENTITY) return;
  // Merge the two smallest fragments (the visual decomposition may then differ
  // slightly from `size`, which stays authoritative)
  int a = -1, b = -1;
  for (int i = 0; i < c.fragmentCount; i++) {
    if (a < 0 || c.fragments[i].sizeLog2 < c.fragments[a].sizeLog2) {
      b = a;
      a = i;
    } else if (b < 0 || c.fragments[i].sizeLog2 < c.fragments[b].sizeLog2) {
      b = i;
    }
  }
  Fragment &pa = c.fragments[a], &pb = c.fragments[b];
  if (pa.sizeLog2 == pb.sizeLog2) {
    pb.sizeLog2++;
  }
  pb.x = (int32_t)(((int64_t)pa.x + pb.x) >> 1);
  pb.y = (int32_t)(((int64_t)pa.y + pb.y) >> 1);
  c.fragments[a] = c.fragments[c.fragmentCount - 1];
  c.fragmentCount--;
}

// Visual only: add a fragment to the body (size is not changed) and heal
void Game::pushFragment(Entity &c, int sizeLog2, int32_t lx, int32_t ly) {
  enforceFragmentLimit(c);
  Fragment &p = c.fragments[c.fragmentCount++];
  int32_t s = fragmentHalfSize(sizeLog2);
  int32_t lim = c.bodyRadius * 2 + s * 4;
  p.x = clampI32(s >> 2, lim, absI32(lx));
  p.y = clampI32(-lim, lim, ly);
  p.vx = p.vy = 0;
  p.sizeLog2 = (uint8_t)clampI32(0, MAX_SIZE_LOG2, sizeLog2);
  healByFragment(c, sizeLog2);
}

// Touching a fragment heals in proportion to its size
void Game::healByFragment(Entity &c, int sizeLog2) {
  int64_t heal =
      (int64_t)HP_PER_SIZE * HEAL_PER_FRAGMENT_MUL * (1u << sizeLog2);
  if (heal > c.hpMax) heal = c.hpMax;
  c.hp += (int32_t)heal;
  if (c.hp > c.hpMax) c.hp = c.hpMax;
}

// Change the size, keeping the health ratio
void Game::setEntitySize(Entity &c, uint32_t size) {
  int32_t oldMax = c.hpMax;
  c.size = size;
  c.hpMax = HP_PER_SIZE * (int32_t)c.size;
  if (oldMax > 0) c.hp = (int32_t)(((int64_t)c.hp * c.hpMax) / oldMax);
  if (c.hp > c.hpMax) c.hp = c.hpMax;
}

void Game::addFragmentToEntity(Entity &c, int sizeLog2, int32_t lx,
                               int32_t ly) {
  int k = clampI32(0, MAX_SIZE_LOG2, sizeLog2);
  setEntitySize(c, c.size + (1u << k));
  pushFragment(c, k, lx, ly);
}

// Bring the visual decomposition back in line with `size` after it changed
// (gradual absorption): shed the smallest fragments while they exceed the
// size, add fragments while they fall short. (lx, ly): where new fragments
// enter the body.
void Game::syncFragments(Entity &c, int32_t lx, int32_t ly) {
  uint32_t sum = 0;
  for (int i = 0; i < c.fragmentCount; i++)
    sum += 1u << c.fragments[i].sizeLog2;
  while (sum > c.size) {
    if (c.fragmentCount <= 1) {
      // A single fragment that is too big: split it in two halves so that
      // one of them can go
      Fragment &f = c.fragments[0];
      if (c.fragmentCount < 1 || f.sizeLog2 == 0 ||
          c.fragmentCount >= MAX_FRAGMENTS_PER_ENTITY) {
        break;
      }
      f.sizeLog2--;
      Fragment half = f;
      half.x = f.x + fragmentHalfSize(f.sizeLog2);
      c.fragments[c.fragmentCount++] = half;
      continue;
    }
    int a = 0;
    for (int i = 1; i < c.fragmentCount; i++) {
      if (c.fragments[i].sizeLog2 < c.fragments[a].sizeLog2) a = i;
    }
    sum -= 1u << c.fragments[a].sizeLog2;
    c.fragments[a] = c.fragments[c.fragmentCount - 1];
    c.fragmentCount--;
  }
  for (int n = 0; n < 4 && sum < c.size; n++) {
    int k = log2Floor(c.size - sum);
    pushFragment(c, k, lx, ly);
    sum += 1u << k;
  }
}

}  // namespace devoursphere::sim
