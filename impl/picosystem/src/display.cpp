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

void Display::command(uint8_t c, const uint8_t *data, size_t len) {
  gpio_put(PIN_DC, 0);  // command
  spi_write_blocking(SPI, &c, 1);
  if (len) {
    gpio_put(PIN_DC, 1);  // parameters
    spi_write_blocking(SPI, data, len);
  }
}

void Display::init() {
  // Backlight straight on; the SDK dims it through PWM, which nothing here
  // needs
  gpio_init(PIN_BACKLIGHT);
  gpio_set_dir(PIN_BACKLIGHT, GPIO_OUT);
  gpio_put(PIN_BACKLIGHT, 0);  // off until the panel is configured

  gpio_init(PIN_CS);
  gpio_set_dir(PIN_CS, GPIO_OUT);
  gpio_put(PIN_CS, 1);
  gpio_init(PIN_DC);
  gpio_set_dir(PIN_DC, GPIO_OUT);
  gpio_init(PIN_RESET);
  gpio_set_dir(PIN_RESET, GPIO_OUT);

  // The commands go out at the full rate too: the panel takes them at the
  // pixel clock (that is what the Xiamocon SDK does), and it saves switching
  // the baud rate per band
  spi_init(SPI, SPI_HZ);
  spiFormat8();
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

  // Reset cycle
  gpio_put(PIN_RESET, 0);
  sleep_ms(100);
  gpio_put(PIN_RESET, 1);
  sleep_ms(10);

  // The PicoSystem SDK's bring-up sequence (hardware.cpp, MIT), except for
  // COLMOD: 0x55 selects the 16-bit RGB565 mode instead of the SDK's 12-bit
  // one. The panel-specific voltages and gamma tables are copied verbatim.
  gpio_put(PIN_CS, 0);
  command(SWRESET);
  sleep_ms(150);
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
  gpio_put(PIN_CS, 1);

  // One DMA channel, reused for every band: 16-bit words, byte-swapped on
  // the way out (the buffer is big-endian, the SPI shifts each word MSB
  // first), paced by the SPI's TX FIFO
  dma_ = dma_claim_unused_channel(true);

  // Clear the panel before the backlight comes on, so the first thing seen
  // is not whatever the panel RAM held
  static uint16_t black[SCREEN_W];
  for (int i = 0; i < SCREEN_W; i++) black[i] = 0;
  for (int y = 0; y < SCREEN_H; y++) {
    writeStart(y, SCREEN_W, 1, black);
    complete();
  }
  gpio_put(PIN_BACKLIGHT, 1);
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
  complete();  // the SPI must be idle before its frame size is changed
  gpio_put(PIN_CS, 0);
  spiFormat8();
  setWindow(0, y, w, h);
  gpio_put(PIN_DC, 1);  // pixel data from here on
  spiFormat16();

  dma_channel_config c = dma_channel_get_default_config(dma_);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, spi_get_dreq(SPI, true));
  channel_config_set_bswap(&c, true);
  dma_channel_configure(dma_, &c, &spi_get_hw(SPI)->dr, pixels,
                        (uint32_t)(w * h), true);
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
