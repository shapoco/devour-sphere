// The StickS3's side of ds_platform.hpp: memory, the serial trace, the NVS
// record and the stack watch.

#include "ds_platform.hpp"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <new>

#include "devoursphere/sim/game.hpp"
#include "devoursphere/sim/high_score_record.hpp"

namespace ds {

namespace {
constexpr const char *TAG = "devoursphere";
constexpr const char *NVS_NAMESPACE = "devoursphere";
constexpr const char *NVS_KEY = "highscore";

TaskHandle_t g_task0 = nullptr;
TaskHandle_t g_task1 = nullptr;
}  // namespace

devoursphere::sim::Game *allocGame() {
  // PSRAM, as on the other ESP32S3 board: 84 KB of internal SRAM is worth
  // more to the arena and the band buffers than to a structure that is read
  // once a tick, by the core that is not rasterizing. Placement new because
  // heap_caps_malloc does not run constructors and Game has plenty.
  void *p = heap_caps_malloc(sizeof(devoursphere::sim::Game), MALLOC_CAP_SPIRAM);
  if (!p) {
    trace("Game did not fit in PSRAM, bytes", sizeof(devoursphere::sim::Game));
    return nullptr;
  }
  return new (p) devoursphere::sim::Game();
}

uint32_t freeInternalRam() {
  return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

uint32_t largestInternalBlock() {
  return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

uint32_t freeSpiRam() {
  return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

void trace(const char *what, uint32_t value) {
  ESP_LOGI(TAG, "%s: %u", what, (unsigned)value);
}

void frameIdle() {
  // One tick of the scheduler. Both cores run FreeRTOS here and the idle
  // task has to get in (the speaker's mixer and the attitude sampler are
  // pinned to core1, the SPI bus is core0's), so this yields rather than
  // spinning the way the handhelds do.
  vTaskDelay(1);
}

bool readHighScoreRecord(uint8_t *out) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = devoursphere::sim::HIGH_SCORE_RECORD_BYTES;
  const esp_err_t err = nvs_get_blob(h, NVS_KEY, out, &len);
  nvs_close(h);
  return err == ESP_OK && len == devoursphere::sim::HIGH_SCORE_RECORD_BYTES;
}

bool writeHighScoreRecord(const uint8_t *in) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err =
      nvs_set_blob(h, NVS_KEY, in, devoursphere::sim::HIGH_SCORE_RECORD_BYTES);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

uint32_t randomSeed() { return esp_random(); }

void stackWatchInitCore0() { g_task0 = xTaskGetCurrentTaskHandle(); }
void stackWatchInitCore1() { g_task1 = xTaskGetCurrentTaskHandle(); }

namespace {
// uxTaskGetStackHighWaterMark() gives the smallest the free space ever got,
// in words; the overlay wants the bytes used, like the other front ends.
// Neither task's size is exposed by FreeRTOS, so the sizes app_main.cpp
// creates them with are repeated here -- keep the two in step.
uint32_t usedOf(TaskHandle_t t, uint32_t total) {
  if (!t) return 0;
  const uint32_t freeBytes =
      (uint32_t)uxTaskGetStackHighWaterMark(t) * sizeof(StackType_t);
  return freeBytes < total ? total - freeBytes : 0;
}
}  // namespace

uint32_t stackUsedCore0() { return usedOf(g_task0, STACK_BYTES_CORE0); }
uint32_t stackUsedCore1() { return usedOf(g_task1, STACK_BYTES_CORE1); }

}  // namespace ds
