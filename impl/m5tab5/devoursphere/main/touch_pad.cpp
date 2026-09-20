#include "touch_pad.hpp"

#include <M5Unified.h>

#include "devoursphere/sim/game.hpp"
#include "shapoco/gfx2d/fonts.hpp"

namespace ds {

namespace {
namespace sim = devoursphere::sim;

constexpr int MAX_POINTS = 5;

// The pad's colors. Translucent, like the web page's 0.55 opacity overlay,
// so the game stays visible under it.
const g2::Color PAD_LINE = g2::makeColor(170, 215, 255, PAD_OPACITY);
const g2::Color PAD_LINE_DOWN = g2::makeColor(210, 240, 255, 230);
const g2::Color PAD_FILL_DOWN = g2::makeColor(80, 140, 200, 110);

// One pixel wide, deliberately: everything here is translucent, and two
// concentric outlines blend twice wherever they touch, which comes out as a
// mottled ring rather than a thicker one. A single pixel of the frame is two
// of the panel's after the scale -- the same weight as the web page's 2 px
// border.
void ring(g2::Graphics2D &g, int cx, int cy, int r, g2::Color c) {
  g.drawCircle(cx, cy, r, c);
}

int isqrt32(int32_t v) {
  if (v <= 0) return 0;
  int32_t r = 0, bit = 1 << 15;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return (int)r;
}

}  // namespace

bool TouchPad::inside(const Circle &c, int x, int y) {
  const int dx = x - c.cx, dy = y - c.cy;
  return dx * dx + dy * dy <= c.r * c.r;
}

void TouchPad::track(Grab &g, const Circle &c, const int32_t *ids,
                     const int *xs, const int *ys, int n) {
  // Keep the finger already holding this control, wherever it has moved to
  if (g.id >= 0) {
    for (int i = 0; i < n; i++) {
      if (ids[i] == g.id) {
        g.x = xs[i];
        g.y = ys[i];
        return;
      }
    }
    g.id = -1;  // it lifted
  }
  // Otherwise take one that is inside it and not already holding another
  for (int i = 0; i < n; i++) {
    if (ids[i] == disc_.id || ids[i] == a_.id || ids[i] == b_.id ||
        ids[i] == pause_.id) {
      continue;
    }
    if (inside(c, xs[i], ys[i])) {
      g.id = ids[i];
      g.x = xs[i];
      g.y = ys[i];
      return;
    }
  }
}

uint8_t TouchPad::poll() {
  int32_t ids[MAX_POINTS];
  int xs[MAX_POINTS], ys[MAX_POINTS];
  int n = 0;
  // getDetail(), not getTouchPointRaw(): the raw list is what the controller
  // reported, in its own coordinates, while the detail is what M5 has put
  // through the display's rotation -- which is the rotation the frame is
  // turned by, so these land on the pad we drew. getCount() also counts the
  // points that were just released, hence the isPressed().
  const int count = M5.Touch.getCount();
  for (int i = 0; i < count && n < MAX_POINTS; i++) {
    const auto &t = M5.Touch.getDetail(i);
    if (!t.isPressed()) continue;
    ids[n] = t.id;
    // Landscape pixels; the frame is half that on each axis.
    xs[n] = t.x / SCALE;
    ys[n] = t.y / SCALE;
    n++;
  }
  count_ = n;

  // The order matters only in that a finger can hold one control at a time;
  // the disc goes first because it is the one you put a thumb on blind.
  track(disc_, PAD_DISC, ids, xs, ys, n);
  track(a_, PAD_A, ids, xs, ys, n);
  track(b_, PAD_B, ids, xs, ys, n);
  track(pause_, PAD_PAUSE, ids, xs, ys, n);

  uint8_t bits = 0;
  dir_ = 0;
  knobX_ = knobY_ = 0;
  if (disc_.id >= 0) {
    int dx = disc_.x - PAD_DISC.cx, dy = disc_.y - PAD_DISC.cy;
    const int len = isqrt32(dx * dx + dy * dy);
    // The knob follows the finger up to its own travel, for the feedback
    if (len > PAD_KNOB_R && len > 0) {
      dx = dx * PAD_KNOB_R / len;
      dy = dy * PAD_KNOB_R / len;
    }
    knobX_ = dx;
    knobY_ = dy;
    if (len > PAD_DEAD_R) {
      // Each axis on its own, so the diagonals give dash-while-turning
      const int d = disc_.x - PAD_DISC.cx, e = disc_.y - PAD_DISC.cy;
      if (d < -PAD_AXIS_R) dir_ |= sim::Button::LEFT;
      if (d > PAD_AXIS_R) dir_ |= sim::Button::RIGHT;
      if (e < -PAD_AXIS_R) dir_ |= sim::Button::UP;
      if (e > PAD_AXIS_R) dir_ |= sim::Button::DOWN;
    }
  }
  bits |= dir_;
  if (a_.id >= 0) bits |= sim::Button::A;
  if (b_.id >= 0) bits |= sim::Button::B;

  // PAUSE toggles the game, so it has to arrive as a single tick's press --
  // held down it would toggle every tick.
  const bool pauseDown = pause_.id >= 0;
  if (pauseDown && !pausePrev_) bits |= sim::Button::PAUSE;
  pausePrev_ = pauseDown;
  return bits;
}

void TouchPad::drawButton(g2::Graphics2D &g, int oy, const Circle &c, bool down,
                          const char *label) const {
  const g2::Color line = down ? PAD_LINE_DOWN : PAD_LINE;
  if (down) g.fillCircle(c.cx, c.cy - oy, c.r - 2, PAD_FILL_DOWN);
  ring(g, c.cx, c.cy - oy, c.r, line);
  if (label && *label) {
    g.setFont(&ShapoSansP_s12c09a01w02, 2);
    const int tw = g.measureText(label);
    g.setTextColor(line);
    g.drawString(c.cx - tw / 2, c.cy - oy - g.textHeight() / 2, label);
  }
}

void TouchPad::drawDisc(g2::Graphics2D &g, int oy) const {
  const bool down = disc_.id >= 0;
  const g2::Color line = down ? PAD_LINE_DOWN : PAD_LINE;
  ring(g, PAD_DISC.cx, PAD_DISC.cy - oy, PAD_DISC.r, line);
  // The cross the web page draws inside the disc, so which way is which is
  // visible without pressing it. Drawn as three rectangles rather than two
  // crossing ones: the overlap would be blended twice and show as a bright
  // square in the middle.
  const int arm = PAD_DISC.r * 85 / 100;
  const int th = PAD_DISC.r * 20 / 100;
  const int cx = PAD_DISC.cx, cy = PAD_DISC.cy - oy;
  g.fillRect(cx - arm, cy - th / 2, 2 * arm, th, PAD_LINE);
  g.fillRect(cx - th / 2, cy - arm, th, arm - th / 2, PAD_LINE);
  g.fillRect(cx - th / 2, cy + th - th / 2, th, arm - th / 2, PAD_LINE);
  // The knob, where the finger is
  const int kx = PAD_DISC.cx + knobX_, ky = PAD_DISC.cy + knobY_ - oy;
  const int kr = PAD_DISC.r * 31 / 100;  // the web page's 46 px of 150
  if (down) g.fillCircle(kx, ky, kr - 2, PAD_FILL_DOWN);
  ring(g, kx, ky, kr, line);
}

void TouchPad::draw(const g2::Surface &band, int bandY) const {
  g2::Graphics2D g(band);
  // Everything is given in frame coordinates and the band starts at bandY,
  // so each call shifts by that; anything outside is clipped away.
  drawDisc(g, bandY);
  drawButton(g, bandY, PAD_A, a_.id >= 0, "A");
  drawButton(g, bandY, PAD_B, b_.id >= 0, "B");
  // The pause button carries two bars rather than a letter
  {
    const Circle &c = PAD_PAUSE;
    const bool down = pause_.id >= 0;
    const g2::Color line = down ? PAD_LINE_DOWN : PAD_LINE;
    ring(g, c.cx, c.cy - bandY, c.r, line);
    const int bw = c.r * 22 / 100, bh = c.r * 90 / 100;
    g.fillRect(c.cx - bw * 2, c.cy - bandY - bh / 2, bw, bh, line);
    g.fillRect(c.cx + bw, c.cy - bandY - bh / 2, bw, bh, line);
  }
}

}  // namespace ds
