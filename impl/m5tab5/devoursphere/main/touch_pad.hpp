#ifndef DS_TOUCH_PAD_HPP
#define DS_TOUCH_PAD_HPP

// The virtual game pad: the WASM front end's landscape layout (play.js,
// setupTouchPad) brought over as it is -- a direction disc in the bottom
// left, A in the bottom right, B above and left of it, and a pause button in
// the top right corner that the web page does not need (it has a keyboard).
//
// The disc is a disc and not a D-pad on purpose: the direction comes from
// the offset of the touch from its center, with left/right and up/down
// decided separately, so dashing while turning and braking into a quick turn
// both work. The thresholds are play.js's, as fractions of the radius
// (ds_config.hpp).
//
// Each control captures the finger that pressed it, the way the web page's
// pointer capture does: once the disc has a finger it keeps steering with it
// even when it slides off, until that finger lifts. That is what makes the
// disc usable without looking at it.
//
// Coordinates come from M5.Touch in the display's logical (landscape)
// rotation, which is the same rotation panel_out.cpp turns the frame by, and
// are halved into the 640x360 frame the pad is drawn in.

#include <cstdint>

#include "ds_config.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"

namespace ds {

namespace g2 = shapoco::gfx2d;

class TouchPad {
 public:
  // Read the touch points and return the sim::Button bits they mean. Call
  // once per frame, after M5.update().
  uint8_t poll();

  // Draw the pad into a band of the frame (PanelOut::OverlayFn). Uses the
  // state the last poll() left, so the controls light up as they are held.
  void draw(const g2::Surface &band, int bandY) const;

  // For the overlay's own line
  int touchCount() const { return count_; }

 private:
  struct Grab {
    int32_t id = -1;  // the touch point holding this control, -1 for none
    int x = 0, y = 0;  // where it is now, in frame pixels
  };
  Grab disc_, a_, b_, pause_;
  uint8_t dir_ = 0;      // the disc's direction bits, for the knob
  int knobX_ = 0, knobY_ = 0;
  int count_ = 0;
  bool pausePrev_ = false;  // the pause button is an edge, not a level

  static bool inside(const Circle &c, int x, int y);
  // Take or keep a control's finger. `points` is the frame-space touch list.
  void track(Grab &g, const Circle &c, const int32_t *ids, const int *xs,
             const int *ys, int n);
  void drawDisc(g2::Graphics2D &g, int oy) const;
  void drawButton(g2::Graphics2D &g, int oy, const Circle &c, bool down,
                  const char *label) const;
};

}  // namespace ds

#endif
