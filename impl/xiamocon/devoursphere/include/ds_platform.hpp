#ifndef DS_PLATFORM_HPP
#define DS_PLATFORM_HPP

// The handful of things that differ between the two Xiamocon targets. The
// Xiamocon SDK covers the display, DMA, input and multicore; what is left is
// the entropy source and the stack watch, neither of which it abstracts.

#include <cstddef>
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

// A band buffer the display DMA can read. On ESP32S3 that means the DMA
// capable heap, which is what the SDK's own transfers use -- a plain static
// is not guaranteed to satisfy the SPI driver, and a buffer it rejects is
// dropped silently, leaving the screen black with everything else running.
// On RP2350 every byte of SRAM is DMA capable, so this is just an aligned
// static.
uint16_t *allocBandBuffer(size_t bytes);

// Bytes of internal RAM still free, or 0 where the question does not apply.
// The band buffers, the SPI driver's own buffers and every FreeRTOS task
// stack come out of this, and xmc::startCore1() does not check whether the
// task was actually created -- so running out shows up as a core1 that never
// runs, with XMC_OK returned.
uint32_t freeInternalRam();

// Print a line to the serial console during bring up. Nothing on RP2350,
// where no serial port is configured.
void trace(const char *what, uint32_t value);
// One line of text to the same console (the benchmark's results)
void traceLine(const char *line);

// What the render side does while it waits. RP2350 spins for both: nothing
// else runs on that core.
//
// frameIdle() is the long wait, for core0 to hand over the next frame -- on
// ESP32S3 it blocks for a tick, so the core it is pinned to can run
// everything else the framework has to do there.
//
// transferIdle() is the short wait, for a band to finish going out. It only
// gives way; blocking here would round a 4 ms transfer up to the tick.
void frameIdle();
void transferIdle();

// The high score record in flash (sim::HIGH_SCORE_RECORD_BYTES bytes; when
// it is read and written is high_score_store.hpp). read: false when the
// platform has nothing to give (never written reads as blank flash, which
// fails to decode instead). write: blocks for as long as it takes -- up to a
// few hundred milliseconds for the sector erase -- with the other core held
// and interrupts off; call it with the Game idle on the other core and no
// sound playing. RP2350: the last sector of the 4 MB through xmc::flash
// (which does the lockout). ESP32S3: the NVS partition through Preferences
// (the IDF halts the other core itself).
bool readHighScoreRecord(uint8_t *out);
bool writeHighScoreRecord(const uint8_t *in);

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
