// Balancing / performance run: an AI-driven player on a sphere of the given
// level. Prints the player's progress and the global statistics over time,
// then one SUMMARY line for scripts (see core/tools/bench_levels.sh).
//
// Usage: devoursphere_sim_bench [level] [seconds] [seed] [-q]
//        devoursphere_sim_bench ttk      (the analytic damage table)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "devoursphere/sim/game.hpp"

using namespace devoursphere::sim;

// How many seconds an idle player at full health survives one enemy of the
// given size ratio firing the given weapon without a miss, on sphere `level`
// with `upgrades` total upgrade levels (no shield). Mirrors the rules of
// damageEntity() and the AI's fire chance; a quick sanity table for tuning.
static void printTtk() {
  std::printf("seconds to kill a full-health player, every shot hits\n");
  std::printf("level  fire%%  dmg%%  | vulcan x1  x1.5  x2  x4 | laser x1  x2\n");
  for (int level = 1; level <= 10; level++) {
    int lv = level - 1;
    int fi = lv < AI_TIER_LEVELS ? lv : AI_TIER_LEVELS - 1;
    double chance = AI_TIERS[fi].fireChance / 256.0;
    int64_t spherePct = 100 + (int64_t)DIFF_DAMAGE_PCT_PER_SPHERE * lv;
    if (spherePct > DIFF_DAMAGE_PCT_SPHERE_MAX) spherePct = DIFF_DAMAGE_PCT_SPHERE_MAX;
    double mult = spherePct / 100.0;
    std::printf("%5d  %4.0f  %4.0f  |", level, chance * 100, mult * 100);
    const double ratios[] = {1, 1.5, 2, 4};
    for (int w = 0; w < 2; w++) {
      const WeaponSpec &ws = WEAPON_SPECS[w == 0 ? 0 : 1];
      double shotsPerSec = (double)TICK_RATE / ws.cooldown * chance;
      double mercyCap = (double)TICK_RATE / (PLAYER_MERCY_TICKS + 1);
      if (shotsPerSec > mercyCap) shotsPerSec = mercyCap;
      for (int k = 0; k < (w == 0 ? 4 : 2); k++) {
        double r = ratios[w == 0 ? k : k * 2];
        double rmax = AI_TIERS[fi].hitSizeRatioMax;
        double rc = r > rmax ? rmax : r;
        double frac = ws.powerPerSize * rc / 8.0 / HP_PER_SIZE * mult;
        if (frac > PLAYER_MAX_HIT_PERCENT / 100.0) frac = PLAYER_MAX_HIT_PERCENT / 100.0;
        std::printf(" %5.1f", 1.0 / (frac * shotsPerSec));
      }
      std::printf(" |");
    }
    std::printf("\n");
  }
}

int main(int argc, char **argv) {
  if (argc > 1 && std::strcmp(argv[1], "ttk") == 0) {
    printTtk();
    return 0;
  }
  int level = argc > 1 ? std::atoi(argv[1]) : 1;
  int seconds = argc > 2 ? std::atoi(argv[2]) : 180;
  uint32_t seed = argc > 3 ? (uint32_t)std::atoi(argv[3]) : 7u;
  bool quiet = argc > 4 && std::strcmp(argv[4], "-q") == 0;
  Game g;
  g.reset(seed);
  g.debugStartSphere(level, 0);
  g.debugAutoPlayer(true);

  int ticks = seconds * TICK_RATE;
  double total = 0;
  int deaths = 0, clears = 0;
  int aliveTicks = 0;
  // Enemies hunting the player, sampled every second, by size ratio
  double huntersSmall = 0, huntersEqual = 0, huntersBig = 0;
  int samples = 0;
  uint32_t sizeMax = 0;
  int lastLog2 = -1;
  char growth[256] = "";
  int t = 0;
  for (; t < ticks; t++) {
    auto t0 = std::chrono::steady_clock::now();
    g.tick(0);
    total += std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - t0)
                 .count();
    const Entity &p = g.player();
    if (g.events() & Event::PLAYER_DIED) deaths++;
    if (g.events() & Event::SPHERE_CLEARED) clears++;
    if (g.state() == GameState::PLAYING && p.alive) {
      aliveTicks++;
      if (p.size > sizeMax) sizeMax = p.size;
      int k = log2Floor(p.size);
      if (k > lastLog2) {
        lastLog2 = k;
        char b[16];
        std::snprintf(b, sizeof(b), " %d@%ds", k, t / TICK_RATE);
        std::strncat(growth, b, sizeof(growth) - std::strlen(growth) - 1);
      }
      if (t % TICK_RATE == 0) {
        samples++;
        for (int i = 0; i < MAX_ENTITIES; i++) {
          const Entity &c = g.entities[i];
          if (!c.alive || c.isPlayer) continue;
          if (c.aiMode != AiMode::HUNT_ENTITY || c.aiTarget != g.playerIndex())
            continue;
          if (c.size * 2 < p.size) huntersSmall += 1;
          else if (c.size <= p.size * 2) huntersEqual += 1;
          else huntersBig += 1;
        }
      }
    }
    if (g.state() == GameState::DEAD) {
      if (!quiet)
        std::printf("t=%3ds player died (rank %d, %s)\n", t / TICK_RATE,
                    g.playerRank(),
                    g.player().hp <= 0 ? "shot down" : "absorbed");
      break;
    }
    if (!quiet && t % (10 * TICK_RATE) == 0) {
      const DebugStats &st = g.debugStats();
      int fragments = 0, bullets = 0;
      for (const auto &fp : g.floatingFragments) fragments += fp.alive;
      for (const auto &b : g.bullets) bullets += b.alive;
      uint32_t maxSize = 0;
      for (const auto &c : g.entities)
        if (c.alive && c.size > maxSize) maxSize = c.size;
      std::printf(
          "t=%3ds lv=%d size=%u (x2^%u) rank=%d/%d hp=%d%% fragments=%d max=%u "
          "| "
          "floating=%d bullets=%d | shots=%u hits=%u kills=%u "
          "absorbs=%u eaten=%u\n",
          t / TICK_RATE, g.sphereLevel(), p.size, g.playerDisplayScaleLog2(),
          p.rank, g.aliveEntities(),
          (int)((int64_t)p.hp * 100 / (p.hpMax ? p.hpMax : 1)), p.fragmentCount,
          maxSize, fragments, bullets, st.shots, st.hits, st.kills, st.absorbs,
          st.fragmentsEaten);
    }
  }
  const DebugStats &st = g.debugStats();
  double aliveSec = (double)aliveTicks / TICK_RATE;
  if (samples == 0) samples = 1;
  std::printf(
      "SUMMARY lv=%d seed=%u alive=%.0fs deaths=%d clears=%d sizeMax=%u "
      "hits=%u dmg/s=%.1f%% hunters=%.2f (small %.2f equal %.2f big %.2f) "
      "kills=%u growth:%s\n",
      level, seed, aliveSec, deaths, clears, sizeMax, st.playerHits,
      aliveSec > 0 ? st.playerDamageQ8 * 100.0 / 256 / aliveSec : 0.0,
      (huntersSmall + huntersEqual + huntersBig) / samples,
      huntersSmall / samples, huntersEqual / samples, huntersBig / samples,
      st.kills, growth);
  if (!quiet) std::printf("avg tick %.3f ms\n", total / (t > 0 ? t : 1));
  return 0;
}
