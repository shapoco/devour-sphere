#include "display.hpp"

#include <driver/gpio.h>
#include <driver/spi.h>
#include <esp8266/spi_struct.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "ds_config.hpp"

namespace ds {

namespace {

// ST7735 commands used here
enum : uint8_t {
  SWRESET = 0x01,
  SLPOUT = 0x11,
  NORON = 0x13,
  INVOFF = 0x20,
  DISPON = 0x29,
  CASET = 0x2A,
  RASET = 0x2B,
  RAMWR = 0x2C,
  MADCTL = 0x36,
  COLMOD = 0x3A,
  FRMCTR1 = 0xB1,
  FRMCTR2 = 0xB2,
  FRMCTR3 = 0xB3,
  INVCTR = 0xB4,
  PWCTR1 = 0xC0,
  PWCTR2 = 0xC1,
  PWCTR3 = 0xC2,
  PWCTR4 = 0xC3,
  PWCTR5 = 0xC4,
  VMCTR1 = 0xC5,
  GMCTRP1 = 0xE0,
  GMCTRN1 = 0xE1,
};

inline void spiWait() {
  while (SPI1.cmd.usr) {
  }
}

void delayMs(int ms) { vTaskDelay(pdMS_TO_TICKS(ms) + 1); }

}  // namespace

// Up to 64 bytes per FIFO load. The words are copied as they lie in memory
// and the controller sends the low byte of each first (wr_byte_order 0),
// so the byte stream on the wire is the buffer's.
void Display::bytes(const uint8_t *p, size_t n) {
  while (n > 0) {
    const size_t chunk = n > 64 ? 64 : n;
    spiWait();
    SPI1.user1.usr_mosi_bitlen = chunk * 8 - 1;
    const size_t words = (chunk + 3) / 4;
    if (((uintptr_t)p & 3) == 0) {
      const uint32_t *w = (const uint32_t *)p;
      for (size_t i = 0; i < words; i++) SPI1.data_buf[i] = w[i];
    } else {
      uint32_t tmp[16];
      std::memcpy(tmp, p, chunk);
      for (size_t i = 0; i < words; i++) SPI1.data_buf[i] = tmp[i];
    }
    SPI1.cmd.usr = 1;
    p += chunk;
    n -= chunk;
  }
}

// One command with its parameters: D/C low for the command byte and high
// for the parameters. The chip select stays low (see display.hpp), so the
// SPI must be idle before D/C moves.
void Display::command(uint8_t c, const uint8_t *data, size_t len) {
  spiWait();
  gpio_set_level((gpio_num_t)PIN_DC, 0);
  bytes(&c, 1);
  spiWait();
  gpio_set_level((gpio_num_t)PIN_DC, 1);
  if (len) bytes(data, len);
}

void Display::init() {
  gpio_config_t dc = {};
  dc.pin_bit_mask = 1ull << PIN_DC;
  dc.mode = GPIO_MODE_OUTPUT;
  gpio_config(&dc);
  gpio_set_level((gpio_num_t)PIN_DC, 1);

  // The HSPI as a master with MOSI and SCK only: no MISO, and no chip
  // select (GPIO15 stays a plain pin; the panel's CS is on the expander).
  // Mode 0, MSB first, bytes in memory order.
  spi_config_t cfg = {};
  cfg.interface.val = 0;
  cfg.interface.cpol = 0;
  cfg.interface.cpha = 0;
  cfg.interface.bit_tx_order = SPI_BIT_ORDER_MSB_FIRST;
  cfg.interface.byte_tx_order = SPI_BYTE_ORDER_LSB_FIRST;
  cfg.interface.mosi_en = 1;
  cfg.interface.miso_en = 0;
  cfg.interface.cs_en = 0;
  cfg.intr_enable.val = 0;
  cfg.event_cb = nullptr;
  cfg.mode = SPI_MASTER_MODE;
  cfg.clk_div = (spi_clk_div_t)SPI_CLK_DIV;
  spi_init(HSPI_HOST, &cfg);
  // bytes() drives the controller itself (the driver's spi_trans() would
  // set these per call): a write-data phase and nothing else
  spiWait();
  SPI1.user.usr_command = 0;
  SPI1.user.usr_addr = 0;
  SPI1.user.usr_dummy = 0;
  SPI1.user.usr_miso = 0;
  SPI1.user.usr_mosi = 1;
  SPI1.user.usr_mosi_highpart = 0;
  SPI1.user.duplex = 0;

  // TFT_eSPI's ST7735 "green tab" bring-up (Rcmd1, Rcmd2green, Rcmd3), as
  // the ESPboy library configures it, command for command. The delays are
  // the library's.
  command(SWRESET);
  delayMs(150);
  command(SLPOUT);
  delayMs(500);
  command(FRMCTR1, (const uint8_t *)"\x01\x2C\x2D", 3);
  command(FRMCTR2, (const uint8_t *)"\x01\x2C\x2D", 3);
  command(FRMCTR3, (const uint8_t *)"\x01\x2C\x2D\x01\x2C\x2D", 6);
  command(INVCTR, (const uint8_t *)"\x07", 1);
  command(PWCTR1, (const uint8_t *)"\xA2\x02\x84", 3);
  command(PWCTR2, (const uint8_t *)"\xC5", 1);
  command(PWCTR3, (const uint8_t *)"\x0A\x00", 2);
  command(PWCTR4, (const uint8_t *)"\x8A\x2A", 2);
  command(PWCTR5, (const uint8_t *)"\x8A\xEE", 2);
  command(VMCTR1, (const uint8_t *)"\x0E", 1);
  command(INVOFF);
  command(MADCTL, &PANEL_MADCTL, 1);
  command(COLMOD, (const uint8_t *)"\x05", 1);  // 16-bit color
  command(CASET, (const uint8_t *)"\x00\x02\x00\x81", 4);
  command(RASET, (const uint8_t *)"\x00\x01\x00\xA0", 4);
  command(GMCTRP1,
          (const uint8_t *)"\x02\x1C\x07\x12\x37\x32\x29\x2D\x29\x25\x2B\x39"
                           "\x00\x01\x03\x10",
          16);
  command(GMCTRN1,
          (const uint8_t *)"\x03\x1D\x07\x06\x2E\x2C\x29\x2D\x2E\x2E\x37\x3F"
                           "\x00\x00\x02\x10",
          16);
  command(NORON);
  delayMs(10);
  command(DISPON);
  delayMs(100);

  // Clear the panel before the backlight comes on (input.cpp turns it on
  // after this returns), so the first thing seen is not the panel's RAM
  fill(0x0000);
}

void Display::fill(uint16_t rgb565be) {
  static uint16_t row[SCREEN_W];
  for (int i = 0; i < SCREEN_W; i++) row[i] = rgb565be;
  for (int y = 0; y < SCREEN_H; y++) write(y, SCREEN_W, 1, row);
}

void Display::setWindow(int x, int y, int w, int h) {
  x += PANEL_COL_START;
  y += PANEL_ROW_START;
  const uint16_t x1 = (uint16_t)(x + w - 1), y1 = (uint16_t)(y + h - 1);
  const uint8_t cols[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x1 >> 8),
                           (uint8_t)x1};
  const uint8_t rows[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8),
                           (uint8_t)y1};
  command(CASET, cols, 4);
  command(RASET, rows, 4);
  command(RAMWR);
}

void Display::write(int y, int w, int h, const uint16_t *pixels) {
  setWindow(0, y, w, h);
  bytes((const uint8_t *)pixels, (size_t)w * h * 2);
  spiWait();
}

}  // namespace ds
