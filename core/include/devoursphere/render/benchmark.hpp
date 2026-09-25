#ifndef DEVOURSPHERE_RENDER_BENCHMARK_HPP
#define DEVOURSPHERE_RENDER_BENCHMARK_HPP

// The built-in benchmark: B held for three seconds on the title screen runs a
// fixed sequence of scenes with the AI playing, measures every frame, and
// shows the results as a text screen (and hands them to the platform's log).
//
// Every run draws the same frames: each scene starts from a fixed seed, the
// renderer is restarted, and one tick is run per frame with a fixed dt,
// however long the frame takes. The frames are not paced: the benchmark runs
// as fast as the board can draw, and the frame period is what it measures.
//
// The platform's part, once per frame where the Game belongs to the render
// core (between batches), after its own use of the phase profiles and before
// beginFrame():
//
//   bench.beforeFrame(game, renderer, sample);   // what it measured last frame
//   profileClockUs = bench.active() || overlayOn ? clock : nullptr;
//   renderer.beginFrame(game, bench.dt(ran));
//   ... run bench.ticks(ticksDue()) ticks with bench.input(in) ...

#include <cstdint>

#include "devoursphere/render/renderer.hpp"
#include "devoursphere/sim/game.hpp"

namespace devoursphere::render {

// What the platform measured of the previous frame, in microseconds. A field
// the platform has no such thing for stays 0 (and is left off the results).
struct BenchSample {
  uint32_t tickUs = 0;    // the ticks of the batch
  uint32_t beginUs = 0;   // beginFrame()
  uint32_t rasterUs = 0;  // renderBand() over the whole frame
  uint32_t dmaUs = 0;     // waiting for the display
  uint32_t cmdUs = 0;     // per-band display commands
  uint32_t waitUs = 0;    // the render core waiting for the sim core
  // Not per frame: the full-screen transfer measured at start up, and the
  // peak stack use of each core so far (bytes)
  uint32_t xferUs = 0;
  uint32_t stack0 = 0, stack1 = 0;
};

class Benchmark {
 public:
  static constexpr int SCENES = 3;
  static constexpr int WARMUP_SECONDS = 1;  // not measured: the camera settles
  static constexpr int SCENE_SECONDS = 10;
  static constexpr int MAX_ROWS = 40;  // of the results table

  // The platform's name for the results, and where to send them as text
  // (a serial console; nullptr for none). Call once at start up.
  void setPlatform(const char *name, void (*log)(const char *line)) {
    name_ = name;
    log_ = log;
  }

  // Once per frame, between batches, before beginFrame() (see the top of
  // this file): starts a run when the game asks for one, records the frame
  // before, moves on through the scenes and ends with the results.
  void beforeFrame(sim::Game &game, Renderer &renderer,
                   const BenchSample &prev);

  // Running the scenes
  bool running() const { return phase_ == Phase::RUNNING; }
  // Running or showing the results: the phase clock must be installed
  bool active() const { return phase_ != Phase::IDLE; }
  // Ticks for the next batch: one while running, else what the platform owes
  int ticks(int owed) const { return running() ? 1 : owed; }
  // dt for beginFrame() of a frame that ran `ran` ticks
  float dt(int ran) const {
    return (running() ? 1 : ran) * (1.0f / sim::TICK_RATE);
  }
  // The input to hand the game. Nothing while running; on the results A
  // turns the page and B closes them (as does A on the last page), and
  // after that nothing reaches the game until every button is released and
  // the direction is back near the center.
  sim::Input input(const sim::Input &in);

  // The results of the last run as lines of monospace text (the table, one
  // column per scene), as the log gets them; empty before a run has
  // finished. line() formats into a buffer the next call overwrites.
  int lineCount() const { return rowCount_ ? 2 + rowCount_ : 0; }
  const char *line(int i);

 private:
  enum class Phase : uint8_t { IDLE, RUNNING, RESULTS };

  struct Scene {
    const char *name;
    int level, weapon;
    uint32_t seed;
    bool launch;  // leave the sphere once warmed up (LAUNCH, then ARRIVE)
  };
  static const Scene SCENE_LIST[SCENES];

  // Per scene: sums and peaks over the measured frames
  struct Acc {
    uint32_t frames;
    uint32_t periodSum, periodMax;
    uint32_t tickSum, tickMax, beginSum, beginMax, rasterSum, rasterMax;
    uint32_t dmaSum, cmdSum, waitSum;
    uint32_t tickPhase[sim::Game::TP_COUNT];
    uint32_t framePhase[FP_COUNT];
    int triMax, spanMax, linesMax, dropped;
    uint32_t triBytesMax, arenaMax;
  };

  void startScene(sim::Game &game, Renderer &renderer);
  void record(const sim::Game &game, const Renderer &renderer,
              const BenchSample &s, uint32_t periodUs);
  void finish(sim::Game &game, Renderer &renderer, uint32_t seed);
  void buildTable();
  void addRow(const char *label, const uint32_t *v, char kind, bool always);
  void showPage(Renderer &renderer);

  const char *name_ = "?";
  void (*log_)(const char *) = nullptr;
  Phase phase_ = Phase::IDLE;
  int scene_ = 0;
  int frame_ = 0;  // frames drawn since the scene started
  uint32_t lastUs_ = 0;
  bool savedAuto_ = false;
  Acc acc_[SCENES] = {};
  BenchSample last_;  // for the per-run fields
  int width_ = 0, height_ = 0;
  char banner_[Renderer::TEXT_COLS + 1] = {};

  // Results: head items, then a table of a label and one value per scene
  static constexpr int HEAD_ITEMS = 6;
  static constexpr int CELL = 8;  // characters of a value, with the '\0'
  char head_[HEAD_ITEMS][Renderer::TEXT_COLS + 1] = {};
  const char *headPtr_[HEAD_ITEMS] = {};
  char values_[MAX_ROWS][SCENES][CELL] = {};
  const char *cells_[MAX_ROWS * (1 + SCENES)] = {};
  const char *titles_[SCENES] = {};
  int rowCount_ = 0;
  Renderer::TextTable table_;
  char footer_[Renderer::TEXT_COLS + 1] = {};
  char lineBuf_[HEAD_ITEMS * (Renderer::TEXT_COLS + 2)] = {};
  int pageIndex_ = 0;
  int pages_ = 1;
  // Input on the results (set by input(), applied by beforeFrame(), which
  // is where the renderer belongs to the caller)
  uint8_t prevButtons_ = 0xFF;
  int pagesWanted_ = 0;
  bool closeWanted_ = false;
  bool holdOff_ = false;
};

}  // namespace devoursphere::render

#endif
