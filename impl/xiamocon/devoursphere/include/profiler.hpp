#ifndef DS_PROFILER_HPP
#define DS_PROFILER_HPP

// On-screen timing for the device, toggled at run time with FUNC.
//
// There is no serial console on this board (the SDK never enables USB stdio
// and never calls stdio_init_all), and turning it on would add USB interrupt
// load to the very frame times being measured. So the numbers are drawn into
// the band buffers instead.
//
// The panel shows the PREVIOUS frame: this frame's rasterization time cannot
// be printed while it is still being produced.
//
// Build with -DDS_PROFILE=0 to compile the whole thing out.

#include <cstdint>

#include "devoursphere/render/renderer.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"

// Angle brackets on purpose: this file is shared with the PicoSystem front
// end (impl/picosystem/), which compiles it against a ds_config.hpp /
// ds_platform.hpp of its own. A quoted include would find the ones next to
// this file first, whatever the include path says.
#include <ds_config.hpp>

#ifndef DS_PROFILE
#define DS_PROFILE 1
#endif

namespace ds {

namespace g2 = shapoco::gfx2d;

class Profiler {
 public:
#if DS_PROFILE
  void toggle() { on_ = !on_; }
  bool on() const { return on_; }

  // Microsecond counters of the frame being built. app.cpp fills the first
  // two, BandWriter the last two.
  uint32_t tickUs = 0, beginUs = 0, rasterUs = 0, dmaWaitUs = 0, cmdUs = 0;
  uint32_t core1WaitUs = 0;  // core0 idle, waiting for core1 to finish
  uint32_t xferUs = 0;       // a whole screen, measured once at start up
  int ticks = 0;

  // Format what was just measured for the next frame to draw
  void endFrame(uint64_t nowUs, const devoursphere::render::RenderStats &stats);

  // Draw the panel into one band. Lines that straddle a band boundary come
  // out right: the clip rectangle splits them between the two bands.
  void drawOverlay(const g2::Surface &band, int bandY);

  // Up to two lines a platform may add below the standard ones (clocks, a
  // transfer benchmark by several methods, ...). An empty line is not drawn.
  static constexpr int COLS = 21;
  static constexpr int EXTRA_LINES = 2;
  char extra[EXTRA_LINES][COLS + 1] = {};

 private:
  static constexpr int LINES = 8;
  bool on_ = false;
  char lines_[LINES][COLS + 1] = {};
  uint64_t windowUs_ = 0;  // start of the frame rate window
  uint32_t frames_ = 0;
  uint32_t fps100_ = 0;
#else
  void toggle() {}
  bool on() const { return false; }
  uint32_t tickUs = 0, beginUs = 0, rasterUs = 0, dmaWaitUs = 0, cmdUs = 0;
  uint32_t core1WaitUs = 0;
  uint32_t xferUs = 0;
  int ticks = 0;
  void endFrame(uint64_t, const devoursphere::render::RenderStats &) {}
  void drawOverlay(const g2::Surface &, int) {}
  static constexpr int COLS = 21;
  static constexpr int EXTRA_LINES = 2;
  char extra[EXTRA_LINES][COLS + 1] = {};
#endif
};

}  // namespace ds

#endif
