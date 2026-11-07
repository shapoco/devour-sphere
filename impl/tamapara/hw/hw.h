/* Tamagotchi Paradise hardware (Sonix SNC73410): what hw.c offers the
 * programs in this directory. Taken over from Paramix Studio (mameise,
 * GPL-3.0-or-later), see NOTICE.md. Everything here runs from PRAM. */
#ifndef HW_H
#define HW_H
#include "io.h"
#define BTN_A 1u       /* P0.9  */
#define BTN_B 2u       /* P0.11 */
#define BTN_C 4u       /* P0.10 */
#define BTN_WHEEL 8u   /* P0.8, the dial pressed */
#ifndef PARADISE_REBOOT
#define PARADISE_REBOOT 0x100FF568u
#endif
void hw_init(void);
void hw_wdt_feed(void);
u32  hw_buttons(void);
/* The dial's quadrature inputs, P2.0 and P2.1, as two bits (poll every millisecond) */
static inline u32 hw_encoder(void){ return REG(0x4001A000u) & 3u; }
void hw_push(const u16* fb, u32 stride, u32 x0, u32 y0, u32 x1, u32 y1);
void hw_back_to_paradise(void);
void hw_backlight(int on);
void hw_bl_pwm_off(void);
void hw_tone(u32 hz);              /* 0 = off */
/* Pixel output: a window, then the pixels in panel order (big-endian RGB565) */
void hw_px_begin(u32 x0, u32 y0, u32 x1, u32 y1);
void hw_px_row(const u16* r, u32 n);   /* host-order RGB565, swapped on the way */
void hw_px_raw(const u8* p, u32 n);    /* bytes as they are */
void hw_px_end(void);
#endif
