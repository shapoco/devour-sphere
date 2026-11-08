/* Does the panel's SPI0 work with DMA in this program's context, and how long
 * does a full screen take? The registers follow the stock firmware's transfer
 * (PRAM 0x3FC8, completion 0x3A84), polled instead of IRQ 58.
 *
 *   A          next method: CPU, DMA 8 BIT, DMA 16 BIT
 *   dial held  2 s: back to the Paradise
 *
 * The screen names the method before it touches the DMA, so a hang shows which
 * one hung. The time is for all 128x128 pixels. Below the text the picture must
 * be red, green, blue from top to bottom, each dark on the left and bright on
 * the right; the words under the time say so. GPL-3.0-or-later (see ../hw/NOTICE.md). */
#include "hw.h"

/* The launcher expects this block at 0x140 of the image (after the 80 vectors) */
typedef struct { u32 magic, version, guest_off, guest_size, save_off, device; u8 map[128]; } PmxCfg;
__attribute__((section(".pmxcfg"), used)) volatile const PmxCfg g_cfg = { 0x43584D50u, 1, 0, 0, 0, 0, {0} };

#define SPI 0x4000E000u
#define DMA 0x4000F000u
#define CH  0x4000F100u
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define DMA_POLLS 4000000u

#define W 128u
#define H 128u
#define TEXT_H 32u
#define RED 0xF800u
#define GREEN 0x07E0u
#define BLUE 0x001Fu
#define WHITE 0xFFFFu
#define GRAY 0x8410u

enum { M_CPU, M_DMA8, M_DMA16, M_COUNT };
enum { R_OK, R_BUSY, R_TIMEOUT };

static u16 g_pix[W * H] __attribute__((aligned(4)));
static u16 g_row[W];
static const char *volatile g_stage = "START";

static void fill_rect(u32 x0, u32 y0, u32 w, u32 h, u16 c){
  for(u32 i = 0; i < w; i++) g_row[i] = c;
  for(u32 y = y0; y < y0 + h; y++){ hw_px_begin(x0, y, x0 + w, y + 1); hw_px_row(g_row, w); hw_px_end(); }
}

/* 3x5 glyphs, one octal digit per row: 0-9, then A-Z */
static const u16 g_font[36] = {
  075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717,
  025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227, 011156,
  055655, 044447, 057555, 065555, 025552, 065644, 025573, 065655, 034716, 072222,
  055557, 055522, 055575, 055255, 055222, 071247,
};
static void draw_text(u32 x, u32 y, const char *s, u16 c){
  for(; *s; s++, x += 8u){
    u32 g = 0;
    if(*s >= '0' && *s <= '9') g = g_font[*s - '0'];
    else if(*s >= 'A' && *s <= 'Z') g = g_font[*s - 'A' + 10];
    for(u32 r = 0; r < 5u; r++)
      for(u32 k = 0; k < 3u; k++)
        fill_rect(x + 2u * k, y + 2u * r, 2u, 2u, ((g >> (14u - (3u * r + k))) & 1u) ? c : 0u);
  }
}
static void draw_dec(u32 x, u32 y, u32 v, u16 c){
  char s[11]; u32 i = 10; s[10] = 0;
  do { s[--i] = (char)('0' + v % 10u); v /= 10u; } while(v);
  draw_text(x, y, s + i, c);
}
static void draw_hex(u32 x, u32 y, u32 v, u16 c){
  char s[9];
  for(u32 i = 0; i < 8u; i++){ u32 d = (v >> (28u - 4u * i)) & 15u; s[i] = (char)(d < 10u ? '0' + d : 'A' + d - 10u); }
  s[8] = 0;
  draw_text(x, y, s, c);
}

/* The picture in the method's byte order. In 8 bit frames the bytes go out in memory order (high
 * byte first in memory, like the game's band); in 16 bit frames each halfword goes out high byte first. */
static void make_picture(int high_byte_first){
  for(u32 y = 0; y < H; y++){
    for(u32 x = 0; x < W; x++){
      u32 v = x >> 2;   /* 0..31 */
      u16 c = 0;
      if(y >= 96u) c = (u16)v;
      else if(y >= 64u) c = (u16)((v << 1) << 5);
      else if(y >= TEXT_H) c = (u16)(v << 11);
      g_pix[y * W + x] = high_byte_first ? (u16)((c >> 8) | (c << 8)) : c;
    }
  }
}

static void spi_frame_bits(u32 bits){
  REG(SPI + 0x00) = (REG(SPI + 0x00) & ~0xF00u) | ((bits - 1u) << 8);
}

/* As after the panel's setup in hw.c, for the CPU path */
static void spi_restore_8bit(void){
  spi_frame_bits(8u);
  REG(SPI + 0x50) = 0;
  REG(SPI + 0x24) |= 0x80000000u; REG(SPI + 0x24) &= ~0xFF0000u;
  REG(SPI + 0x24) |= 0x80000000u; REG(SPI + 0x24) &= ~0xFFu;
}

static int dma_start(const void *buf, u32 frames, u32 bits){
  if(REG(DMA + 0x1C) & 1u) return R_BUSY;
  REG(DMA + 0x08) |= 1u;
  REG(DMA + 0x10) |= 1u; REG(DMA + 0x10) |= 0x10000u;
  REG(DMA + 0x28) |= 1u; REG(DMA + 0x24) |= 1u;

  REG(SPI + 0x24) &= ~0xFF0000u;
  REG(SPI + 0x24) = (REG(SPI + 0x24) & ~0xFFu) | (bits == 16u ? 7u : 15u);
  REG(SPI + 0x50) = 0; REG(SPI + 0x50) |= 0x80000000u;
  REG(SPI + 0x50) = (REG(SPI + 0x50) & ~0x3FFFFFu) | frames;

  REG(CH + 0x04) = 0;
  REG(CH + 0x04) |= 4u; REG(CH + 0x04) |= 2u;
  REG(CH + 0x04) |= 0x2000u; REG(CH + 0x04) |= 0x80u;
  REG(CH + 0x10) = 0;
  REG(CH + 0x14) = (REG(CH + 0x14) & ~0x3FFFFFu) | frames;
  REG(CH + 0x0C) = SPI + 0x1Cu;
  REG(CH + 0x08) = (u32)buf;

  REG(CH + 0x00) = 0;
  REG(CH + 0x00) |= 0x80u; REG(CH + 0x00) |= 2u;
  REG(CH + 0x00) = (REG(CH + 0x00) & ~(3u << 3)) | (2u << 3);
  if(bits == 16u) REG(CH + 0x00) = (REG(CH + 0x00) & ~0x3F00u) | (1u << 11) | (1u << 8);
  dsb();
  REG(CH + 0x00) |= 1u;
  return R_OK;
}

static int dma_wait(void){
  for(u32 g = 0; g < DMA_POLLS; g++){
    if(REG(DMA + 0x00) & 1u){
      REG(CH + 0x00) &= ~1u;
      REG(DMA + 0x08) |= 1u;
      REG(DMA + 0x10) |= 1u; REG(DMA + 0x10) |= 0x10000u;
      for(u32 s = 0; (REG(SPI + 0x0C) & 0x10u) && s < DMA_POLLS; s++){}
      REG(SPI + 0x00) |= 0xC0u;
      return R_OK;
    }
    if((g & 0xFFFFu) == 0) hw_wdt_feed();
  }
  REG(CH + 0x00) &= ~1u;   /* give up: the channel off, or the next try reads busy */
  return R_TIMEOUT;
}

static int g_status[3];   /* what the registers read after a failed transfer */

static int send_full_screen(int method, u32 *us){
  int r = R_OK;
  hw_px_begin(0, 0, W, H);
  SYST_RVR = 0x00FFFFFFu; SYST_CVR = 0; SYST_CSR = 5u;   /* the core clock, no interrupt */
  u32 t0 = SYST_CVR;
  if(method == M_CPU){
    hw_px_raw((const u8 *)g_pix, sizeof g_pix);
  }else{
    u32 bits = method == M_DMA16 ? 16u : 8u;
    if(bits == 16u) spi_frame_bits(16u);
    r = dma_start(g_pix, bits == 16u ? W * H : 2u * W * H, bits);
    if(r == R_OK) r = dma_wait();
    g_status[0] = (int)REG(DMA + 0x00); g_status[1] = (int)REG(CH + 0x00); g_status[2] = (int)REG(SPI + 0x0C);
    spi_restore_8bit();
  }
  u32 t1 = SYST_CVR;
  hw_px_end();
  *us = ((t0 - t1) & 0x00FFFFFFu) / 96u;
  return r;
}

static const char *const k_names[M_COUNT] = { "CPU", "DMA 8 BIT", "DMA 16 BIT" };

static void run_method(int m){
  u32 us = 0;
  fill_rect(0, 0, W, H, 0);
  g_stage = k_names[m];
  draw_text(0, 0, k_names[m], WHITE);
  make_picture(m != M_DMA16);
  int r = send_full_screen(m, &us);
  fill_rect(0, 0, W, TEXT_H, 0);
  draw_text(0, 0, k_names[m], WHITE);
  if(r == R_OK){
    draw_dec(0, 11, us, GREEN); draw_text(56, 11, "US", GREEN);
    draw_text(0, 22, "SEE RGB BELOW", GRAY);
  }else{
    draw_text(0, 11, r == R_BUSY ? "BUSY" : "TIMEOUT", RED);
    draw_text(0, 22, "DMA", GRAY); draw_hex(32, 22, (u32)g_status[0], WHITE);
    fill_rect(0, TEXT_H, W, H - TEXT_H, 0);
    draw_text(0, 40, "CH", GRAY); draw_hex(24, 40, (u32)g_status[1], WHITE);
    draw_text(0, 52, "SPI", GRAY); draw_hex(32, 52, (u32)g_status[2], WHITE);
  }
}

static void leave(void){
  REG(0x20000FFCu) = 0x4C584D50u;   /* "PMXL": back on purpose */
  SYST_CSR = 0;
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
  for(u32 i = 0; i < 4; i++){ REG(0xE000E180u + 4u * i) = 0xFFFFFFFFu; REG(0xE000E280u + 4u * i) = 0xFFFFFFFFu; }

  /* the start keys first, or A would skip the first method */
  for(u32 t = 0; (hw_buttons() & (BTN_A | BTN_C)) && t < 20000000u; t++) if((t & 0xFFFFu) == 0) hw_wdt_feed();

  int method = M_CPU;
  run_method(method);
  u32 last = hw_buttons(), held = 0;
  for(;;){
    hw_wdt_feed();
    u32 b = hw_buttons();
    if((b & BTN_A) && !(last & BTN_A)){ method = (method + 1) % M_COUNT; run_method(method); b = hw_buttons(); }
    last = b;
    if(b & BTN_WHEEL){ if(++held > 2000000u) leave(); } else held = 0;
  }
}

__attribute__((naked)) static void Fault_Handler(void){
  __asm__ volatile("ldr r0, =0x1801F000\n mov sp, r0\n b fault_c\n .ltorg\n");
}
__attribute__((used)) void fault_c(void){
  u32 cfsr = REG(0xE000ED28u), bfar = REG(0xE000ED38u);
  fill_rect(0, 0, W, H, RED);
  draw_text(0, 0, "FAULT IN", WHITE);
  draw_text(0, 11, g_stage, WHITE);
  draw_hex(0, 22, cfsr, WHITE);
  draw_hex(0, 33, bfar, WHITE);
  for(u32 t = 0;; t++){ hw_wdt_feed(); if(hw_buttons() & BTN_WHEEL){ if(t > 2000000u) break; } else t = 0; }
  leave();
}
__attribute__((naked)) static void Irq_Off(void){ __asm__ volatile("bx lr\n"); }
__attribute__((section(".vectors"), used))
const void* const g_vectors[16 + 64] = { (void*)0x18020000u, (void*)Reset_Handler,
  [2 ... 14] = (void*)Fault_Handler, [11] = (void*)Irq_Off, [14] = (void*)Irq_Off,
  [15] = (void*)Irq_Off, [16 ... 79] = (void*)Irq_Off };
