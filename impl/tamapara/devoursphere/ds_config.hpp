#ifndef DS_CONFIG_HPP
#define DS_CONFIG_HPP

// Tunables of the Tamagotchi Paradise front end. See ../SPEC.md for the
// reasoning. The Xiamocon, PicoSystem and ESPboy front ends have a file of
// the same name with the same members, which is what lets them share
// profiler.cpp (impl/xiamocon/devoursphere/src/).

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/config.hpp"

namespace ds {

constexpr int SCREEN_W = 128;
constexpr int SCREEN_H = 128;

// One band buffer of 32 rows (8 KB): the panel is fed by the CPU through
// SPI1's FIFO, so a second buffer would overlap nothing. The ESPboy's
// reasoning for 32 rows holds here too.
constexpr int BAND_H = 32;
constexpr int BAND_COUNT = SCREEN_H / BAND_H;
static_assert(SCREEN_H % BAND_H == 0, "the bands must tile the screen exactly");

// Working memory of the 3D renderer (the ESPboy's figure; the 128 KB SRAM
// would allow more, the CPU is what limits the scene)
constexpr size_t ARENA_SIZE = 16 * 1024;

// Triangles a frame may spend on full entity bodies
constexpr int DETAIL_TRIANGLES = 72;

// Spans held per scanline
constexpr int SPAN_CAPACITY = 64;

// The simulation runs at a fixed rate whatever the frame rate is
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// Ticks a single frame may catch up on
constexpr int MAX_CATCHUP = 3;

// The main stack: the top 16 KB of the SRAM (0x1801C000..0x18020000). The
// stack watch fills this much below the top with a pattern at start-up.
constexpr uint32_t STACK_TOP = 0x18020000u;
constexpr uint32_t STACK_BYTES = 16 * 1024;

// The core clock after hw_init (the PLL the firmware itself uses); SysTick
// counts it, 1 ms per interrupt
constexpr uint32_t SYS_CLOCK_KHZ = 96000;
constexpr uint32_t SYSTICK_RELOAD = SYS_CLOCK_KHZ - 1;

// --- The dial -----------------------------------------------------------
// Quadrature steps per detent: the Paradise's dial gives one step per click
// (the hello program's bar, measured on the device 2026-11-07)
constexpr int DETENT_STEPS = 1;
// The steering lever: clicks from the middle to the stop on either side
// (counterclockwise left, clockwise right)
constexpr int LEVER_MAX = 5;

// --- Holds --------------------------------------------------------------
// B and C count as a pair (the pause) only when they went down within this
// time of each other; C joining a long-held B is a brake, not a pause
constexpr uint32_t PAUSE_PAIR_MS = 200;
// C pressed alone and released within this time on the title or the pause
// screen toggles the sound (a longer press, or one B joined, does not)
constexpr uint32_t MUTE_PRESS_MS = 400;
// The B and C pair held this long on the title starts the benchmark (the
// core wants B for three seconds; the dial's push is taken by the way out
// below)
constexpr uint32_t BENCH_HOLD_MS = 3000;
// The dial held this long on the title or the pause screen goes back to
// the Paradise
constexpr uint32_t LEAVE_HOLD_MS = 2000;

}  // namespace ds

#endif
