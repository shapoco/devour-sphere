#ifndef DS_PANEL_OUT_HPP
#define DS_PANEL_OUT_HPP

// Rasterizes the frame band by band and hands each band to the PPA, which
// scales it, turns it a quarter clockwise and writes it into the panel's
// MIPI-DSI framebuffer -- so band N+1 is drawn while band N is still on its
// way out, the same ping-pong the handhelds run against their display DMA
// (impl/xiamocon/devoursphere/include/band_writer.hpp).
//
// The geometry, once, because everything here depends on it:
//
//   the game draws  a 640x360 landscape frame, RGB565 big-endian
//   the panel is    a 720x1280 portrait raster, RGB565 little-endian,
//                   scanned out continuously by the DSI
//
// M5GFX's rotation 1 maps a landscape pixel (u, v) to the panel pixel
// (X, Y) = (719 - v, u); taking the landscape picture and turning it a
// quarter CLOCKWISE does exactly that, so with the PPA's angles measured
// counter-clockwise the operation is PPA_SRM_ROTATION_ANGLE_270. Using the
// same rotation the display is configured with is what makes M5.Touch's
// coordinates land on the pad we drew.
//
// Band b covers landscape rows [v0, v0 + 2 * BAND_H) after the scale, and
// under that mapping it becomes the vertical panel strip
// X in [720 - v0 - 2 * BAND_H, 720 - v0), spanning the full 1280 rows.
//
// The PPA also swaps the bytes on the way (byte_swap), which is what lets
// ShapoGFX keep drawing RGB565_SWAPPED -- the format every other target wants --
// with no pass over the pixels to fix it.

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

// What the platform draws over the game after the renderer has filled a
// band and before it goes out (the virtual pad). Called once per band with
// that band's surface and the landscape row its first line is.
using OverlayFn = void (*)(const g2::Surface &band, int bandY, void *user);

class PanelOut {
 public:
  // Take the panel's framebuffer, register a PPA client and allocate the
  // band buffers. Call once, after M5.begin(); false if any of it failed,
  // in which case nothing may be drawn.
  bool init(m5gfx::M5GFX *gfx);

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
  static constexpr uint32_t BAND_PIXELS = (uint32_t)SCREEN_W * BAND_H;
  static constexpr uint32_t BAND_BYTES = BAND_PIXELS * 2;

  uint16_t *buf_[2] = {nullptr, nullptr};
  void *ppa_ = nullptr;       // ppa_client_handle_t
  void *done_ = nullptr;      // SemaphoreHandle_t, given by the PPA callback
  uint16_t *panelFb_ = nullptr;
  // Free running across frames, never reset per frame: the last band of a
  // frame is still in flight when the next one starts, so restarting at 0
  // would let an odd band count draw into the buffer being read.
  int cur_ = 0;
  bool pending_ = false;

  g2::Surface surface(int i) {
    return {g2::PixelFormat::RGB565_SWAPPED, (int16_t)SCREEN_W, (int16_t)BAND_H,
            (uint32_t)(SCREEN_W * 2), buf_[i]};
  }
  bool submit(int idx, int bandY);
};

}  // namespace ds

#endif
