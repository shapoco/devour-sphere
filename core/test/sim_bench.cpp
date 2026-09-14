// Balancing / performance run: an AI-driven player on a planet of the given
// level. Prints the player's progress and the global statistics over time.
//
// Usage: devoursphere_sim_bench [level] [seconds] [seed]

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "devoursphere/sim/game.hpp"

using namespace devoursphere::sim;

int main(int argc, char **argv) {
  int level = argc > 1 ? std::atoi(argv[1]) : 1;
  int seconds = argc > 2 ? std::atoi(argv[2]) : 180;
  uint32_t seed = argc > 3 ? (uint32_t)std::atoi(argv[3]) : 7u;
  Game g;
  g.reset(seed);
  g.debugStartPlanet(level, 0);
  g.debugAutoPlayer(true);

  int ticks = seconds * TICK_RATE;
  double total = 0;
  int deaths = 0, clears = 0;
  for (int t = 0; t < ticks; t++) {
    auto t0 = std::chrono::steady_clock::now();
    g.tick(0);
    total += std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - t0)
                 .count();
    if (g.events() & Event::PLAYER_DIED) deaths++;
    if (g.events() & Event::PLANET_CLEARED) clears++;
    if (g.state() == GameState::DEAD) {
      std::printf("t=%3ds player died (rank %d)\n", t / TICK_RATE,
                  g.playerRank());
      break;
    }
    if (t % (10 * TICK_RATE) == 0) {
      const Creature &p = g.player();
      const DebugStats &st = g.debugStats();
      int parts = 0, particles = 0, bullets = 0;
      for (const auto &fp : g.floatingParts) parts += fp.alive;
      for (const auto &pt : g.particles) particles += pt.alive;
      for (const auto &b : g.bullets) bullets += b.alive;
      uint32_t maxSize = 0;
      for (const auto &c : g.creatures)
        if (c.alive && c.size > maxSize) maxSize = c.size;
      std::printf(
          "t=%3ds lv=%d size=%u (x2^%u) rank=%d/%d hp=%d%% parts=%d max=%u | "
          "floating=%d particles=%d bullets=%d | shots=%u hits=%u kills=%u "
          "absorbs=%u eaten=%u\n",
          t / TICK_RATE, g.planetLevel(), p.size, g.playerDisplayScaleLog2(),
          p.rank, g.aliveCreatures(),
          (int)((int64_t)p.hp * 100 / (p.hpMax ? p.hpMax : 1)), p.partCount,
          maxSize, parts, particles, bullets, st.shots, st.hits, st.kills,
          st.absorbs, st.partsEaten);
    }
  }
  std::printf("avg tick %.3f ms, deaths=%d clears=%d\n", total / ticks, deaths,
              clears);
  return 0;
}
