#ifndef DS_PLATFORM_HPP
#define DS_PLATFORM_HPP

// The handful of things that differ between the two Xiamocon targets. The
// Xiamocon SDK covers the display, DMA, input and multicore; what is left is
// the entropy source and the stack watch, neither of which it abstracts.

#include <cstdint>

namespace devoursphere::sim {
class Game;
}

namespace ds {

// Where the simulation state lives. It is 133 KB, which the ESP32S3 cannot
// spare from its 320 KB of internal DRAM, so there it is allocated from the
// 8 MB of PSRAM instead -- slower to step, but the simulation is not what
// limits that board. On RP2350 it is an ordinary static.
devoursphere::sim::Game *allocGame();

// Print a line to the serial console during bring up. Nothing on RP2350,
// where no serial port is configured.
void trace(const char *what, uint32_t value);

// A seed that differs from boot to boot. Not xmc::randomU32(): that is an
// unseeded newlib rand(), so it yields the same sequence every time.
uint32_t randomSeed();

// Stack high water marks. Call the init from the core it belongs to, before
// that core does any real work; used() then returns bytes, or 0 if the
// platform cannot tell. RP2350 paints the stack and scans it (the stacks are
// fixed 4 KB regions in SCRATCH_X / SCRATCH_Y); the ESP32S3 renderer is a
// FreeRTOS task, which tracks its own high water mark.
void stackWatchInitCore0();
void stackWatchInitCore1();
uint32_t stackUsedCore0();
uint32_t stackUsedCore1();

}  // namespace ds

#endif
