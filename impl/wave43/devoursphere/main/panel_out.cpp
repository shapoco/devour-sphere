// The band -> PPA -> panel framebuffer pipeline (see panel_out.hpp for the
// geometry and why the rotation is what it is).

#include "panel_out.hpp"

#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ds_platform.hpp"

namespace ds {

namespace {

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

bool PanelOut::init(uint16_t *panelFb, uint32_t panelBytes) {
  if (!panelFb || panelBytes != (uint32_t)PANEL_W * PANEL_H * 2) {
    trace("unexpected panel framebuffer bytes", panelBytes);
    return false;
  }
  panelFb_ = panelFb;
  panelBytes_ = panelBytes;

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

  // The tallest band the heap has a pair of. What runs out is nearly always
  // the largest single block, not the total: a band buffer is one contiguous
  // allocation and the internal heap is several regions (the Tab5 ran into
  // exactly this), so both numbers are traced when a height is given up on.
  for (int h : BAND_H_CHOICES) {
    if (allocBands(h)) {
      trace("band rows", (uint32_t)bandH_);
      trace("band buffer bytes", bandBytes_);
      return true;
    }
    trace("no room for band rows", (uint32_t)h);
    trace("  internal free", freeInternalRam());
    trace("  largest internal block", largestInternalBlock());
  }
  return false;
}

bool PanelOut::allocBands(int bandH) {
  const uint32_t bytes = (uint32_t)SCREEN_W * bandH * 2;
  // Internal SRAM, so that the PPA's reads do not share the PSRAM interface
  // with its own writes and with the DSI's continuous scan-out of the same
  // framebuffer -- the one bus on this board that is genuinely busy.
  uint16_t *b[2] = {nullptr, nullptr};
  for (int i = 0; i < 2; i++) {
    b[i] = (uint16_t *)heap_caps_aligned_alloc(
        CONFIG_CACHE_L2_CACHE_LINE_SIZE, bytes,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!b[i]) {
      if (i == 1) heap_caps_free(b[0]);
      return false;
    }
    // aligned_alloc does not zero, and measureTransfer() pushes these
    // straight to the panel: without this the first thing on screen is a
    // burst of whatever was in SRAM.
    memset(b[i], 0, bytes);
  }
  buf_[0] = b[0];
  buf_[1] = b[1];
  bandH_ = bandH;
  bandCount_ = SCREEN_H / bandH;
  bandBytes_ = bytes;
  return true;
}

bool PanelOut::submit(int idx, int bandY) {
  // The band's rows after the scale, and the panel strip they land in.
  // Both are whole numbers: every band height is a multiple of 4
  // (ds_config.hpp), and so is every band's first row.
  const uint32_t v0 = (uint32_t)bandY * SCALE_NUM / SCALE_DEN;
  const uint32_t strip = (uint32_t)bandH_ * SCALE_NUM / SCALE_DEN;

  ppa_srm_oper_config_t op = {};
  op.in.buffer = buf_[idx];
  op.in.pic_w = SCREEN_W;
  op.in.pic_h = bandH_;
  op.in.block_w = SCREEN_W;
  op.in.block_h = bandH_;
  op.in.block_offset_x = 0;
  op.in.block_offset_y = 0;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  op.out.buffer = panelFb_;
  op.out.buffer_size = panelBytes_;
  op.out.pic_w = PANEL_W;
  op.out.pic_h = PANEL_H;
  // Rotation 1 puts scaled landscape row v at panel column 479 - v, so the
  // band lands at the far end and the strips march backwards; rotation 3 is
  // the other way round. Both come from ds_config.hpp's PANEL_ROTATION, which
  // touch_pad.cpp maps the touch by as well, so the picture and the pad
  // cannot disagree.
  op.out.block_offset_x = PANEL_ROTATION == 1 ? (PANEL_W - v0 - strip) : v0;
  op.out.block_offset_y = 0;
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  op.rotation_angle = PANEL_ROTATION == 1 ? PPA_SRM_ROTATION_ANGLE_270
                                          : PPA_SRM_ROTATION_ANGLE_90;
  // 1.25 is 20/16, exactly on the PPA's 1/16 grid
  op.scale_x = SCALE;
  op.scale_y = SCALE;
  // ShapoGFX writes RGB565 big-endian (what every other target's display
  // wants on the wire); the DSI framebuffer is little-endian
  op.byte_swap = true;
  op.mode = PPA_TRANS_MODE_NON_BLOCKING;
  op.user_data = done_;

  flushForDma(buf_[idx], bandBytes_);
  const esp_err_t err =
      ppa_do_scale_rotate_mirror((ppa_client_handle_t)ppa_, &op);
  if (err != ESP_OK) {
    // The band is dropped rather than retried -- and pending_ stays false, so
    // the next drain() does not wait for a transfer that was never started.
    // Traced once: a rejected operation is rejected the same way every frame
    // (the geometry does not change), and a black screen with nothing in the
    // log is the worst thing to be handed during bring-up.
    static bool complained = false;
    if (!complained) {
      complained = true;
      trace("ppa refused the band, err", (uint32_t)err);
      trace("  block_offset_x", op.out.block_offset_x);
    }
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
  for (int b = 0; b < bandCount_; b++) {
    const int idx = cur_;
    cur_ ^= 1;
    drain();
    submit(idx, b * bandH_);
  }
  drain();
  return (uint32_t)(esp_timer_get_time() - t0);
}

void PanelOut::present(devoursphere::render::Renderer &renderer, Profiler &prof,
                       OverlayFn overlay, void *user) {
  uint32_t rasterUs = 0, waitUs = 0;
  for (int b = 0; b < bandCount_; b++) {
    const int idx = cur_;
    cur_ ^= 1;
    const int bandY = b * bandH_;

    // Draw into the buffer that is not in flight. The first band of a frame
    // draws into the one the last band of the previous frame did not use,
    // which is why cur_ runs on across frames.
    const int64_t r0 = esp_timer_get_time();
    renderer.renderBand(surface(idx), bandY, bandH_, 0);
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
