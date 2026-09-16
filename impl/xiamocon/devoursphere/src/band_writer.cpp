#include "band_writer.hpp"

#include "xmc/display.hpp"
#include "xmc/spi.hpp"
#include "xmc/timer.hpp"

namespace ds {

bool BandWriter::init() {
  for (int i = 0; i < 2; i++) {
    buf_[i] = ds::allocBandBuffer(BAND_BYTES);
    if (!buf_[i]) return false;
  }
  return true;
}

void BandWriter::drain() {
  if (!pending_) return;
  pending_ = false;
  // Poll rather than going straight into writePixelsComplete(), which spins:
  // on ESP32S3 the transfer is performed by another task, so the renderer has
  // to give way for it to make progress. writePixelsComplete() then returns
  // at once and releases CS and the SPI lock.
  while (xmc::spi::dmaIsBusy()) ds::transferIdle();
  xmc::display::writePixelsComplete();
}

void BandWriter::start(int idx, int y, Profiler &prof) {
  // Must come first: setWindow() below takes the SPI lock that a transfer
  // still in flight is holding.
  uint32_t t = (uint32_t)xmc::getTimeUs();
  drain();
  const uint32_t t2 = (uint32_t)xmc::getTimeUs();
  prof.dmaWaitUs += t2 - t;
  // The ST7789 restarts a memory write at the window origin, so every band
  // sets its own window (this is what the SDK's sprite transfer does too).
  // Each command takes and releases the SPI lock, so this is not free and
  // its cost scales with the number of bands.
  xmc::display::setWindow(0, y, SCREEN_W, BAND_H);
  xmc::display::writePixelsStart(buf_[idx], BAND_BYTES);
  pending_ = true;
  prof.cmdUs += (uint32_t)xmc::getTimeUs() - t2;
}

uint32_t BandWriter::measureTransfer() {
  drain();
  const uint32_t t0 = (uint32_t)xmc::getTimeUs();
  for (int b = 0; b < BAND_COUNT; b++) {
    xmc::display::setWindow(0, b * BAND_H, SCREEN_W, BAND_H);
    xmc::display::writePixelsStart(buf_[0], BAND_BYTES);
    xmc::display::writePixelsComplete();
  }
  return (uint32_t)xmc::getTimeUs() - t0;
}

void BandWriter::present(devoursphere::render::Renderer &renderer,
                         Profiler &prof) {
  prof.rasterUs = 0;
  prof.dmaWaitUs = 0;
  prof.cmdUs = 0;
  for (int b = 0; b < BAND_COUNT; b++) {
    const int y = b * BAND_H;
    const int idx = cur_;
    cur_ ^= 1;
    // Drawn into the buffer the previous band is not using, so this overlaps
    // the previous band's transfer
    const uint32_t t = (uint32_t)xmc::getTimeUs();
    renderer.renderBand(surface(idx), y, BAND_H, 0);
    prof.drawOverlay(surface(idx), y);
    prof.rasterUs += (uint32_t)xmc::getTimeUs() - t;
    // Waits for the previous band (we outran the display), then queues this
    // one; start() times the two halves separately.
    start(idx, y, prof);
  }
  // The last band is deliberately left in flight: it overlaps the next
  // frame's ticks and beginFrame(). It is drained by the next start(), or by
  // xmcAppTerminate() when the power button is pressed.
}

}  // namespace ds
