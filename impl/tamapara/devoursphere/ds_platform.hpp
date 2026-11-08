#ifndef DS_PLATFORM_HPP
#define DS_PLATFORM_HPP

// The pieces of this front end that are not the frame loop, the panel or
// the buttons: the clock, the entropy source and the
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

// Stack high water mark of the main stack (STACK_BYTES below STACK_TOP are
// filled with a pattern at start-up and scanned). There is no second core.
void stackWatchInit();
uint32_t stackUsedCore0();
uint32_t stackUsedCore1();

}  // namespace ds

#endif
