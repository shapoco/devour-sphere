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
    rowCount_ = 0;
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
      if (pageIndex_ >= pages_) closeWanted_ = true;
    }
    pagesWanted_ = 0;
    if (closeWanted_) {
      closeWanted_ = false;
      renderer.showTable(nullptr, 0);
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
  buildTable();
  if (log_) {
    for (int i = 0; i < lineCount(); i++) log_(line(i));
    log_("benchmark: end");
  }
  phase_ = Phase::RESULTS;
  pageIndex_ = 0;
  pagesWanted_ = 0;
  closeWanted_ = false;
  showPage(renderer);
}

sim::Input Benchmark::input(const sim::Input &in) {
  const uint8_t buttons = in.buttons;
  const uint8_t pressed = buttons & (uint8_t)~prevButtons_;
  prevButtons_ = buttons;
  switch (phase_) {
    case Phase::RUNNING: return {};
    case Phase::RESULTS:
      if (pressed & sim::Button::B) closeWanted_ = true;
      if (pressed & sim::Button::A) pagesWanted_++;
      return {};
    case Phase::IDLE: break;
  }
  if (holdOff_) {
    if (buttons || sim::directionBits(in, 0)) return {};
    holdOff_ = false;
  }
  return in;
}

// --- Results ----------------------------------------------------------------

// One row of the table: a label and a value per scene. `kind`: 'm' micro-
// seconds shown as milliseconds, 'f' tenths shown with one decimal, 'k'
// bytes shown as kilobytes, 'i' as it is. A row whose values are all 0 is
// something the platform does not measure, and is left out unless `always`.
void Benchmark::addRow(const char *label, const uint32_t *v, char kind,
                       bool always) {
  if (rowCount_ >= MAX_ROWS) return;
  bool any = always;
  for (int s = 0; s < SCENES; s++) any |= v[s] != 0;
  if (!any) return;
  const int r = rowCount_++;
  cells_[r * (1 + SCENES)] = label;
  for (int s = 0; s < SCENES; s++) {
    char *c = values_[r][s], *p = c, *end = c + CELL - 1;
    switch (kind) {
      case 'm': p = putMs(p, end, v[s]); break;
      case 'k': p = putUint(p, end, (v[s] + 512) / 1024); break;
      case 'f':
        p = putUint(p, end, v[s] / 10);
        if (p < end) *p++ = '.';
        if (p < end) *p++ = (char)('0' + v[s] % 10);
        break;
      default: p = putUint(p, end, v[s]); break;
    }
    *p = '\0';
    cells_[r * (1 + SCENES) + 1 + s] = c;
  }
}

void Benchmark::buildTable() {
  // Head items: what ran where, and the per-run numbers
  int items = 0;
  auto item = [&]() {
    char *h = head_[items];
    headPtr_[items++] = h;
    return h;
  };
  char *p, *end;
  auto start = [&]() {
    p = item();
    end = p + Renderer::TEXT_COLS;
  };
  start();
  p = put(p, end, "BENCHMARK ");
  p = put(p, end, sim::VERSION_STRING);
  *p = '\0';
  start();
  p = put(p, end, name_);
  *p = '\0';
  start();
  p = putUint(p, end, (uint32_t)width_);
  p = put(p, end, "x");
  p = putUint(p, end, (uint32_t)height_);
  p = put(p, end, " T");
  p = putUint(p, end, sim::TICK_RATE);
  *p = '\0';
  if (last_.xferUs) {
    start();
    p = put(p, end, "XFR ");
    p = putMs(p, end, last_.xferUs);
    *p = '\0';
  }
  if (last_.stack0) {
    start();
    p = put(p, end, "STK0 ");
    p = putUint(p, end, last_.stack0);
    *p = '\0';
  }
  if (last_.stack1) {
    start();
    p = put(p, end, "STK1 ");
    p = putUint(p, end, last_.stack1);
    *p = '\0';
  }

  // One value per scene for each row
  rowCount_ = 0;
  uint32_t v[SCENES];
  auto row = [&](const char *label, char kind, auto value,
                 bool always = false) {
    for (int s = 0; s < SCENES; s++) {
      const Acc &a = acc_[s];
      v[s] = value(a, a.frames ? a.frames : 1u);
    }
    addRow(label, v, kind, always);
  };
  row("FPS", 'f', [](const Acc &a, uint32_t) {
    return a.periodSum
               ? (uint32_t)((uint64_t)a.frames * 10000000u / a.periodSum)
               : 0u;
  });
  row("FRM", 'm', [](const Acc &a, uint32_t f) { return a.periodSum / f; });
  row("FRM^", 'm', [](const Acc &a, uint32_t) { return a.periodMax; });
  row("TCK", 'm', [](const Acc &a, uint32_t f) { return a.tickSum / f; });
  row("TCK^", 'm', [](const Acc &a, uint32_t) { return a.tickMax; });
  row("BGN", 'm', [](const Acc &a, uint32_t f) { return a.beginSum / f; });
  row("BGN^", 'm', [](const Acc &a, uint32_t) { return a.beginMax; });
  row("RAS", 'm', [](const Acc &a, uint32_t f) { return a.rasterSum / f; });
  row("RAS^", 'm', [](const Acc &a, uint32_t) { return a.rasterMax; });
  row("DMA", 'm', [](const Acc &a, uint32_t f) { return a.dmaSum / f; });
  row("CMD", 'm', [](const Acc &a, uint32_t f) { return a.cmdSum / f; });
  row("W", 'm', [](const Acc &a, uint32_t f) { return a.waitSum / f; });
  row("TRI", 'i', [](const Acc &a, uint32_t) { return (uint32_t)a.triMax; });
  row("TRIK", 'k', [](const Acc &a, uint32_t) { return a.triBytesMax; });
  row("SPN", 'i', [](const Acc &a, uint32_t) { return (uint32_t)a.spanMax; });
  row("ARNK", 'k', [](const Acc &a, uint32_t) { return a.arenaMax; });
  row("LIN", 'i', [](const Acc &a, uint32_t) { return (uint32_t)a.linesMax; });
  // Dropped primitives and spans: shown at 0 too, the one row where 0 is
  // the answer rather than "not measured"
  row(
      "DRP", 'i', [](const Acc &a, uint32_t) { return (uint32_t)a.dropped; },
      true);
  // The phase timers (core/SPEC.md "フェーズ計測"): the tick's per tick
  // (one tick a frame), the frame's per frame
  struct Phase {
    const char *label;
    bool tick;
    int a, b, c;  // slots summed (-1 none)
  };
  static const Phase PHASES[] = {
      {"tAI", true, Game::TP_AI, -1, -1},
      {"tMOV", true, Game::TP_MOVE, -1, -1},
      {"tLAY", true, Game::TP_LAYOUT, -1, -1},
      {"tFIR", true, Game::TP_FIRE, -1, -1},
      {"tBUL", true, Game::TP_BULLETS, -1, -1},
      {"tFRG", true, Game::TP_FRAGMENTS, -1, -1},
      {"tEAT", true, Game::TP_EATING, -1, -1},
      {"tETC", true, Game::TP_COLLISIONS, Game::TP_ORDERS, Game::TP_OTHER},
      {"fCAM", false, FP_CAMERA, FP_EFFECTS, FP_SORT},
      {"fSPH", false, FP_SPHERE, -1, -1},
      {"fENT", false, FP_ENTITIES, -1, -1},
      {"fFRG", false, FP_FRAGMENTS, -1, -1},
      {"fBUL", false, FP_BULLETS, -1, -1},
      {"fEFX", false, FP_EFFECTS_DRAW, -1, -1},
      {"fOVL", false, FP_OVERLAYS, -1, -1},
      {"f3D", false, FP_BAND_3D, -1, -1},
      {"f2D", false, FP_BAND_2D, -1, -1},
  };
  for (const Phase &ph : PHASES) {
    for (int s = 0; s < SCENES; s++) {
      const Acc &a = acc_[s];
      const uint32_t *slots = ph.tick ? a.tickPhase : a.framePhase;
      uint32_t sum = slots[ph.a];
      if (ph.b >= 0) sum += slots[ph.b];
      if (ph.c >= 0) sum += slots[ph.c];
      v[s] = sum / (a.frames ? a.frames : 1u);
    }
    addRow(ph.label, v, 'm', false);
  }

  for (int s = 0; s < SCENES; s++) titles_[s] = SCENE_LIST[s].name;
  table_.head = headPtr_;
  table_.headCount = items;
  table_.titles = titles_;
  table_.columns = SCENES;
  table_.cells = cells_;
  table_.rows = rowCount_;
  table_.footer = footer_;
}

// The table as monospace text: the head items on one line, the titles, then
// a row per line, the label padded to 5 and each value right aligned in 7
const char *Benchmark::line(int i) {
  char *p = lineBuf_, *end = lineBuf_ + sizeof(lineBuf_) - 1;
  auto pad = [&](const char *s, int width, bool right) {
    int n = 0;
    while (s[n]) n++;
    if (right) {
      for (int k = n; k < width && p < end; k++) *p++ = ' ';
    }
    p = put(p, end, s);
    if (!right) {
      for (int k = n; k < width && p < end; k++) *p++ = ' ';
    }
  };
  if (i == 0) {
    for (int k = 0; k < table_.headCount; k++) {
      if (k > 0) p = put(p, end, "  ");
      p = put(p, end, headPtr_[k]);
    }
  } else if (i == 1) {
    pad("", 5, false);
    for (int s = 0; s < SCENES; s++) pad(titles_[s], 7, true);
  } else {
    const int r = i - 2;
    pad(cells_[r * (1 + SCENES)], 5, false);
    for (int s = 0; s < SCENES; s++) {
      pad(cells_[r * (1 + SCENES) + 1 + s], 7, true);
    }
  }
  *p = '\0';
  return lineBuf_;
}

void Benchmark::showPage(Renderer &renderer) {
  // Laid out with the widest footer there can be, then the real one
  char *p = put(footer_, footer_ + Renderer::TEXT_COLS, "A:NEXT B:CLOSE 9/9");
  *p = '\0';
  pages_ = renderer.showTable(&table_, pageIndex_);
  p = footer_;
  char *end = footer_ + Renderer::TEXT_COLS;
  p = put(p, end, pageIndex_ + 1 < pages_ ? "A:NEXT B:CLOSE " : "A/B:CLOSE ");
  if (pages_ > 1) {
    p = putUint(p, end, (uint32_t)pageIndex_ + 1);
    p = put(p, end, "/");
    p = putUint(p, end, (uint32_t)pages_);
  }
  *p = '\0';
}

}  // namespace devoursphere::render
