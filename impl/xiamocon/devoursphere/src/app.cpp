// Devour Sphere on Xiamocon (XIAO RP2350).
//
// The game core (core/) is shared with the WASM front end; everything here is
// the platform layer: the SDK entry points, the frame loop, the input mapping
// and the memory the core is given. None of the SDK's graphics API is used --
// the frame is drawn by core/render into band buffers and pushed by
// BandWriter (see band_writer.hpp).
//
// Nothing is allocated dynamically: Game, Renderer, the 3D arena and the band
// buffers are all statics. Game alone is 133 KB, so it must never go on a
// stack (both cores get 4 KB, in SCRATCH_X / SCRATCH_Y).

#include <pico/rand.h>

#include "band_writer.hpp"
#include "devoursphere/devoursphere.hpp"
#include "ds_config.hpp"
#include "xmc/app.hpp"
#include "xmc/system.hpp"

// Deliberately no `using namespace`: xmc and shapoco::gfx2d both have a
// PixelFormat, a Color and a Graphics2D.
namespace sim = devoursphere::sim;
namespace render = devoursphere::render;

namespace {

sim::Game g_game;
render::Renderer g_renderer;
uint8_t g_arena[ds::ARENA_SIZE];
ds::BandWriter g_bands;

}  // namespace

xmc::AppConfig xmcAppGetConfig(void) {
  xmc::AppConfig cfg = xmc::getDefaultAppConfig();
  // RGB565 on this display is big-endian in memory, which is bit for bit what
  // ShapoGFX's PixelFormat::RGB565BE produces. No conversion anywhere.
  cfg.displayPixelFormat = xmc::PixelFormat::RGB565;
  cfg.speakerEnabled = false;  // no audio yet
  return cfg;
  // Careful: this runs before display::init(), so nothing here may draw.
}

void xmcAppSetup(void) {
  // xmc::randomU32() is an unseeded newlib rand(), identical on every boot;
  // get_rand_32() is the SDK's ring-oscillator entropy.
  g_game.reset(get_rand_32());
  g_renderer.setControlHints(render::ControlHints{
      "MOVE: D-PAD    A/B/X/Y: FIRE",
      "D-PAD: MOVE   A: FIRE",
      // The dash line already describes this device correctly
      render::ControlHints{}.dash,
      render::ControlHints{}.dashAlt,
  });
  g_renderer.init(ds::SCREEN_W, ds::SCREEN_H, g_arena, sizeof(g_arena));
}

void xmcAppLoop(void) {
  // Until the frame loop lands, draw the current state over and over.
  g_renderer.beginFrame(g_game, 1.0f / sim::TICK_RATE);
  g_bands.present(g_renderer);
  g_renderer.endFrame();
}

XmcStatus xmcAppTerminate(xmc::system::ShutdownReason reason) {
  (void)reason;
  // requestShutdown() calls us before it deinits anything and then draws its
  // power-off message through the same SPI bus, so the band left in flight by
  // present() has to be collected here.
  g_bands.drain();
  return XMC_OK;
}
