#include "ds_platform.hpp"

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/flash.h>
#include <pico/rand.h>

#include <cstring>

#include "devoursphere/sim/high_score_record.hpp"
#include "ds_config.hpp"

namespace ds {

uint32_t randomSeed() { return get_rand_32(); }

namespace {
// The last sector; the firmware ends near 240 KB
constexpr uint32_t RECORD_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
// flash_range_program() takes whole 256-byte pages. Static: the stacks are
// 4 KB and the ROM's flash routines run on this one
alignas(4) uint8_t g_page[FLASH_PAGE_SIZE];

void program(void *) {
  flash_range_erase(RECORD_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(RECORD_OFFSET, g_page, FLASH_PAGE_SIZE);
}
}  // namespace

bool readHighScoreRecord(uint8_t *out) {
  std::memcpy(out, (const uint8_t *)(XIP_BASE + RECORD_OFFSET),
              devoursphere::sim::HIGH_SCORE_RECORD_BYTES);
  return true;
}

bool writeHighScoreRecord(const uint8_t *in) {
  std::memset(g_page, 0xFF, sizeof(g_page));
  std::memcpy(g_page, in, devoursphere::sim::HIGH_SCORE_RECORD_BYTES);
#if DS_SIM_ON_CORE1
  // Interrupts off here, core1 parked in RAM (its lockout handler), then
  // the ROM erases and programs with the XIP away
  return flash_safe_execute(program, nullptr, 1000) == PICO_OK;
#else
  // One core: nothing else executes from flash while interrupts are off
  const uint32_t irq = save_and_disable_interrupts();
  program(nullptr);
  restore_interrupts(irq);
  return true;
#endif
}

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
