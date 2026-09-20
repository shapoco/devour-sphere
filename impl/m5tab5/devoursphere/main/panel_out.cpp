// The band -> PPA -> panel framebuffer pipeline (see panel_out.hpp for the
// geometry and why the rotation is what it is).

#include "panel_out.hpp"

#include <M5GFX.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ds_platform.hpp"
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"

namespace ds {

namespace {

constexpr uint32_t PANEL_BYTES = (uint32_t)PANEL_W * PANEL_H * 2;

// The PPA reads the band as a bus master, which does not see the CPU's data
// cache, so what we just wrote has to be written back first. The band
// buffers are aligned to the L2 line, which satisfies the check whichever
// cache the address turns out to belong to.
//
// A failure here would be invisible -- the PPA would read whatever was last
// written back, and the picture would be subtly stale rather than absent --
// so the first one is traced. Once is enough: it can only ever be the same
// buffers and the same size.
void flushForDma(const void *p, size_t bytes) {
  static bool complained = false;
  const esp_err_t err = esp_cache_msync(
      (void *)p, bytes,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  if (err != ESP_OK && !complained) {
    complained = true;
    trace("cache msync refused the band buffer, err", (uint32_t)err);
  }
}

bool IRAM_ATTR onPpaDone(ppa_client_handle_t, ppa_event_data_t *, void *user) {
  auto sem = (SemaphoreHandle_t)user;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(sem, &woken);
  return woken == pdTRUE;
}

}  // namespace

bool PanelOut::init(m5gfx::M5GFX *gfx) {
  auto *panel = static_cast<lgfx::Panel_DSI *>(gfx->getPanel());
  if (!panel) {
    trace("no DSI panel", 0);
    return false;
  }
  // Panel_DSI::init() computes line_length = ((panel_width * bits >> 3) + 3)
  // & ~3, which for 720 pixels of 16 bits is already 1440: the framebuffer
  // is a plain contiguous 720x1280 raster with no per-row padding.
  if (panel->config().panel_width != PANEL_W ||
      panel->config().panel_height != PANEL_H ||
      !panel->config_detail().buffer) {
    trace("unexpected panel geometry", (uint32_t)panel->config().panel_width);
    return false;
  }
  panelFb_ = (uint16_t *)panel->config_detail().buffer;

  ppa_client_config_t cfg = {};
  cfg.oper_type = PPA_OPERATION_SRM;
  // One band may be queued behind the one being transferred; the ping-pong
  // never gets further ahead than that.
  cfg.max_pending_trans_num = 2;
  ppa_client_handle_t ppa = nullptr;
  if (ppa_register_client(&cfg, &ppa) != ESP_OK) {
    trace("ppa_register_client failed", 0);
    return false;
  }
  ppa_ = ppa;

  done_ = xSemaphoreCreateBinary();
  if (!done_) return false;
  ppa_event_callbacks_t cbs = {};
  cbs.on_trans_done = onPpaDone;
  if (ppa_client_register_event_callbacks(ppa, &cbs) != ESP_OK) {
    trace("ppa callbacks failed", 0);
    return false;
  }

  // Internal SRAM, so that the PPA's reads do not share the PSRAM interface
  // with its own writes and with the DSI's continuous scan-out of the same
  // framebuffer -- the one bus on this board that is genuinely busy.
  for (int i = 0; i < 2; i++) {
    buf_[i] = (uint16_t *)heap_caps_aligned_alloc(
        CONFIG_CACHE_L2_CACHE_LINE_SIZE, BAND_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf_[i]) {
      trace("no band buffers, internal free", freeInternalRam());
      return false;
    }
    // aligned_alloc does not zero, and measureTransfer() pushes these
    // straight to the panel: without this the first thing on screen is a
    // burst of whatever was in SRAM.
    memset(buf_[i], 0, BAND_BYTES);
  }
  // M5GFX drew into the framebuffer through the cache on its way up (the
  // clear, at least). Those lines have to reach PSRAM now, or they would be
  // written back later over what the PPA has since put there.
  esp_cache_msync(panelFb_, PANEL_BYTES,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  trace("panel fb", (uint32_t)(uintptr_t)panelFb_);
  trace("band buffer bytes", BAND_BYTES);
  return true;
}

bool PanelOut::submit(int idx, int bandY) {
  // The band's rows after the scale, and the panel strip they land in
  const uint32_t v0 = (uint32_t)bandY * SCALE;
  const uint32_t strip = (uint32_t)BAND_H * SCALE;

  ppa_srm_oper_config_t op = {};
  op.in.buffer = buf_[idx];
  op.in.pic_w = SCREEN_W;
  op.in.pic_h = BAND_H;
  op.in.block_w = SCREEN_W;
  op.in.block_h = BAND_H;
  op.in.block_offset_x = 0;
  op.in.block_offset_y = 0;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  op.out.buffer = panelFb_;
  op.out.buffer_size = PANEL_BYTES;
  op.out.pic_w = PANEL_W;
  op.out.pic_h = PANEL_H;
  // Rotation 1 puts landscape row v at panel column 719 - v, so the band
  // lands at the far end and the strips march backwards; rotation 3 is the
  // other way round. Both come from ds_config.hpp's PANEL_ROTATION, which is
  // also what the display is set to, so the picture and the touch cannot
  // disagree.
  op.out.block_offset_x =
      PANEL_ROTATION == 1 ? (PANEL_W - v0 - strip) : v0;
  op.out.block_offset_y = 0;
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  op.rotation_angle = PANEL_ROTATION == 1 ? PPA_SRM_ROTATION_ANGLE_270
                                          : PPA_SRM_ROTATION_ANGLE_90;
  op.scale_x = (float)SCALE;
  op.scale_y = (float)SCALE;
  // ShapoGFX writes RGB565 big-endian (what every other target's display
  // wants on the wire); the DSI framebuffer is little-endian
  op.byte_swap = true;
  op.mode = PPA_TRANS_MODE_NON_BLOCKING;
  op.user_data = done_;

  flushForDma(buf_[idx], BAND_BYTES);
  if (ppa_do_scale_rotate_mirror((ppa_client_handle_t)ppa_, &op) != ESP_OK) {
    return false;
  }
  pending_ = true;
  return true;
}

void PanelOut::drain() {
  if (!pending_) return;
  xSemaphoreTake((SemaphoreHandle_t)done_, portMAX_DELAY);
  pending_ = false;
}

uint32_t PanelOut::measureTransfer() {
  if (!buf_[0]) return 0;
  const int64_t t0 = esp_timer_get_time();
  for (int b = 0; b < BAND_COUNT; b++) {
    const int idx = cur_;
    cur_ ^= 1;
    drain();
    submit(idx, b * BAND_H);
  }
  drain();
  return (uint32_t)(esp_timer_get_time() - t0);
}

void PanelOut::present(devoursphere::render::Renderer &renderer,
                       Profiler &prof, OverlayFn overlay, void *user) {
  uint32_t rasterUs = 0, waitUs = 0;
  for (int b = 0; b < BAND_COUNT; b++) {
    const int idx = cur_;
    cur_ ^= 1;
    const int bandY = b * BAND_H;

    // Draw into the buffer that is not in flight. The first band of a frame
    // draws into the one the last band of the previous frame did not use,
    // which is why cur_ runs on across frames.
    const int64_t r0 = esp_timer_get_time();
    renderer.renderBand(surface(idx), bandY, BAND_H, 0);
    if (overlay) overlay(surface(idx), bandY, user);
    if (prof.on()) prof.drawOverlay(surface(idx), bandY);
    const int64_t r1 = esp_timer_get_time();

    // Only now wait for the previous band: it was transferring while this
    // one was being drawn, which is the whole point of the ping-pong.
    drain();
    const int64_t r2 = esp_timer_get_time();
    submit(idx, bandY);

    rasterUs += (uint32_t)(r1 - r0);
    waitUs += (uint32_t)(r2 - r1);
  }
  // The last band is left in flight on purpose: its transfer overlaps the
  // next frame's ticks and beginFrame(), so the frame time approaches
  // max(CPU, PPA) instead of their sum. drain() at the start of the next
  // band collects it.
  prof.rasterUs = rasterUs;
  prof.dmaWaitUs = waitUs;
  prof.cmdUs = 0;  // no per-band command on this panel: it is a framebuffer
}

}  // namespace ds
