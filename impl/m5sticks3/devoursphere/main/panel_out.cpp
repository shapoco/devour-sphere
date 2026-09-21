// The band -> SPI -> ST7789 pipeline (see panel_out.hpp for why there is
// no pass over the pixels anywhere in it).

#include "panel_out.hpp"

#include <M5GFX.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "ds_platform.hpp"

namespace ds {

bool PanelOut::init(m5gfx::M5GFX *gfx) {
  gfx_ = gfx;
  for (int i = 0; i < 2; i++) {
    // DMA capable and internal: the SPI master reads the buffer as a bus
    // master, and a band in PSRAM would either be refused or silently
    // copied. Each band is one contiguous 21.6 KB block.
    buf_[i] = (uint16_t *)heap_caps_malloc(
        BAND_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf_[i]) {
      trace("band buffer did not fit, largest block",
            largestInternalBlock());
      return false;
    }
  }
  // Held for the life of the program: see panel_out.hpp. This also takes
  // the bus lock once instead of twice a band.
  gfx_->startWrite();
  return true;
}

void PanelOut::submit(int idx, int bandY) {
  // The ST7789 restarts its memory write at the window origin, so every
  // band sets its own window. The coordinates are the rotated, logical ones
  // (M5GFX's rotation put the panel in landscape and its setWindow adds the
  // 52 / 40 pixel offsets of this 135x240 glass), and the end coordinates
  // are inclusive.
  gfx_->setWindow(0, bandY, SCREEN_W - 1, bandY + BAND_H - 1);
  // swap = false: the band is already RGB565 big-endian, which is the order
  // the panel wants, so this takes Panel_LCD's no_convert path straight
  // into Bus_SPI::writeBytes with DMA.
  gfx_->writePixelsDMA(buf_[idx], (int32_t)BAND_PIXELS, false);
  pending_ = true;
}

void PanelOut::drain() {
  if (!pending_) return;
  gfx_->waitDMA();
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
                       Profiler &prof) {
  uint32_t rasterUs = 0, waitUs = 0, cmdUs = 0;
  for (int b = 0; b < BAND_COUNT; b++) {
    const int idx = cur_;
    cur_ ^= 1;
    const int bandY = b * BAND_H;

    // Draw into the buffer that is not in flight. The first band of a frame
    // draws into the one the last band of the previous frame did not use,
    // which is why cur_ runs on across frames.
    const int64_t r0 = esp_timer_get_time();
    renderer.renderBand(surface(idx), bandY, BAND_H, 0);
    if (prof.on()) prof.drawOverlay(surface(idx), bandY);
    const int64_t r1 = esp_timer_get_time();

    // Only now wait for the previous band: it was going out while this one
    // was being drawn, which is the whole point of the ping-pong. The wait
    // is explicit rather than left to setWindow's own (the bus has to
    // finish before DC may change) so that the overlay can tell the two
    // apart: DMA is how long the CPU stood still, CMD is what the window
    // command itself costs.
    drain();
    const int64_t r2 = esp_timer_get_time();
    submit(idx, bandY);

    rasterUs += (uint32_t)(r1 - r0);
    waitUs += (uint32_t)(r2 - r1);
    cmdUs += (uint32_t)(esp_timer_get_time() - r2);
  }
  // The last band is left in flight on purpose: its transfer overlaps the
  // next frame's ticks and beginFrame(), so the frame time approaches
  // max(CPU, SPI) instead of their sum. The next frame's first drain()
  // collects it.
  prof.rasterUs = rasterUs;
  prof.dmaWaitUs = waitUs;
  prof.cmdUs = cmdUs;
}

}  // namespace ds
