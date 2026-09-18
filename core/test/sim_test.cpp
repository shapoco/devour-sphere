// Self-checking tests of the simulation: fixed-point math, determinism and
// invariants after thousands of ticks of random input.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "devoursphere/render/renderer.hpp"
#include "devoursphere/sim/game.hpp"

using namespace devoursphere::sim;

static int failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                 \
    }                                                             \
  } while (0)

static void testFixed() {
  // sin/cos against the C library
  for (int a = 0; a < 65536; a += 37) {
    double rad = a * (2 * M_PI / 65536.0);
    double s = sinQ30((uint16_t)a) / (double)Q30_ONE;
    double c = cosQ30((uint16_t)a) / (double)Q30_ONE;
    CHECK(std::fabs(s - std::sin(rad)) < 2e-6);
    CHECK(std::fabs(c - std::cos(rad)) < 2e-6);
  }
  CHECK(sinQ30(0) == 0);
  CHECK(sinQ30(BRAD_QUARTER) == Q30_ONE);
  CHECK(sinQ30(BRAD_HALF) == 0);
  CHECK(sinQ30((uint16_t)(3 * BRAD_QUARTER)) == -Q30_ONE);

  // isqrt
  for (uint64_t v = 0; v < 100000; v += 7)
    CHECK(isqrt64(v) == (uint32_t)std::floor(std::sqrt((double)v)));
  CHECK(isqrt64((uint64_t)1 << 60) == (1u << 30));
  CHECK(isqrt64(UINT64_MAX) == 0xFFFFFFFFu);

  // atan2
  for (int a = 0; a < 65536; a += 97) {
    double rad = a * (2 * M_PI / 65536.0);
    int32_t x = (int32_t)(std::cos(rad) * 1000000),
            y = (int32_t)(std::sin(rad) * 1000000);
    int16_t diff = (int16_t)(atan2Brad(y, x) - (uint16_t)a);
    CHECK(std::abs(diff) < 40);  // < 0.22 degrees
  }

  // normalize / rotate / orthonormalize
  Vec3 n = normalizeQ30({Q30_ONE, Q30_ONE, Q30_ONE});
  CHECK(std::llabs(dot64(n, n) - ((int64_t)1 << 60)) < ((int64_t)1 << 32));
  Vec3 up = {0, 0, Q30_ONE};
  Vec3 fwd = {0, Q30_ONE, 0};
  Vec3 r =
      rotateAroundQ30(fwd, up, BRAD_QUARTER);  // +90 deg: towards up x fwd = -x
  CHECK(std::abs(r.x + Q30_ONE) < 4 && std::abs(r.y) < 4);
  Vec3 o = orthonormalizeQ30({Q30_ONE / 2, Q30_ONE / 2, Q30_ONE / 2}, up);
  CHECK(o.z == 0);
  CHECK(std::abs(dotQ30(o, o) - Q30_ONE) < 8);

  CHECK(fragmentHalfSize(0) == FU / 2);
  CHECK(fragmentHalfSize(4) == FU);
  CHECK(std::abs(fragmentHalfSize(2) - 181) <= 1);
  CHECK(fragmentHalfSize(8) == 2 * FU);
}

static uint8_t scriptedInput(uint32_t t) {
  // Deterministic pseudo random button pattern
  uint32_t h = t * 2654435761u;
  uint8_t b = 0;
  if ((h >> 3) % 5 == 0) b |= Button::LEFT;
  if ((h >> 7) % 5 == 1) b |= Button::RIGHT;
  if ((h >> 11) % 7 == 0) b |= Button::UP;
  if ((h >> 13) % 11 == 0) b |= Button::DOWN;
  if ((h >> 17) % 3 == 0) b |= Button::A;
  return b;
}

static void playTicks(Game &g, int n, uint32_t t0) {
  for (int i = 0; i < n; i++) g.tick(scriptedInput(t0 + i));
}

// Through the menus into the arrival flight to the first sphere
static void startGame(Game &g) {
  g.tick(0);
  g.tick(Button::A);  // title -> weapon select
  g.tick(0);
  g.tick(Button::RIGHT);
  g.tick(0);
  g.tick(Button::A);  // -> the arrival flight to the first sphere
  CHECK(g.state() == GameState::ARRIVE);
  CHECK(g.sphereLevel() == 1);
}

// ... and through the flight onto the sphere
static void enterPlay(Game &g) {
  startGame(g);
  // The first sphere starts at the switch point of the arrival (no sphere
  // to leave), so the flight is shorter
  int flown = 0;
  while (g.state() == GameState::ARRIVE) g.tick(0), flown++;
  CHECK(g.state() == GameState::PLAYING);
  CHECK(g.sphereLevel() == 1);
  CHECK(flown == ARRIVE_TICKS - ARRIVE_SWITCH_TICKS - 1);
  CHECK(g.selectedWeapon() == 1);
}

static void checkInvariants(const Game &g) {
  for (int i = 0; i < MAX_ENTITIES; i++) {
    const Entity &c = g.entities[i];
    if (!c.alive) continue;
    CHECK(c.fragmentCount >= 1 && c.fragmentCount <= MAX_FRAGMENTS_PER_ENTITY);
    CHECK(c.size >= 1);
    CHECK(c.hp >= 0 && c.hp <= c.hpMax);
    CHECK(c.r > SPHERE_RADIUS);
    CHECK(std::llabs(dot64(c.frame.n, c.frame.n) - ((int64_t)1 << 60)) <
          ((int64_t)1 << 40));
    CHECK(std::llabs(dot64(c.frame.t, c.frame.t) - ((int64_t)1 << 60)) <
          ((int64_t)1 << 40));
    CHECK(std::abs(dotQ30(c.frame.n, c.frame.t)) < (1 << 12));
    for (int k = 0; k < c.fragmentCount; k++) {
      CHECK(c.fragments[k].x >= 0);
      CHECK(c.fragments[k].sizeLog2 <= MAX_SIZE_LOG2);
      CHECK(std::abs(c.fragments[k].x) < 4096 * FU &&
            std::abs(c.fragments[k].y) < 4096 * FU);
    }
    CHECK(c.bodyRadius > 0 && c.bodyRadius < 4096 * FU);
  }
}

static void testDeterminism() {
  Game a, b;
  a.reset(12345);
  b.reset(12345);
  enterPlay(a);
  enterPlay(b);
  playTicks(a, 3000, 0);
  playTicks(b, 3000, 0);
  CHECK(a.stateHash() == b.stateHash());
  CHECK(std::memcmp(a.entities, b.entities, sizeof(a.entities)) == 0);
  Game c;
  c.reset(12346);
  enterPlay(c);
  playTicks(c, 3000, 0);
  CHECK(a.stateHash() != c.stateHash());
}

static void testGameplay() {
  Game g;
  g.reset(777);
  CHECK(g.state() == GameState::TITLE);
  playTicks(g, 300, 5);  // attract mode
  checkInvariants(g);

  // Weapon select: both axes move the cursor, and they wrap around
  g.reset(777);
  g.tick(0);  // buttons held during reset do not count as presses
  g.tick(Button::A);
  CHECK(g.state() == GameState::WEAPON_SELECT);
  CHECK(g.selectedWeapon() == 0);
  g.tick(Button::DOWN);
  CHECK(g.selectedWeapon() == 1);
  g.tick(0);
  g.tick(Button::UP);
  CHECK(g.selectedWeapon() == 0);
  g.tick(0);
  g.tick(Button::UP);
  CHECK(g.selectedWeapon() == WEAPON_COUNT - 1);
  g.tick(0);
  g.tick(Button::RIGHT);
  CHECK(g.selectedWeapon() == 0);

  g.reset(777);
  startGame(g);
  const Entity &p = g.player();
  CHECK(p.alive && p.isPlayer);
  CHECK(p.size == (1u << PLAYER_START_SIZE_LOG2));
  CHECK(p.r > SPHERE_RADIUS + ALTITUDE + ARRIVE_ALTITUDE / 2);  // flying in
  int alive = 0;
  for (int i = 0; i < MAX_ENTITIES; i++) alive += g.entities[i].alive;
  CHECK(alive == INITIAL_ENTITIES);
  while (g.state() == GameState::ARRIVE) g.tick(0);
  CHECK(g.state() == GameState::PLAYING);

  bool sawBullet = false, sawFragment = false;
  for (int i = 0; i < 6000; i++) {
    g.tick(scriptedInput(100 + i));
    for (int k = 0; k < MAX_BULLETS; k++) sawBullet |= g.bullets[k].alive;
    for (int k = 0; k < MAX_FLOATING_FRAGMENTS; k++)
      sawFragment |= g.floatingFragments[k].alive;
    if ((i % 500) == 0) checkInvariants(g);
    if (g.state() == GameState::DEAD) break;
  }
  checkInvariants(g);
  CHECK(sawBullet);
  CHECK(g.playerRank() >= 1 && g.playerRank() <= MAX_ENTITIES);

  // On a high level sphere the enemies fight each other: fragments must
  // appear
  Game f;
  f.reset(31337);
  f.debugStartSphere(4, 0);
  CHECK(f.sphereLevel() == 4);
  sawFragment = false;
  int deaths = 0, respawns = 0;
  for (int i = 0; i < 4000; i++) {
    int before = f.aliveEntities();
    f.tick(scriptedInput(900 + i));
    if (f.aliveEntities() < before) deaths++;
    if (f.events() & Event::PLAYER_RESPAWNED) respawns++;
    for (int k = 0; k < MAX_FLOATING_FRAGMENTS; k++)
      sawFragment |= f.floatingFragments[k].alive;
    if ((i % 500) == 0) checkInvariants(f);
    if (f.state() != GameState::PLAYING) break;
  }
  CHECK(sawFragment);
  CHECK(deaths > 0);
  CHECK(f.debugStats().crits > 0);  // ~1 in 30 hits knocks a fragment out
  // Upgrades are never lost: carried + floating + taken == assigned
  {
    int carried = 0, floating = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) {
      if (f.entities[i].alive &&
          f.entities[i].upgrade != (uint8_t)UpgradeKind::NONE) {
        carried++;
      }
    }
    for (int i = 0; i < MAX_FLOATING_UPGRADES; i++)
      floating += f.floatingUpgrades[i].alive;
    int taken = f.totalUpgradeLevel() + (f.cores() - CORES_START);
    int total = carried + floating + taken;
    // Losing a core costs the player a core and a level of every upgrade,
    // so the sum only balances while the player has not respawned. Whether
    // that happens in 4000 ticks depends on the trajectory, which any change
    // to the arithmetic can move.
    if (respawns == 0) {
      CHECK(total == 3 || total == 4);
    } else {
      CHECK(carried + floating <= 4);
      CHECK(total + respawns >= 3 - respawns * UPGRADE_KINDS);
    }
  }

  // Kill the player to reach the DEAD state, then restart
  Game h;
  h.reset(4242);
  enterPlay(h);
  Entity &hp = h.entities[h.playerIndex()];
  hp.invincible = 0;
  hp.hp = 1;
  hp.alive = false;  // simulate death
  h.tick(0);
  // One spare core: after watching the wreck the player respawns smaller
  // with a fresh gauge
  CHECK(h.state() == GameState::PLAYING);
  CHECK(!h.player().alive);
  CHECK(h.respawnDelay() > 0);
  for (int i = 0; i < RESPAWN_DELAY_TICKS + 1; i++) h.tick(0);
  CHECK(h.player().alive);
  CHECK(h.cores() == 0);
  CHECK(h.player().hp == h.player().hpMax);
  // Second death: game over
  h.entities[h.playerIndex()].alive = false;
  h.tick(0);
  CHECK(h.state() == GameState::DEAD);
  for (int i = 0; i < 2 * TICK_RATE + 10; i++) h.tick(0);
  h.tick(Button::A);
  CHECK(h.state() == GameState::TITLE);

  // Sphere transition: make the player the largest
  Game q;
  q.reset(99);
  enterPlay(q);
  Entity &qp = q.entities[q.playerIndex()];
  for (int i = 0; i < 12; i++)
    qp.fragments[0].sizeLog2 = 18, qp.size = 1u << 18;
  qp.hpMax = HP_PER_SIZE * (int32_t)qp.size;
  qp.hp = qp.hpMax;
  for (int i = 0; i < 4 * TICK_RATE + 5; i++) q.tick(0);
  CHECK(q.state() == GameState::LAUNCH);
  const int32_t rLaunch = q.player().r;
  while (q.state() == GameState::LAUNCH) q.tick(0);
  CHECK(q.state() == GameState::ARRIVE);
  CHECK(q.player().r > rLaunch + LAUNCH_ALTITUDE / 2);  // climbed away
  CHECK(q.sphereLevel() == 1);  // the sphere is switched midway
  while (q.state() == GameState::ARRIVE && q.sphereLevel() == 1) q.tick(0);
  CHECK(q.state() == GameState::ARRIVE);
  CHECK(q.stateTimer() == ARRIVE_SWITCH_TICKS);
  CHECK(q.sphereLevel() == 2);
  CHECK(q.player().r == SPHERE_RADIUS + ALTITUDE + ARRIVE_ALTITUDE);
  CHECK(q.player().invincible == 0);  // frozen instead, until it lands
  while (q.state() == GameState::ARRIVE) q.tick(0);
  CHECK(q.state() == GameState::PLAYING);
  CHECK(q.player().invincible > 0);
  CHECK(q.player().r < SPHERE_RADIUS + ALTITUDE + 8 * FU);  // landed
  CHECK(q.player().bank == 0);  // the roll ended where it began
  CHECK(q.sphereLevel() == 2);
  CHECK(q.score() >= 2000);  // clear bonus on sphere 1
  CHECK(q.player().size < 64);
  CHECK(q.playerDisplayScaleLog2() > 0);
  checkInvariants(q);
}

// An equal-sized enemy straight ahead must die from sustained vulcan fire,
// and the fragments of an entity must come to rest after a while
static void testCombatAndLayout() {
  Game g;
  g.reset(2024);
  g.debugStartSphere(1, 0);
  Entity &p = g.entities[g.playerIndex()];
  int enemy = -1;
  for (int i = 1; i < MAX_ENTITIES; i++) {
    if (g.entities[i].alive) {
      enemy = i;
      break;
    }
  }
  CHECK(enemy > 0);
  Entity &e = g.entities[enemy];
  // Same size and heading, 20 FU ahead of the player, both stopped
  e.size = p.size;
  e.hpMax = HP_PER_SIZE * (int32_t)e.size;
  e.hp = e.hpMax;
  e.fragmentCount = p.fragmentCount;
  for (int k = 0; k < p.fragmentCount; k++) e.fragments[k] = p.fragments[k];
  e.frame.t = p.frame.t;
  e.frame.n = normalizeQ30(
      p.frame.n +
      scaleQ30(p.frame.t, (20 * FU) << (Q30_SHIFT - SPHERE_RADIUS_SHIFT)));
  e.frame.t = orthonormalizeQ30(e.frame.t, e.frame.n);
  e.r = p.r;
  e.invincible = 0;
  int32_t hp0 = e.hp;
  bool died = false;
  for (int t = 0; t < 12 * TICK_RATE && !died; t++) {
    // keep both entities still by braking (the enemy is not AI driven
    // while its think tick is skipped: force its controls every tick)
    g.entities[enemy].braking = true;
    g.entities[enemy].turn = 0;
    g.entities[enemy].firing = false;
    g.tick(Button::DOWN | Button::A);
    died = !g.entities[enemy].alive;
  }
  CHECK(g.debugStats().hits > 0);
  CHECK(died || g.entities[enemy].hp < hp0 * 3 / 4);
  if (died) CHECK(g.score() > 0);  // a kill scores (ratio depends on growth)

  // Upgrades: three (sometimes four) enemies carry one at the start; a
  // floating one raises the level when the player touches it
  {
    Game u;
    u.reset(8);
    u.debugStartSphere(1, 0);
    int carriers = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) {
      if (u.entities[i].alive &&
          u.entities[i].upgrade != (uint8_t)UpgradeKind::NONE)
        carriers++;
    }
    CHECK(carriers == 3 || carriers == 4);
    CHECK(u.cores() == CORES_START);
    CHECK(u.upgradeLevel(UpgradeKind::SHIELD) == 0);
    const Entity &p = u.player();
    u.floatingUpgrades[0] = {
        true, (uint8_t)UpgradeKind::SHIELD, p.frame.n, p.r, {0, 0, 0}, 0};
    u.tick(Button::DOWN);
    CHECK(u.upgradeLevel(UpgradeKind::SHIELD) == 1);
    CHECK(!u.floatingUpgrades[0].alive);
    // Extra cores cap at CORES_MAX; beyond that the pickup scores
    for (int n = 0; n < 4; n++) {
      u.floatingUpgrades[0] = {true,
                               (uint8_t)UpgradeKind::EXTRA_CORE,
                               u.player().frame.n,
                               u.player().r,
                               {0, 0, 0},
                               0};
      u.tick(Button::DOWN);
    }
    CHECK(u.cores() == CORES_MAX);
    CHECK(u.score() > 0);
  }

  // Contact: size flows gradually from the smaller entity to the bigger one
  {
    Game b;
    b.reset(77);
    b.debugStartSphere(1, 0);
    Entity &p = b.entities[b.playerIndex()];
    int other = -1;
    for (int i = 1; i < MAX_ENTITIES; i++) {
      if (b.entities[i].alive) {
        other = i;
        break;
      }
    }
    Entity &o = b.entities[other];
    o.fragmentCount = p.fragmentCount;
    for (int k = 0; k < p.fragmentCount; k++) o.fragments[k] = p.fragments[k];
    o.fragments[0].sizeLog2 += 3;  // clearly bigger
    o.size = 0;
    for (int k = 0; k < o.fragmentCount; k++)
      o.size += 1u << o.fragments[k].sizeLog2;
    o.hpMax = HP_PER_SIZE * (int32_t)o.size;
    o.hp = o.hpMax;
    o.frame = p.frame;
    o.r = p.r;
    o.invincible = 0;
    p.invincible = 0;
    p.absorbGuard = 0;
    uint32_t p0 = p.size, o0 = o.size;
    for (int t = 0; t < 8; t++) {
      b.entities[other].frame = b.player().frame;
      b.entities[other].r = b.player().r;
      b.tick(Button::DOWN);
    }
    // gradual: the player is still alive after a few ticks, but smaller
    CHECK(b.player().alive);
    CHECK(b.player().size < p0);
    CHECK(b.entities[other].size > o0);
    CHECK(b.player().size + b.entities[other].size == p0 + o0);
    for (int t = 0; t < 10 * TICK_RATE && b.player().alive; t++) {
      b.entities[other].frame = b.player().frame;
      b.entities[other].r = b.player().r;
      b.tick(Button::DOWN);
    }
    CHECK(!b.player().alive);  // eventually absorbed
  }

  // Absorption drains health too: every ABSORB_HP_INTERVAL ticks the smaller
  // one loses ABSORB_HP_PCT % of its gauge (on top of the size flowing out,
  // which keeps its ratio) and the bigger one heals by the same amount
  {
    Game b;
    b.reset(77);
    b.debugStartSphere(1, 0);
    Entity &p = b.entities[b.playerIndex()];
    int other = -1;
    for (int i = 1; i < MAX_ENTITIES && other < 0; i++)
      if (b.entities[i].alive) other = i;
    Entity &o = b.entities[other];
    o.fragmentCount = p.fragmentCount;
    for (int k = 0; k < p.fragmentCount; k++) o.fragments[k] = p.fragments[k];
    o.fragments[0].sizeLog2 += 3;  // clearly bigger
    o.size = 0;
    for (int k = 0; k < o.fragmentCount; k++)
      o.size += 1u << o.fragments[k].sizeLog2;
    o.hpMax = HP_PER_SIZE * (int32_t)o.size;
    o.hp = o.hpMax / 2;  // room to heal
    o.frame = p.frame;
    o.r = p.r;
    o.invincible = 0;
    p.invincible = 0;
    p.absorbGuard = 0;
    p.hp = p.hpMax;
    int64_t drained = 0, gained = 0, sizeHeal = 0;
    // A third of a second, not a fixed tick count: at 30 Hz twice as much
    // game time would pass and the player would be absorbed outright
    for (int t = 0; t < ticks30(10) && b.player().alive; t++) {
      b.entities[other].frame = b.player().frame;
      b.entities[other].r = b.player().r;
      int32_t sHp = p.hp, sMax = p.hpMax, bHp = o.hp, bMax = o.hpMax;
      uint32_t bSize = o.size;
      b.tick(Button::DOWN);
      if (!p.alive) break;
      // health kept its ratio through the size change; anything beyond is
      // the drain / the heal (size growth also heals HP_PER_SIZE per unit)
      drained += (int64_t)sHp * p.hpMax / sMax - p.hp;
      gained += p.alive ? o.hp - (int64_t)bHp * o.hpMax / bMax : 0;
      sizeHeal +=
          (int64_t)HP_PER_SIZE * HEAL_PER_FRAGMENT_MUL * (o.size - bSize);
    }
    CHECK(drained > 0);
    CHECK(b.player().hp < b.player().hpMax);  // the ratio dropped
    int64_t diff = gained - sizeHeal - drained;
    if (diff < 0) diff = -diff;
    CHECK(diff <= 40);  // rounding of the per-tick rescaling
  }

  // A fragment left far behind the core is pulled back into the body
  {
    Game b;
    b.reset(5);
    b.debugStartSphere(1, 0);
    Entity &c = b.entities[b.playerIndex()];
    c.fragments[0].x = 2 * FU;
    c.fragments[0].y = -60 * FU;
    for (int t = 0; t < 4 * TICK_RATE; t++) b.tick(0);
    CHECK(std::abs(b.player().fragments[0].y) < 8 * FU);
  }

  // Layout settles: after 5 seconds without eating, fragment velocities are 0
  Game h;
  h.reset(99);
  h.debugStartSphere(1, 0);
  for (int t = 0; t < 5 * TICK_RATE; t++) h.tick(0);
  const Entity &q = h.player();
  for (int k = 0; k < q.fragmentCount; k++)
    CHECK(q.fragments[k].vx == 0 && q.fragments[k].vy == 0);
}

// A bullet resting on `target`, fired by `owner`, that hits on the next tick
static void plantBullet(Game &g, int slot, int owner, int target, int32_t power,
                        bool fromPlayer) {
  const Entity &o = g.entities[owner];
  const Entity &t = g.entities[target];
  Bullet &b = g.bullets[slot];
  b = {};
  b.alive = true;
  b.frame = t.frame;
  b.prevN = t.frame.n;
  b.r = t.r;
  b.speed = 1;
  b.life = 2;
  b.owner = (int16_t)owner;
  b.kind = Weapon::VULCAN;
  b.ownerSize = o.size;
  b.power = power;
  b.target = -1;
  b.fromPlayer = fromPlayer;
}

static int firstEnemy(const Game &g, uint32_t minSize) {
  for (int i = 0; i < MAX_ENTITIES; i++) {
    if (i != g.playerIndex() && g.entities[i].alive &&
        g.entities[i].size >= minSize)
      return i;
  }
  return -1;
}

// Largest health drop of `victim` over a few planted hits. A critical hit
// takes no health but shrinks the body (and the gauge with it), so ticks on
// which hpMax changed are ignored; the maximum is the plain damage of one hit
static int32_t maxHitDrop(Game &g, int shooter, int victim, int32_t power,
                          bool fromPlayer) {
  int32_t maxDrop = 0;
  for (int n = 0; n < 6; n++) {
    g.entities[victim].invincible = 0;
    g.entities[victim].hp = g.entities[victim].hpMax;  // never dies here
    plantBullet(g, n, shooter, victim, power, fromPlayer);
    int32_t hp0 = g.entities[victim].hp, hpMax0 = g.entities[victim].hpMax;
    g.tick(Button::DOWN);
    if (g.entities[victim].hpMax != hpMax0) continue;
    int32_t drop = hp0 - g.entities[victim].hp;
    if (drop > maxDrop) maxDrop = drop;
  }
  return maxDrop;
}

// Enemy fire hurts the player more on higher spheres, the player's own fire
// never scales, the shield is applied before the per-hit cap, and an enemy
// under fire breaks out of the line of fire with a quick turn and a dash
static void testDifficultyAndEvade() {
  // Enemy bullet damage: x (100 + 40 * 3) % on sphere 4 against the player
  int32_t playerDrop[2] = {0, 0}, enemyDrop[2] = {0, 0};
  for (int k = 0; k < 2; k++) {
    int level = k == 0 ? 1 : 4;
    Game g;
    g.reset(31);
    g.debugStartSphere(level, 0);
    int e = firstEnemy(g, 8);
    CHECK(e > 0);
    playerDrop[k] = maxHitDrop(g, e, g.playerIndex(), 8, false);
    enemyDrop[k] = maxHitDrop(g, g.playerIndex(), e, 8, true);
  }
  CHECK(playerDrop[0] == 8);
  CHECK(playerDrop[1] == 8 * (100 + 3 * DIFF_DAMAGE_PCT_PER_SPHERE) / 100);
  CHECK(enemyDrop[0] == 8);
  CHECK(enemyDrop[1] == 8);

  // Shield before the cap: with Shield Lv.3 (50 %) and three upgrade levels
  // (x 130 %), a 40 point hit takes 40 * 1.3 * 0.5 = 26, below the 30 % cap;
  // capping first would leave only 19
  {
    Game g;
    g.reset(31);
    g.debugStartSphere(1, 0);
    for (int n = 0; n < UPGRADE_MAX_LEVEL; n++) {
      const Entity &p = g.player();
      g.floatingUpgrades[0] = {
          true, (uint8_t)UpgradeKind::SHIELD, p.frame.n, p.r, {0, 0, 0}, 0};
      g.tick(Button::DOWN);
    }
    CHECK(g.upgradeLevel(UpgradeKind::SHIELD) == UPGRADE_MAX_LEVEL);
    int e = firstEnemy(g, 8);
    int32_t cap = g.player().hpMax * PLAYER_MAX_HIT_PERCENT / 100;
    CHECK(cap > 26);
    CHECK(maxHitDrop(g, e, g.playerIndex(), 40, false) == 26);
    // ... and the cap still bounds a huge hit after the shield
    cap = g.player().hpMax * PLAYER_MAX_HIT_PERCENT / 100;
    CHECK(maxHitDrop(g, e, g.playerIndex(), 100000, false) == cap);
  }

  // Evasion: an equal enemy straight ahead under vulcan fire starts evading
  // after two quick hits and remembers the shooter. Depending on the roll it
  // either breaks off (dashes and leaves the line of fire sideways) or
  // counterattacks (turns on the player and fires back); over a handful of
  // seeds both must occur. The scene around the pair is whatever the seed
  // spawned, so on some seeds the enemy wanders off (a fragment to eat, a
  // bigger neighbor) before it is hit twice; a couple of those are allowed.
  {
    int broke = 0, countered = 0, notEvaded = 0;
    for (uint32_t seed = 2024; seed < 2032; seed++) {
      Game g;
      g.reset(seed);
      g.debugStartSphere(1, 0);
      Entity &p = g.entities[g.playerIndex()];
      int enemy = firstEnemy(g, 1);
      Entity &e = g.entities[enemy];
      e.size = p.size;
      e.hpMax = HP_PER_SIZE * (int32_t)e.size;
      e.hp = e.hpMax;
      e.fragmentCount = p.fragmentCount;
      for (int k = 0; k < p.fragmentCount; k++) e.fragments[k] = p.fragments[k];
      e.frame.t = p.frame.t;
      e.frame.n = normalizeQ30(
          p.frame.n +
          scaleQ30(p.frame.t, (20 * FU) << (Q30_SHIFT - SPHERE_RADIUS_SHIFT)));
      e.frame.t = orthonormalizeQ30(e.frame.t, e.frame.n);
      e.r = p.r;
      e.invincible = 0;
      bool evaded = false, dashed = false, fromPlayer = false;
      bool counterMode = false, firedBack = false;
      int32_t lateralMax = 0;
      int after = 0;
      for (int t = 0; t < 8 * TICK_RATE && g.entities[enemy].alive; t++) {
        g.tick(Button::DOWN | Button::A);
        const Entity &c = g.entities[enemy];
        const Entity &q = g.player();
        if (c.evadeTicks > 0) {
          evaded = true;
          if (c.evadeFrom == g.playerIndex()) fromPlayer = true;
          if (c.evadeMode == (uint8_t)EvadeMode::COUNTER) counterMode = true;
        }
        if (evaded) {
          if (c.dashing) dashed = true;
          for (const Bullet &b : g.bullets)
            if (b.alive && b.owner == enemy) firedBack = true;
          Vec3 rel =
              scaleToLength(c.frame.n, c.r) - scaleToLength(q.frame.n, q.r);
          int32_t lateral = dotQ30(rel, q.frame.right());
          if (lateral < 0) lateral = -lateral;
          if (lateral > lateralMax) lateralMax = lateral;
          if (++after > 3 * TICK_RATE) break;
        }
      }
      if (!evaded) {
        notEvaded++;
        continue;
      }
      CHECK(fromPlayer);
      if (counterMode) {
        CHECK(firedBack);
        countered++;
      } else {
        CHECK(dashed);
        CHECK(lateralMax > 8 * FU);
        broke++;
      }
    }
    CHECK(notEvaded <= 2);
    CHECK(broke >= 1);
    CHECK(countered >= 1);
  }
}

// The renderer must work at every frame buffer size the front ends allow:
// the HUD metrics stay inside the screen, something is actually drawn, and
// nothing is written outside the frame buffer.
static void testRenderSizes() {
  namespace g2 = shapoco::gfx2d;
  static uint8_t arena[256 * 1024];
  static devoursphere::render::Renderer renderer;
  static Game game;
  struct Size {
    int w, h;
  };
  const Size sizes[] = {{128, 128}, {160, 160},  {240, 240}, {320, 240},
                        {480, 320}, {640, 360},  {640, 480}, {1280, 720},
                        {64, 480},  {720, 1280}, {300, 200}};
  constexpr uint16_t GUARD = 0xA5C3;
  for (const Size &sz : sizes) {
    const size_t pixels = (size_t)sz.w * sz.h;
    std::vector<uint16_t> fb(pixels + 16, GUARD);
    const g2::Surface surf = {g2::PixelFormat::RGB565BE, (int16_t)sz.w,
                              (int16_t)sz.h, (uint32_t)(sz.w * 2), fb.data()};
    renderer.init(sz.w, sz.h, arena, sizeof(arena));
    const devoursphere::render::UiMetrics &m = renderer.uiMetrics();
    CHECK(m.scale8 >= 2 && m.scale8 <= 32);
    CHECK(m.fontMult >= 1);
    CHECK(m.margin >= 1);
    CHECK(m.gaugeH >= 1);
    CHECK(m.margin * 2 + m.gaugeW <= sz.w);  // the gauge stays on screen
    CHECK(m.wireLines >= 220 && m.wireLines <= 1100);

    // Title, weapon select and playing, each rendered once
    game.reset(4242);
    for (int state = 0; state < 3; state++) {
      if (state == 1) {
        for (int i = 0; i < 40 && game.state() != GameState::WEAPON_SELECT; i++)
          game.tick(i % 2 ? Button::A : 0);
      } else if (state == 2) {
        game.debugStartSphere(3, 0);
        game.debugAutoPlayer(true);
        for (int i = 0; i < 400; i++) game.tick(0);
      } else {
        for (int i = 0; i < 60; i++) game.tick(0);
      }
      std::fill(fb.begin(), fb.end(), GUARD);
      renderer.beginFrame(game, 1.0f / 60);
      renderer.renderBand(surf, 0, sz.h, 0);
      renderer.endFrame();
      size_t lit = 0;
      for (size_t i = 0; i < pixels; i++)
        if (fb[i] != 0 && fb[i] != GUARD) lit++;
      CHECK(lit > pixels / 200);  // the scene reaches the frame buffer
      for (size_t i = pixels; i < fb.size(); i++)
        CHECK(fb[i] == GUARD);  // nothing drawn past the end
    }
  }
}

// A device without a frame buffer draws the frame a band at a time, so a band
// must come out exactly as the same rows of a frame drawn in one go --
// including a last band shorter than the rest.
static void testRenderBands() {
  namespace g2 = shapoco::gfx2d;
  static uint8_t arena[256 * 1024];
  static devoursphere::render::Renderer renderer;
  static Game game;
  constexpr int W = 240, H = 240;
  static std::vector<uint16_t> whole(W * H), banded(W * H), band(W * H);

  renderer.init(W, H, arena, sizeof(arena));
  const g2::Surface full = {g2::PixelFormat::RGB565BE, W, H, W * 2,
                            whole.data()};

  game.reset(4242);
  for (int state = 0; state < 3; state++) {
    if (state == 1) {
      for (int i = 0; i < 40 && game.state() != GameState::WEAPON_SELECT; i++)
        game.tick(i % 2 ? Button::A : 0);
    } else if (state == 2) {
      game.debugStartSphere(3, 0);
      game.debugAutoPlayer(true);
      for (int i = 0; i < 400; i++) {
        game.tick(0);
        renderer.pollEffects(game);
      }
    } else {
      for (int i = 0; i < 60; i++) game.tick(0);
    }
    // 40 is what the Xiamocon front end uses; 7 leaves a short last band
    for (int bandH : {40, 20, 7}) {
      std::fill(whole.begin(), whole.end(), 0);
      std::fill(banded.begin(), banded.end(), 0);
      // Both drawings belong to the same frame: renderBand() may be called
      // any number of times between beginFrame() and endFrame()
      renderer.beginFrame(game, 1.0f / TICK_RATE);
      renderer.renderBand(full, 0, H, 0);
      for (int y = 0; y < H; y += bandH) {
        const int h = (y + bandH <= H) ? bandH : (H - y);
        const g2::Surface b = {g2::PixelFormat::RGB565BE, W, (int16_t)h, W * 2,
                               band.data()};
        renderer.renderBand(b, y, h, 0);
        std::memcpy(banded.data() + (size_t)y * W, band.data(),
                    (size_t)W * h * 2);
      }
      renderer.endFrame();
      // Guard against both drawings being empty, which would match trivially
      CHECK(std::count(whole.begin(), whole.end(), 0) <
            (ptrdiff_t)whole.size() * 99 / 100);
      CHECK(whole == banded);
    }
  }
}

int main() {
  testFixed();
  testRenderSizes();
  testRenderBands();
  testCombatAndLayout();
  testDifficultyAndEvade();
  testDeterminism();
  testGameplay();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
