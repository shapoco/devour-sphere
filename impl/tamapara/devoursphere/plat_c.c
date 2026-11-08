/* The C side of the Paradise front end: what has to run from PRAM and what
 * comes from the Paramix headers (the Paradise's clock, the way back), plus
 * the fault screen. GPL-3.0-or-later (this directory, see ../hw/NOTICE.md). */
#include "hw.h"
#include "flash.h"

#define PRAM_TEXT __attribute__((section(".pram_text.plat"), noinline))
#define PARA_ATTR PRAM_TEXT
#define PB_ATTR PRAM_TEXT
#define PB_MARK_ATTR PRAM_TEXT
#include "paraclock.h"
#include "paraback.h"

/* Before the SRAM is touched: note the Paradise's clock and the seconds
 * counter in the mailbox, so the time spent here can be given back */
PRAM_TEXT void ds_para_mark(void) {
  const u32 cnt = REG(0x45000304u);
  u32 t = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!t) t = para_mailbox(cnt);
  para_leave_mark(cnt, t);
}

/* On the way back: move the Paradise's "lived up to" date forward by the time
 * spent here (both copies of its settings page, new CRC). buf: 4 KB. */
PRAM_TEXT int ds_para_back_fix(u8 *buf) { return para_back_fix(buf); }

/* The high score sector: read the record, or erase and program the sector
 * from a 4 KB buffer the caller holds. Nothing in the flash window may run
 * while the flash is written, so this stays in PRAM with the interrupts off. */
PRAM_TEXT void ds_flash_read(u32 off, u8 *dst, u32 n) {
  const volatile u8 *s = (const volatile u8 *)(0x60000000u + (off & 0x00FFFFFFu));
  for (u32 i = 0; i < n; i++) dst[i] = s[i];
}
PRAM_TEXT void ds_flash_write_sector(u32 off, const u8 *buf4k) {
  __asm__ volatile("cpsid i" ::: "memory");
  flash_prog4k(off, buf4k);
  __asm__ volatile("cpsie i" ::: "memory");
}

/* UART1 at 460800 baud (the dongle's contacts), for the benchmark's lines.
 * The init and the output are the flash app's (Paramix flash/src/fw.c). */
#define U_DATA REG(UART + 0x00)
#define U_IER REG(UART + 0x04)
#define U_FCR REG(UART + 0x08)
#define U_LCR REG(UART + 0x0C)
#define U_LSR REG(UART + 0x14)
#define U_FDR REG(UART + 0x28)
#define U_CTRL REG(UART + 0x30)
void ds_uart_init(void) {
  U_LCR = 0x83; U_DATA = 0x1A; U_IER = 0x00; U_LCR = 0x03; U_FDR = 0x100; U_FCR = 0x07; U_CTRL = 0xC1;
}
void ds_uart_puts(const char *s) {
  for (; *s; s++) {
    for (u32 g = 0; !(U_LSR & 0x20u) && g < 200000u; g++) {}
    U_DATA = (u8)*s;
  }
}

/* A fault: the buzzer off, a red screen, the dial held 2 s goes back to the
 * Paradise. The simulator catches this by name and prints the frame. */
__attribute__((used, section(".pram_text.fault"))) void ds_fault_c(u32 *fr) {
  (void)fr;
  hw_tone(0);
  hw_px_dma_wait();  /* a band may still be going out */
  static u16 row[128];
  for (u32 x = 0; x < 128; x++) row[x] = 0xF800u;
  for (u32 y = 0; y < 128; y++) { hw_px_begin(0, y, 128, y + 1); hw_px_row(row, 128); hw_px_end(); }
  for (u32 t = 0;; t++) {
    hw_wdt_feed();
    if (hw_buttons() & BTN_WHEEL) { if (t > 2000000u) break; } else t = 0;
  }
  REG(0x20000FFCu) = 0x4C584D50u;   /* "PMXL": the launcher's autostart stays off */
  hw_back_to_paradise();
}

/* newlib's heap (snprintf in the HUD wants one): a small static arena */
static u8 g_heap[2048];
static u32 g_heap_used;
void *_sbrk(int n) {
  if (n < 0 || g_heap_used + (u32)n > sizeof g_heap) return (void *)-1;
  void *p = g_heap + g_heap_used;
  g_heap_used += (u32)n;
  return p;
}

/* The vector table: 80 words, so the launcher's block follows at 0x140 */
extern void Reset_Handler(void);
extern void Fault_Handler(void);
extern void Exc_Off(void);
extern void SysTick_Handler(void);
__attribute__((section(".vectors"), used))
const void* const g_vectors[16 + 64] = { (void*)0x18020000u, (void*)Reset_Handler,
  [2 ... 14] = (void*)Fault_Handler, [11] = (void*)Exc_Off, [14] = (void*)Exc_Off,
  [15] = (void*)SysTick_Handler, [16 ... 79] = (void*)Exc_Off };
