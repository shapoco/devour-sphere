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
// RP2350. Three bands cut that to 2.37 and the per-band waiting with it, for
// 38 KB more heap.
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
// Both targets use the same size. The ESP32S3 can afford it because the
// simulation state is in PSRAM; see ds_platform.hpp for why it has to be.
constexpr size_t ARENA_SIZE = 128 * 1024;

// The simulation runs at a fixed 60 Hz whatever the frame rate is
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// How the work is divided between the two cores. Either way the point is the
// same: one core runs the simulation for the next frame while the other
// rasterizes and pushes this one. beginFrame() can never overlap anything,
// because it reads the whole Game.
//
//   DS_SPLIT_NONE    one core does everything
//   DS_SPLIT_RENDER  core1 renders, core0 simulates
//   DS_SPLIT_SIM     core1 simulates, core0 renders
//
// RP2350 renders on core1, which is the better arrangement: the frame drawn
// is the state just ticked, so there is no added latency.
//
// ESP32S3 has to be the other way round, because **the display only works
// when the transfers are issued from core0**. The scene is built correctly
// from the render task -- triangles, lines and rasterization time all sane --
// and a transfer issued from core0 during setup reaches the panel, but the
// same call from that task does not. Giving way while waiting (vTaskDelay in
// the long wait, taskYIELD in the short one) made no difference, and the SPI
// transfer task turns out to run on the other core anyway, so starvation was
// never the mechanism. Simulating on core1 instead gets the overlap back, at
// the cost of one frame of input latency: what is drawn is the state core1
// finished, and the input just read affects the frame after.
#define DS_SPLIT_NONE 0
#define DS_SPLIT_RENDER 1
#define DS_SPLIT_SIM 2

#ifndef DS_SPLIT
#if defined(ESP32)
#define DS_SPLIT DS_SPLIT_SIM
#else
#define DS_SPLIT DS_SPLIT_RENDER
#endif
#endif

// Ticks a single frame may catch up on. 4 ticks = 66.7 ms covers any
// realistic hitch; beyond that the surplus is dropped so a long stall cannot
// turn into a burst of simulation.
constexpr int MAX_CATCHUP = 4;

}  // namespace ds

#endif
