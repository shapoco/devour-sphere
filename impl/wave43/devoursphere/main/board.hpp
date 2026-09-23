#ifndef DS_BOARD_HPP
#define DS_BOARD_HPP

// Bringing the wave_43 up: the backlight, the MIPI-DSI panel (an ST7701
// behind two lanes) and the GT911 touch controller. What M5Unified does for
// the Tab5 port, done here on the IDF's own drivers, with the panel's init
// sequence and timings taken from Kern's BSP for the same board
// (~/Kern/components/wave_43/wave_43.c), which runs them on IDF 6.1.
//
// Nothing here draws: the panel is left scanning out its framebuffer, and
// PanelOut writes into that with the PPA.

#include <cstdint>

#include "ds_config.hpp"

namespace ds {

// One finger, in the panel's own portrait pixels (x across the 480, y down
// the 800), as the GT911 reports it. The mapping into the landscape frame is
// touch_pad.cpp's, because it follows PANEL_ROTATION.
struct TouchPoint {
  int32_t id;  // the controller's track id: the same finger keeps it
  int x, y;
};

namespace board {

// The panel, the backlight and the touch, in that order. False if the panel
// could not be brought up (the touch is allowed to fail: the game still
// runs, showing its title demo, and the log says why).
bool init();

// The DSI framebuffer: PANEL_W x PANEL_H RGB565, little-endian, no row
// padding. Null before init().
uint16_t *framebuffer();
uint32_t framebufferBytes();

// Start the task that polls the touch controller, pinned to core0 at a
// priority above the frame loop's so that its 100 Hz wakeups are on time.
// It spends its time blocked on the I2C bus, not on the CPU.
void startTouch();

// The points held down as of the last read, copied out under a lock. Returns
// how many were written (at most `max`).
int touchPoints(TouchPoint *out, int max);

}  // namespace board
}  // namespace ds

#endif
