#ifndef DS_PANEL_OUT_HPP
#define DS_PANEL_OUT_HPP

// Rasterizes the frame band by band and pushes each band to the ST7789 over
// SPI with DMA, so band N+1 is drawn while band N is still going out -- the
// same ping-pong the handhelds run against their display DMA
// (impl/xiamocon/devoursphere/include/band_writer.hpp) and the Tab5 runs
// against the PPA.
//
// What makes this the simplest of the three: the panel is a 135x240
// portrait raster, but the ST7789 turns it itself. M5GFX's setRotation()
// writes the MADCTL bits and swaps the panel's logical width and height, so
// from here on the display IS 240x135 and a band is a run of landscape rows
// at its own y. Nothing walks over the pixels: ShapoGFX's RGB565_SWAPPED is
// already the order the panel wants on the wire, which is M5GFX's
// swap565_t, so writePixelsDMA(..., swap = false) hands the band straight
// to the bus (Panel_LCD::writePixels takes the no_convert path and calls
// Bus_SPI::writeBytes with use_dma).
//
// The transaction is opened once, in init(), and never closed: the bus is
// this panel's alone (M5GFX configures it bus_shared = false), and
// endWrite() would wait for the transfer in flight, which is exactly the
// one we want to leave running across the frame boundary.

#include <cstdint>

#include "devoursphere/render/renderer.hpp"
#include "ds_config.hpp"
#include "profiler.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"

// M5GFX.h declares `using M5GFX = m5gfx::M5GFX`, so the class has to be
// named through its namespace here rather than forward declared as `class
// M5GFX` -- which would clash with that alias wherever both are seen.
namespace m5gfx {
class M5GFX;
}

namespace ds {

namespace g2 = shapoco::gfx2d;

class PanelOut {
 public:
  // Allocate the band buffers and take the bus. Call once, after M5.begin()
  // and after the rotation is settled; false if the buffers did not fit, in
  // which case nothing may be drawn.
  bool init(m5gfx::M5GFX *gfx);

  // Wait for the transfer still in flight, if any. Idempotent.
  void drain();

  // Time a whole frame's worth of transfers with nothing else running, in
  // microseconds. Call once at start up, before the simulation core starts:
  // this is the one cost of a frame that does not depend on what is in it
  // (240 x 135 x 2 bytes at the 40 MHz M5GFX configures is about 13 ms).
  uint32_t measureTransfer();

  // Draw the whole frame and push it. The caller must have called
  // Renderer::beginFrame() and must not advance the simulation until this
  // returns.
  void present(devoursphere::render::Renderer &renderer, Profiler &prof);

 private:
  static constexpr uint32_t BAND_PIXELS = (uint32_t)SCREEN_W * BAND_H;
  static constexpr uint32_t BAND_BYTES = BAND_PIXELS * 2;

  uint16_t *buf_[2] = {nullptr, nullptr};
  m5gfx::M5GFX *gfx_ = nullptr;
  // Free running across frames, never reset per frame: the last band of a
  // frame is still in flight when the next one starts, so restarting at 0
  // would let an odd band count draw into the buffer being read.
  int cur_ = 0;
  bool pending_ = false;

  g2::Surface surface(int i) {
    return {g2::PixelFormat::RGB565_SWAPPED, (int16_t)SCREEN_W, (int16_t)BAND_H,
            (uint32_t)(SCREEN_W * 2), buf_[i]};
  }
  void submit(int idx, int bandY);
};

}  // namespace ds

#endif
