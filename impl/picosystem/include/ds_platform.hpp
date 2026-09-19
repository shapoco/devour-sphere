#ifndef DS_PLATFORM_HPP
#define DS_PLATFORM_HPP

// The pieces of this front end that are not the frame loop or the display:
// the entropy source and the stack watch. Same names as the Xiamocon front
// end's ds_platform.hpp, because profiler.cpp is shared and reads the stack
// figures through them.

#include <cstdint>

namespace ds {

// A seed that differs from boot to boot (pico_rand)
uint32_t randomSeed();

// The high score record in flash (sim::HIGH_SCORE_RECORD_BYTES bytes; when
// it is read and written is the Xiamocon front end's high_score_store.hpp).
// It lives in the last 4 KB sector of the 16 MB. read: the XIP window (never
// written reads as blank flash, which fails to decode). write: erases and
// programs with interrupts off and core1 held in RAM through the multicore
// lockout (pico_flash's flash_safe_execute(); core1 registers as a victim
// when it starts) -- up to a few hundred milliseconds for the erase. Call it
// with the Game idle on core1 and no sound playing: the sound DMA reads the
// flash, which is away during the write.
bool readHighScoreRecord(uint8_t *out);
bool writeHighScoreRecord(const uint8_t *in);

// Stack high water marks. Call the init from the core it belongs to, before
// that core does any real work; used() then returns bytes. The stacks are
// fixed 4 KB regions in SCRATCH_Y (core0) and SCRATCH_X (core1): the stack
// is painted and later scanned for the paint.
void stackWatchInitCore0();
void stackWatchInitCore1();
uint32_t stackUsedCore0();
uint32_t stackUsedCore1();

}  // namespace ds

#endif
