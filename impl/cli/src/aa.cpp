#include "aa.hpp"

#include <algorithm>
#include <cstring>

#include "shapoco/gfx2d/fonts.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"

namespace g2 = shapoco::gfx2d;

namespace ds::aa {

namespace {

// Sub-pixel grid per mode: (wide, high)
void subGrid(Mode m, int *sw, int *sh) {
  switch (m) {
    case Mode::ASCII:
      *sw = 3;
      *sh = 6;
      break;
    case Mode::BRAILLE:
      *sw = 2;
      *sh = 4;
      break;
    case Mode::HALF:
      *sw = 1;
      *sh = 2;
      break;
  }
}

inline uint8_t lumaOf(uint32_t rgb) {
  const uint32_t r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  return (uint8_t)((r * 77 + g * 151 + b * 28) >> 8);
}

// The braille dot for sub-pixel (i, j) of a 2 x 4 cell (U+2800 + bits)
constexpr uint8_t BRAILLE_BIT[4][2] = {
    {0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};

}  // namespace

uint32_t rgb565SwappedTo888(uint16_t p) {
  p = (uint16_t)((p << 8) | (p >> 8));
  const uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
  return ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 31);
}

void Converter::init(const Options &opts) {
  opts_ = opts;
  if (opts_.mode == Mode::ASCII && table_.empty()) buildTable();
}

// The glyph of every printable ASCII character, drawn by ShapoGFX with its
// own 8-pixel monospace font and shrunk to 3 x 6 (a sub-pixel is lit when
// any pixel of its box is), is the table's seed. Every other pattern takes
// the character at the smallest Hamming distance; ties go to the closer
// number of lit pixels, then to the earlier character.
void Converter::buildTable() {
  static constexpr int BOX = 16;
  uint16_t px[BOX * BOX];
  g2::Surface s = g2::makeSurface(g2::PixelFormat::RGB565_SWAPPED, BOX, BOX, px);
  g2::Graphics2D g(s);
  g.setFont(&ShapoSansMono_s08c07);
  g.setTextColor(g2::Colors::WHITE);
  const int adv = (int)g.charMetrics('M').width;
  const int lh = (int)g.charMetrics('M').height;
  uint32_t seed[95];
  for (int c = 32; c < 127; c++) {
    g.clear(g2::Colors::BLACK);
    g.drawChar(0, 0, c);
    uint32_t pat = 0;
    for (int j = 0; j < 6; j++) {
      for (int i = 0; i < 3; i++) {
        const int x0 = i * adv / 3, x1 = std::max(x0 + 1, (i + 1) * adv / 3);
        const int y0 = j * lh / 6, y1 = std::max(y0 + 1, (j + 1) * lh / 6);
        bool lit = false;
        for (int y = y0; y < y1 && y < BOX && !lit; y++)
          for (int x = x0; x < x1 && x < BOX; x++)
            if (px[y * BOX + x]) {
              lit = true;
              break;
            }
        if (lit) pat |= 1u << (j * 3 + i);
      }
    }
    seed[c - 32] = pat;
  }
  seed[0] = 0;  // the space is exactly "nothing"
  table_.assign(1u << 18, ' ');
  // A lit sub-pixel the glyph leaves dark costs MISS, a glyph pixel the
  // pattern does not have costs EXTRA. Symmetric costs made a single dot
  // (a star) tie between the space and a one-dot glyph, and the space won
  // by character order, so stars came and went as they crossed cells. The
  // space is now only ever the empty pattern.
  constexpr int MISS = 2, EXTRA = 1;
  for (uint32_t p = 1; p < (1u << 18); p++) {
    int best = 1, bestScore = 1 << 30;
    for (int c = 1; c < 95; c++) {
      const int score = MISS * __builtin_popcount(p & ~seed[c]) +
                        EXTRA * __builtin_popcount(seed[c] & ~p);
      if (score < bestScore) {
        bestScore = score;
        best = c;
      }
    }
    table_[p] = (uint8_t)(32 + best);
  }
}

void Converter::setScreen(int cols, int rows, int fbW, int fbH) {
  if (cols == cols_ && rows == rows_ && fbW == fbW_ && fbH == fbH_) return;
  cols_ = cols;
  rows_ = rows;
  fbW_ = fbW;
  fbH_ = fbH;
  // A cell is twice as high as wide: gh / gw = (fbH / fbW) / 2
  int gw = cols, gh = (int)((int64_t)gw * fbH / (fbW * 2));
  if (gh > rows) {
    gh = rows;
    gw = (int)((int64_t)gh * fbW * 2 / fbH);
  }
  gw_ = std::max(1, std::min(gw, cols));
  gh_ = std::max(1, std::min(gh, rows));
  ox_ = (cols - gw_) / 2;
  oy_ = (rows - gh_) / 2;
  cells_.assign((size_t)gw_ * gh_, Cell{});
  prev_.assign((size_t)gw_ * gh_, Cell{});
  redraw_ = true;
}

// The most common lit color of a rectangle (bins of RGB222), averaged over
// the pixels of that bin; or, with brightColor, the bin with the most light
uint32_t Converter::cellColor(const uint16_t *fb, uint32_t stride, int x0,
                              int y0, int x1, int y1) const {
  uint32_t count[64] = {}, sumR[64] = {}, sumG[64] = {}, sumB[64] = {},
           weight[64] = {};
  for (int y = y0; y < y1; y++) {
    const uint16_t *row =
        (const uint16_t *)((const uint8_t *)fb + (size_t)y * stride);
    const uint8_t *lum = &luma_[(size_t)y * fbW_];
    for (int x = x0; x < x1; x++) {
      if (lum[x] < opts_.threshold) continue;
      const uint32_t rgb = rgb565SwappedTo888(row[x]);
      const uint32_t r = rgb >> 16, g = (rgb >> 8) & 255, b = rgb & 255;
      const int bin = (int)((r >> 6) << 4 | (g >> 6) << 2 | (b >> 6));
      count[bin]++;
      weight[bin] += lum[x];
      sumR[bin] += r;
      sumG[bin] += g;
      sumB[bin] += b;
    }
  }
  int best = -1;
  uint32_t bestV = 0;
  for (int i = 0; i < 64; i++) {
    const uint32_t v = opts_.brightColor ? weight[i] : count[i];
    if (v > bestV) {
      bestV = v;
      best = i;
    }
  }
  if (best < 0) return 0;
  const uint32_t n = count[best];
  return ((sumR[best] / n) << 16) | ((sumG[best] / n) << 8) | (sumB[best] / n);
}

// The sw x sh sub-pixels of a rectangle, bit (j * sw + i) set when any pixel
// of that sub-box is lit. "Any" rather than the average: a wireframe line is
// one pixel wide and would otherwise vanish in a coarse grid.
uint32_t Converter::pattern(int x0, int y0, int x1, int y1, int sw,
                            int sh) const {
  uint32_t pat = 0;
  const int w = x1 - x0, h = y1 - y0;
  for (int j = 0; j < sh; j++) {
    const int sy0 = y0 + j * h / sh,
              sy1 = std::min(y1, std::max(sy0 + 1, y0 + (j + 1) * h / sh));
    for (int i = 0; i < sw; i++) {
      const int sx0 = x0 + i * w / sw,
                sx1 = std::min(x1, std::max(sx0 + 1, x0 + (i + 1) * w / sw));
      bool lit = false;
      for (int y = sy0; y < sy1 && !lit; y++) {
        const uint8_t *lum = &luma_[(size_t)y * fbW_];
        for (int x = sx0; x < sx1; x++) {
          if (lum[x] >= opts_.threshold) {
            lit = true;
            break;
          }
        }
      }
      if (lit) pat |= 1u << (j * sw + i);
    }
  }
  return pat;
}

void Converter::convert(const uint16_t *fb, int w, int h, uint32_t stride) {
  if (w != fbW_ || h != fbH_)
    setScreen(cols_ ? cols_ : 80, rows_ ? rows_ : 24, w, h);
  luma_.resize((size_t)w * h);
  for (int y = 0; y < h; y++) {
    const uint16_t *row =
        (const uint16_t *)((const uint8_t *)fb + (size_t)y * stride);
    uint8_t *lum = &luma_[(size_t)y * w];
    for (int x = 0; x < w; x++) lum[x] = lumaOf(rgb565SwappedTo888(row[x]));
  }
  int sw = 3, sh = 6;
  subGrid(opts_.mode, &sw, &sh);
  for (int cy = 0; cy < gh_; cy++) {
    const int y0 = cy * h / gh_,
              y1 = std::min(h, std::max(y0 + 1, (cy + 1) * h / gh_));
    for (int cx = 0; cx < gw_; cx++) {
      const int x0 = cx * w / gw_,
                x1 = std::min(w, std::max(x0 + 1, (cx + 1) * w / gw_));
      Cell &c = cells_[(size_t)cy * gw_ + cx];
      const uint32_t pat = pattern(x0, y0, x1, y1, sw, sh);
      switch (opts_.mode) {
        case Mode::ASCII:
          c.ch = table_[pat];
          c.fg = pat ? cellColor(fb, stride, x0, y0, x1, y1) : 0;
          c.bg = 0;
          break;
        case Mode::BRAILLE: {
          uint32_t bits = 0;
          for (int j = 0; j < 4; j++)
            for (int i = 0; i < 2; i++)
              if (pat & (1u << (j * 2 + i))) bits |= BRAILLE_BIT[j][i];
          c.ch = bits ? 0x2800 + bits : ' ';
          c.fg = bits ? cellColor(fb, stride, x0, y0, x1, y1) : 0;
          c.bg = 0;
          break;
        }
        case Mode::HALF: {
          const int ym = y0 + std::max(1, (y1 - y0) / 2);
          const uint32_t top =
              (pat & 1) ? cellColor(fb, stride, x0, y0, x1, std::min(ym, y1))
                        : 0;
          const uint32_t bot =
              (pat & 2) && ym < y1 ? cellColor(fb, stride, x0, ym, x1, y1) : 0;
          if (top) {
            c.ch = 0x2580;  // upper half block
            c.fg = top;
            c.bg = bot;
          } else {
            c.ch = ' ';
            c.fg = 0;
            c.bg = bot;
          }
          break;
        }
      }
    }
  }
}

void Converter::overlay(int x, int y, const char *text) {
  if (y < 0 || y >= gh_) return;
  for (; *text && x < gw_; text++, x++) {
    if (x < 0) continue;
    Cell &c = cells_[(size_t)y * gw_ + x];
    c.ch = (uint8_t)*text;
    c.fg = 0xFFFFFF;
    c.bg = 0;
  }
}

std::string Converter::dump() const {
  Converter tmp;
  tmp.opts_ = opts_;
  for (int cy = 0; cy < gh_; cy++) {
    uint32_t curFg = 0xFFFFFFFF, curBg = 0xFFFFFFFF;
    if (opts_.mode != Mode::HALF) {
      tmp.appendColor(0, true);
      curBg = 0;
    }
    for (int cx = 0; cx < gw_; cx++) {
      const Cell &c = cells_[(size_t)cy * gw_ + cx];
      if (opts_.mode == Mode::HALF && c.bg != curBg) {
        tmp.appendColor(c.bg, true);
        curBg = c.bg;
      }
      if (c.ch != ' ' && c.fg != curFg) {
        tmp.appendColor(c.fg, false);
        curFg = c.fg;
      }
      tmp.appendChar(c.ch);
    }
    tmp.out_ += "\x1b[0m\n";
  }
  return tmp.out_;
}

void Converter::appendColor(uint32_t rgb, bool background) {
  char buf[32];
  const int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  if (opts_.palette == Palette::TRUECOLOR) {
    std::snprintf(buf, sizeof(buf), "\x1b[%d;2;%d;%d;%dm", background ? 48 : 38,
                  r, g, b);
  } else {
    const int idx = 16 + 36 * (r * 5 / 255) + 6 * (g * 5 / 255) + (b * 5 / 255);
    std::snprintf(buf, sizeof(buf), "\x1b[%d;5;%dm", background ? 48 : 38, idx);
  }
  out_ += buf;
}

void Converter::appendChar(uint32_t ch) {
  if (ch < 0x80) {
    out_ += (char)ch;
  } else if (ch < 0x800) {
    out_ += (char)(0xC0 | (ch >> 6));
    out_ += (char)(0x80 | (ch & 0x3F));
  } else {
    out_ += (char)(0xE0 | (ch >> 12));
    out_ += (char)(0x80 | ((ch >> 6) & 0x3F));
    out_ += (char)(0x80 | (ch & 0x3F));
  }
}

const std::string &Converter::emit() {
  out_.clear();
  out_ += "\x1b[?2026h";  // synchronized output: no tearing where supported
  const bool half = opts_.mode == Mode::HALF;
  if (redraw_) {
    // Black under everything, the margins around the grid included: a
    // terminal with its own background color would otherwise show it
    // through. Spaces rather than "CSI 2 J", which not every terminal
    // fills with the current background.
    out_ += "\x1b[0m";
    appendColor(0, true);
    for (int r = 0; r < rows_; r++) {
      char pos[16];
      std::snprintf(pos, sizeof(pos), "\x1b[%d;1H", r + 1);
      out_ += pos;
      out_.append((size_t)cols_, ' ');
    }
    std::fill(prev_.begin(), prev_.end(), Cell{'\0', 0, 0});  // nothing matches
    redraw_ = false;
  }
  out_ += "\x1b[0m";
  if (!half) appendColor(0, true);  // the black background, once per frame
  uint32_t curFg = 0xFFFFFFFF, curBg = half ? 0xFFFFFFFF : 0;
  int curX = -1, curY = -1;  // where the cursor is, in grid cells
  char buf[32];
  for (int cy = 0; cy < gh_; cy++) {
    for (int cx = 0; cx < gw_; cx++) {
      const Cell &c = cells_[(size_t)cy * gw_ + cx];
      Cell &p = prev_[(size_t)cy * gw_ + cx];
      if (c == p) continue;
      if (curX != cx || curY != cy) {
        std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", oy_ + cy + 1,
                      ox_ + cx + 1);
        out_ += buf;
      }
      if (half && c.bg != curBg) {
        appendColor(c.bg, true);
        curBg = c.bg;
      }
      if (c.ch != ' ' && c.fg != curFg) {
        appendColor(c.fg, false);
        curFg = c.fg;
      }
      appendChar(c.ch);
      curX = cx + 1;
      curY = cy;
      p = c;
    }
  }
  out_ += "\x1b[0m\x1b[?2026l";
  return out_;
}

}  // namespace ds::aa
