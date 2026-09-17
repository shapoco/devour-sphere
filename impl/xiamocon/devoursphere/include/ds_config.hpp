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

// Working memory of the 3D renderer. What binds is not the whole arena but
// the triangle buffer it is divided into: buildScene() hands out what that
// buffer has left, so a small one costs the entities their per-fragment
// detail (and, much further down, starts dropping primitives outright).
//
// Since ShapoGFX ba15719 the buffer is a byte budget rather than a slot
// count -- a primitive's record holds only the attributes it has, 52 to 96
// bytes here on a 32-bit target with the entry included. Measured at 240x240
// over levels 1-7 x 3 seeds x 600 ticks driven by the AI player, priced on
// the boards:
//
//     the peak frame uses                     17.0 KB
//     below this the renderer starts thinning 36.9 KB   (bit-identical above)
//     64 KB arena with SPAN_CAPACITY spans    53.2 KB   = 1.48x / 3.13x
//
// 64 KB was 128 (RP2350) and 96 (ESP32S3) when the buffer was a slot count
// and every slot cost the same. Three ShapoGFX changes took the peak from
// 25.1 KB to 17.7 (records sized to their contents, the texture path
// compiled out, three of the four layers carrying no depth), and tuning
// SPAN_CAPACITY gave the span pool's share back, so the same headroom now
// fits in half the memory.
//
// On the ESP32S3 that is the point of the exercise: everything not static
// comes out of one pool -- the band buffers (77 KB at 80-row bands), the SPI
// driver's own buffers (32 KB) and every FreeRTOS task stack, including the
// 8 KB core1 asks for. At 128 KB the arena left too little and core1 was
// silently not created (xmc::startCore1() does not check); this hands 32 KB
// back to that pool. On the RP2350 it is 64 KB of .bss that nothing else
// needs today.
constexpr size_t ARENA_SIZE = 64 * 1024;

// Spans held per scanline. The ShapoGFX default reserves a quarter of what
// is left after the fixed part -- 451 spans, 23 KB, at the old 96 KB arena --
// against a measured peak of 32 at 240x240. It barely grows with the screen
// (54 at 480x320, 59 at 1280x720), because what sets it is how many
// primitives cross one scanline, not how wide the scanline is. 128 is four
// times the peak here and still twice the worst seen at any resolution.
//
// Overflowing drops spans, which leaves holes in the picture, so this is not
// a number to shave: the profiling overlay's SPN line reports the real peak
// and the drop count.
constexpr int SPAN_CAPACITY = 128;

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
