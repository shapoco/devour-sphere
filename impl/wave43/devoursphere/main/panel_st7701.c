// The wave_43's ST7701 panel on MIPI-DSI. The init sequence and the video
// timing are Waveshare's for this 4.3" glass, as Kern's BSP carries them
// (~/Kern/components/wave_43/wave_43.c) and runs them on IDF 6.1; see
// panel_st7701.h.

#include "panel_st7701.h"

#include <esp_check.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_st7701.h>
#include <esp_ldo_regulator.h>
#include <esp_log.h>

static const char *TAG = "st7701";

#define LCD_RST_GPIO 27
#define DSI_LANES 2
#define DSI_LANE_MBPS 500
#define DSI_PHY_LDO_CHAN 3
#define DSI_PHY_LDO_MV 2500

/* Vendor-specific ST7701 init sequence (from Waveshare upstream BSP for the
   4.3" panel -- required for correct gamma/timing/voltage on this glass). */
static const st7701_lcd_init_cmd_t vendor_specific_init_default[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x17, 0x08}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xB0,
     (uint8_t[]){0x40, 0xC9, 0x94, 0x0E, 0x10, 0x05, 0x0B, 0x09, 0x08, 0x26,
                 0x04, 0x52, 0x10, 0x69, 0x6B, 0x69},
     16, 0},
    {0xB1,
     (uint8_t[]){0x40, 0xD2, 0x98, 0x0C, 0x92, 0x07, 0x09, 0x08, 0x07, 0x25,
                 0x02, 0x0E, 0x0C, 0x6E, 0x78, 0x55},
     16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x4E}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x4E}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xB9, (uint8_t[]){0x10, 0x1F}, 2, 0},
    {0xBB, (uint8_t[]){0x03}, 1, 0},
    {0xBC, (uint8_t[]){0x00}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x3A, 0x02}, 3, 0},
    {0xE1,
     (uint8_t[]){0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40,
                 0x40},
     11, 0},
    {0xE2,
     (uint8_t[]){0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0,
                 0x00, 0xA0, 0x00},
     13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5,
     (uint8_t[]){0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A,
                 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0},
     16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8,
     (uint8_t[]){0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29,
                 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0},
     16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}, 7, 0},
    {0xEC, (uint8_t[]){0x08, 0x01}, 2, 0},
    {0xED,
     (uint8_t[]){0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF,
                 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B},
     16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

esp_err_t ds_panel_st7701_new(esp_lcd_panel_handle_t *ret_panel) {
  esp_err_t ret = ESP_OK;

  // The DSI PHY is powered from one of the chip's own LDOs. Acquired for
  // good: the panel is never torn down.
  static esp_ldo_channel_handle_t phy_ldo = NULL;
  const esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = DSI_PHY_LDO_CHAN,
      .voltage_mv = DSI_PHY_LDO_MV,
  };
  ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo), TAG,
                      "DSI PHY LDO");

  esp_lcd_dsi_bus_handle_t bus = NULL;
  const esp_lcd_dsi_bus_config_t bus_cfg = {
      .bus_id = 0,
      .num_data_lanes = DSI_LANES,
      .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = DSI_LANE_MBPS,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "DSI bus");

  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_handle_t panel = NULL;
  const esp_lcd_dbi_io_config_t dbi_cfg = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &io), err, TAG,
                    "DBI io");

  // 30 MHz over (480 + 42 + 12 + 42) x (800 + 2 + 8 + 60) is 59.9 Hz. One
  // framebuffer: the PPA writes into it while it is scanned (SPEC.md,
  // "tearing").
  const esp_lcd_dpi_panel_config_t dpi_cfg = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = 30,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = 1,
      .video_timing =
          {
              .h_size = 480,
              .v_size = 800,
              .hsync_pulse_width = 12,
              .hsync_back_porch = 42,
              .hsync_front_porch = 42,
              .vsync_pulse_width = 8,
              .vsync_back_porch = 2,
              .vsync_front_porch = 60,
          },
  };
  const st7701_vendor_config_t vendor_cfg = {
      .init_cmds = vendor_specific_init_default,
      .init_cmds_size = sizeof(vendor_specific_init_default) /
                        sizeof(vendor_specific_init_default[0]),
      .mipi_config =
          {
              .dsi_bus = bus,
              .dpi_config = &dpi_cfg,
          },
      .flags =
          {
              .use_mipi_interface = 1,
          },
  };
  const esp_lcd_panel_dev_config_t dev_cfg = {
      .reset_gpio_num = LCD_RST_GPIO,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
      .vendor_config = (void *)&vendor_cfg,
  };
  ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7701(io, &dev_cfg, &panel), err, TAG,
                    "ST7701 panel");
  ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(panel), err, TAG, "panel reset");
  ESP_GOTO_ON_ERROR(esp_lcd_panel_init(panel), err, TAG, "panel init");

  *ret_panel = panel;
  return ESP_OK;

err:
  if (panel) esp_lcd_panel_del(panel);
  if (io) esp_lcd_panel_io_del(io);
  if (bus) esp_lcd_del_dsi_bus(bus);
  return ret;
}
