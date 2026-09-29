#ifndef DS_CONFIG_HPP
#define DS_CONFIG_HPP

// Tunables of the wave_43 front end (Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3).
// See impl/wave43/SPEC.md for the reasoning behind the values.

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/config.hpp"

namespace ds {

// --- Geometry ---------------------------------------------------------------
// The panel is a 480x800 portrait raster (its MIPI-DSI framebuffer), used in
// landscape. The game is drawn into a 640x384 landscape frame and the PPA
// scales it by 5/4 and turns it a quarter on its way to the panel
// (panel_out.cpp), in one hardware operation -- so the rotation is free and
// the resolution is purely a question of how much rasterizing the CPU can
// afford.
//
// 640x384 is the Tab5's 640x360 frame give or take 7%, which that board
// rasterizes at 40-50 fps; the panel's own 800x480 would be 1.56 times the
// pixels. 5/4 is the one ratio between the two that lands both axes on whole
// pixels (800 / 1.25 = 640, 480 / 1.25 = 384) and is on the PPA's 1/16 grid.
constexpr int PANEL_W = 480;
constexpr int PANEL_H = 800;

// Which way round landscape is, in M5GFX's numbering (the Tab5 port uses the
// same, so the two read alike): 1 is the landscape picture turned a quarter
// clockwise onto the panel, a landscape pixel (u, v) of the scaled 800x480
// picture landing on panel pixel (479 - v, u) -- 270 on the PPA's
// counter-clockwise dial. 3 is a quarter counter-clockwise, (v, 799 - u),
// which is 90. The PPA's angle, the band offsets and the touch mapping all
// come from this one value, so the picture and the pad cannot disagree.
//
// Which one is the right way up depends on how you hold the board, and
// nothing in the software can tell. 1 comes out right on hardware
// (2026-09-23).
constexpr int PANEL_ROTATION = 1;
static_assert(PANEL_ROTATION == 1 || PANEL_ROTATION == 3,
              "landscape is rotation 1 or 3");

// The scale as a fraction, for the integer arithmetic on offsets and touch
constexpr int SCALE_NUM = 5;
constexpr int SCALE_DEN = 4;
constexpr float SCALE = (float)SCALE_NUM / SCALE_DEN;
constexpr int SCREEN_W = PANEL_H * SCALE_DEN / SCALE_NUM;  // 640
constexpr int SCREEN_H = PANEL_W * SCALE_DEN / SCALE_NUM;  // 384
static_assert(SCREEN_W * SCALE_NUM == PANEL_H * SCALE_DEN &&
                  SCREEN_H * SCALE_NUM == PANEL_W * SCALE_DEN,
              "the scale must land both axes on whole pixels");

// Rows of the landscape frame rasterized and handed to the PPA at a time,
// tried tallest first until a pair of band buffers fits the internal heap
// (PanelOut::init, which traces the one it got). Taller means longer runs in
// each panel row and fewer PPA operations; the Tab5 found the ceiling at
// 45 rows x 640 there (60 would not allocate: a band buffer is one
// contiguous block and the internal heap is several regions), but this build
// has no M5 libraries in it, so where it lands here is for the log to say.
//
// Each must be a multiple of 4, so that a band is a whole number of rows
// after the 5/4 scale and every band starts on the same phase of it -- the
// PPA's interpolation restarts at each band, and a band starting mid-period
// would show as a seam -- and must divide the frame's 384 rows.
constexpr int BAND_H_CHOICES[] = {48, 32, 24, 16};
constexpr int BAND_H_MAX = BAND_H_CHOICES[0];
constexpr bool bandChoicesOk() {
  for (int h : BAND_H_CHOICES) {
    if (h % SCALE_DEN != 0 || SCREEN_H % h != 0 || h > BAND_H_MAX) return false;
  }
  return true;
}
static_assert(bandChoicesOk(),
              "each band height must be a whole number of 5/4 periods and "
              "tile the frame");

// --- The renderer's working memory ------------------------------------------
// The Tab5's measurement at 640x360 (impl/m5tab5/ds_config.hpp): with
// ShapoGFX d538138's smaller records, 24 KB is where buildScene() still gets
// everything it asks for, and what is spent is how many primitives there are,
// not how many pixels they cover -- so 24 more rows do not move it. 32 KB
// keeps a third over that, as the Tab5 does, and leaves the internal heap the
// band buffers come out of 16 KB more.
constexpr size_t ARENA_SIZE = 32 * 1024;

// Spans held per scanline. The measured peak is 33 at 640x360 (36 at
// 1280x720): what sets it is how many primitives cross one scanline, not how
// long the scanline is. Overflowing drops spans and leaves holes.
constexpr int SPAN_CAPACITY = 128;

// --- Timing -----------------------------------------------------------------
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// Ticks a single frame may catch up on. Beyond this the surplus is dropped,
// so a long stall cannot turn into a burst of simulation.
constexpr int MAX_CATCHUP = 4;

// core1 runs the simulation while core0 builds the scene, rasterizes the
// bands and hands them to the PPA -- the Tab5's arrangement. Set to 0 to put
// everything on core0, which is slower but tells you whether a problem is
// the split.
#ifndef DS_SIM_ON_CORE1
#define DS_SIM_ON_CORE1 1
#endif

// --- The touch panel --------------------------------------------------------
// How often the touch task reads the GT911. Its INT line is not wired on this
// board, so it is polled; 100 Hz is above any frame rate this reaches, and a
// read at 400 kHz is about a millisecond the task spends blocked on the bus,
// not on the CPU.
constexpr uint32_t TOUCH_PERIOD_MS = 10;
constexpr int TOUCH_MAX_POINTS = 5;

// --- The virtual pad --------------------------------------------------------
// The WASM front end's landscape layout, in the pixels of the landscape
// frame: a direction disc in the bottom left, A in the bottom right and B
// above and left of it, drawn translucent over the game. The sizes are the
// web page's own against its 360 px tall view (a 150 px disc, 96 px A, 72 px
// B), kept in proportion to this frame's 384 rows. The panel's pixels are
// about 0.117 mm, so a frame pixel is 0.146 mm and the disc comes out 23 mm
// across.
struct Circle {
  int cx, cy, r;
};
constexpr int PAD_EDGE = 10;  // from the frame's edge
constexpr Circle PAD_DISC = {PAD_EDGE + 80, SCREEN_H - PAD_EDGE - 80, 80};
constexpr Circle PAD_A = {SCREEN_W - PAD_EDGE - 51, SCREEN_H - PAD_EDGE - 51,
                          51};
constexpr Circle PAD_B = {PAD_A.cx - PAD_A.r - 38 - PAD_EDGE,
                          PAD_A.cy - PAD_A.r - 38 + 26, 38};
// Pause: a small button in the top right corner, the one control the game
// needs that the disc and the two buttons cannot give (the mute and the
// timing overlay are DOWN and UP on the title / pause screen, as everywhere
// else). HUD_INSET_TOP keeps the HUD's top row clear of it.
constexpr Circle PAD_PAUSE = {SCREEN_W - PAD_EDGE - 17, PAD_EDGE + 17, 17};

// The disc is analog and reads like play.js's (roundAxes): the strength is
// the knob's distance from the center, nothing up to PAD_DEAD_R and full
// from PAD_FULL_R, in any direction (touch_pad.cpp). The thresholds are
// play.js's as fractions of the radius (it expresses them against the
// diameter: the knob travels up to 0.32 w, dead 0.06 w, full 0.28 w).
constexpr int PAD_KNOB_R = PAD_DISC.r * 64 / 100;
constexpr int PAD_DEAD_R = PAD_DISC.r * 12 / 100;
constexpr int PAD_FULL_R = PAD_DISC.r * 56 / 100;
// Each component of the direction carried onto a square is 0 up to
// PAD_AXIS_DEAD of the larger one (about 8.5 degrees off an axis: no weak
// dash or brake leaks into a turn) and full from PAD_AXIS_FULL (about 31
// degrees): within about 14 degrees of a diagonal both axes are full, as
// with two keys (play.js's AXIS_DEAD / AXIS_FULL)
constexpr float PAD_AXIS_DEAD = 0.15f;
constexpr float PAD_AXIS_FULL = 0.6f;

// Opacity of the pad over the game (play.js uses 0.55 in landscape)
constexpr int PAD_OPACITY = 140;  // of 255

// What the HUD has to keep out of (Renderer::setHudInsets). The side pair
// applies to the bottom row alone, which is the only one the pad reaches:
// the disc's right edge and A's left edge, plus a little air. B sits above
// that row, and the pause button is what the top inset is for.
constexpr int HUD_INSET_LEFT = PAD_DISC.cx + PAD_DISC.r + 8;
constexpr int HUD_INSET_RIGHT = SCREEN_W - (PAD_A.cx - PAD_A.r) + 8;
constexpr int HUD_INSET_TOP = PAD_PAUSE.cy + PAD_PAUSE.r + 6;
constexpr int HUD_INSET_BOTTOM = 0;

// --- Sound ------------------------------------------------------------------
// Sounds mixed at once, as the Tab5's M5Unified speaker does: every sound a
// tick asks for is played, the way the browser plays them, and a request that
// finds all of them busy is dropped (inaudible under the ones playing).
constexpr int SE_VOICES = 8;

// Frames the mixer hands the codec at a time, and how many such DMA buffers
// the I2S keeps queued. At the pack's 24 kHz, 144 frames are 6 ms, so a
// sound starts at most about 4 x 6 = 24 ms after the tick that asked for it
// -- a little over a frame, against the 60 ms the I2S driver's defaults would
// add.
constexpr int SE_CHUNK_FRAMES = 144;
constexpr int SE_DMA_BUFFERS = 4;

// The codec's output volume, 0-100 (esp_codec_dev_set_out_vol). The mix
// itself is at unity and saturates rather than wrapping, so this is the knob.
// Waveshare's own example plays music at 60.
constexpr int SE_VOLUME = 75;

// --- Backlight --------------------------------------------------------------
constexpr int BACKLIGHT_PERCENT = 100;

}  // namespace ds

#endif
