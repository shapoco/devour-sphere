#ifndef DS_DISPLAY_HPP
#define DS_DISPLAY_HPP

// The ESPboy's ST7735 (128x128), driven in its 16-bit color mode over the
// HSPI. This is the only file that touches the panel.
//
// The ESP8266's SPI has no DMA path, so a band is pushed by the CPU: the
// controller's 64-byte FIFO is filled a word at a time and sent, sixty-four
// times for a 4 KB band. The bands are RGB565BE (big-endian in memory,
// which is what the panel wants on the wire), so the words go out in
// memory order without conversion. A 128x128 frame at 26.7 MHz is 9.8 ms
// of the CPU's time.
//
// The chip select is not ours: the ESPboy holds it low through the
// MCP23017 (input.cpp), so every command and every band is framed by D/C
// alone.

#include <cstddef>
#include <cstdint>

namespace ds {

class Display {
 public:
  // Bring the panel up (the ESPboy library's ST7735 sequence). Call once,
  // after input::init() has pulled the chip select low.
  void init();

  // Push `h` rows of `w` pixels (RGB565BE) to rows y..y+h-1. Synchronous:
  // returns when the last byte has left the FIFO.
  void write(int y, int w, int h, const uint16_t *pixels);

  // Fill the whole panel with one RGB565BE value
  void fill(uint16_t rgb565be);

 private:
  void command(uint8_t c, const uint8_t *data = nullptr, size_t len = 0);
  void setWindow(int x, int y, int w, int h);
  void bytes(const uint8_t *p, size_t n);
};

}  // namespace ds

#endif
