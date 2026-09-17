#ifndef DS_DISPLAY_HPP
#define DS_DISPLAY_HPP

// The PicoSystem's ST7789 (240x240), driven in its 16-bit color mode over
// the hardware SPI with a DMA channel per band. This is the only file that
// touches the panel.
//
// The PicoSystem SDK drives the same panel in 12-bit mode through a PIO
// program that bit-bangs the two SPI pins; that path needs its whole-frame
// buffer. Here the pins go to SPI0 instead, and each band is a plain window
// write: CASET / RASET / RAMWR, then the pixels. The bands are RGB565BE
// (big-endian in memory, which is what the panel wants on the wire); the
// DMA reads them as 16-bit words and byte-swaps each one, so the SPI can run
// with 16-bit frames -- one DREQ handshake per pixel rather than per byte.

#include <cstddef>
#include <cstdint>

namespace ds {

class Display {
 public:
  // Bring the panel up. Call once, after the system clock is set: the SPI
  // divider is derived from clk_peri at this point.
  void init();

  // Start pushing `h` rows of `w` pixels (RGB565BE) to rows y..y+h-1. The
  // window commands go out synchronously (a few microseconds), the pixels by
  // DMA; call complete() before touching the buffer again or starting the
  // next band. Calling this with a transfer still in flight waits for it.
  void writeStart(int y, int w, int h, const uint16_t *pixels);

  // Whether a transfer is still in flight
  bool busy() const;

  // Wait for the transfer to drain out of the SPI and release the chip
  // select. Idempotent.
  void complete();

 private:
  void command(uint8_t c, const uint8_t *data = nullptr, size_t len = 0);
  void setWindow(int x, int y, int w, int h);
  int dma_ = -1;
  bool pending_ = false;
};

}  // namespace ds

#endif
