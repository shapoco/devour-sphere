#include "display.hpp"

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <pico/stdlib.h>

#include "ds_config.hpp"

namespace ds {

namespace {

// Pin numbers from pico-sdk's board file (pimoroni_picosystem.h)
constexpr uint PIN_CS = PICOSYSTEM_LCD_CSN_PIN;
constexpr uint PIN_SCK = PICOSYSTEM_LCD_SCLK_PIN;
constexpr uint PIN_MOSI = PICOSYSTEM_LCD_MOSI_PIN;
constexpr uint PIN_DC = PICOSYSTEM_LCD_DC_PIN;
constexpr uint PIN_RESET = PICOSYSTEM_LCD_RESET_PIN;
constexpr uint PIN_BACKLIGHT = PICOSYSTEM_BACKLIGHT_PIN;
spi_inst_t *const SPI = spi0;

// ST7789 commands used here
enum : uint8_t {
  SWRESET = 0x01,
  SLPOUT = 0x11,
  INVON = 0x21,
  GAMSET = 0x26,
  DISPON = 0x29,
  CASET = 0x2A,
  RASET = 0x2B,
  RAMWR = 0x2C,
  TEON = 0x35,
  MADCTL = 0x36,
  COLMOD = 0x3A,
  FRMCTR2 = 0xB2,
  GCTRL = 0xB7,
  VCOMS = 0xBB,
  LCMCTRL = 0xC0,
  VDVVRHEN = 0xC2,
  VRHS = 0xC3,
  VDVS = 0xC4,
  FRCTRL2 = 0xC6,
  PWRCTRL1 = 0xD0,
  GMCTRP1 = 0xE0,
  GMCTRN1 = 0xE1,
};

void spiFormat8() {
  spi_set_format(SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}
void spiFormat16() {
  spi_set_format(SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

}  // namespace

// One command with its parameters, framed exactly as the PicoSystem SDK's
// _screen_command(): chip select low for the duration of this command only,
// D/C low for the command byte and high for the parameters, at the SDK's
// 8 MHz command clock. The SPI is left in 8-bit mode at that clock.
void Display::command(uint8_t c, const uint8_t *data, size_t len) {
  gpio_put(PIN_CS, 0);
  gpio_put(PIN_DC, 0);
  spi_write_blocking(SPI, &c, 1);
  if (len) {
    gpio_put(PIN_DC, 1);
    spi_write_blocking(SPI, data, len);
  }
  gpio_put(PIN_CS, 1);
}

void Display::init() {
  // Backlight: the SDK drives it through PWM (gamma corrected); a plain
  // output is enough here. Off until the panel is configured.
  gpio_init(PIN_BACKLIGHT);
  gpio_set_dir(PIN_BACKLIGHT, GPIO_OUT);
  gpio_put(PIN_BACKLIGHT, 0);

  // From here to DISPON this is the PicoSystem SDK's _init_hardware()
  // (hardware.cpp, MIT) step for step, including the order of the pin setup,
  // the delays and the 8 MHz command clock. The one difference is COLMOD:
  // 0x55 selects the 16-bit RGB565 mode, where the SDK's 0x03 selects
  // 12-bit. Keep it that way: an ST7789 that is not brought up exactly like
  // this has shown a black panel with the backlight on.
  spi_init(SPI, CMD_HZ);
  if (pixelHz_ == 0) pixelHz_ = SPI_HZ;

  gpio_set_function(PIN_RESET, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_RESET, GPIO_OUT);
  gpio_put(PIN_RESET, 0);
  sleep_ms(100);
  gpio_put(PIN_RESET, 1);

  gpio_set_function(PIN_DC, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_DC, GPIO_OUT);
  gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
  gpio_set_dir(PIN_CS, GPIO_OUT);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
  gpio_put(PIN_CS, 1);

  command(SWRESET);
  sleep_ms(5);
  command(MADCTL, (const uint8_t *)"\x04", 1);
  command(TEON, (const uint8_t *)"\x00", 1);
  command(FRMCTR2, (const uint8_t *)"\x0C\x0C\x00\x33\x33", 5);
  command(COLMOD, (const uint8_t *)"\x55", 1);
  command(GAMSET, (const uint8_t *)"\x01", 1);

  command(GCTRL, (const uint8_t *)"\x14", 1);
  command(VCOMS, (const uint8_t *)"\x25", 1);
  command(LCMCTRL, (const uint8_t *)"\x2C", 1);
  command(VDVVRHEN, (const uint8_t *)"\x01", 1);
  command(VRHS, (const uint8_t *)"\x12", 1);
  command(VDVS, (const uint8_t *)"\x20", 1);
  command(PWRCTRL1, (const uint8_t *)"\xA4\xA1", 2);
  command(FRCTRL2, (const uint8_t *)"\x1E", 1);
  command(GMCTRP1,
          (const uint8_t *)"\xD0\x04\x0D\x11\x13\x2B\x3F\x54\x4C\x18\x0D\x0B"
                           "\x1F\x23",
          14);
  command(GMCTRN1,
          (const uint8_t *)"\xD0\x04\x0C\x11\x13\x2C\x3F\x44\x51\x2F\x1F\x1F"
                           "\x20\x23",
          14);
  command(INVON);
  sleep_ms(115);
  command(SLPOUT);
  command(DISPON);

  // One DMA channel, reused for every band: 16-bit words, byte-swapped on
  // the way out (the buffer is big-endian, the SPI shifts each word MSB
  // first), paced by the SPI's TX FIFO
  dma_ = dma_claim_unused_channel(true);

  // Clear the panel before the backlight comes on, so the first thing seen
  // is not whatever the panel RAM held
  fill(0x0000);
  gpio_put(PIN_BACKLIGHT, 1);
}

void Display::fill(uint16_t rgb565be) {
  static uint16_t row[SCREEN_W];
  for (int i = 0; i < SCREEN_W; i++) row[i] = rgb565be;
  for (int y = 0; y < SCREEN_H; y++) {
    writeStart(y, SCREEN_W, 1, row);
    complete();
  }
}

void Display::setWindow(int x, int y, int w, int h) {
  const uint16_t x1 = (uint16_t)(x + w - 1), y1 = (uint16_t)(y + h - 1);
  const uint8_t cols[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x1 >> 8),
                           (uint8_t)x1};
  const uint8_t rows[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8),
                           (uint8_t)y1};
  command(CASET, cols, 4);
  command(RASET, rows, 4);
  command(RAMWR);
}

void Display::writeStart(int y, int w, int h, const uint16_t *pixels) {
  complete();  // the SPI must be idle before its clock or frame size change
  // Window commands as the SDK sends them (8-bit, 8 MHz, CS per command),
  // then the pixels as one CS-low burst at the pixel clock
  spi_set_baudrate(SPI, CMD_HZ);
  spiFormat8();
  setWindow(0, y, w, h);
  spi_set_baudrate(SPI, pixelHz_);
  const uint32_t words = (uint32_t)(w * h);

  if (xfer_ == Xfer::CPU16) {
    // Synchronous: swap each word (the buffer is big-endian, the SPI shifts
    // a word MSB first) through a small staging buffer and keep the FIFO fed
    static uint16_t stage[256];
    spiFormat16();
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);
    for (uint32_t i = 0; i < words; i += 256) {
      const uint32_t n = words - i < 256 ? words - i : 256;
      for (uint32_t k = 0; k < n; k++) {
        stage[k] = (uint16_t)__builtin_bswap16(pixels[i + k]);
      }
      spi_write16_blocking(SPI, stage, n);
    }
    while (spi_is_busy(SPI)) tight_loop_contents();
    gpio_put(PIN_CS, 1);
    return;
  }

  dma_channel_config c = dma_channel_get_default_config(dma_);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, spi_get_dreq(SPI, true));
  uint32_t count = words;
  if (xfer_ == Xfer::DMA16) {
    // 16-bit words, byte-swapped on the way out, 16-bit SPI frames
    spiFormat16();
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_bswap(&c, true);
  } else {
    // Bytes in the order they already sit in memory, 8-bit SPI frames (the
    // SPI is still in 8-bit mode from the commands)
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    count = words * 2;
  }
  gpio_put(PIN_DC, 1);
  gpio_put(PIN_CS, 0);
  dma_channel_configure(dma_, &c, &spi_get_hw(SPI)->dr, pixels, count, true);
  pending_ = true;
}

bool Display::busy() const {
  return pending_ && (dma_channel_is_busy(dma_) || spi_is_busy(SPI));
}

void Display::complete() {
  if (!pending_) return;
  dma_channel_wait_for_finish_blocking(dma_);
  // The DMA is done when the last word is in the TX FIFO, not when it has
  // left the pins
  while (spi_is_busy(SPI)) tight_loop_contents();
  pending_ = false;
  gpio_put(PIN_CS, 1);
}

}  // namespace ds
