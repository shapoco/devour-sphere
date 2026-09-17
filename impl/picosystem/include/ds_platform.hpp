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
