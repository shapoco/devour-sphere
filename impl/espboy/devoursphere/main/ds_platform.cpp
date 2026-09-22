#include "ds_platform.hpp"

#include <esp_partition.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "devoursphere/sim/high_score_record.hpp"
#include "ds_config.hpp"

namespace ds {

uint64_t nowUs() { return (uint64_t)esp_timer_get_time(); }

uint32_t randomSeed() { return esp_random(); }

namespace {
// partitions.csv: type 0x40, subtype 0, one sector
constexpr esp_partition_type_t RECORD_TYPE = (esp_partition_type_t)0x40;
constexpr esp_partition_subtype_t RECORD_SUBTYPE = (esp_partition_subtype_t)0;

const esp_partition_t *recordPartition() {
  return esp_partition_find_first(RECORD_TYPE, RECORD_SUBTYPE, "hiscore");
}
}  // namespace

bool readHighScoreRecord(uint8_t *out) {
  const esp_partition_t *p = recordPartition();
  if (!p) return false;
  return esp_partition_read(p, 0, out,
                            devoursphere::sim::HIGH_SCORE_RECORD_BYTES) ==
         ESP_OK;
}

bool writeHighScoreRecord(const uint8_t *in) {
  const esp_partition_t *p = recordPartition();
  if (!p) return false;
  if (esp_partition_erase_range(p, 0, p->size) != ESP_OK) return false;
  // Whole words: the flash is written 4 bytes at a time
  alignas(4) uint8_t buf[devoursphere::sim::HIGH_SCORE_RECORD_BYTES];
  std::memcpy(buf, in, sizeof(buf));
  return esp_partition_write(p, 0, buf, sizeof(buf)) == ESP_OK;
}

namespace {
TaskHandle_t g_task = nullptr;
}

void stackWatchInit() { g_task = xTaskGetCurrentTaskHandle(); }

// uxTaskGetStackHighWaterMark() gives the smallest the free space ever got,
// in words; what was used is the rest of the stack
uint32_t stackUsedCore0() {
  if (!g_task) return 0;
  const uint32_t freeBytes =
      (uint32_t)uxTaskGetStackHighWaterMark(g_task) * sizeof(StackType_t);
  return freeBytes < STACK_BYTES ? STACK_BYTES - freeBytes : 0;
}

uint32_t stackUsedCore1() { return 0; }

}  // namespace ds
