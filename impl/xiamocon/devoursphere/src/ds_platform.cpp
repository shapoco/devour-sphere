#include "ds_platform.hpp"

#include <cstring>
#include <new>

#include "devoursphere/sim/game.hpp"
#include "devoursphere/sim/high_score_record.hpp"
#include "ds_config.hpp"

#if defined(ESP32)

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace ds {

uint32_t randomSeed() { return esp_random(); }

// One tick, 1 ms at the 1000 Hz Arduino-ESP32 configures. Blocking rather
// than spinning is the point: our task is pinned at priority 10 and would
// otherwise never let its core do anything else.
void frameIdle() { vTaskDelay(1); }
void transferIdle() { taskYIELD(); }

devoursphere::sim::Game *allocGame() {
  // MALLOC_CAP_SPIRAM, not the default heap. 136 KB does not fit in internal
  // DRAM: putting it there links, but only by taking the arena down to 48 KB
  // and the bands back to 40 rows, and the board then fails to boot because
  // the heap no longer has room for the band buffers and the SPI DMA
  // buffers. Tried on hardware; do not go round again.
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

uint32_t freeInternalRam() {
  return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

// NVS, which the Arduino core has initialized by now (20 KB partition on
// the board's default table). NVS keeps its own CRC and wear leveling; the
// record's own CRC and version check come on top. A write of one small key
// takes a few milliseconds, plus a sector erase when an NVS page fills.
namespace {
const char *const PREF_NAMESPACE = "devoursphere";
const char *const PREF_KEY = "highscore";
}  // namespace

bool readHighScoreRecord(uint8_t *out) {
  Preferences p;
  // Read only: fails while the namespace does not exist yet, i.e. before
  // the first write
  if (!p.begin(PREF_NAMESPACE, true)) return false;
  const size_t n = p.getBytes(PREF_KEY, out,
                              devoursphere::sim::HIGH_SCORE_RECORD_BYTES);
  p.end();
  return n == devoursphere::sim::HIGH_SCORE_RECORD_BYTES;
}

bool writeHighScoreRecord(const uint8_t *in) {
  Preferences p;
  if (!p.begin(PREF_NAMESPACE, false)) return false;
  const size_t n = p.putBytes(PREF_KEY, in,
                              devoursphere::sim::HIGH_SCORE_RECORD_BYTES);
  p.end();
  return n == devoursphere::sim::HIGH_SCORE_RECORD_BYTES;
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

void traceLine(const char *line) {
  trace("benchmark", 0);  // opens the port the first time
  Serial.printf("%s\n", line);
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

#include <hardware/flash.h>
#include <pico/rand.h>

#include "xmc/flash.hpp"
#include "xmc/xmc_common.hpp"

namespace ds {

uint32_t randomSeed() { return get_rand_32(); }

// The last 4 KB sector of the flash. The SDK's flash API spans the whole
// chip (getRange() gives 0..4 MB, the program included) and reserves nothing
// itself; the firmware ends near 1 MB. xmc::flash::erase() / write() hold
// core1 through the multicore lockout (the SDK's core1 loop registered as a
// victim) and disable interrupts for the duration.
namespace {
uint32_t recordOffset() {
  size_t base = 0, size = 0;
  xmc::flash::getRange(&base, &size);
  return (uint32_t)(base + size - xmc::flash::getSectorSize());
}
}  // namespace

bool readHighScoreRecord(uint8_t *out) {
  void *handle = nullptr;
  const uint8_t *p = nullptr;
  if (xmc::flash::mmap(recordOffset(), devoursphere::sim::HIGH_SCORE_RECORD_BYTES,
                       &handle, &p) != XMC_OK) {
    return false;
  }
  std::memcpy(out, p, devoursphere::sim::HIGH_SCORE_RECORD_BYTES);
  xmc::flash::munmap(handle);
  return true;
}

bool writeHighScoreRecord(const uint8_t *in) {
  // flash_range_program() takes whole 256-byte pages, and xmc::flash::write()
  // passes the size on unchecked: pad the record to one page
  alignas(4) static uint8_t page[FLASH_PAGE_SIZE];
  std::memset(page, 0xFF, sizeof(page));
  std::memcpy(page, in, devoursphere::sim::HIGH_SCORE_RECORD_BYTES);
  const uint32_t off = recordOffset();
  return xmc::flash::erase(off, xmc::flash::getSectorSize()) == XMC_OK &&
         xmc::flash::write(off, page, sizeof(page)) == XMC_OK;
}

void frameIdle() { xmc::tightLoopContents(); }
void transferIdle() { xmc::tightLoopContents(); }

devoursphere::sim::Game *allocGame() {
  static devoursphere::sim::Game game;
  return &game;
}

uint32_t freeInternalRam() { return 0; }  // fixed layout, nothing to report

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
void traceLine(const char *) {}

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
