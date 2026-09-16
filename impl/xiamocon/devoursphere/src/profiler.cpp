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

}  // namespace

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

  const uint32_t cpuUs = tickUs + beginUs + rasterUs;
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
