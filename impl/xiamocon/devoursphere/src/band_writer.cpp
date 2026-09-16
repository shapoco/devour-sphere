#include "band_writer.hpp"

#include "xmc/display.hpp"
#include "xmc/timer.hpp"

namespace ds {

void BandWriter::drain() {
  if (!pending_) return;
  pending_ = false;
  // Waits for the DMA, then releases CS and the SPI lock
  xmc::display::writePixelsComplete();
}

void BandWriter::start(int idx, int y) {
  // Must come first: setWindow() below takes the SPI lock that a transfer
  // still in flight is holding.
  drain();
  // The ST7789 restarts a memory write at the window origin, so every band
  // sets its own window (this is what the SDK's sprite transfer does too).
  xmc::display::setWindow(0, y, SCREEN_W, BAND_H);
  xmc::display::writePixelsStart(buf_[idx], SCREEN_W * BAND_H * 2);
  pending_ = true;
}

void BandWriter::present(devoursphere::render::Renderer &renderer,
                         Profiler &prof) {
  prof.rasterUs = 0;
  prof.dmaWaitUs = 0;
  for (int b = 0; b < BAND_COUNT; b++) {
    const int y = b * BAND_H;
    const int idx = cur_;
    cur_ ^= 1;
    // Drawn into the buffer the previous band is not using, so this overlaps
    // the previous band's transfer
    uint32_t t = (uint32_t)xmc::getTimeUs();
    renderer.renderBand(surface(idx), y, BAND_H, 0);
    prof.drawOverlay(surface(idx), y);
    const uint32_t t2 = (uint32_t)xmc::getTimeUs();
    prof.rasterUs += t2 - t;
    // Most of this is start()'s drain(): time spent here is the display
    // finishing a transfer we already outran.
    start(idx, y);
    prof.dmaWaitUs += (uint32_t)xmc::getTimeUs() - t2;
  }
  // The last band is deliberately left in flight: it overlaps the next
  // frame's ticks and beginFrame(). It is drained by the next start(), or by
  // xmcAppTerminate() when the power button is pressed.
}

}  // namespace ds
