#ifndef DS_CONFIG_HPP
#define DS_CONFIG_HPP

// Tunables of the M5Tab5 front end. See impl/m5tab5/SPEC.md for the
// reasoning behind the values.

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/config.hpp"

namespace ds {

// --- Geometry ---------------------------------------------------------------
// The panel is a 720x1280 portrait raster (its MIPI-DSI framebuffer), used
// in landscape. The game is drawn into a 640x360 landscape frame and the PPA
// scales it by two and turns it a quarter clockwise on its way to the panel
// (panel_out.cpp), which is one hardware operation either way -- so the
// rotation is free and the resolution is purely a question of how much
// rasterizing the CPU can afford.
//
// 640x360 rather than the panel's own 1280x720: the scene build is nearly
// resolution independent but the band work is not, and at four times the
// pixels it is what sets the frame time. Measured on the host over the same
// frame, going to 1280x720 leaves beginFrame() flat (0.073 -> 0.084 ms) and
// multiplies the band work by 2.5 -- and a CPU without x86's wide stores
// pays closer to the full 4x. The HUD ends up the same physical size either
// way: 1280x720 magnifies the bitmap fonts by two (UiMetrics::fontMult), and
// so does the scaler here.
constexpr int PANEL_W = 720;
constexpr int PANEL_H = 1280;

// Which way round landscape is. M5GFX's rotations 1 and 3 are both
// landscape and differ by half a turn; which one is the right way up
// depends on how the panel is mounted in the case, and nothing in the
// software can tell. The display, the PPA's angle and the band offsets are
// all derived from this one value (panel_out.cpp), and the touch follows
// the display, so the picture and the pad can never disagree.
//
// Rotation 1 maps a landscape pixel (u, v) to the panel pixel
// (719 - v, u) -- a quarter turn clockwise, which is 270 on the PPA's
// counter-clockwise dial. Rotation 3 is (v, 1279 - u), a quarter
// counter-clockwise, which is 90.
//
// 3 is the one that comes out the right way up on hardware (2026-09-20;
// 1 was upside down).
constexpr int PANEL_ROTATION = 3;
static_assert(PANEL_ROTATION == 1 || PANEL_ROTATION == 3,
              "landscape is rotation 1 or 3");
constexpr int SCALE = 2;
constexpr int SCREEN_W = PANEL_H / SCALE;  // 640, the landscape frame
constexpr int SCREEN_H = PANEL_W / SCALE;  // 360
static_assert(PANEL_H % SCALE == 0 && PANEL_W % SCALE == 0, "integer scale");

// Rows of the landscape frame rasterized and handed to the PPA at a time.
// Each band becomes a vertical strip of the panel SCALE * BAND_H pixels
// wide and the full 1280 tall, so a taller band means longer runs inside
// each panel row (2 * 45 * 2 = 180 bytes at 45) and fewer transactions --
// but two band buffers of it have to come out of the internal heap, and
// that is what sets the ceiling.
//
// 45 rows is 8 bands and 115 KB for the pair, and is what runs on hardware.
// 60 (150 KB) does not: the board comes up with a black screen, which is
// the band buffers failing to allocate and init() giving up (the serial log
// says so). The total free internal heap is not what runs out -- there is
// about 246 KB of it at that point -- but a band buffer has to be ONE
// contiguous block, and the internal heap is several regions. 40 rows
// (100 KB) is the value this started at and is the strip height the LcdTap
// example settled on for the same panel.
constexpr int BAND_H = 45;
constexpr int BAND_COUNT = SCREEN_H / BAND_H;
static_assert(SCREEN_H % BAND_H == 0, "the bands must tile the frame exactly");

// --- The renderer's working memory -------------------------------------------
// What binds is the triangle buffer the arena is divided into, not the arena.
// Measured at 640x360 over levels 1-7 x 3 seeds x 600 ticks with the AI
// driving (12,600 frames, the same run impl/xiamocon/SPEC.md uses), hashing
// every frame. With ShapoGFX d538138 (16-byte spans, smaller records; sizes
// from a 32-bit wasm build of the same run):
//
//     arena    triangle budget   peak used   frames
//     48 KB          42,168 B      9,984 B   identical
//     32 KB          25,784 B      9,984 B   identical
//     24 KB          17,592 B      9,984 B   identical   <- the inflection
//     20 KB          13,496 B      9,828 B   the entities start losing detail
//     16 KB           9,400 B      9,396 B   primitives dropped
//
// (Before that ShapoGFX release the inflection was at 40 KB and this was
// 48 KB.) 32 KB keeps a third over the inflection and 2.6x over the peak
// frame, and leaves the internal heap -- what the band buffers come out of,
// and the one thing this board is actually short of -- 16 KB more.
constexpr size_t ARENA_SIZE = 32 * 1024;

// Spans held per scanline. The measured peak is 33 at 640x360 (36 at
// 1280x720): what sets it is how many primitives cross one scanline, not how
// long the scanline is. Overflowing drops spans and leaves holes, so this is
// not a number to shave.
constexpr int SPAN_CAPACITY = 128;

// --- Timing -------------------------------------------------------------------
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// Ticks a single frame may catch up on. Beyond this the surplus is dropped,
// so a long stall cannot turn into a burst of simulation.
constexpr int MAX_CATCHUP = 4;

// core1 runs the simulation while core0 builds the scene, rasterizes the
// bands and hands them to the PPA -- the arrangement both handhelds use, for
// the same reason: the ticks happen while the frame goes out. Set to 0 to
// put everything on core0, which is slower but tells you whether a problem
// is the split.
#ifndef DS_SIM_ON_CORE1
#define DS_SIM_ON_CORE1 1
#endif

// --- The virtual pad ----------------------------------------------------------
// The WASM front end's landscape layout, in the pixels of the landscape
// frame: a direction disc in the bottom left, A in the bottom right and B
// above and left of it, drawn translucent over the game. The sizes are the
// web page's own (a 150 px disc, 96 px A, 72 px B against a 360 px tall
// view), which on this panel come out at 25.6, 16.4 and 12.3 mm across.
struct Circle {
  int cx, cy, r;
};
constexpr int PAD_EDGE = 10;  // from the frame's edge
constexpr Circle PAD_DISC = {PAD_EDGE + 75, SCREEN_H - PAD_EDGE - 75, 75};
constexpr Circle PAD_A = {SCREEN_W - PAD_EDGE - 48, SCREEN_H - PAD_EDGE - 48,
                          48};
constexpr Circle PAD_B = {PAD_A.cx - PAD_A.r - 36 - PAD_EDGE,
                          PAD_A.cy - PAD_A.r - 36 + 24, 36};
// Pause: a small button in the top right corner, the one control the game
// needs that the disc and the two buttons cannot give (the mute and the
// timing overlay are DOWN and UP on the title / pause screen, as everywhere
// else). HUD_INSET_TOP keeps the HUD's top row clear of it.
constexpr Circle PAD_PAUSE = {SCREEN_W - PAD_EDGE - 16, PAD_EDGE + 16, 16};

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

// --- Sound ------------------------------------------------------------------
// M5Unified's master volume, which its mixer SQUARES: a sample's gain is
// magnification * master^2 * channel^2, so loudness goes as the square of
// this number and its default of 64 is not a midpoint. 64 * sqrt(3) = 111 is
// three times as loud as the default, which is what this speaker wants to be
// heard across a room. The mixer saturates at the 16-bit limit rather than
// wrapping, so raising it further clips the peaks instead of tearing.
constexpr uint8_t SE_MASTER_VOLUME = 111;

// What the HUD has to keep out of (Renderer::setHudInsets). The side pair
// applies to the bottom row alone, which is the only one the pad reaches:
// the disc's right edge and A's left edge, plus a little air. B sits above
// that row, and the pause button is what the top inset is for.
constexpr int HUD_INSET_LEFT = PAD_DISC.cx + PAD_DISC.r + 8;
constexpr int HUD_INSET_RIGHT = SCREEN_W - (PAD_A.cx - PAD_A.r) + 8;
constexpr int HUD_INSET_TOP = PAD_PAUSE.cy + PAD_PAUSE.r + 6;
constexpr int HUD_INSET_BOTTOM = 0;

}  // namespace ds

#endif
