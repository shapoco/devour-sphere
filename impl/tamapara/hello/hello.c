/* The smallest program for the Paradise: the way in and the way out.
 *
 * Shows that the launcher, the panel, the buttons, the dial, the buzzer and
 * the millisecond tick work before the game is tried. Runs from PRAM only
 * (one 64 KB image, no flash window), started from the home screen with
 * A and C held.
 *
 *   screen     four squares light up with A, B, C and the dial's push;
 *              a bar follows the dial's turn (one step = one pixel, so the
 *              steps per detent can be counted; clockwise lengthens it); a dot blinks once a second
 *   buzzer     B gives a short beep
 *   dial held  2 s: back to the Paradise
 *
 * GPL-3.0-or-later (this directory, see ../hw/NOTICE.md). */
#include "hw.h"
#include "flash.h"

/* The launcher expects this block at 0x140 of the image (after the 80 vectors) */
typedef struct { u32 magic, version, guest_off, guest_size, save_off, device; u8 map[128]; } PmxCfg;
__attribute__((section(".pmxcfg"), used)) volatile const PmxCfg g_cfg = { 0x43584D50u, 1, 0, 0, 0, 0, {0} };

#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)

static volatile u32 g_ms;
static volatile u32 g_enc_last; static volatile int g_enc_steps;
static const signed char g_qd[16] = {0,1,-1,0, -1,0,0,1, 1,0,0,-1, 0,-1,1,0}; /* clockwise positive */

void systick_c(void){
  g_ms++;
  hw_wdt_feed();
  u32 n = hw_encoder();
  if(n != g_enc_last){ g_enc_steps += g_qd[(g_enc_last << 2) | n]; g_enc_last = n; }
}
__attribute__((naked)) void SysTick_Handler(void){ __asm__ volatile("b systick_c\n"); }

static u16 g_row[128];
static void fill_rect(u32 x0, u32 y0, u32 w, u32 h, u16 c){
  for(u32 i = 0; i < w; i++) g_row[i] = c;
  for(u32 y = y0; y < y0 + h; y++){ hw_px_begin(x0, y, x0 + w, y + 1); hw_px_row(g_row, w); hw_px_end(); }
}
static void clear_screen(void){
  for(u32 y = 0; y < 128; y++){ for(u32 x = 0; x < 128; x++) g_row[x] = 0x0000; hw_px_begin(0, y, 128, y + 1); hw_px_row(g_row, 128); hw_px_end(); }
}

static void leave(void){
  hw_tone(0);
  __asm__ volatile("cpsid i");
  SYST_CSR = 0;
  REG(0x20000FFCu) = 0x4C584D50u;   /* "PMXL": back on purpose (the launcher's autostart stays off) */
  hw_bl_pwm_off();
  for(u32 t = 0; hw_buttons() && t < 20000000u; t++) if((t & 0xFFFFu) == 0) hw_wdt_feed();
  hw_back_to_paradise();
}

extern u32 __data_load, __data_start, __data_end, __bss_start, __bss_end;
void Reset_Handler(void){
  __asm__ volatile("cpsid i");
  REG(0xE000ED14u) &= ~(1u << 3);   /* UNALIGN_TRP off */
  { const u32 *s = &__data_load; for(u32 *p = &__data_start; p < &__data_end; p++) *p = *s++; }
  for(u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  hw_init();
  clear_screen();
  for(u32 i = 0; i < 4; i++){ REG(0xE000E180u + 4u * i) = 0xFFFFFFFFu; REG(0xE000E280u + 4u * i) = 0xFFFFFFFFu; }
  REG(0x4001A000u + 0x04) &= ~3u;   /* P2.0, P2.1 inputs */
  g_enc_last = hw_encoder();
  SYST_RVR = 96000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");
  /* wait until the start keys (A+C) are released */
  for(u32 t0 = g_ms; (hw_buttons() & (BTN_A | BTN_C)) && g_ms - t0 < 3000u;){}
  u32 last = 0xFFFFFFFFu, wheel_t0 = 0, beep_end = 0, blink = 0;
  int last_steps = 0x7FFFFFFF;
  for(;;){
    hw_wdt_feed();
    const u32 b = hw_buttons(), now = g_ms;
    if(b != last){
      fill_rect(8, 40, 24, 24, (b & BTN_A) ? 0xF800u : 0x2104u);
      fill_rect(40, 40, 24, 24, (b & BTN_B) ? 0x07E0u : 0x2104u);
      fill_rect(72, 40, 24, 24, (b & BTN_C) ? 0x001Fu : 0x2104u);
      fill_rect(104, 40, 16, 24, (b & BTN_WHEEL) ? 0xFFE0u : 0x2104u);
      if((b & BTN_B) && !(last & BTN_B)){ hw_tone(880); beep_end = now + 100u; }
      last = b;
    }
    if(beep_end && (int)(now - beep_end) >= 0){ hw_tone(0); beep_end = 0; }
    int steps; __asm__ volatile("cpsid i"); steps = g_enc_steps; __asm__ volatile("cpsie i");
    if(steps != last_steps){
      /* the bar: 64 pixels at rest, one pixel per step, clamped */
      int w = 64 + steps; if(w < 0) w = 0; if(w > 128) w = 128;
      fill_rect(0, 80, 128, 12, 0x2104u);
      fill_rect(0, 80, (u32)w, 12, steps >= 0 ? 0x07FFu : 0xF81Fu);
      last_steps = steps;
    }
    if(now / 500u != blink){ blink = now / 500u; fill_rect(60, 110, 8, 8, (blink & 1u) ? 0xFFFFu : 0x0000u); }
    if(b & BTN_WHEEL){ if(!wheel_t0) wheel_t0 = now | 1u; else if((int)(now - wheel_t0) > 2000) leave(); }
    else wheel_t0 = 0;
  }
}

__attribute__((naked)) static void Fault_Handler(void){
  /* a fault: a red screen, the dial held 2 s goes back */
  __asm__ volatile("ldr r0, =0x1801F000\n mov sp, r0\n b fault_c\n .ltorg\n");
}
__attribute__((used)) void fault_c(void){
  hw_tone(0);
  for(u32 y = 0; y < 128; y++){ for(u32 x = 0; x < 128; x++) g_row[x] = 0xF800u; hw_px_begin(0, y, 128, y + 1); hw_px_row(g_row, 128); hw_px_end(); }
  for(u32 t = 0;; t++){ hw_wdt_feed(); if(hw_buttons() & BTN_WHEEL){ if(t > 2000000u) break; } else t = 0; }
  hw_back_to_paradise();
}
__attribute__((naked)) static void Irq_Off(void){ __asm__ volatile("bx lr\n"); }
__attribute__((section(".vectors"), used))
const void* const g_vectors[16 + 64] = { (void*)0x18020000u, (void*)Reset_Handler,
  [2 ... 14] = (void*)Fault_Handler, [11] = (void*)Irq_Off, [14] = (void*)Irq_Off,
  [15] = (void*)SysTick_Handler, [16 ... 79] = (void*)Irq_Off };
