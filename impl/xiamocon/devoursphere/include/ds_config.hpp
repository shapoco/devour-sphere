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

// Working memory of the 3D renderer. What binds here is not the whole arena
// (23.4 KB in use at the measured peak) but the triangle buffer it is
// divided into: buildScene() hands out what that buffer has left, so a small
// one costs the entities their per-fragment detail and then thins the sphere
// wireframe.
//
// Since ShapoGFX ba15719 that buffer is a byte budget rather than a slot
// count: a primitive's record holds only the attributes it has (52 to 96
// bytes here on a 32-bit target, entry included). Measured at 240x240 over
// levels 1-7 x 3 seeds x 600 ticks driven by the AI player, priced on the
// boards -- these are the numbers the profiling overlay shows:
//
//     bytes the peak frame needs            17.7 KB
//     bytes needed to stop thinning         about 38 KB
//     triangle buffer at  96 KB (ESP32S3)   68.8 KB  = 1.8x the knee, 4.0x the peak
//     triangle buffer at 128 KB (RP2350)    97.7 KB  = 2.6x the knee, 5.6x the peak
//
// Below the knee buildScene() starts handing out less, and the entities lose
// their per-fragment detail first; primitives are only dropped much further
// down. Both boards clear it comfortably, so ARENA_SIZE stays where it is.
// The ESP32S3 keeps 96 KB rather than 128. Everything that is not static
// there comes out of one pool: the band buffers (77 KB at 80-row bands), the
// SPI driver's own buffers (32 KB) and every FreeRTOS task stack, including
// the 8 KB core1 asks for. At 128 KB the arena leaves too little and the
// core1 task is silently not created -- xmc::startCore1() does not check.
//
// There is room to go the other way now: 64 KB would still be 1.5x the knee
// and 3x the peak, and Config::spanCapacity would give back another 18 KB
// (the default reserves a quarter of the arena for spans, 451 of them, while
// the measured peak is 35). That is worth doing on the ESP32S3, where the
// DRAM is the scarce thing -- see ../SPEC.md.
#if defined(ESP32)
constexpr size_t ARENA_SIZE = 96 * 1024;
#else
constexpr size_t ARENA_SIZE = 128 * 1024;
#endif

// The simulation runs at a fixed 60 Hz whatever the frame rate is
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// core1 runs the simulation while core0 builds the scene, rasterizes the
// bands and pushes them. The two overlap: core0 asks for a batch of ticks
// and then draws the frame the previous batch left behind, so the ticks
// happen while the display is being written.
//
// Both boards are arranged this way. It matches what core1 is for on
// Xiamocon -- computation, not peripherals -- and on ESP32S3 it is the only
// arrangement that works at all: **the display only answers to core0** there
// (see "ESP32S3 で core1 描画を切っている理由" in ../SPEC.md).
//
// It costs one frame of input latency, which is inherent: what is drawn is
// the state core1 finished, so the buttons read now reach the screen next
// time round. Rendering on core1 instead would avoid that, but then the
// ticks and beginFrame() serialize on core0 and it comes out no faster.
//
// Set to 0 to put everything on core0 -- slower, but a way to tell whether
// a problem is the split.
#ifndef DS_SIM_ON_CORE1
#define DS_SIM_ON_CORE1 1
#endif

// Ticks a single frame may catch up on. 4 ticks = 66.7 ms covers any
// realistic hitch; beyond that the surplus is dropped so a long stall cannot
// turn into a burst of simulation.
constexpr int MAX_CATCHUP = 4;

}  // namespace ds

#endif
