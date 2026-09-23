#ifndef DS_PANEL_OUT_HPP
#define DS_PANEL_OUT_HPP

// Rasterizes the frame band by band and hands each band to the PPA, which
// scales it, turns it a quarter and writes it into the panel's MIPI-DSI
// framebuffer -- so band N+1 is drawn while band N is still on its way out,
// the same ping-pong the handhelds run against their display DMA
// (impl/xiamocon/devoursphere/include/band_writer.hpp). The Tab5's
// panel_out, with the geometry of this board.
//
// The geometry, once, because everything here depends on it:
//
//   the game draws  a 640x384 landscape frame, RGB565 big-endian
//   the panel is    a 480x800 portrait raster, RGB565 little-endian,
//                   scanned out continuously by the DSI
//
// The PPA scales by 5/4 into an 800x480 landscape picture and turns that a
// quarter onto the panel. With PANEL_ROTATION 1, the scaled pixel (u, v)
// lands on panel pixel (X, Y) = (479 - v, u): the landscape picture turned a
// quarter CLOCKWISE, which with the PPA's angles measured counter-clockwise
// is PPA_SRM_ROTATION_ANGLE_270. Rotation 3 is the other quarter, (v, 799 - u)
// and ANGLE_90.
//
// Band b covers scaled rows [v0, v0 + 5/4 * bandH) and becomes the vertical
// panel strip X in [480 - v0 - 5/4 * bandH, 480 - v0) under rotation 1, or
// X in [v0, v0 + 5/4 * bandH) under 3, spanning all 800 rows either way.
//
// The PPA also swaps the bytes on the way (byte_swap), which is what lets
// ShapoGFX keep drawing RGB565_SWAPPED -- the format every other target wants
// -- with no pass over the pixels to fix it.

#include <cstdint>

#include "devoursphere/render/renderer.hpp"
#include "ds_config.hpp"
#include "profiler.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"

namespace ds {

namespace g2 = shapoco::gfx2d;

// What the platform draws over the game after the renderer has filled a
// band and before it goes out (the virtual pad). Called once per band with
// that band's surface and the landscape row its first line is.
using OverlayFn = void (*)(const g2::Surface &band, int bandY, void *user);

class PanelOut {
 public:
  // Register a PPA client and allocate the band buffers, the tallest pair
  // of BAND_H_CHOICES that the internal heap has room for. Call once, after
  // board::init(); false if any of it failed, in which case nothing may be
  // drawn.
  bool init(uint16_t *panelFb, uint32_t panelBytes);

  // Rows per band, as init() settled on
  int bandHeight() const { return bandH_; }

  // Wait for the transfer still in flight, if any. Idempotent.
  void drain();

  // Time a whole frame's worth of PPA transfers with nothing else running,
  // in microseconds. Call once at start up, before the simulation core
  // starts: on this board the panel write is the one cost that does not
  // depend on what the frame contains, so it is worth knowing on its own.
  uint32_t measureTransfer();

  // Draw the whole frame and push it. The caller must have called
  // Renderer::beginFrame() and must not advance the simulation until this
  // returns.
  void present(devoursphere::render::Renderer &renderer, Profiler &prof,
               OverlayFn overlay, void *user);

 private:
  int bandH_ = 0;
  int bandCount_ = 0;
  uint32_t bandBytes_ = 0;

  uint16_t *buf_[2] = {nullptr, nullptr};
  void *ppa_ = nullptr;   // ppa_client_handle_t
  void *done_ = nullptr;  // SemaphoreHandle_t, given by the PPA callback
  uint16_t *panelFb_ = nullptr;
  uint32_t panelBytes_ = 0;
  // Free running across frames, never reset per frame: the last band of a
  // frame is still in flight when the next one starts, so restarting at 0
  // would let an odd band count draw into the buffer being read.
  int cur_ = 0;
  bool pending_ = false;

  g2::Surface surface(int i) {
    return {g2::PixelFormat::RGB565_SWAPPED, (int16_t)SCREEN_W, (int16_t)bandH_,
            (uint32_t)(SCREEN_W * 2), buf_[i]};
  }
  bool allocBands(int bandH);
  bool submit(int idx, int bandY);
};

}  // namespace ds

#endif
