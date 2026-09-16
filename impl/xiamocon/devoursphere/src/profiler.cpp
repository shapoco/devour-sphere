#include "profiler.hpp"

#if DS_PROFILE

#include "shapoco/gfx2d/fonts.hpp"

namespace ds {

namespace {

constexpr int PANEL_X = 2;
constexpr int PANEL_Y = 26;  // below the health gauge and the rank
constexpr int LINE_ADV = 9;
constexpr int PAD = 2;

// Integer formatting only: newlib's %f pulls in about 10 KB and costs
// hundreds of microseconds, which would be a sizeable share of what we are
// trying to measure.
char *putStr(char *p, char *end, const char *s) {
  while (*s && p < end) *p++ = *s++;
  return p;
}

char *putUint(char *p, char *end, uint32_t v, int width = 0) {
  char tmp[12];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v && n < (int)sizeof(tmp));
  while (n < width && p < end) *p++ = ' ', width--;
  while (n > 0 && p < end) *p++ = tmp[--n];
  return p;
}

// Microseconds as milliseconds with two decimals, e.g. 8403 -> "8.40"
char *putMs(char *p, char *end, uint32_t us) {
  p = putUint(p, end, us / 1000);
  if (p < end) *p++ = '.';
  uint32_t frac = (us % 1000) / 10;
  if (p < end) *p++ = (char)('0' + frac / 10);
  if (p < end) *p++ = (char)('0' + frac % 10);
  return p;
}

// Core1 runs beginFrame(), whose sphere traversal recurses, on a 4 KB stack
// in SCRATCH_X -- that size is a hard ceiling, so it is worth watching.
constexpr uint32_t STACK_PAINT = 0xC1C1C1C1u;

}  // namespace

// Linker symbols. Each core gets 4 KB: core0 in SCRATCH_Y, core1 in
// SCRATCH_X, and they are adjacent -- so an overflow of one now lands in the
// other, which is exactly what makes this worth measuring. Declared as
// arrays so that pointer arithmetic over them is well defined.
extern "C" {
extern uint32_t __StackOneBottom[];  // core1, low address
extern uint32_t __StackOneTop[];
extern uint32_t __StackBottom[];  // core0, low address
extern uint32_t __StackTop[];
}

namespace {
uint32_t g_painted[2] = {0, 0};  // words painted, 0 = paint never ran

// Reading sp is Arm only; on a host build (the overlay is rendered there to
// check its layout) the readout simply stays at zero.
uint32_t paint(uint32_t *bottom, int which) {
#ifdef __arm__
  uint32_t sp;
  __asm volatile("mov %0, sp" : "=r"(sp));
  // Everything below the current frame, less a little slack, is unused
  uint32_t *end = (uint32_t *)(sp - 64);
  uint32_t n = 0;
  for (uint32_t *q = bottom; q < end; q++, n++) *q = STACK_PAINT;
  g_painted[which] = n;
  return n;
#else
  (void)bottom;
  (void)which;
  return 0;
#endif
}

uint32_t used(const uint32_t *bottom, const uint32_t *top, int which) {
  if (g_painted[which] == 0) return 0;  // never painted: nothing to report
  const uint32_t *q = bottom;
  while (q < top && *q == STACK_PAINT) q++;
  return (uint32_t)((size_t)(top - q) * sizeof(uint32_t));
}
}  // namespace

void Profiler::paintCore1Stack() { paint(__StackOneBottom, 1); }
void Profiler::paintCore0Stack() { paint(__StackBottom, 0); }

uint32_t Profiler::core1StackUsed() {
  return used(__StackOneBottom, __StackOneTop, 1);
}
uint32_t Profiler::core0StackUsed() {
  return used(__StackBottom, __StackTop, 0);
}

void Profiler::endFrame(uint64_t nowUs,
                        const devoursphere::render::RenderStats &stats) {
  frames_++;
  if (windowUs_ == 0) windowUs_ = nowUs;
  const uint64_t elapsed = nowUs - windowUs_;
  if (elapsed >= 500000) {
    fps100_ = (uint32_t)((uint64_t)frames_ * 100000000ull / elapsed);
    frames_ = 0;
    windowUs_ = nowUs;
  }
  if (!on_) return;

  // core1's critical path: building the scene plus getting the bands out
  const uint32_t cpuUs = beginUs + rasterUs + dmaWaitUs + cmdUs;
  for (int i = 0; i < LINES; i++) {
    char *p = lines_[i], *end = lines_[i] + COLS;
    switch (i) {
      case 0:  // frame rate, and the ticks the last frame ran
        p = putStr(p, end, "FPS ");
        p = putUint(p, end, fps100_ / 100);
        p = putStr(p, end, ".");
        p = putUint(p, end, fps100_ % 100 / 10);
        p = putStr(p, end, "  TCK ");
        p = putMs(p, end, tickUs);
        p = putStr(p, end, "x");
        p = putUint(p, end, (uint32_t)ticks);
        break;
      case 1:  // the two render stages
        p = putStr(p, end, "BGN ");
        p = putMs(p, end, beginUs);
        p = putStr(p, end, "  RAS ");
        p = putMs(p, end, rasterUs);
        break;
      case 2:  // time spent waiting on a transfer, and the CPU total. CPU
               // above the 14.75 ms of a full frame's DMA means the frame
               // rate is limited by this core, not by the display.
        p = putStr(p, end, "DMA ");
        p = putMs(p, end, dmaWaitUs);
        p = putStr(p, end, "  CPU ");
        p = putMs(p, end, cpuUs);
        break;
      case 5:  // per band setWindow + writePixelsStart, and core1's stack.
               // The command cost scales with the band count, so it says
               // whether fewer, taller bands would pay off.
        p = putStr(p, end, "CMD ");
        p = putMs(p, end, cmdUs);
        p = putStr(p, end, "  W ");
        p = putMs(p, end, core1WaitUs);
        break;
      case 6:  // Stack high water marks. Both stacks are 4096 bytes and sit
               // next to each other in SCRATCH_X / SCRATCH_Y, so neither can
               // be grown and either reaching the limit is a bug.
        // A whole screen of band transfers with nothing else running,
        // measured once at start up. The SPI clock says this should be
        // 14.75 ms; more than that is command overhead or a starved DMA.
        p = putStr(p, end, "XFR ");
        p = putMs(p, end, xferUs);
        break;
      case 7:
        p = putStr(p, end, "STK1 ");
        p = putUint(p, end, core1StackUsed());
        p = putStr(p, end, "  STK0 ");
        p = putUint(p, end, core0StackUsed());
        break;
      case 3:  // did the scene fit in the triangle buffer and the span pool?
        p = putStr(p, end, "TRI ");
        p = putUint(p, end, (uint32_t)stats.gfx.triCount);
        p = putStr(p, end, "/");
        p = putUint(p, end, (uint32_t)stats.gfx.triCapacity);
        p = putStr(p, end, " d");
        p = putUint(p, end, (uint32_t)stats.gfx.triDropped);
        break;
      case 4:  // ... and in the arena
        p = putStr(p, end, "SPN ");
        p = putUint(p, end, (uint32_t)stats.gfx.spanPeak);
        p = putStr(p, end, " d");
        p = putUint(p, end, (uint32_t)stats.gfx.spanDropped);
        p = putStr(p, end, " ARN ");
        p = putUint(p, end, (uint32_t)(stats.gfx.arenaUsed / 1024));
        p = putStr(p, end, "K");
        break;
    }
    *p = '\0';
  }
}

void Profiler::drawOverlay(const g2::Surface &band, int bandY) {
  if (!on_) return;
  g2::Graphics2D g(band);
  g.setClipRect(0, 0, band.width, band.height);
  // Monospace, so the columns do not dance as the digits change
  g.setFont(&ShapoSansMono_s08c07, 1);
  // Frame coordinates to band coordinates. Anything outside this band is
  // clipped away, so a line crossing the boundary is drawn in both bands and
  // comes out seamless.
  const int oy = -bandY;
  const int w = g.measureText("00000000000000000000") + 2 * PAD;
  const int h = LINES * LINE_ADV + 2 * PAD;
  g.fillRect(PANEL_X, PANEL_Y + oy, w, h, g2::makeColor(0, 0, 0, 190));
  g.setTextColor(g2::makeColor(150, 255, 170));
  for (int i = 0; i < LINES; i++) {
    g.drawString(PANEL_X + PAD, PANEL_Y + oy + PAD + i * LINE_ADV, lines_[i]);
  }
}

}  // namespace ds

#endif  // DS_PROFILE
