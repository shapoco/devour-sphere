#ifndef DS_CONFIG_HPP
#define DS_CONFIG_HPP

// Tunables of the M5StickS3 front end. See impl/m5sticks3/SPEC.md for the
// reasoning behind the values.

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/config.hpp"

namespace ds {

// --- Geometry ---------------------------------------------------------------
// The panel is a 135x240 portrait ST7789, used in landscape: the stick is
// tipped onto its side to play. The rotation is a MADCTL bit, so the ST7789
// itself does the turning and the bands are landscape rows that go out as
// they are -- no pass over the pixels anywhere (unlike the Tab5, which has
// to feed a portrait DSI framebuffer through the PPA).
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;

// Which way round landscape is. M5GFX's rotations 1 and 3 are both
// landscape and differ by half a turn; here the player picks between them
// at start up by tipping the stick, so unlike every other front end this is
// not a constant but the one thing the boot screen is for (attitude.hpp).
//
// Tipping the stick clockwise (the top going right) and tipping it
// anti-clockwise want opposite rotations, and which rotation belongs to
// which way round cannot be told from the software -- it depends on how the
// panel is mounted. ROTATION_TOP_RIGHT is the one used when the stick's top
// ends up on the player's right; flipping this one constant swaps both
// cases at once, and the tilt axes follow it (attitude.cpp).
//
// **1 is the one that comes out the right way up on hardware** (2026-09-21).
constexpr int ROTATION_TOP_RIGHT = 1;
constexpr int ROTATION_TOP_LEFT = 4 - ROTATION_TOP_RIGHT;  // 3
static_assert(ROTATION_TOP_RIGHT == 1 || ROTATION_TOP_RIGHT == 3,
              "landscape is rotation 1 or 3");

// Rows rasterized and pushed at a time. 135 = 27 * 5, so a band height has
// to divide it: 45 (3 bands) or 27 (5). 45 costs 43 KB for the pair out of
// the internal heap and makes the fewest transactions, which is what the
// other ESP32S3 board found mattered -- there a per-band setWindow ran
// through a queue-to-another-task path and six bands cost 4.4 ms of command
// time against three bands' 2.4 (impl/xiamocon/SPEC.md). M5GFX talks to the
// bus from this task instead, so the per-band cost should be far smaller
// here; the overlay's CMD line is what says whether 27 would do.
constexpr int BAND_H = 45;
constexpr int BAND_COUNT = SCREEN_H / BAND_H;
static_assert(SCREEN_H % BAND_H == 0, "the bands must tile the frame exactly");

// --- The renderer's working memory -------------------------------------------
// What binds is the triangle buffer the arena is divided into, not the
// arena. The figures measured on the other ESP32S3 board at 240x240 apply
// here almost unchanged -- the scene is nearly the same, since the entity
// detail thresholds now follow the screen height (core/SPEC.md) so a
// shorter screen draws the same bodies in fewer pixels: the peak frame used
// 17.0 KB of buffer and the renderer started thinning below 36.9 KB. 64 KB
// keeps 1.5x over that, and with 8 MB of PSRAM holding the simulation there
// is no fight over the internal SRAM here.
constexpr size_t ARENA_SIZE = 64 * 1024;

// Spans held per scanline. The measured peak is 32 at 240x240 and it barely
// moves with the resolution -- what sets it is how many primitives cross one
// scanline. Overflowing drops spans and leaves holes in the picture, so this
// is not a number to shave; the overlay's SPN line reports the real peak.
constexpr int SPAN_CAPACITY = 128;

// --- Timing -------------------------------------------------------------------
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// Ticks a single frame may catch up on. Beyond this the surplus is dropped,
// so a long stall cannot turn into a burst of simulation.
constexpr int MAX_CATCHUP = 4;

// core1 runs the simulation while core0 builds the scene, rasterizes and
// pushes the bands -- the arrangement every other front end uses, for the
// same reason: the ticks happen while the frame goes out. On an ESP32S3 it
// is also the only arrangement that works, because the display answers to
// core0 alone (impl/xiamocon/SPEC.md). Set to 0 to put everything on core0,
// which is slower but tells you whether a problem is the split.
#ifndef DS_SIM_ON_CORE1
#define DS_SIM_ON_CORE1 1
#endif

// --- Sound ------------------------------------------------------------------
// M5Unified's master volume, which its mixer SQUARES: a sample's gain is
// magnification * master^2 * channel^2, so loudness goes as the square of
// this number and its default of 64 is not a midpoint. This is a 1 W
// speaker a hand's width from the player rather than the Tab5's
// room-filling one, but it still wants driving: 64 was quiet on the device
// and 90 (twice the loudness) was still short, so 120 -- 3.5x the default's
// loudness and not quite half the scale. The mixer saturates at the 16-bit
// limit rather than wrapping, so the failure mode above this is clipped
// peaks, not tearing (2026-09-21).
constexpr uint8_t SE_MASTER_VOLUME = 120;

}  // namespace ds

#endif
