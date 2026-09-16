#include "ds_platform.hpp"

#include <new>

#include "devoursphere/sim/game.hpp"
#include "ds_config.hpp"

#if defined(ESP32)

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace ds {

uint32_t randomSeed() { return esp_random(); }

devoursphere::sim::Game *allocGame() {
  // MALLOC_CAP_SPIRAM, not the default heap: 133 KB would not fit in the
  // internal DRAM the linker leaves us
  void *p =
      heap_caps_malloc(sizeof(devoursphere::sim::Game), MALLOC_CAP_SPIRAM);
  trace("game in psram", (uint32_t)(uintptr_t)p);
  if (!p) return nullptr;
  return new (p) devoursphere::sim::Game();
}

uint16_t *allocBandBuffer(size_t bytes) {
  void *p = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  trace("band buffer", (uint32_t)(uintptr_t)p);
  return (uint16_t *)p;
}

void trace(const char *what, uint32_t value) {
  static bool begun = false;
  if (!begun) {
    begun = true;
    Serial.begin(115200);
  }
  Serial.printf("[ds] %s: %lu\n", what, (unsigned long)value);
  Serial.flush();
}

namespace {
TaskHandle_t g_core1 = nullptr;
uint32_t usedOf(TaskHandle_t t, uint32_t total) {
  if (!t) return 0;
  // uxTaskGetStackHighWaterMark returns the smallest the free space ever got,
  // in words
  const uint32_t freeBytes =
      uxTaskGetStackHighWaterMark(t) * sizeof(StackType_t);
  return freeBytes < total ? total - freeBytes : 0;
}
}  // namespace

// FreeRTOS already records the deepest each task has been, so there is
// nothing to paint. Both cores run as tasks here: core0 is the Arduino loop
// task, core1 the one xmc::startCore1() pins to the other core. The handle
// has to be taken from inside that task -- the readout is asked for from
// core0, where xTaskGetCurrentTaskHandle() would name the wrong one.
void stackWatchInitCore0() {}
void stackWatchInitCore1() { g_core1 = xTaskGetCurrentTaskHandle(); }

uint32_t stackUsedCore0() {
  return usedOf(xTaskGetCurrentTaskHandle(), CONFIG_ARDUINO_LOOP_STACK_SIZE);
}

uint32_t stackUsedCore1() {
  return usedOf(g_core1, 8192);  // the size xmc::startCore1 asks for
}

}  // namespace ds

#else  // RP2350

#include <pico/rand.h>

namespace ds {

uint32_t randomSeed() { return get_rand_32(); }

devoursphere::sim::Game *allocGame() {
  static devoursphere::sim::Game game;
  return &game;
}

uint16_t *allocBandBuffer(size_t bytes) {
  // One static block carved in two; alignas keeps the DMA happy
  alignas(32) static uint8_t storage[2 * SCREEN_W * BAND_H * 2];
  static size_t used = 0;
  if (used + bytes > sizeof(storage)) return nullptr;
  uint16_t *p = (uint16_t *)(storage + used);
  used += bytes;
  return p;
}

void trace(const char *, uint32_t) {}  // no serial port on this target

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

// Linker symbols. Each core gets 4 KB: core0 in SCRATCH_Y, core1 in
// SCRATCH_X, and they are adjacent -- so an overflow of one lands in the
// other, which is what makes this worth watching. Declared as arrays so that
// pointer arithmetic over them is well defined.
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

#endif
