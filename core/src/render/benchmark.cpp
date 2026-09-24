#include "devoursphere/render/benchmark.hpp"

#include "devoursphere/profile.hpp"

namespace devoursphere::render {

using sim::Game;

// Three kinds of frame: a quiet first sphere, a crowd on a late one, and
// the flight to the next sphere (LAUNCH, then ARRIVE). Chosen from 8 levels
// x 8 seeds on the three kinds of build (TICK_RATE 60, TICK_RATE 30, the
// reduced ESPboy build), whose simulations differ: QUIET is below the median
// in triangles on all three, CROWD 1.3-2x above it, and the AI player lives
// through every scene on all three (core/SPEC.md "ベンチマーク").
const Benchmark::Scene Benchmark::SCENE_LIST[SCENES] = {
    {"QUIET", 1, 1, 1234, false},
    {"CROWD", 8, 2, 7, false},
    {"FLIGHT", 2, 2, 99, true},
};

static constexpr int WARMUP_FRAMES = Benchmark::WARMUP_SECONDS * sim::TICK_RATE;
static constexpr int SCENE_FRAMES = Benchmark::SCENE_SECONDS * sim::TICK_RATE;

// --- Text without printf (newlib's pulls in kilobytes and takes long) ------

static char *put(char *p, char *end, const char *s) {
  while (*s && p < end) *p++ = *s++;
  return p;
}

static char *putUint(char *p, char *end, uint32_t v) {
  char tmp[10];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (n > 0 && p < end) *p++ = tmp[--n];
  return p;
}

// Milliseconds from microseconds: one decimal below 100 ms, whole above
static char *putMs(char *p, char *end, uint32_t us) {
  const uint32_t tenths = (us + 50) / 100;
  if (tenths >= 1000) return putUint(p, end, (tenths + 5) / 10);
  p = putUint(p, end, tenths / 10);
  if (p < end) *p++ = '.';
  if (p < end) *p++ = (char)('0' + tenths % 10);
  return p;
}

static char *putKb(char *p, char *end, uint32_t bytes) {
  p = putUint(p, end, (bytes + 512) / 1024);
  return p < end ? (*p++ = 'K', p) : p;
}

// --- Running ----------------------------------------------------------------

void Benchmark::beforeFrame(Game &game, Renderer &renderer,
                            const BenchSample &prev) {
  const uint32_t now = profileClockUs ? profileClockUs() : 0;

  if (phase_ == Phase::IDLE) {
    if (!game.takeBenchmarkRequest()) return;
    for (auto &a : acc_) a = {};
    savedAuto_ = game.autoPlayer();
    width_ = renderer.width();
    height_ = renderer.height();
    lineCount_ = 0;
    scene_ = 0;
    phase_ = Phase::RUNNING;
    if (log_) log_("benchmark: start");
    startScene(game, renderer);
    lastUs_ = now;
    return;
  }

  if (phase_ == Phase::RESULTS) {
    if (!closeWanted_ && pagesWanted_ > 0) {
      pageIndex_ += pagesWanted_;
      const int rows = pageRows(renderer);
      const int pages = (lineCount_ + rows - 1) / rows;
      if (pageIndex_ >= pages) closeWanted_ = true;
    }
    pagesWanted_ = 0;
    if (closeWanted_) {
      closeWanted_ = false;
      renderer.showText(nullptr, 0);
      phase_ = Phase::IDLE;
      holdOff_ = true;
    } else {
      showPage(renderer);
    }
    return;
  }

  // Running: the sample describes the frame drawn before this call
  last_ = prev;
  const int drawn = frame_ - 1;
  if (drawn >= WARMUP_FRAMES) record(game, renderer, prev, now - lastUs_);
  lastUs_ = now;
  game.resetTickProfile();
  renderer.resetFrameProfile();

  if (frame_ == WARMUP_FRAMES + SCENE_FRAMES) {
    if (++scene_ < SCENES) {
      startScene(game, renderer);
    } else {
      finish(game, renderer, now ^ game.stateHash());
    }
    return;
  }
  if (frame_ == WARMUP_FRAMES && SCENE_LIST[scene_].launch) game.debugLaunch();
  frame_++;
}

void Benchmark::startScene(Game &game, Renderer &renderer) {
  const Scene &s = SCENE_LIST[scene_];
  game.reset(s.seed);
  game.setBenchmark(true);
  game.debugAutoPlayer(true);
  game.debugStartSphere(s.level, s.weapon);
  game.resetTickProfile();
  renderer.restart();
  frame_ = 1;  // counts the frame about to be drawn
  char *p = banner_, *end = banner_ + Renderer::TEXT_COLS;
  p = put(p, end, "BENCHMARK ");
  p = putUint(p, end, (uint32_t)scene_ + 1);
  p = put(p, end, "/");
  p = putUint(p, end, SCENES);
  p = put(p, end, " ");
  p = put(p, end, s.name);
  *p = '\0';
  renderer.setBanner(banner_);
}

void Benchmark::record(const Game &game, const Renderer &renderer,
                       const BenchSample &s, uint32_t periodUs) {
  Acc &a = acc_[scene_];
  auto sumMax = [](uint32_t v, uint32_t &sum, uint32_t &max) {
    sum += v;
    if (v > max) max = v;
  };
  a.frames++;
  sumMax(periodUs, a.periodSum, a.periodMax);
  sumMax(s.tickUs, a.tickSum, a.tickMax);
  sumMax(s.beginUs, a.beginSum, a.beginMax);
  sumMax(s.rasterUs, a.rasterSum, a.rasterMax);
  a.dmaSum += s.dmaUs;
  a.cmdSum += s.cmdUs;
  a.waitSum += s.waitUs;
  for (int i = 0; i < Game::TP_COUNT; i++) {
    a.tickPhase[i] += game.tickProfile().us[i];
  }
  for (int i = 0; i < FP_COUNT; i++) {
    a.framePhase[i] += renderer.frameProfile().us[i];
  }
  const RenderStats st = renderer.stats();
  if (st.gfx.triCount > a.triMax) a.triMax = st.gfx.triCount;
  if (st.gfx.triBytes > a.triBytesMax) a.triBytesMax = (uint32_t)st.gfx.triBytes;
  if (st.gfx.spanPeak > a.spanMax) a.spanMax = st.gfx.spanPeak;
  if (st.gfx.arenaUsed > a.arenaMax) a.arenaMax = (uint32_t)st.gfx.arenaUsed;
  if (st.lines > a.linesMax) a.linesMax = st.lines;
  a.dropped += st.gfx.triDropped + st.gfx.spanDropped;
}

void Benchmark::finish(Game &game, Renderer &renderer, uint32_t seed) {
  game.setBenchmark(false);
  game.debugAutoPlayer(savedAuto_);
  game.reset(seed);
  renderer.restart();
  renderer.setBanner(nullptr);
  buildLines();
  if (log_) {
    for (int i = 0; i < lineCount_; i++) log_(lines_[i]);
    log_("benchmark: end");
  }
  phase_ = Phase::RESULTS;
  pageIndex_ = 0;
  pagesWanted_ = 0;
  closeWanted_ = false;
  showPage(renderer);
}

uint8_t Benchmark::input(uint8_t buttons) {
  const uint8_t pressed = buttons & (uint8_t)~prevButtons_;
  prevButtons_ = buttons;
  switch (phase_) {
    case Phase::RUNNING: return 0;
    case Phase::RESULTS:
      if (pressed & sim::Button::B) closeWanted_ = true;
      if (pressed & sim::Button::A) pagesWanted_++;
      return 0;
    case Phase::IDLE: break;
  }
  if (holdOff_) {
    if (buttons) return 0;
    holdOff_ = false;
  }
  return buttons;
}

// --- Results ----------------------------------------------------------------

char *Benchmark::newLine() {
  if (lineCount_ >= MAX_LINES) return nullptr;
  char *l = lines_[lineCount_++];
  l[0] = '\0';
  return l;
}

// Per frame (or per tick: one tick a frame) averages of the phase timers,
// "A1.8 M4.6 ...": the letters of the profiling overlays
static void phaseLine(char *l, const char *letters, const uint32_t *us,
                      int n, uint32_t frames) {
  char *p = l, *end = l + Renderer::TEXT_COLS;
  for (int i = 0; i < n; i++) {
    if (i > 0 && p < end) *p++ = ' ';
    if (p < end) *p++ = letters[i];
    p = putMs(p, end, us[i] / frames);
  }
  *p = '\0';
}

void Benchmark::buildLines() {
  lineCount_ = 0;
  char *l, *p, *end;
  auto begin = [&]() {
    l = newLine();
    p = l;
    end = l ? l + Renderer::TEXT_COLS : l;
    return l != nullptr;
  };
  auto done = [&]() {
    if (l) *p = '\0';
  };

  if (begin()) {
    p = put(p, end, "BENCHMARK ");
    p = put(p, end, sim::VERSION_STRING);
    done();
  }
  if (begin()) {
    p = put(p, end, name_);
    done();
  }
  if (begin()) {
    p = putUint(p, end, (uint32_t)width_);
    p = put(p, end, "x");
    p = putUint(p, end, (uint32_t)height_);
    p = put(p, end, " T");
    p = putUint(p, end, sim::TICK_RATE);
    if (last_.xferUs) {
      p = put(p, end, " XFR ");
      p = putMs(p, end, last_.xferUs);
    }
    done();
  }
  if ((last_.stack0 || last_.stack1) && begin()) {
    p = put(p, end, "STK0 ");
    p = putUint(p, end, last_.stack0);
    if (last_.stack1) {
      p = put(p, end, " STK1 ");
      p = putUint(p, end, last_.stack1);
    }
    done();
  }

  for (int s = 0; s < SCENES; s++) {
    const Acc &a = acc_[s];
    const uint32_t f = a.frames ? a.frames : 1;
    if (begin()) done();  // blank line between the blocks
    if (begin()) {
      p = putUint(p, end, (uint32_t)s + 1);
      p = put(p, end, " ");
      p = put(p, end, SCENE_LIST[s].name);
      p = put(p, end, " LV");
      p = putUint(p, end, (uint32_t)SCENE_LIST[s].level);
      p = put(p, end, " ");
      p = putUint(p, end, a.frames);
      p = put(p, end, "F");
      done();
    }
    if (begin()) {
      // Frames per second over the whole scene, and the frame period
      const uint32_t fps10 =
          a.periodSum ? (uint32_t)((uint64_t)a.frames * 10000000u / a.periodSum)
                      : 0;
      p = put(p, end, "FPS ");
      p = putUint(p, end, fps10 / 10);
      if (p < end) *p++ = '.';
      if (p < end) *p++ = (char)('0' + fps10 % 10);
      p = put(p, end, " F ");
      p = putMs(p, end, a.periodSum / f);
      p = put(p, end, "/");
      p = putMs(p, end, a.periodMax);
      done();
    }
    // Average/peak of what the platform timed
    struct Pair {
      const char *label;
      uint32_t sum, max;
    };
    const Pair pairs[3] = {{"TCK ", a.tickSum, a.tickMax},
                           {"BGN ", a.beginSum, a.beginMax},
                           {"RAS ", a.rasterSum, a.rasterMax}};
    for (const Pair &pr : pairs) {
      if (!pr.max || !begin()) continue;
      p = put(p, end, pr.label);
      p = putMs(p, end, pr.sum / f);
      p = put(p, end, "/");
      p = putMs(p, end, pr.max);
      done();
    }
    if ((a.dmaSum || a.cmdSum || a.waitSum) && begin()) {
      p = put(p, end, "DMA ");
      p = putMs(p, end, a.dmaSum / f);
      p = put(p, end, " CMD ");
      p = putMs(p, end, a.cmdSum / f);
      p = put(p, end, " W");
      p = putMs(p, end, a.waitSum / f);
      done();
    }
    if (begin()) {
      p = put(p, end, "TRI ");
      p = putUint(p, end, (uint32_t)a.triMax);
      p = put(p, end, "/");
      p = putKb(p, end, a.triBytesMax);
      p = put(p, end, " SPN ");
      p = putUint(p, end, (uint32_t)a.spanMax);
      done();
    }
    if (begin()) {
      p = put(p, end, "ARN ");
      p = putKb(p, end, a.arenaMax);
      p = put(p, end, " L");
      p = putUint(p, end, (uint32_t)a.linesMax);
      p = put(p, end, " D");
      p = putUint(p, end, (uint32_t)a.dropped);
      done();
    }
    // The phase timers, in the letters of the PicoSystem overlay
    // (impl/picosystem/SPEC.md "デバッグ表示"): per tick, then per frame
    const uint32_t *tp = a.tickPhase;
    const uint32_t *fp = a.framePhase;
    bool timed = false;
    for (int i = 0; i < Game::TP_COUNT; i++) timed |= tp[i] != 0;
    for (int i = 0; i < FP_COUNT; i++) timed |= fp[i] != 0;
    if (!timed) continue;
    const uint32_t tickRest[4] = {
        tp[Game::TP_BULLETS], tp[Game::TP_FRAGMENTS], tp[Game::TP_EATING],
        tp[Game::TP_COLLISIONS] + tp[Game::TP_ORDERS] + tp[Game::TP_OTHER]};
    const uint32_t rest[4] = {fp[FP_FRAGMENTS], fp[FP_BULLETS],
                              fp[FP_EFFECTS_DRAW], fp[FP_OVERLAYS]};
    const uint32_t scene[4] = {
        fp[FP_SPHERE], fp[FP_ENTITIES], rest[0] + rest[1] + rest[2] + rest[3],
        fp[FP_CAMERA] + fp[FP_EFFECTS] + fp[FP_SORT]};
    const uint32_t bands[2] = {fp[FP_BAND_3D], fp[FP_BAND_2D]};
    if ((l = newLine())) phaseLine(l, "AMLF", tp, 4, f);
    if ((l = newLine())) phaseLine(l, "BFKX", tickRest, 4, f);
    if ((l = newLine())) phaseLine(l, "SOHX", scene, 4, f);
    if ((l = newLine())) phaseLine(l, "FBEM", rest, 4, f);
    if ((l = newLine())) phaseLine(l, "DU", bands, 2, f);
  }
}

int Benchmark::pageRows(const Renderer &renderer) {
  int rows = renderer.textRows() - 1;  // the last row is the footer
  if (rows > MAX_PAGE_ROWS - 1) rows = MAX_PAGE_ROWS - 1;
  return rows < 1 ? 1 : rows;
}

void Benchmark::showPage(Renderer &renderer) {
  const int rows = pageRows(renderer);
  const int pages = (lineCount_ + rows - 1) / rows;
  const int first = pageIndex_ * rows;
  int n = 0;
  for (int i = first; i < lineCount_ && n < rows; i++) page_[n++] = lines_[i];
  while (n < rows) page_[n++] = nullptr;  // the footer stays at the bottom
  char *p = footer_, *end = footer_ + Renderer::TEXT_COLS;
  p = put(p, end, pageIndex_ + 1 < pages ? "A:NEXT B:CLOSE " : "A/B:CLOSE ");
  p = putUint(p, end, (uint32_t)pageIndex_ + 1);
  p = put(p, end, "/");
  p = putUint(p, end, (uint32_t)pages);
  *p = '\0';
  page_[n++] = footer_;
  renderer.showText(page_, n);
}

}  // namespace devoursphere::render
