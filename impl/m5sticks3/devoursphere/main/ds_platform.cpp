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
#include "ds_config.hpp"
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
  // Internal SRAM if it fits with room to spare, PSRAM otherwise. Call this
  // AFTER the band buffers, so the fussy allocation has already had its
  // pick and these figures are what is really left.
  //
  // Why it is worth the SRAM: the simulation is this board's critical path
  // (see DS_GAME_IN_SRAM in ds_config.hpp). Why it is guarded rather than
  // just asked for: the internal heap running dry here does not fail
  // loudly. It fails as a black screen or a core1 that never runs.
  //
  // Placement new because heap_caps_malloc does not run constructors and
  // Game has plenty.
  constexpr size_t BYTES = sizeof(devoursphere::sim::Game);
  void *p = nullptr;
#if DS_GAME_IN_SRAM
  const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (freeInternal >= BYTES + SRAM_RESERVE && block >= BYTES + 8 * 1024) {
    p = heap_caps_malloc(BYTES, MALLOC_CAP_INTERNAL);
  }
  trace(p ? "Game in internal SRAM, bytes" : "Game to PSRAM, internal free",
        p ? (uint32_t)BYTES : (uint32_t)freeInternal);
#endif
  if (!p) p = heap_caps_malloc(BYTES, MALLOC_CAP_SPIRAM);
  if (!p) {
    trace("Game fitted nowhere, bytes", BYTES);
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
