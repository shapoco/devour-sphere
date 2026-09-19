// Entity control, AI, movement and the physics of the fragments inside a body.

#include "devoursphere/sim/game.hpp"

namespace devoursphere::sim {

// cos(15 degrees) in Q30: firing cone
static constexpr int32_t COS_FIRE_CONE = (int32_t)(0.9659 * Q30_ONE);
// cos(AI_FLANK_CONE): in front of the player
static constexpr int32_t COS_FLANK_CONE = (int32_t)(0.8660 * Q30_ONE);

void Game::updatePlayerControls(uint8_t buttons, uint8_t pressed) {
  Entity &c = entities[playerIndex_];
  c.turn = 0;
  if (buttons & Button::LEFT) c.turn -= 1;
  if (buttons & Button::RIGHT) c.turn += 1;
  c.braking = (buttons & Button::DOWN) != 0;
  c.dashing =
      (buttons & Button::UP) != 0 && !c.braking && c.hp > (c.hpMax >> 3);
  c.firing = (buttons & Button::A) != 0;
  if ((pressed & Button::B) && dodgeCooldown_ == 0 && dodgeTicks_ == 0) {
    startDodge(buttons);
  }
}

// Emergency dodge: a barrel roll sideways. The side is the turn input, else
// away from the nearest enemy bullet nearby, else to the right
void Game::startDodge(uint8_t buttons) {
  const Entity &c = entities[playerIndex_];
  int dir = 0;
  if (buttons & Button::LEFT) dir -= 1;
  if (buttons & Button::RIGHT) dir += 1;
  if (dir == 0) {
    constexpr int64_t NEAR2 = (int64_t)(60 * FU) * (60 * FU);
    int64_t best = NEAR2;
    Vec3 center = worldPos(c.frame.n, c.r);
    Vec3 right = c.frame.right();
    for (const Bullet &b : bullets) {
      if (!b.alive || b.fromPlayer) continue;
      Vec3 rel = worldPos(b.frame.n, b.r) - center;
      int64_t d2 = length2_64(rel);
      if (d2 >= best) continue;
      best = d2;
      dir = dotQ30(rel, right) > 0 ? -1 : 1;
    }
  }
  if (dir == 0) dir = 1;
  dodgeDir_ = (int8_t)dir;
  dodgeTicks_ = (int16_t)DODGE_TICKS;
  dodgeCooldown_ = (int16_t)DODGE_COOLDOWN_TICKS;
  events_ |= Event::PLAYER_DODGED;
  pushSound(SoundKind::DODGE);
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
  const AiTier &tier = aiTier();
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
  int64_t preyD2 = INT64_MAX;  // weighted: the player looks nearer (bias)
  int64_t preyRealD2 = 0;
  // The pack (AI_PACK): a neighbour already hunting the player calls us in,
  // and the player becomes the prey whatever else is around
  bool packCall = false;
  int packPrey = -1;
  int64_t packPreyD2 = 0;
  // The player is noticed from farther away on higher spheres
  const int32_t playerSight = (int32_t)tier.playerSightFU * FU;
  const int32_t scanSight = playerSight > sight ? playerSight : sight;
  int64_t sightDz = (int64_t)scanSight << (Q30_SHIFT - SPHERE_RADIUS_SHIFT);
  const int64_t ec = effectiveSizeQ8(c);
  constexpr int64_t NEAR2 =
      (int64_t)(AI_FLEE_NEAR_FU * FU) * (AI_FLEE_NEAR_FU * FU);
  constexpr int64_t FLEE2 =
      (int64_t)(AI_FLEE_FAR_FU * FU) * (AI_FLEE_FAR_FU * FU);
  constexpr int64_t PACK2 =
      (int64_t)(AI_PACK_CALL_FU * FU) * (AI_PACK_CALL_FU * FU);
  for (int k = entityLowerBound(c.frame.n.z - sightDz); k < entityOrderCount_;
       k++) {
    int j = entityOrder_[k];
    const Entity &o = entities[j];
    if (o.frame.n.z > c.frame.n.z + sightDz) break;
    if (j == idx || !o.alive) continue;
    if (o.isPlayer && c.isPlayer) continue;
    if (o.isPlayer && playerFrozen()) continue;  // in flight, far above
    int64_t d2;
    if (!tangentialDist2(c.frame.n, o.frame.n, o.isPlayer ? playerSight : sight,
                         d2)) {
      continue;
    }
    if ((tier.flags & AI_PACK) && !o.isPlayer && !c.isPlayer && d2 < PACK2 &&
        o.aiMode == AiMode::HUNT_ENTITY && o.aiTarget == playerIndex_) {
      packCall = true;
    }
    const int64_t eo = effectiveSizeQ8(o);
    if (eo > ec) {
      // Bigger entities are a threat only when close (everything is bigger
      // than somebody; fleeing from every giant in sight would paralyze the
      // small ones), and only beyond the tier's bravery
      bool dangerous = (eo * 100 > ec * tier.fleeFarPct && d2 < FLEE2) ||
                       (eo * 100 > ec * tier.fleeNearPct && d2 < NEAR2);
      if (dangerous) {
        if (d2 < threatD2) threat = j, threatD2 = d2;
        continue;
      }
    }
    // Prey: no bigger than oneself (the player: up to the bravery limit) and
    // not tiny. The player is preferred among the valid prey (bias), but a
    // much smaller player is left alone like anyone else, so a fresh sphere
    // does not gang up on it
    int64_t limit = ec;
    uint32_t ratio = AI_PREY_MIN_RATIO;
    int64_t weighted = d2;
    if (o.isPlayer) {
      limit = ec * tier.fleeFarPct / 100;
      ratio = tier.preyMinRatio;
      weighted = d2 * 100 / tier.playerBiasPct;
    }
    if (eo > limit || (uint64_t)o.size * ratio < c.size) continue;
    if (o.isPlayer) packPrey = j, packPreyD2 = d2;
    if (weighted < preyD2) prey = j, preyD2 = weighted, preyRealD2 = d2;
  }
  if (packCall && packPrey >= 0) {
    prey = packPrey;
    preyD2 = 0;  // beats any fragment
    preyRealD2 = packPreyD2;
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
    // Under sustained fire (see EvadeMode)
    int from = c.evadeFrom;
    bool shooter = from >= 0 && from < MAX_ENTITIES && entities[from].alive;
    if (!shooter) {
      // Shooter unknown or gone: break off in the remembered direction
      c.aiMode = AiMode::FLEE;
      c.turn = c.evadeDir;
      c.braking = c.evadeTicks > AI_EVADE_TICKS / 2;
      c.dashing = !c.braking;
      return;
    }
    const Entity &o = entities[from];
    if (c.evadeMode == (uint8_t)EvadeMode::COUNTER) {
      // Counterattack: quick turn under brake until the shooter is within
      // AI_EVADE_TURN_ANGLE, fire whenever it is in the cone and in range,
      // then charge (dash when it is far)
      c.aiMode = AiMode::HUNT_ENTITY;
      c.aiTarget = (int16_t)from;
      int16_t err;
      c.turn = steerTowards(c, o.frame.n, false, &err);
      int32_t a = err < 0 ? -err : err;
      c.braking = a > (int32_t)AI_EVADE_TURN_ANGLE;
      Vec3 dn = normalizeQ30(tangentTowards(c.frame.n, o.frame.n));
      const WeaponSpec &ws = WEAPON_SPECS[(int)c.weapon];
      int64_t range = (int64_t)bulletSpeed(ws, c.size) * ws.lifetime;
      int64_t d2;
      bool inRange =
          tangentialDist2(c.frame.n, o.frame.n,
                          (int32_t)(range > INT32_MAX ? INT32_MAX : range), d2);
      c.firing = inRange && dotQ30(c.frame.t, dn) > COS_FIRE_CONE;
      c.dashing = !c.braking && inRange && d2 > (int64_t)(30 * FU) * (30 * FU);
      return;
    }
    // Break off: get out of the shooter's line of fire. The escape heading
    // is sideways to the shooter, leaning away from it; the side is the one
    // that needs the smaller turn when the maneuver starts (so the enemy
    // keeps crossing the line rather than reversing) and flips halfway
    // through for a zigzag. Quick turn under brake while far off that
    // heading, dash once pointing there.
    c.aiMode = AiMode::FLEE;
    Vec3 d = normalizeQ30(tangentTowards(c.frame.n, o.frame.n));
    Vec3 side = crossQ30(c.frame.n, d);  // across the line of fire
    if (c.evadeMode == (uint8_t)EvadeMode::BREAK_PICK_SIDE) {
      int32_t along = dotQ30(side, c.frame.t);
      constexpr int32_t AMBIGUOUS = Q30_ONE / 8;  // within ~7 deg of the line
      if (along > AMBIGUOUS) c.evadeDir = 1;
      if (along < -AMBIGUOUS) c.evadeDir = -1;
      c.evadeMode = (uint8_t)EvadeMode::BREAK;
    } else if (c.evadeFlipAt > 0 && c.evadeTicks <= c.evadeFlipAt) {
      c.evadeDir = (int8_t)-c.evadeDir;
      c.evadeFlipAt = 0;
    }
    if (c.evadeDir < 0) side = -side;
    Vec3 escape = side - Vec3{d.x >> 1, d.y >> 1, d.z >> 1};
    int16_t err = headingError(c.frame, escape);
    constexpr int16_t DEAD = (int16_t)degToBrad(4);
    c.turn = err > DEAD ? -1 : (err < -DEAD ? 1 : 0);
    int32_t a = err < 0 ? -err : err;
    c.braking = a > (int32_t)AI_EVADE_TURN_ANGLE;
    c.dashing = !c.braking;
    return;
  }
  if (threat >= 0) {
    c.aiMode = AiMode::FLEE;
    c.aiTarget = (int16_t)threat;
    c.turn = steerTowards(c, entities[threat].frame.n, true);
    c.dashing = (tier.flags & AI_DASH) && c.hp > (c.hpMax >> 1) &&
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
    const WeaponSpec &ws = WEAPON_SPECS[(int)c.weapon];
    // Where to fly: beside the player when flanking (AI_FLANK: out of its
    // line of fire, in from the side), ahead of a moving prey (AI_LEAD:
    // the bullet's flight time times the prey's velocity), else at it
    Vec3 goal = o.frame.n;
    bool flanking = false;
    if (o.isPlayer && (tier.flags & AI_FLANK) &&
        preyRealD2 > (int64_t)(AI_FLANK_MIN_FU * FU) * (AI_FLANK_MIN_FU * FU)) {
      Vec3 toMe = normalizeQ30(tangentTowards(o.frame.n, c.frame.n));
      if (dotQ30(o.frame.t, toMe) > COS_FLANK_CONE) {
        Vec3 right = o.frame.right();
        int32_t ang = (int32_t)(((int64_t)(AI_FLANK_OFFSET_FU * FU)
                                 << Q30_SHIFT) /
                                o.r);
        if (dotQ30(right, toMe) < 0) ang = -ang;
        goal = normalizeQ30(o.frame.n + scaleQ30(right, ang));
        flanking = true;
      }
    }
    if (!flanking && (tier.flags & AI_LEAD) && o.speed > 0) {
      uint32_t dist = isqrt64((uint64_t)preyRealD2);
      int32_t bs = bulletSpeed(ws, c.size);
      int32_t flight = bs > 0 ? (int32_t)(dist / (uint32_t)bs) : 0;  // ticks
      if (flight > 3 * TICK_RATE) flight = 3 * TICK_RATE;
      int64_t travel = (int64_t)o.speed * flight;  // units
      int32_t ang = (int32_t)((travel << Q30_SHIFT) / o.r);
      goal = normalizeQ30(o.frame.n + scaleQ30(o.frame.t, ang));
    }
    int16_t err;
    c.turn = steerTowards(c, goal, false, &err);
    // Prey far around and close by: quick turn under brake
    c.braking = wantsQuickTurn(err, preyRealD2);
    // Fire when the aim point is in the cone and the prey within range
    Vec3 dn = normalizeQ30(tangentTowards(c.frame.n, flanking ? o.frame.n : goal));
    int64_t range = (int64_t)bulletSpeed(ws, c.size) * ws.lifetime;
    if (dotQ30(c.frame.t, dn) > COS_FIRE_CONE && preyRealD2 < range * range) {
      // The tier's chance, a little higher with the player's upgrades; the
      // AI-driven player always fires
      int32_t chance = tier.fireChance *
                       (100 + DIFF_FIRE_PCT_PER_LEVEL * totalUpgradeLevel()) /
                       100;
      if (chance > 255) chance = 255;
      c.firing = c.isPlayer || (int32_t)rng_.below(256) < chance;
    }
    c.dashing = (tier.flags & AI_DASH) &&
                preyRealD2 > (int64_t)(40 * FU) * (40 * FU) &&
                c.hp > (c.hpMax >> 1);
    return;
  }
  // Wander: keep a slowly drifting heading
  c.aiWanderAngle = (uint16_t)(c.aiWanderAngle + rng_.range(-3000, 3000));
  int32_t r = rng_.range(0, 7);
  c.turn = (int8_t)(r == 0 ? -1 : (r == 1 ? 1 : 0));
}

// Progress of the dodge after `elapsed` of DODGE_TICKS ticks as a smoothstep
// 3t^2 - 2t^3 in Q16 (0 at the start, 65536 at the end; zero slope at both)
static int32_t dodgeProgressQ16(int elapsed) {
  if (elapsed <= 0) return 0;
  if (elapsed >= DODGE_TICKS) return 65536;
  const int64_t n = DODGE_TICKS, e = elapsed;
  return (int32_t)(((e * e * (3 * n - 2 * e)) << 16) / (n * n * n));
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
  if (c.isPlayer) {
    dashExtra = dashExtra *
                THRUSTER_DASH_PCT[upgradeLevel(UpgradeKind::THRUSTER)] / 100;
  }
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
  // platform shows the same attitude). In flight the player rolls once
  // around its heading instead, from the state timer so that the roll
  // ends at exactly zero.
  const bool flying = c.isPlayer && playerFrozen();
  int rollAt = -1;
  if (flying) {
    rollAt = stateTimer_ - (state_ == GameState::LAUNCH ? LAUNCH_ROLL_START
                                                        : ARRIVE_ROLL_START);
  }
  if (rollAt >= 0 && rollAt <= FLIGHT_ROLL_TICKS) {
    c.bank = (int16_t)(uint16_t)((int64_t)rollAt * 65536 / FLIGHT_ROLL_TICKS);
  } else if (c.isPlayer && dodgeTicks_ > 0) {
    // Emergency dodge: one barrel roll towards the dodge side, eased in
    // and out (the same smoothstep as the sidestep below)
    int32_t roll = dodgeProgressQ16(DODGE_TICKS - dodgeTicks_ + 1);
    c.bank = (int16_t)(uint16_t)(dodgeDir_ > 0 ? roll : -roll);
  } else {
    int32_t bankTarget =
        ((int32_t)(c.braking ? BANK_MAX_BRAKE : BANK_MAX) * c.turnLevel) >> 8;
    int32_t db = bankTarget - c.bank;
    int32_t bs = db >> BANK_APPROACH_SHIFT;
    if (bs == 0 && db != 0) bs = db > 0 ? 1 : -1;
    c.bank = (int16_t)(c.bank + bs);
  }

  if (c.speed > 0) {
    int32_t ang = (int32_t)(((int64_t)c.speed << Q30_SHIFT) / c.r);
    c.frame.n = normalizeQ30(c.frame.n + scaleQ30(c.frame.t, ang));
    c.frame.t = orthonormalizeQ30(c.frame.t, c.frame.n);
  }
  if (c.isPlayer && dodgeTicks_ > 0 && !flying) {
    // ... and the sidestep of the dodge: DODGE_SPEED_MUL times the cruise
    // on average, along a smoothstep so that it starts and stops softly
    int elapsed = DODGE_TICKS - dodgeTicks_;
    int32_t total = cruise * DODGE_SPEED_MUL * DODGE_TICKS;
    int32_t lat = (int32_t)(((int64_t)total * (dodgeProgressQ16(elapsed + 1) -
                                               dodgeProgressQ16(elapsed))) >>
                            16);
    int32_t ang = (int32_t)(((int64_t)lat << Q30_SHIFT) / c.r);
    if (dodgeDir_ < 0) ang = -ang;
    c.frame.n = normalizeQ30(c.frame.n + scaleQ30(c.frame.right(), ang));
    c.frame.t = orthonormalizeQ30(c.frame.t, c.frame.n);
  }

  // Altitude
  int32_t rTarget = SPHERE_RADIUS + altitudeForSize(c.size);
  int approach = ALT_APPROACH_SHIFT;
  if (flying) {
    if (state_ == GameState::LAUNCH ||
        (switchPending_ && stateTimer_ <= ARRIVE_SWITCH_TICKS)) {
      // Slow, accelerating ascent, continued past the launch until the
      // sphere is switched
      int64_t t = stateTimer_;
      if (state_ == GameState::ARRIVE) t += LAUNCH_TICKS;
      rTarget = SPHERE_RADIUS + ALTITUDE +
                (int32_t)((int64_t)LAUNCH_ALTITUDE * t * t /
                          ((int64_t)LAUNCH_TICKS * LAUNCH_TICKS));
    } else {
      // Decelerating descent to the cruising altitude, reached a second
      // before the state ends so that the body levels out before the
      // player takes over (the first sphere of a game dives from the start)
      rTarget = descentTarget(stateTimer_);
    }
    approach = 3;
  }
  int32_t dr = rTarget - c.r;
  int32_t rs = dr >> approach;
  if (rs == 0 && dr != 0) rs = dr > 0 ? 1 : -1;
  c.r += rs;
  if (c.isPlayer) playerClimb_ = rs;

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
  if (dx == 0 && dy == 0) {
    fx += A;  // coincident: push sideways
    return;
  }
  // Local coordinates stay within a few thousand units (15x the core
  // half-size at most, measured), so the squared distance almost always
  // fits 32 bits: the same floor(sqrt) for a fraction of the cost on a core
  // without 64-bit arithmetic. The 64-bit form is kept for the rest.
  uint32_t d;
  if (dx > -32768 && dx < 32768 && dy > -32768 && dy < 32768) {
    const int32_t x = (int32_t)dx, y = (int32_t)dy;
    d = isqrt32((uint32_t)(x * x + y * y));
  } else {
    d = isqrt64((uint64_t)(dx * dx + dy * dy));
  }
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
  // (no healing here: this is only the visual decomposition, which is also
  // rebuilt when a body shrinks)
}

// Taking in size heals in proportion to it (eating, absorbing an enemy)
void Game::healBySize(Entity &c, uint32_t sizeUnits) {
  int64_t heal = (int64_t)HP_PER_SIZE * HEAL_PER_FRAGMENT_MUL * sizeUnits;
  if (heal > c.hpMax) heal = c.hpMax;
  c.hp += (int32_t)heal;
  if (c.hp > c.hpMax) c.hp = c.hpMax;
}

void Game::healByFragment(Entity &c, int sizeLog2) {
  healBySize(c, 1u << sizeLog2);
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
  healByFragment(c, k);
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
