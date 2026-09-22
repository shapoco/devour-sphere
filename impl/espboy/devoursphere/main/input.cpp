#include "input.hpp"

#include <driver/i2c.h>
#include <esp_log.h>

#include "ds_config.hpp"

namespace ds {
namespace input {

namespace {

constexpr i2c_port_t PORT = I2C_NUM_0;
constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(20);
constexpr const char *TAG = "input";

// MCP23017 registers, BANK = 0 (the power-on layout)
enum : uint8_t {
  IODIRA = 0x00,
  IODIRB = 0x01,
  GPPUA = 0x0C,
  GPIOA = 0x12,
  OLATB = 0x15,
};

bool writeRegs(uint8_t addr, uint8_t reg, const uint8_t *data, int n) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (uint8_t)(addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  for (int i = 0; i < n; i++) i2c_master_write_byte(cmd, data[i], true);
  i2c_master_stop(cmd);
  const esp_err_t err = i2c_master_cmd_begin(PORT, cmd, TIMEOUT);
  i2c_cmd_link_delete(cmd);
  return err == ESP_OK;
}

bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  return writeRegs(addr, reg, &value, 1);
}

bool readReg(uint8_t addr, uint8_t reg, uint8_t *out) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (uint8_t)(addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (uint8_t)(addr << 1) | I2C_MASTER_READ, true);
  i2c_master_read_byte(cmd, out, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  const esp_err_t err = i2c_master_cmd_begin(PORT, cmd, TIMEOUT);
  i2c_cmd_link_delete(cmd);
  return err == ESP_OK;
}

}  // namespace

void init() {
  // The SDK's I2C is bit-banged (there is no controller on the ESP8266);
  // the ESPboy library asks for 400 kHz, which this does not reach, and
  // the expander is fine with less
  i2c_config_t cfg = {};
  cfg.mode = I2C_MODE_MASTER;
  cfg.sda_io_num = (gpio_num_t)PIN_SDA;
  cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
  cfg.scl_io_num = (gpio_num_t)PIN_SCL;
  cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
  cfg.clk_stretch_tick = 300;
  i2c_driver_install(PORT, cfg.mode);
  i2c_param_config(PORT, &cfg);

  // The buttons: port A all inputs with pull-ups (pressed reads 0). Port
  // B: GPB0 is the panel's chip select, an output held low; the rest
  // inputs.
  bool ok = writeReg(MCP23017_ADDR, IODIRA, 0xFF);
  ok = writeReg(MCP23017_ADDR, GPPUA, 0xFF) && ok;
  ok = writeReg(MCP23017_ADDR, OLATB, 0x00) && ok;
  ok = writeReg(MCP23017_ADDR, IODIRB,
                (uint8_t)~(1u << MCP23017_TFT_CS_BIT)) &&
       ok;
  if (!ok) ESP_LOGE(TAG, "MCP23017 not answering");
  setBacklight(0);
}

// The DAC's "write DAC register" command (0x40): 12 bits, left aligned in
// two bytes. A board without the DAC (the DIY variants) NAKs and that is
// the end of it.
void setBacklight(int level) {
  if (level < 0) level = 0;
  if (level > 4095) level = 4095;
  const uint8_t data[2] = {(uint8_t)(level >> 4), (uint8_t)((level & 0xF) << 4)};
  writeRegs(MCP4725_ADDR, 0x40, data, 2);
}

uint8_t read() {
  uint8_t v = 0xFF;
  if (!readReg(MCP23017_ADDR, GPIOA, &v)) return 0;
  return (uint8_t)~v;
}

}  // namespace input
}  // namespace ds
