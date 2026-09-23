// The frame buffer (RGB565_SWAPPED, any size) to colored text, and the text to
// the terminal with as few bytes as the previous frame allows.
//
// Three ways to turn a cell of pixels into a character (SPEC.md, "描画"):
//
//   ASCII    the cell is sampled 3 wide x 6 high, thresholded to 18 bits,
//            and the printable ASCII character whose glyph is closest to
//            that pattern (a lit sub-pixel the glyph misses counts double)
//            is looked up in a 2^18-entry table. One color per cell: the
//            most common (or the brightest) lit color.
//   BRAILLE  2 x 4 dots map one-to-one onto U+2800..U+28FF: no lookup and
//            no approximation. One color per cell.
//   HALF     the upper half block U+2580 with the top half's color as the
//            foreground and the bottom half's as the background: 1 x 2
//            pixels per cell, two colors.
//
// The cell grid is the largest one of a 1:2 cell aspect that fits the
// terminal; it is centered, and the rest of the screen stays black. The
// background is set to black explicitly (the terminal's own background is
// not used), so a light-themed terminal shows the same picture.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ds::aa {

enum class Mode { ASCII, BRAILLE, HALF };
enum class Palette { TRUECOLOR, C256 };

struct Options {
  Mode mode = Mode::ASCII;
  Palette palette = Palette::TRUECOLOR;
  int threshold = 26;  // a pixel is lit when its luma (0..255) reaches this
  bool brightColor =
      false;  // color: the brightest bin instead of the most common
};

struct Cell {
  uint32_t ch = ' ';  // code point
  uint32_t fg = 0;    // 0xRRGGBB; ignored for a space
  uint32_t bg = 0;    // 0xRRGGBB (HALF); the default background otherwise
  bool operator==(const Cell &o) const {
    return ch == o.ch && bg == o.bg && (ch == ' ' || fg == o.fg);
  }
};

class Converter {
 public:
  // Builds the ASCII glyph table (about 30 ms) when the mode needs it
  void init(const Options &opts);
  const Options &options() const { return opts_; }

  // The terminal size in cells; the grid is derived from it and from the
  // frame size given to convert(). A change forces a full redraw.
  void setScreen(int cols, int rows, int fbW, int fbH);
  int gridW() const { return gw_; }
  int gridH() const { return gh_; }
  int screenCols() const { return cols_; }
  int screenRows() const { return rows_; }

  // The frame into the cells
  void convert(const uint16_t *fb, int w, int h, uint32_t stride);
  // Text (ASCII) over the cells, white; for the stats line
  void overlay(int x, int y, const char *text);

  // The escape sequences that bring the terminal from the previous frame to
  // this one (everything on the first call or after a resize). The previous
  // frame is replaced by this one.
  const std::string &emit();
  void forceRedraw() { redraw_ = true; }
  // The cells as lines of text with colors (for --once and for tests)
  std::string dump() const;

  // What the glyph table decided, for a look: the character of a pattern
  char asciiFor(uint32_t pattern18) const {
    return (char)table_[pattern18 & 0x3FFFF];
  }

 private:
  Options opts_;
  int cols_ = 0, rows_ = 0, gw_ = 0, gh_ = 0, fbW_ = 0, fbH_ = 0;
  int ox_ = 0, oy_ = 0;  // where the grid sits on the screen
  std::vector<Cell> cells_, prev_;
  std::vector<uint8_t> luma_;
  std::vector<uint8_t> table_;  // 2^18 -> ASCII (ASCII mode)
  std::string out_;
  bool redraw_ = true;

  void buildTable();
  uint32_t cellColor(const uint16_t *fb, uint32_t stride, int x0, int y0,
                     int x1, int y1) const;
  uint32_t pattern(int x0, int y0, int x1, int y1, int sw, int sh) const;
  void appendColor(uint32_t rgb, bool background);
  void appendChar(uint32_t ch);
};

// RGB565_SWAPPED pixel to 0xRRGGBB
uint32_t rgb565SwappedTo888(uint16_t p);

}  // namespace ds::aa
