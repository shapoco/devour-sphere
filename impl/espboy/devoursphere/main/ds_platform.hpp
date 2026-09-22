#ifndef DS_PLATFORM_HPP
#define DS_PLATFORM_HPP

// The pieces of this front end that are not the frame loop, the panel or
// the buttons: the clock, the entropy source, the flash record and the
// stack watch. Same names as the Xiamocon front end's ds_platform.hpp,
// because profiler.cpp is shared and reads the stack figures through them.

#include <cstdint>

namespace ds {

// Microseconds since boot (esp_timer)
uint64_t nowUs();

// A seed that differs from boot to boot (the hardware RNG)
uint32_t randomSeed();

// The high score record in flash (sim::HIGH_SCORE_RECORD_BYTES bytes; when
// it is read and written is the Xiamocon front end's high_score_store.hpp).
// It lives in the "hiscore" partition (partitions.csv, one 4 KB sector).
// read: never written reads as blank flash, which fails to decode. write:
// erases the sector and programs the record; the SDK disables the flash
// cache for the duration (tens of milliseconds), so nothing in flash runs
// meanwhile -- the tone ISR is in IRAM and survives it.
bool readHighScoreRecord(uint8_t *out);
bool writeHighScoreRecord(const uint8_t *in);

// Stack high water mark of the game task (FreeRTOS keeps it; the task's
// stack is STACK_BYTES). Core1 does not exist here and reports 0.
void stackWatchInit();
uint32_t stackUsedCore0();
uint32_t stackUsedCore1();

}  // namespace ds

#endif
