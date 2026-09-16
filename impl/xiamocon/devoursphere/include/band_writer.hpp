#ifndef DS_BAND_WRITER_HPP
#define DS_BAND_WRITER_HPP

// Rasterizes the frame band by band and pushes each band to the display over
// DMA, so that band N+1 is drawn while band N is still being transferred.
//
// This class is the ONLY code that touches xmc::display. The display API takes
// the SPI lock in writePixelsStart() and releases it in writePixelsComplete();
// calling setWindow() or writePixelsStart() again in between spins forever on
// a binary semaphore that nothing will release (it does not return an error,
// the device simply hangs). Keeping that invariant in one place is the whole
// reason this is not inline in app.cpp.

#include <cstdint>

#include "devoursphere/render/renderer.hpp"
#include "ds_config.hpp"
#include "profiler.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"

namespace ds {

namespace g2 = shapoco::gfx2d;

class BandWriter {
 public:
  // Wait for any transfer still in flight and release the SPI lock.
  // Idempotent, and safe to call from xmcAppTerminate().
  void drain();

  // Draw the whole frame and push it. The caller must have called
  // Renderer::beginFrame() and must not advance the simulation until this
  // returns: renderBand() draws the HUD, which reads the live Game.
  void present(devoursphere::render::Renderer &renderer, Profiler &prof);

 private:
  // RGB565 big-endian, which is exactly what the ST7789 wants on the wire
  uint16_t buf_[2][SCREEN_W * BAND_H];
  // Free running across frames, never reset per frame: the last band of a
  // frame is still in flight when the next frame starts, so restarting at 0
  // would let an odd band count draw into the buffer being transferred.
  int cur_ = 0;
  bool pending_ = false;  // a transfer is in flight and we hold the SPI lock

  g2::Surface surface(int i) {
    return {g2::PixelFormat::RGB565BE, (int16_t)SCREEN_W, (int16_t)BAND_H,
            (uint32_t)(SCREEN_W * 2), buf_[i]};
  }
  void start(int idx, int y, Profiler &prof);
};

}  // namespace ds

#endif
