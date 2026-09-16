#ifndef DS_CONFIG_HPP
#define DS_CONFIG_HPP

// Tunables of the Xiamocon front end. See impl/xiamocon/SPEC.md for the
// reasoning behind the values.

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/config.hpp"
#include "xmc/display.hpp"

namespace ds {

// The frame is rasterized and pushed one band at a time, so the device never
// holds a whole frame buffer. 240 / 40 = 6 bands: the band overhead measured
// against a single full-frame band is about +5% at 40 rows (+14% at 20,
// +36% at 8), and an even band count keeps the ping-pong parity simple.
constexpr int SCREEN_W = xmc::display::WIDTH;
constexpr int SCREEN_H = xmc::display::HEIGHT;
constexpr int BAND_H = 40;
constexpr int BAND_COUNT = SCREEN_H / BAND_H;
static_assert(SCREEN_H % BAND_H == 0, "the bands must tile the screen exactly");

// Working memory of the 3D renderer. What binds here is not the bytes the
// scene uses (42.6 KB at the measured peak) but the triangle buffer the
// arena is divided into: below 128 KB the renderer thins the sphere
// wireframe to stay inside it. Measured at 240x240 over levels 1-7, peak
// triangles against the resulting capacity:
//
//     48 KB -> 200/224     96 KB -> 204/473    160 KB -> 240/824
//     64 KB -> 200/307    128 KB -> 240/639    256 KB -> 240/1488
//
// 128 KB is the knee: the scene reaches its natural size and gets 2.7x the
// headroom for a heavier one. Nothing changes above it. The profiling
// overlay reports the real usage.
// Both targets use the same size: the ESP32S3 has only 320 KB of internal
// DRAM, but the simulation state moved to PSRAM (see ds_platform.hpp), which
// freed more than this costs.
constexpr size_t ARENA_SIZE = 128 * 1024;

// The simulation runs at a fixed 60 Hz whatever the frame rate is
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// Ticks a single frame may catch up on. 4 ticks = 66.7 ms covers any
// realistic hitch; beyond that the surplus is dropped so a long stall cannot
// turn into a burst of simulation.
constexpr int MAX_CATCHUP = 4;

}  // namespace ds

#endif
