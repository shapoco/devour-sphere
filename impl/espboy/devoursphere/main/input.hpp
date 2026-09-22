#ifndef DS_INPUT_HPP
#define DS_INPUT_HPP

// The ESPboy's I2C devices: the MCP23017 port expander that carries the
// eight buttons (and the panel's chip select), and the MCP4725 DAC that
// sets the backlight.

#include <cstdint>

namespace ds {
namespace input {

// The eight buttons as the expander reports them (bit set = pressed)
enum : uint8_t {
  LEFT = 0x01,
  UP = 0x02,
  DOWN = 0x04,
  RIGHT = 0x08,
  ACT = 0x10,  // A
  ESC = 0x20,  // B
  LFT = 0x40,  // left shoulder
  RGT = 0x80,  // right shoulder
};

// Bring up the bus, the expander (inputs with pull-ups, the panel's chip
// select low) and leave the backlight off. Call before Display::init().
void init();

// The backlight, 0..4095 (ignored on a board without the DAC)
void setBacklight(int level);

// One read of the buttons (one I2C transaction, about a quarter of a
// millisecond). Bits as above.
uint8_t read();

}  // namespace input
}  // namespace ds

#endif
