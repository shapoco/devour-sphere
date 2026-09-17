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
// scene uses (30.4 KB at the measured peak) but the triangle buffer the
// arena is divided into: buildScene() hands out what triCapacity leaves
// over, so a small buffer costs the entities their per-fragment detail and
// then thins the sphere wireframe.
//
// Measured at 240x240 over levels 1-7 driven by the AI player, peak
// triangles against the resulting capacity. The capacities are the 32-bit
// ones the two boards actually get, which is what the profiling overlay
// shows. Both boards build with SHAPOGFX3D_TEXTURE=0 (see CMakeLists.txt),
// which is what the right-hand column is:
//
//                  textured      untextured (what is built)
//      48 KB       193/251       193/350
//      64 KB       193/344       198/478
//      76 KB          -          263/574    <- the knee
//      96 KB       230/530       263/734
//     128 KB       263/717       263/1064
//     256 KB       263/1700      263/2429
//
// 76 KB is the knee: the scene reaches its natural size (263 triangles) and
// nothing changes above it. Dropping the texture path took 36 bytes off a
// Triangle and 16 off a Span, which moved the knee down from 104 KB and is
// what finally lets the ESP32S3's 96 KB hold the whole scene.
// The ESP32S3 keeps 96 KB rather than 128. Everything that is not static
// there comes out of one pool: the band buffers (77 KB at 80-row bands), the
// SPI driver's own buffers (32 KB) and every FreeRTOS task stack, including
// the 8 KB core1 asks for. At 128 KB the arena leaves too little and the
// core1 task is silently not created -- xmc::startCore1() does not check.
// 96 KB now gives 734 entries against a peak of 263, so it clears the knee
// with 2.8x to spare; 128 KB on the RP2350 has 4x. Both could be cut back
// towards 76 KB if the DRAM were ever needed elsewhere, but neither board is
// short of it today.
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
