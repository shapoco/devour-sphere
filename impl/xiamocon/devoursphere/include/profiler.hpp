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
#include "ds_config.hpp"
#include "shapoco/gfx2d/gfx2d.hpp"

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
  int ticks = 0;

  // Paint core1's stack so that stackUsed() can find the high water mark.
  // Called from core1 itself, once, before it does any real work.
  static void paintCore1Stack();
  static void paintCore0Stack();
  static uint32_t core1StackUsed();
  static uint32_t core0StackUsed();

  // Format what was just measured for the next frame to draw
  void endFrame(uint64_t nowUs, const devoursphere::render::RenderStats &stats);

  // Draw the panel into one band. Lines that straddle a band boundary come
  // out right: the clip rectangle splits them between the two bands.
  void drawOverlay(const g2::Surface &band, int bandY);

 private:
  static constexpr int LINES = 7;
  static constexpr int COLS = 21;
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
  int ticks = 0;
  static void paintCore1Stack() {}
  static void paintCore0Stack() {}
  static uint32_t core1StackUsed() { return 0; }
  static uint32_t core0StackUsed() { return 0; }
  void endFrame(uint64_t, const devoursphere::render::RenderStats &) {}
  void drawOverlay(const g2::Surface &, int) {}
#endif
};

}  // namespace ds

#endif
