#include "ds_platform.hpp"

#include <pico/rand.h>

namespace ds {

uint32_t randomSeed() { return get_rand_32(); }

namespace {
constexpr uint32_t PAINT = 0xC1C1C1C1u;
uint32_t g_painted[2] = {0, 0};  // words painted; 0 = the paint never ran

void paint(uint32_t *bottom, int which) {
  uint32_t sp;
  __asm volatile("mov %0, sp" : "=r"(sp));
  // Everything below the current frame, less a little slack, is unused
  uint32_t *end = (uint32_t *)(sp - 64);
  uint32_t n = 0;
  for (uint32_t *q = bottom; q < end; q++, n++) *q = PAINT;
  g_painted[which] = n;
}

uint32_t used(const uint32_t *bottom, const uint32_t *top, int which) {
  if (g_painted[which] == 0) return 0;
  const uint32_t *q = bottom;
  while (q < top && *q == PAINT) q++;
  return (uint32_t)((size_t)(top - q) * sizeof(uint32_t));
}
}  // namespace

}  // namespace ds

// Linker symbols from pico-sdk's memmap_default.ld. Each core gets 4 KB:
// core0 in SCRATCH_Y, core1 in SCRATCH_X, and they are adjacent -- so an
// overflow of one lands in the other, which is what makes this worth
// watching. Declared as arrays so that pointer arithmetic over them is well
// defined.
extern "C" {
extern uint32_t __StackOneBottom[];  // core1, low address
extern uint32_t __StackOneTop[];
extern uint32_t __StackBottom[];  // core0, low address
extern uint32_t __StackTop[];
}

namespace ds {

void stackWatchInitCore0() { paint(__StackBottom, 0); }
void stackWatchInitCore1() { paint(__StackOneBottom, 1); }
uint32_t stackUsedCore0() { return used(__StackBottom, __StackTop, 0); }
uint32_t stackUsedCore1() { return used(__StackOneBottom, __StackOneTop, 1); }

}  // namespace ds
