/* Paradise hardware (SNC73410), taken over from the mameise flash app
 * (display confirmed on the device). Runs from PRAM 0 after SYSRESETREQ. */
#include "io.h"
#include "hw.h"
#include "disp_power.h"
#define PANEL 0x4000E000u
#define FCTL_ 0x40022000u
#define F_STAT REG(FCTL_+0x000)
static void udelay(volatile u32 n){ while(n--) __asm__ volatile("nop"); }
#define MS(x) udelay((u32)(x)*16000u)
#define BB(reg,bit) (*(volatile u32*)(0x42000000u + (((u32)(reg)-0x40000000u)<<5) + ((u32)(bit)<<2)))
#define GPIO0 0x40018000u
#define GPIO1 0x40019000u
#define GPIO2 0x4001A000u
#define LCD_RST(v) (BB(GPIO0+0x00,4)=(v))
#define LCD_DC(v)  (BB(GPIO0+0x00,5)=(v))
#define SPI_CTRL REG(PANEL+0x00)
#define SPI_STAT REG(PANEL+0x0C)
#define SPI_DATA REG(PANEL+0x1C)
#define SPIWAIT 200000u
#define CT2 0x40002000u
TC_INIT static void clock_init(void){
  REG(CLK+0x00) |= 1u;
  REG(CLK+0x0C) = 0;
  REG(CLK+0x00) = 0x41u;
  { u32 r = (REG(CLK+0x34) & 0xF00u) | 0xA55A0000u; REG(CLK+0x04) = r; }
  REG(CLK+0x18) = 0x21u;
  REG(CLK+0x50) = 0;
  REG(CLK+0x10C) = 0x001FFFFDu;
  REG(CLK+0x110) = 0;
  REG(CLK+0x108) = 0;
  REG(CLK+0x10)  = 0;
  REG(CLK+0x00) |= 0x10u;
  for(u32 g=0; !((REG(CLK+0x08) >> 4) & 1u) && g<4000000u; g++){}
  REG(CLK+0x00) |= 0x4000u;
  REG(CLK+0x04) &= ~1u;
  REG(CLK+0x00) |= 0x8000u;
  for(u32 g=0; !((REG(CLK+0x08) >> 6) & 1u) && g<4000000u; g++){}
  { u32 r = (REG(CLK+0x0C) & ~7u) | 4u; REG(CLK+0x0C) = r; }
  for(u32 g=0; ((REG(CLK+0x0C) >> 4) & 7u) != (REG(CLK+0x0C) & 7u) && g<4000000u; g++){}  /* bounded: never hang forever */
  REG(CLK+0x100) = 0x00157EB7u;
}
TC_INIT static void flashctl_init(void){
  /* 1.4.2: enable XIP. We read through the 0x60000000 window that the XIP block
   * maps in. Without XIP that window only shows the config register (0x40), not
   * the flash data. The original flash utility does this at startup, our app did
   * not do it after reset until now. That was exactly what was missing. */
  REG(0x4002F004u)=0x60011000u;
  REG(0x4002F000u)=0x00000003u;
  /* 1.4.1: controller init exactly like the original flash utility (0x40022000).
   * This includes setting the read command/mode in 0x008 and 0x00c (0x3077 / 0xf000),
   * otherwise the chip only returns its config register (0x40) instead of the flash data. */
  F_STAT=0x0000d018u;
  F_STAT=0x0000d000u;
  F_STAT=0x0000d080u;
  F_STAT=0x0000d088u;
  REG(FCTL+0x008)=0x00003077u;   /* read command/mode */
  REG(FCTL+0x00C)=0x0000f000u;
  REG(FCTL+0x004)=0x00000400u;
  REG(FCTL+0x004)=0x00004000u;
}
TC_INIT static void wdt_off(void){
  /* Set up the system/power block EXACTLY like the original flash utility. The
     only place where we deliberately differed before (bit0 of 0x40008000) might
     be the master enable for the display domain. The FU runs with bit0=1 without
     a reset, so we copy it 1:1 and feed the watchdog in the loop. */
  REG(0x4000900Cu)=0x5AFA55AAu; REG(0x40009000u)=0x5AFA0000u;   /* WDT1 */
  REG(0x40008000u)=0x5AFA0000u;                                  /* unlock */
  REG(0x40008004u)=0x5AFA0002u;
  REG(0x40008008u)=0x5AFA00FFu;   /* power/clock domains on (incl. display) */
  REG(0x4000800Cu)=0x5AFA55AAu;   /* feed the watchdog */
  /* Do NOT set bit0 (watchdog enable): otherwise it reset the Tama during
     display init (back to the planet). */
}
TC_INIT static void mailbox_clear(void){
  REG(0x20000000u)=0; REG(0x20000004u)=0; REG(0x20000008u)=0; REG(0x2000000Cu)=0;
}
TC_INIT static void periph_enable(void){ REG(CLK+0x20)=0x40u; }
TC_INIT static void pin_set(u8 id, u8 mode){ *(volatile u8*)(0xE000E400u + id) = (u8)(mode<<4); }
TC_INIT static void pin_mux(void){
  static const u8 m12[]={6,7,8,9,0xf,0x10,0x11,0x12,0x13,0x14,0x19,0x1f,0x20,0x22,
    0x24,0x25,0x2e,0x2f,0x35,0x36,0x1a,0x1b,0x1c,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x3a,0x3f};
  static const u8 m8[]={0,1,2,3};
  static const u8 m4[]={0x15,0x17};
  for(unsigned i=0;i<sizeof m12;i++) pin_set(m12[i],0xC);
  for(unsigned i=0;i<sizeof m8;i++) pin_set(m8[i],0x8);
  for(unsigned i=0;i<sizeof m4;i++) pin_set(m4[i],0x4);
}
TC_INIT static void display_power(void){
  /* Sequence from the original FU (clock enable, pin setup, timer CT32B3, GPIO1).
     As in the FU, 0x45000210..218 get the current contents of the GPIO data
     registers (the emulator had returned 0 there). */
  for(unsigned i=0;i<DISP_POWER_N;i++){
    u32 a=disp_power_seq[i][0], v=disp_power_seq[i][1];
    if(a>=0x45000210u && a<=0x45000218u) v=REG(0x4001803Cu + ((a-0x45000210u)>>2)*0x1000u);
    REG(a)=v;
  }
  /* Make sure the panel pins are outputs: reset, D/C, CS (P1.5 low), power (P1.9 low) */
  LCD_RST(1); BB(GPIO0+0x04,4)=1;
  LCD_DC(0);  BB(GPIO0+0x04,5)=1;
  BB(GPIO1+0x00,5)=0; BB(GPIO1+0x04,5)=1;
  BB(GPIO1+0x00,9)=0; BB(GPIO1+0x04,9)=1;
  MS(20);
}
TC_INIT static void spi_init(void){
  /* like FU 0x3f44, as read-modify-write */
#ifdef LCD_FASTCLK
  REG(PANEL+0x08)=0;                                   /* clock divider: test v0.8.18, twice as fast? */
#else
  REG(PANEL+0x08)=1;                                   /* clock divider */
#endif
  SPI_CTRL &= ~(1u<<18);
  REG(PANEL+0x04) &= ~6u; REG(PANEL+0x04) &= ~1u;
  SPI_CTRL &= ~0x10u; SPI_CTRL &= ~0x08u;
  REG(PANEL+0x20)=0;
  SPI_CTRL = (SPI_CTRL & ~0xF00u) | 0x700u;            /* 8 bit */
  REG(PANEL+0x24) |= 0x80000000u;
  REG(PANEL+0x24) &= ~0xFF0000u;
  REG(PANEL+0x24) |= 0x80000000u;
  REG(PANEL+0x24) &= ~0xFFu;
  SPI_CTRL |= 1u;                                      /* SPI on */
  SPI_CTRL |= 0xC0u;                                   /* clear FIFOs */
}
static void spi_tx(const u8* p, u32 n){
  for(u32 i=0;i<n;i++){
    for(u32 g=0; (SPI_STAT & 2u) && g<SPIWAIT; g++){}
    SPI_DATA=p[i];
  }
  udelay(8);
  for(u32 g=0; (SPI_STAT & 0x10u) && g<SPIWAIT; g++){}
  SPI_CTRL |= 0xC0u;
}
static void lcd_cmd(u8 c, const u8* par, u32 n){
  LCD_DC(0); spi_tx(&c,1);
  if(n){ LCD_DC(1); spi_tx(par,n); LCD_DC(0); }
}
static const u8 p36[]={0x08}, p3a[]={0x05}, p26[]={0x01},
  pb1[]={0x08,0x3b,0x3b}, pb2[]={0x08,0x3b,0x3b}, pb3[]={0x08,0x3b,0x3b,0x08,0x3b,0x3b},
  pb4[]={0x03}, pc0[]={0xe0,0x00,0x84}, pc1[]={0xc4}, pc2[]={0x0d,0x00}, pc3[]={0x8d,0x2a},
  pc4[]={0x8d,0xee}, pc5[]={0x06},
  pe0[]={0x0d,0x17,0x16,0x28,0x3f,0x39,0x30,0x31,0x2c,0x28,0x2f,0x39,0x00,0x04,0x01,0x10},
  pe1[]={0x0a,0x15,0x18,0x25,0x3f,0x37,0x2e,0x30,0x2f,0x2c,0x33,0x3f,0x00,0x03,0x04,0x10},
  p35[]={0x00}, pcas0[]={0x00,0x02,0x00,0x83}, pras0[]={0x00,0x01,0x00,0x81},
  pcas[]={0x00,0x02,0x00,0x81}, pras[]={0x00,0x01,0x00,0x80};
TC_INIT static void display_init(void){
  spi_init();
  LCD_RST(1); MS(10); LCD_RST(0); MS(20); LCD_RST(1); MS(150);   /* panel reset */
  lcd_cmd(0x11,0,0); MS(150);                                    /* SLPOUT */
  lcd_cmd(0x36,p36,1); lcd_cmd(0x3a,p3a,1); lcd_cmd(0x26,p26,1); lcd_cmd(0x20,0,0);
  lcd_cmd(0xb1,pb1,3); lcd_cmd(0xb2,pb2,3); lcd_cmd(0xb3,pb3,6); lcd_cmd(0xb4,pb4,1);
  lcd_cmd(0xc0,pc0,3); lcd_cmd(0xc1,pc1,1); lcd_cmd(0xc2,pc2,2); lcd_cmd(0xc3,pc3,2);
  lcd_cmd(0xc4,pc4,2); lcd_cmd(0xc5,pc5,1);
  lcd_cmd(0xe0,pe0,16); lcd_cmd(0xe1,pe1,16);
  lcd_cmd(0x35,p35,1);
  lcd_cmd(0x29,0,0); MS(20);                                     /* DISPON */
  lcd_cmd(0x2a,pcas0,4); lcd_cmd(0x2b,pras0,4);
  lcd_cmd(0x11,0,0); MS(150);
}
static void pwm2(u32 mr1, int first){
  if(first) REG(CT2+0x34)=0;
  REG(CT2+0x00)=0;
  REG(CT2+0x18)=0; REG(CT2+0x1C)=0; REG(CT2+0x20)=0; REG(CT2+0x24)=0;
  REG(CT2+0x14)=0;
  REG(CT2+0x00)=2; REG(CT2+0x08)=0; REG(CT2+0x00)=0;
  REG(CT2+0x14)=0x400u;
  REG(CT2+0x24)=0x17700u;
  REG(CT2+0x1C)=mr1;
  REG(CT2+0x14)=0;
  REG(CT2+0x34)=0x00200002u;
  REG(CT2+0x00)=2;
  REG(CT2+0x3C)=0x1Fu;
  REG(CT2+0x00)=1;
}
TC_INIT static void backlight_on(void){
  REG(0x40007034u)=0;
  pwm2(0x17700u,1);
  REG(0x40009000u)=0x5AFA0000u; REG(0x40008000u)=0x5AFA0000u;     /* like FU */
  REG(0x40018034u)=0x15555155u; REG(0x40019034u)=0x55540140u;
  pwm2(0,0);
  REG(CLK+0x20)=0x42u;
}

/* ---------------- our own additions ---------------- */
/* Buzzer on timer 7, started and stopped exactly like the firmware does:
 * period in 72 MHz clock ticks, short pulse (quiet, like on the Paradise). */
#define CT7 0x40007000u
#ifdef TONE_FIX
/* v0.8.15: the counter runs periodically (MCTRL bit 10 = restart at MR3, like the backlight with 0x400).
 * Before, MCTRL ended up at 0: only a single pulse per call, so a click instead of a tone.
 * If only the pitch changes, only MR0/MR3 are updated (no restart, no click). */
void hw_tone(u32 hz){
  static u32 on;
  if(!hz){ REG(CT7+0x34)=0; REG(CT7+0x00)=0; on=0; return; }
  u32 per = 72000000u / hz, mr0 = per - per/32u;
  if(on){ REG(CT7+0x24)=per; REG(CT7+0x18)=mr0;
    /* TONE_SAFE (Paramix 1.6.9, GBEmu): a shorter period below the running counter left the buzzer
     * silent until the 32-bit counter wrapped; the counter starts again */
    if(REG(CT7+0x08) >= per){ REG(CT7+0x00)=2; REG(CT7+0x08)=0; REG(CT7+0x00)=1; }
    return; }
  REG(CT7+0x34)=0;
  REG(CT7+0x00)=2; REG(CT7+0x08)=0; REG(CT7+0x00)=0;
  REG(CT7+0x18)=mr0; REG(CT7+0x1C)=0; REG(CT7+0x20)=0;
  REG(CT7+0x24)=per;
  REG(CT7+0x14)=0x400u;
  REG(CT7+0x34)=0x00100001u;
  REG(CT7+0x3C)=0x1Fu;
  REG(CT7+0x00)=1;
  on=1;
}
#else
void hw_tone(u32 hz){
  if(!hz){ REG(CT7+0x34)=0; return; }
  u32 per = 72000000u / hz, mr0 = per - per/32u;
  REG(CT7+0x34)=0;
  REG(CT7+0x00)=0;
  REG(CT7+0x18)=0; REG(CT7+0x1C)=0; REG(CT7+0x20)=0; REG(CT7+0x24)=0;
  REG(CT7+0x14)=0;
  REG(CT7+0x00)=2; REG(CT7+0x08)=0; REG(CT7+0x00)=0;
  REG(CT7+0x14)=0x600u;
  REG(CT7+0x24)=per;
  REG(CT7+0x18)=mr0;
  REG(CT7+0x14)=0;
  REG(CT7+0x34)=0x00100001u;
  REG(CT7+0x00)=2;
  REG(CT7+0x3C)=0x1Fu;
  REG(CT7+0x00)=1;
}
#endif
void hw_bl_pwm_off(void){ REG(CT2+0x00)=0; REG(CT2+0x34)=0; }   /* backlight off only (when exiting) */
#ifdef BL_SLEEP
/* v0.8.18: off = put the display to sleep (DISPOFF, SLPIN) and stop the backlight timer,
 * PWM output off (like panel_down in the flash tool). On = SLPOUT, short wait, DISPON, backlight again.
 * MR1 values alone (v0.8.17) did nothing on the device. */
void hw_backlight(int on){
  static int is_on=1;
  if(on){ if(is_on) return; lcd_cmd(0x11,0,0); MS(10); lcd_cmd(0x29,0,0); pwm2(0u,1); is_on=1; }
  else  { if(!is_on) return; REG(CT2+0x00)=0; REG(CT2+0x34)=0; lcd_cmd(0x28,0,0); lcd_cmd(0x10,0,0); is_on=0; }
}
#elif defined(BL_OFFFIX)
/* v0.8.17: duty cycle 0 = full brightness. MR1 equal to the period (0x17700) never matches, so it stayed bright.
 * One tick before the end of the period is practically dark. */
void hw_backlight(int on){ pwm2(on ? 0u : 0x176FFu, 0); }
#else
void hw_backlight(int on){ pwm2(on ? 0u : 0x17700u, 0); }
#endif
void hw_wdt_feed(void){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; }

TC_INIT void hw_init(void){
  wdt_off(); mailbox_clear(); clock_init(); periph_enable(); flashctl_init(); pin_mux();
  display_power(); display_init(); backlight_on();
  /* Buttons: P0.8 wheel, P0.9 A, P0.10 C, P0.11 B as inputs (active low) */
  REG(GPIO0+0x04) &= ~((1u<<8)|(1u<<9)|(1u<<10)|(1u<<11));
  flashctl_init();
}

u32 hw_buttons(void){
  u32 d = REG(GPIO0+0x00);
  u32 b = 0;
  if(!(d & (1u<<9)))  b |= BTN_A;
  if(!(d & (1u<<11))) b |= BTN_B;
  if(!(d & (1u<<10))) b |= BTN_C;
  if(!(d & (1u<<8)))  b |= BTN_WHEEL;
  return b;
}

/* Rectangle x0..x1-1, y0..y1-1 from a 128 wide framebuffer (RGB565) to the panel. */
void hw_push(const u16* fb, u32 stride, u32 x0, u32 y0, u32 x1, u32 y1){
  u8 ca[4]={0x00,(u8)(x0+2),0x00,(u8)(x1+1)};
  u8 ra[4]={0x00,(u8)(y0+1),0x00,(u8)(y1)};
  lcd_cmd(0x2a,ca,4); lcd_cmd(0x2b,ra,4);
  LCD_DC(0); { u8 c=0x2c; spi_tx(&c,1); }
  LCD_DC(1);
  for(u32 y=y0;y<y1;y++){
    const u16* r=fb+y*stride;
    for(u32 x=x0;x<x1;x++){
      u8 two[2]={(u8)(r[x]>>8),(u8)r[x]};
      spi_tx(&two[0],1); spi_tx(&two[1],1);   /* byte by byte like the FU (1.3.8) */
    }
  }
  LCD_DC(0);
}

#ifdef FAST_LCD
/* v0.8.17: pixel output in one go per rectangle, 16 bytes per SPI pass (like the gamma tables
 * of the display setup, which work this way on the device). Before, per byte: wait, pause of 8, wait. */
void hw_px_begin(u32 x0, u32 y0, u32 x1, u32 y1){
  u8 ca[4]={0x00,(u8)(x0+2),0x00,(u8)(x1+1)};
  u8 ra[4]={0x00,(u8)(y0+1),0x00,(u8)(y1)};
  lcd_cmd(0x2a,ca,4); lcd_cmd(0x2b,ra,4);
  LCD_DC(0); { u8 c=0x2c; spi_tx(&c,1); }
  LCD_DC(1);
}
void hw_px_row(const u16* r, u32 n){
  u8 b[16]; u32 k=0;
  for(u32 i=0;i<n;i++){ b[k++]=(u8)(r[i]>>8); b[k++]=(u8)r[i]; if(k==16u){ spi_tx(b,16); k=0; } }
  if(k) spi_tx(b,k);
}
void hw_px_end(void){ LCD_DC(0); }
#endif

/* Back to the Paradise: firmware routine that reloads the encrypted PRAM image
   and restarts (flash 0x110568, XIP 0x100FF569). */
void hw_back_to_paradise(void){
  __asm__ volatile("cpsid i");
  ((void(*)(void))(PARADISE_REBOOT|1u))();
  for(;;){}
}

/* DOOM: pixels as ready-made bytes (palette already in send order), 16 bytes per SPI pass.
   LCD_STREAM (Devour Sphere): the whole run in one pass, polling only the FIFO-full bit per
   byte, so the bus is not drained and refilled every 16 bytes. */
void hw_px_raw(const u8* p, u32 n){
#ifdef LCD_STREAM
  spi_tx(p,n);
#else
  while(n>=16u){ spi_tx(p,16); p+=16; n-=16u; }
  if(n) spi_tx(p,n);
#endif
}
