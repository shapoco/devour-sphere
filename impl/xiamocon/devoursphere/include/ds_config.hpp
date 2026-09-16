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
#if defined(ESP32)
// Taller bands, so fewer of them. Every band costs a setWindow, and on this
// board a command goes through the same queue-to-another-task path a bulk
// transfer does: CMD measured 4.41 ms across six bands, against 0.26 ms on
// RP2350. Three bands cut that and the per-band waiting with it, for 38 KB
// more RAM.
constexpr int BAND_H = 80;
#else
constexpr int BAND_H = 40;
#endif
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

// Render on the second core, so that core0's ticks for the next frame
// overlap this frame's rasterization. That is what takes the RP2350 from 30
// to 41 fps.
//
// Off on ESP32S3: the display only works when the transfers are issued from
// core0. The scene is built correctly there (triangles, lines and
// rasterization time all sane) and a transfer issued from core0 during setup
// reaches the panel, but the same call from the render task does not. Giving
// way while waiting -- vTaskDelay in the long wait, taskYIELD in the short
// one -- made no difference, and the SPI transfer task turns out to run on
// the other core anyway, so starvation was never the mechanism.
#ifndef DS_RENDER_ON_CORE1
#if defined(ESP32)
#define DS_RENDER_ON_CORE1 0
#else
#define DS_RENDER_ON_CORE1 1
#endif
#endif

// Ticks a single frame may catch up on. 4 ticks = 66.7 ms covers any
// realistic hitch; beyond that the surplus is dropped so a long stall cannot
// turn into a burst of simulation.
constexpr int MAX_CATCHUP = 4;

}  // namespace ds

#endif
