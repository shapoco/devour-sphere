#ifndef DS_CONFIG_HPP
#define DS_CONFIG_HPP

// Tunables of the ESPboy front end. See ../../SPEC.md for the reasoning
// behind the values. The Xiamocon and PicoSystem front ends have a file of
// the same name with the same members, which is what lets the three share
// profiler.cpp.

#include <cstddef>
#include <cstdint>

#include "devoursphere/sim/config.hpp"

namespace ds {

constexpr int SCREEN_W = 128;
constexpr int SCREEN_H = 128;

// The frame is rasterized and pushed one band at a time; there is one band
// buffer, because the ESP8266 pushes the pixels itself (no DMA reaches the
// SPI), so nothing could overlap a second one. 16 rows is 4 KB: the
// per-band fixed cost measured on the RP2350 was +5% at 40 rows, +14% at
// 20 and +36% at 8, so 8 rows would buy 2 KB for a fifth of the frame.
constexpr int BAND_H = 16;
constexpr int BAND_COUNT = SCREEN_H / BAND_H;
static_assert(SCREEN_H % BAND_H == 0, "the bands must tile the screen exactly");

// Working memory of the 3D renderer. Without depth planes a body's kite
// record is 48 bytes; the fixed part is about 1.5 KB with the 16-entry
// vertex cache, the span pool below 1.8 KB, and a crowded 128 px frame
// (4 full bodies, the fragments, the bullets, a few auras) is under 6 KB
// of records. 16 KB leaves room for the arena to say so on the overlay
// (ARN) before anything is thinned.
constexpr size_t ARENA_SIZE = 16 * 1024;

// Triangles a frame may spend on full entity bodies (Renderer::
// setDetailTriangles): 72 keeps the four nearest bodies (18 triangles each
// at most) and draws the rest as outlines.
constexpr int DETAIL_TRIANGLES = 72;

// Spans held per scanline. The peak at 240x240 was 32 on the Xiamocon; a
// 128 px scanline crosses fewer primitives. Overflowing drops spans, which
// leaves holes, so watch SPN on the overlay before shaving this.
constexpr int SPAN_CAPACITY = 64;

// The simulation runs at a fixed rate whatever the frame rate is
constexpr uint32_t TICK_US = 1000000u / devoursphere::sim::TICK_RATE;

// Ticks a single frame may catch up on. At 10 fps every frame owes three
// ticks; beyond that the surplus is dropped and the game slows down rather
// than the frame stalling on a burst of simulation.
constexpr int MAX_CATCHUP = 3;

// The main task's stack (CONFIG_ESP_MAIN_TASK_STACK_SIZE in
// sdkconfig.defaults; the overlay's STK0 is measured against this)
constexpr uint32_t STACK_BYTES = 8192;

// --- The panel (ST7735, 128x128, "green tab" glass) --------------------
// HSPI: SCK GPIO14, MOSI GPIO13. The chip select is not an ESP pin -- the
// ESPboy wires it to the MCP23017 (GPB0) and holds it low (input.cpp).
// D/C is GPIO16; there is no reset line (a software reset does it).
constexpr int PIN_DC = 16;
// The SPI clock is 80 MHz divided by this. 2 is 40 MHz: the transfer is
// the CPU's time here (11 ms a frame at 26.7 MHz, 7.5 at 40), and the
// WildCardBoy's LcdTap, where this build is played, follows 62.5 MHz. The
// ESPboy library runs a real ST7735 at 27 MHz (divider 3; "more than
// 27 MHz may not work"), so a real panel that shows stray pixels wants 3.
constexpr int SPI_CLK_DIV = 2;
constexpr uint32_t SPI_HZ = 80000000u / SPI_CLK_DIV;
// Where the 128x128 glass sits in the controller's 132x162 memory
// (TFT_eSPI's ST7735_GREENTAB3 at rotation 0: MADCTL 0xC8)
constexpr int PANEL_COL_START = 2;
constexpr int PANEL_ROW_START = 3;
constexpr uint8_t PANEL_MADCTL = 0xC8;  // MX | MY | BGR

// --- I2C (the MCP23017 buttons, the MCP4725 backlight) -----------------
constexpr int PIN_SDA = 4;
constexpr int PIN_SCL = 5;
constexpr uint8_t MCP23017_ADDR = 0x20;
constexpr uint8_t MCP4725_ADDR = 0x60;
constexpr uint8_t MCP23017_TFT_CS_BIT = 0;  // GPB0

// --- The speaker (GPIO0, a square wave from a timer) --------------------
constexpr int PIN_SPEAKER = 0;

// The profiler's ds_config.hpp members it prints (the Xiamocon overlay
// shows the clocks; here they are just reported once at boot)
constexpr uint32_t SYS_CLOCK_KHZ = 160000;

}  // namespace ds

#endif
