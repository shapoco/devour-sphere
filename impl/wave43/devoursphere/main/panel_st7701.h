#ifndef DS_PANEL_ST7701_H
#define DS_PANEL_ST7701_H

// The wave_43's panel brought up on the IDF's MIPI-DSI driver: the PHY's
// LDO, the DSI bus, the ST7701's vendor init sequence and the DPI video
// timing. C, because the init table is written the way Waveshare and Kern
// write it -- with compound literals, which C++ does not have.

#include <esp_err.h>
#include <esp_lcd_types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reset and initialize the panel and leave it scanning out a single
// framebuffer of 480x800 RGB565 in PSRAM (esp_lcd_dpi_panel_get_frame_buffer
// hands it out).
esp_err_t ds_panel_st7701_new(esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif

#endif
