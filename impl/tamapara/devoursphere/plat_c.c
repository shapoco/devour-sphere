/* The C side of the Paradise front end: UART1, the fault screen, the heap.
 * GPL-3.0-or-later (this directory, see ../hw/NOTICE.md). */
#include "hw.h"

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
