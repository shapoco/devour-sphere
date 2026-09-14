// Self-checking tests of the simulation: fixed-point math, determinism and
// invariants after thousands of ticks of random input.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

  CHECK(partHalfSize(0) == PU / 2);
  CHECK(partHalfSize(2) == PU);
  CHECK(std::abs(partHalfSize(1) - 181) <= 1);
  CHECK(canAttack(100, 67) && canAttack(100, 150) && !canAttack(100, 66) &&
        !canAttack(100, 151));
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

static void enterPlay(Game &g) {
  g.tick(0);
  g.tick(Button::A);  // title -> weapon select
  g.tick(0);
  g.tick(Button::RIGHT);
  g.tick(0);
  g.tick(Button::A);  // -> playing
  CHECK(g.state() == GameState::PLAYING);
  CHECK(g.selectedWeapon() == 1);
}

static void checkInvariants(const Game &g) {
  for (int i = 0; i < MAX_CREATURES; i++) {
    const Creature &c = g.creatures[i];
    if (!c.alive) continue;
    CHECK(c.partCount >= 1 && c.partCount <= MAX_PARTS_PER_CREATURE);
    CHECK(c.size >= 1);
    CHECK(c.hp >= 0 && c.hp <= c.hpMax);
    CHECK(c.r > PLANET_RADIUS);
    CHECK(std::llabs(dot64(c.frame.n, c.frame.n) - ((int64_t)1 << 60)) <
          ((int64_t)1 << 40));
    CHECK(std::llabs(dot64(c.frame.t, c.frame.t) - ((int64_t)1 << 60)) <
          ((int64_t)1 << 40));
    CHECK(std::abs(dotQ30(c.frame.n, c.frame.t)) < (1 << 12));
    for (int k = 0; k < c.partCount; k++) {
      CHECK(c.parts[k].x >= 0);
      CHECK(c.parts[k].sizeLog2 <= MAX_SIZE_LOG2);
      CHECK(std::abs(c.parts[k].x) < 4096 * PU &&
            std::abs(c.parts[k].y) < 4096 * PU);
    }
    CHECK(c.bodyRadius > 0 && c.bodyRadius < 4096 * PU);
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
  CHECK(std::memcmp(a.creatures, b.creatures, sizeof(a.creatures)) == 0);
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
  g.reset(777);
  enterPlay(g);
  const Creature &p = g.player();
  CHECK(p.alive && p.isPlayer);
  CHECK(p.size == (1u << PLAYER_START_SIZE_LOG2));
  int alive = 0;
  for (int i = 0; i < MAX_CREATURES; i++) alive += g.creatures[i].alive;
  CHECK(alive == INITIAL_CREATURES);

  bool sawBullet = false, sawPart = false, sawParticle = false;
  for (int i = 0; i < 6000; i++) {
    g.tick(scriptedInput(100 + i));
    for (int k = 0; k < MAX_BULLETS; k++) sawBullet |= g.bullets[k].alive;
    for (int k = 0; k < MAX_FLOATING_PARTS; k++)
      sawPart |= g.floatingParts[k].alive;
    for (int k = 0; k < MAX_PARTICLES; k++) sawParticle |= g.particles[k].alive;
    if ((i % 500) == 0) checkInvariants(g);
    if (g.state() == GameState::DEAD) break;
  }
  checkInvariants(g);
  CHECK(sawBullet);
  CHECK(g.playerRank() >= 1 && g.playerRank() <= MAX_CREATURES);

  // On a high level planet the enemies fight each other: parts and energy
  // particles must appear
  Game f;
  f.reset(31337);
  f.debugStartPlanet(4, 0);
  CHECK(f.planetLevel() == 4);
  sawPart = sawParticle = false;
  int deaths = 0;
  for (int i = 0; i < 4000; i++) {
    int before = f.aliveCreatures();
    f.tick(scriptedInput(900 + i));
    if (f.aliveCreatures() < before) deaths++;
    for (int k = 0; k < MAX_FLOATING_PARTS; k++)
      sawPart |= f.floatingParts[k].alive;
    for (int k = 0; k < MAX_PARTICLES; k++) sawParticle |= f.particles[k].alive;
    if ((i % 500) == 0) checkInvariants(f);
    if (f.state() != GameState::PLAYING) break;
  }
  CHECK(sawPart);
  CHECK(sawParticle);
  CHECK(deaths > 0);

  // Kill the player to reach the DEAD state, then restart
  Game h;
  h.reset(4242);
  enterPlay(h);
  Creature &hp = h.creatures[h.playerIndex()];
  hp.invincible = 0;
  hp.hp = 1;
  hp.alive = false;  // simulate death
  h.tick(0);
  CHECK(h.state() == GameState::DEAD);
  for (int i = 0; i < 100; i++) h.tick(0);
  h.tick(Button::A);
  CHECK(h.state() == GameState::TITLE);

  // Planet transition: make the player the largest
  Game q;
  q.reset(99);
  enterPlay(q);
  Creature &qp = q.creatures[q.playerIndex()];
  for (int i = 0; i < 12; i++) qp.parts[0].sizeLog2 = 18, qp.size = 1u << 18;
  qp.hpMax = HP_PER_SIZE * (int32_t)qp.size;
  qp.hp = qp.hpMax;
  for (int i = 0; i < 4 * TICK_RATE + 5; i++) q.tick(0);
  CHECK(q.state() == GameState::LAUNCH);
  for (int i = 0; i < 3 * TICK_RATE + 5; i++) q.tick(0);
  CHECK(q.state() == GameState::PLAYING);
  CHECK(q.planetLevel() == 2);
  CHECK(q.player().size < 64);
  CHECK(q.playerDisplayScaleLog2() > 0);
  checkInvariants(q);
}

// An equal-sized enemy straight ahead must die from sustained vulcan fire,
// and the parts of a creature must come to rest after a while
static void testCombatAndLayout() {
  Game g;
  g.reset(2024);
  g.debugStartPlanet(1, 0);
  Creature &p = g.creatures[g.playerIndex()];
  int enemy = -1;
  for (int i = 1; i < MAX_CREATURES; i++) {
    if (g.creatures[i].alive) {
      enemy = i;
      break;
    }
  }
  CHECK(enemy > 0);
  Creature &e = g.creatures[enemy];
  // Same size and heading, 20 PU ahead of the player, both stopped
  e.size = p.size;
  e.hpMax = HP_PER_SIZE * (int32_t)e.size;
  e.hp = e.hpMax;
  e.partCount = p.partCount;
  for (int k = 0; k < p.partCount; k++) e.parts[k] = p.parts[k];
  e.frame.t = p.frame.t;
  e.frame.n = normalizeQ30(
      p.frame.n +
      scaleQ30(p.frame.t, (20 * PU) << (Q30_SHIFT - PLANET_RADIUS_SHIFT)));
  e.frame.t = orthonormalizeQ30(e.frame.t, e.frame.n);
  e.r = p.r;
  e.invincible = 0;
  int32_t hp0 = e.hp;
  bool died = false;
  for (int t = 0; t < 6 * TICK_RATE && !died; t++) {
    // keep both creatures still by braking (the enemy is not AI driven
    // while its think tick is skipped: force its controls every tick)
    g.creatures[enemy].braking = true;
    g.creatures[enemy].turn = 0;
    g.creatures[enemy].firing = false;
    g.tick(Button::DOWN | Button::A);
    died = !g.creatures[enemy].alive;
  }
  CHECK(g.debugStats().hits > 0);
  CHECK(died || g.creatures[enemy].hp < hp0 / 2);

  // A part left far behind the core is pulled back into the body
  {
    Game b;
    b.reset(5);
    b.debugStartPlanet(1, 0);
    Creature &c = b.creatures[b.playerIndex()];
    c.parts[0].x = 2 * PU;
    c.parts[0].y = -60 * PU;
    for (int t = 0; t < 4 * TICK_RATE; t++) b.tick(0);
    CHECK(std::abs(b.player().parts[0].y) < 8 * PU);
  }

  // Layout settles: after 5 seconds without eating, part velocities are 0
  Game h;
  h.reset(99);
  h.debugStartPlanet(1, 0);
  for (int t = 0; t < 5 * TICK_RATE; t++) h.tick(0);
  const Creature &q = h.player();
  for (int k = 0; k < q.partCount; k++)
    CHECK(q.parts[k].vx == 0 && q.parts[k].vy == 0);
}

int main() {
  testFixed();
  testCombatAndLayout();
  testDeterminism();
  testGameplay();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
