#include "ds_platform.hpp"

#include "ds_config.hpp"

extern "C" {
#include "hw.h"
}

// The millisecond counter the SysTick handler keeps (main.cpp)
extern "C" volatile uint32_t g_ms;

namespace ds {

#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)

uint32_t nowMs() { return g_ms; }

// Milliseconds and the SysTick's position, read until a consistent pair
uint64_t nowUs() {
  uint32_t ms, cvr;
  do {
    ms = g_ms;
    cvr = SYST_CVR;
  } while (ms != g_ms);
  return (uint64_t)ms * 1000u + (SYSTICK_RELOAD - cvr) / (SYS_CLOCK_KHZ / 1000);
}

uint32_t randomSeed() {
  const uint32_t secs = *(volatile uint32_t *)0x45000304u;
  uint32_t s = secs * 2654435761u ^ (SYST_CVR * 0x9E3779B1u) ^ (g_ms << 16);
  return s ? s : 1;
}

// The stack watch: STACK_BYTES below the top are filled with a pattern below
// the current stack pointer; the high water mark is the lowest word touched
namespace {
constexpr uint32_t PATTERN = 0x5AC3F00Du;
}

void stackWatchInit() {
  uint32_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  uint32_t *lo = (uint32_t *)(STACK_TOP - STACK_BYTES);
  uint32_t *hi = (uint32_t *)(sp - 64);  // leave this frame alone
  for (uint32_t *p = lo; p < hi; p++) *p = PATTERN;
}

// The used part only grows, so the scan resumes where it stopped last time
uint32_t stackUsedCore0() {
  static const uint32_t *p = (const uint32_t *)(STACK_TOP - STACK_BYTES);
  const uint32_t *hi = (const uint32_t *)STACK_TOP;
  while (p < hi && *p == PATTERN) p++;
  return (uint32_t)((const uint8_t *)hi - (const uint8_t *)p);
}

uint32_t stackUsedCore1() { return 0; }

}  // namespace ds
