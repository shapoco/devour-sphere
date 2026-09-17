#ifndef DS_CONFIG_HPP
#define DS_CONFIG_HPP

// Tunables of the PicoSystem front end. See ../SPEC.md for the reasoning
// behind the values. The Xiamocon front end has a file of the same name with
// the same members, which is what lets the two share profiler.cpp.

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/config.hpp"

namespace ds {

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 240;

// The frame is rasterized and pushed one band at a time, so the device never
// holds a whole frame buffer. 240 / 40 = 6 bands, as on the Xiamocon RP2350:
// the band overhead measured there against a single full-frame band is about
// +5% at 40 rows (+14% at 20, +36% at 8), and an even band count keeps the
// ping-pong parity simple.
constexpr int BAND_H = 40;
constexpr int BAND_COUNT = SCREEN_H / BAND_H;
static_assert(SCREEN_H % BAND_H == 0, "the bands must tile the screen exactly");

// Working memory of the 3D renderer. What binds is the triangle buffer it is
// divided into: buildScene() gives the entities their full bodies only while
// that buffer has room beyond a reserve for what is drawn after them. With
// the stars and the sphere wireframe drawn outside the 3D pipeline only the
// world and the overlays are in it; 40 KB leaves 29 KB of buffer after the
// fixed part and SPAN_CAPACITY spans (10.8 KB). 32 KB was tried and drew
// every entity as an outline: 21 KB minus the reserve was nothing. The
// Xiamocon build keeps 64 KB because it has the RAM.
constexpr size_t ARENA_SIZE = 40 * 1024;

// Triangles a frame may spend on full entity bodies (Renderer::
// setDetailTriangles). Each body is up to 18 triangles and costs about a
// millisecond through the float vertex pipeline here, so a crowded screen
// fell from 28 to 20 fps with no cap. 144 keeps the eight nearest bodies
// and draws the rest as outlines. 0 lifts the cap.
constexpr int DETAIL_TRIANGLES = 144;

// Spans held per scanline. Measured peak at 240x240 is 32; 128 is the value
// the Xiamocon build settled on. Overflowing drops spans, which leaves holes
// in the picture, so this is not a number to shave.
constexpr int SPAN_CAPACITY = 128;

// The simulation runs at a fixed rate whatever the frame rate is
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// Ticks a single frame may catch up on. 4 ticks = 133 ms at 30 Hz covers any
// realistic hitch; beyond that the surplus is dropped so a long stall cannot
// turn into a burst of simulation.
constexpr int MAX_CATCHUP = 4;

// The PicoSystem SDK runs the RP2040 at 250 MHz with the core at 1.20 V, and
// every timing figure in ../SPEC.md assumes that. Set to 0 for the stock
// 125 MHz, which halves everything.
#ifndef DS_OVERCLOCK
#define DS_OVERCLOCK 1
#endif
constexpr uint32_t SYS_CLOCK_KHZ = DS_OVERCLOCK ? 250000 : 125000;

// Display SPI clock. 62.5 MHz is what both the PicoSystem SDK (through its
// PIO program) and the Xiamocon SDK run the ST7789 at; a full 240x240x2
// frame takes 14.75 ms at this rate, which is the frame rate ceiling.
constexpr uint32_t SPI_HZ = 62500000;
// Command clock: what the PicoSystem SDK initialises the panel at. Commands
// are few and short, so this costs nothing per band.
constexpr uint32_t CMD_HZ = 8000000;

// core1 runs the simulation while core0 builds the scene, rasterizes the
// bands and pushes them, exactly as on Xiamocon. Set to 0 to put everything
// on core0 -- slower, but a way to tell whether a problem is the split.
#ifndef DS_SIM_ON_CORE1
#define DS_SIM_ON_CORE1 1
#endif

}  // namespace ds

#endif
