// The wave_43 board (board.hpp): backlight, panel and touch.

#include "board.hpp"

#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <esp_cache.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_touch_gt911.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

#include "ds_platform.hpp"
#include "panel_st7701.h"

namespace ds {
namespace board {

namespace {

// Pins, from Kern's BSP (components/wave_43/include/bsp/
// esp32_p4_wifi6_touch_lcd_43.h). The touch controller's RST and INT are not
// wired on this board.
constexpr gpio_num_t PIN_BACKLIGHT = GPIO_NUM_26;
constexpr gpio_num_t PIN_I2C_SCL = GPIO_NUM_8;
constexpr gpio_num_t PIN_I2C_SDA = GPIO_NUM_7;
constexpr i2c_port_num_t I2C_PORT = 1;
constexpr uint32_t I2C_HZ = 400000;

constexpr ledc_timer_t BL_TIMER = LEDC_TIMER_1;
constexpr ledc_channel_t BL_CHANNEL = LEDC_CHANNEL_1;

uint16_t *g_fb = nullptr;
constexpr uint32_t FB_BYTES = (uint32_t)PANEL_W * PANEL_H * 2;

i2c_master_bus_handle_t g_i2c = nullptr;
esp_lcd_touch_handle_t g_touch = nullptr;

// What the touch task last read, handed to the frame loop under a lock
portMUX_TYPE g_touchLock = portMUX_INITIALIZER_UNLOCKED;
TouchPoint g_points[TOUCH_MAX_POINTS];
int g_pointCount = 0;

void backlightInit() {
  // The backlight is driven through an inverting buffer, so the output is
  // inverted and a 100% duty is full brightness (Kern's BSP does the same).
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_10_BIT;
  t.timer_num = BL_TIMER;
  t.freq_hz = 5000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&t);

  ledc_channel_config_t c = {};
  c.gpio_num = PIN_BACKLIGHT;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = BL_CHANNEL;
  c.timer_sel = BL_TIMER;
  c.duty = 0;
  c.hpoint = 0;
  c.flags.output_invert = 1;
  ledc_channel_config(&c);
}

void backlightSet(int percent) {
  const uint32_t duty = 1023u * (uint32_t)percent / 100u;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

bool touchInit() {
  i2c_master_bus_config_t bus = {};
  bus.clk_source = I2C_CLK_SRC_DEFAULT;
  bus.i2c_port = I2C_PORT;
  bus.sda_io_num = PIN_I2C_SDA;
  bus.scl_io_num = PIN_I2C_SCL;
  bus.glitch_ignore_cnt = 7;
  if (i2c_new_master_bus(&bus, &g_i2c) != ESP_OK) {
    trace("i2c bus failed", 0);
    return false;
  }

  // The GT911 answers at 0x5D or at 0x14 depending on the level of INT while
  // it comes out of reset -- and INT is not wired here, so which one it is
  // cannot be chosen, only found.
  // ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() spelled out: the macro is a partial
  // designated initializer, which -Wextra rejects in C++.
  esp_lcd_panel_io_i2c_config_t io = {};
  io.control_phase_bytes = 1;
  io.dc_bit_offset = 0;
  io.lcd_cmd_bits = 16;
  io.flags.disable_control_phase = 1;
  if (i2c_master_probe(g_i2c, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100) ==
      ESP_OK) {
    io.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
  } else if (i2c_master_probe(g_i2c, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
                              100) == ESP_OK) {
    io.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
  } else {
    trace("GT911 not found", 0);
    return false;
  }
  io.scl_speed_hz = I2C_HZ;
  trace("GT911 address", io.dev_addr);

  esp_lcd_panel_io_handle_t ioHandle = nullptr;
  if (esp_lcd_new_panel_io_i2c(g_i2c, &io, &ioHandle) != ESP_OK) {
    trace("GT911 io failed", 0);
    return false;
  }
  // The controller's own portrait coordinates; the landscape mapping is
  // touch_pad.cpp's.
  esp_lcd_touch_config_t cfg = {};
  cfg.x_max = PANEL_W;
  cfg.y_max = PANEL_H;
  cfg.rst_gpio_num = GPIO_NUM_NC;
  cfg.int_gpio_num = GPIO_NUM_NC;
  if (esp_lcd_touch_new_i2c_gt911(ioHandle, &cfg, &g_touch) != ESP_OK) {
    trace("GT911 init failed", 0);
    g_touch = nullptr;
    return false;
  }
  return true;
}

void touchTask(void *) {
  esp_lcd_touch_point_data_t data[TOUCH_MAX_POINTS];
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(TOUCH_PERIOD_MS));
    uint8_t n = 0;
    // A failed read leaves the last points standing for one period rather
    // than lifting every finger: a glitch on the bus should not read as the
    // player letting go of the disc.
    if (esp_lcd_touch_read_data(g_touch) != ESP_OK) continue;
    if (esp_lcd_touch_get_data(g_touch, data, &n, TOUCH_MAX_POINTS) != ESP_OK) {
      n = 0;
    }
    portENTER_CRITICAL(&g_touchLock);
    g_pointCount = n;
    for (int i = 0; i < n; i++) {
      g_points[i] = {(int32_t)data[i].track_id, (int)data[i].x, (int)data[i].y};
    }
    portEXIT_CRITICAL(&g_touchLock);
  }
}

}  // namespace

bool init() {
  backlightInit();
  backlightSet(0);  // dark until there is a picture

  esp_lcd_panel_handle_t panel = nullptr;
  if (ds_panel_st7701_new(&panel) != ESP_OK) {
    trace("panel bring-up failed", 0);
    return false;
  }
  void *fb = nullptr;
  if (esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || !fb) {
    trace("no framebuffer", 0);
    return false;
  }
  g_fb = (uint16_t *)fb;
  // Black, and written back: the PPA writes around the cache from here on,
  // and a dirty line flushed later would land over its output.
  memset(g_fb, 0, FB_BYTES);
  esp_cache_msync(
      g_fb, FB_BYTES,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  trace("panel fb", (uint32_t)(uintptr_t)g_fb);

  if (!touchInit()) trace("running without touch", 0);

  backlightSet(BACKLIGHT_PERCENT);
  return true;
}

uint16_t *framebuffer() { return g_fb; }
uint32_t framebufferBytes() { return FB_BYTES; }

void startTouch() {
  if (!g_touch) return;
  // Core0 with the frame loop, one priority above it (app_main runs at 1):
  // it wakes, starts a transfer and sleeps on the bus, so what it takes from
  // the rasterizer is the few microseconds either side of that.
  xTaskCreatePinnedToCore(touchTask, "ds_touch", 4096, nullptr, 2, nullptr, 0);
}

int touchPoints(TouchPoint *out, int max) {
  portENTER_CRITICAL(&g_touchLock);
  const int n = g_pointCount < max ? g_pointCount : max;
  for (int i = 0; i < n; i++) out[i] = g_points[i];
  portEXIT_CRITICAL(&g_touchLock);
  return n;
}

}  // namespace board
}  // namespace ds
