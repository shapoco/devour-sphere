#ifndef DS_PLATFORM_HPP
#define DS_PLATFORM_HPP

// The pieces of this front end that are not the frame loop, the panel or
// the buttons: the clock, the entropy source, the flash record and the
// stack watch. Same names as the Xiamocon front end's ds_platform.hpp,
// because profiler.cpp is shared and reads the stack figures through them.

#include <cstdint>

namespace ds {

// Microseconds since the program started (SysTick, 1 ms interrupts plus the
// counter in between)
uint64_t nowUs();
uint32_t nowMs();

// A seed that differs from start to start (the Paradise's seconds counter
// and the SysTick's position)
uint32_t randomSeed();

// The high score record in flash (sim::HIGH_SCORE_RECORD_BYTES bytes; when
// it is read and written is the Xiamocon front end's high_score_store.hpp).
// It lives in a 4 KB sector of its own behind the program (the settings
// block names it; mkrom.js checks the sector is free). read: a never
// written sector reads as 0xFF, which fails to decode. write: erases and
// programs the sector from PRAM with the interrupts off (tens of ms); the
// buzzer keeps its tone on its own timer meanwhile.
bool readHighScoreRecord(uint8_t *out);
bool writeHighScoreRecord(const uint8_t *in);

// Stack high water mark of the main stack (STACK_BYTES below STACK_TOP are
// filled with a pattern at start-up and scanned). There is no second core.
void stackWatchInit();
uint32_t stackUsedCore0();
uint32_t stackUsedCore1();

}  // namespace ds

#endif
