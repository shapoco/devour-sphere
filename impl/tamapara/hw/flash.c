/* Write the Paradise flash: erase and program a sector, exactly like the
 * mameise flash app (confirmed on the device). Runs from PRAM; nothing may be
 * read from the XIP window while writing. */
#include "io.h"
#include "flash.h"
#define F_DMEM REG(FCTL+0x100)
#define F_DLEN REG(FCTL+0x104)
#define F_DCTL REG(FCTL+0x108)
#define F_DFLA REG(FCTL+0x10C)

static void feed(void){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; }
static void wait_loop(u32 n){ for(volatile u32 i=0;i<n;i++){ if((i & 0xFFFFu)==0) feed(); } }

/* Read the flash status register, exactly like the Paradise firmware (PRAM 0x5808
 * with -1): set read command bit 14, wait until it clears, byte from 0x14. */
static u32 flash_status(void)
{
  REG(FCTL+0x004) |= 0x4000u;
  for(u32 g=0; (REG(FCTL+0x004)&0x4000u) && g<1000000u; g++){}
  return REG(FCTL+0x014) & 0xFFu;
}
/* Wait until the flash is ready (WIP = bit 0), like the firmware after
 * erasing (0x38B0) and programming (0x3D28). Same upper limit as before. */
static void wait_ready(void)
{
  wait_loop(2000u);
  for(u32 i=0;i<400000u;i++){
    if(!(flash_status() & 1u)) return;
    if((i & 0xFFu)==0) feed();
  }
}

void flash_prog4k(u32 addr, const void *src)
{
  u32 a1=addr>>1;
  u32 hi=(a1>>16)&0xFFFFu;
  u32 lo=a1 & 0xFFFFu;
  /* erase */
  REG(FCTL+0x008)=hi;
  REG(FCTL+0x00C)=lo;
  REG(FCTL+0x004)=0x00000400u;
  for(u32 g=0; (REG(FCTL+0x004)&0x400u) && g<60000000u; g++){ if((g & 0xFFFFu)==0) feed(); }
  for(u32 g=0; (REG(FCTL+0x004)&0x2u) && g<60000000u; g++){ if((g & 0xFFFFu)==0) feed(); }
  wait_ready();
  /* program */
  REG(FCTL+0x008)=0x3000u | hi;
  REG(FCTL+0x00C)=lo;
  REG(FCTL+0x004)=0x00000400u;
  REG(FCTL+0x004)=0x00004000u;
  REG(FCTL+0x000)=0x0000d080u;
  REG(FCTL+0x020)=0;
  REG(FCTL+0x010)=0;
  REG(FCTL+0x000)=0x0000d082u;
  F_DFLA=0x60000000u | (addr & 0x00FFFFFFu);
  F_DLEN=0x1000u; F_DMEM=(u32)src;
  F_DCTL=0u;
  F_DCTL=1u;
  for(u32 g=0; (F_DCTL & 1u) && g<20000000u; g++){ if((g & 0xFFFFu)==0) feed(); }
  wait_ready();
}

void flash_read(u32 addr, void *dst, u32 len)
{
  const volatile u8 *s=(const volatile u8*)(0x60000000u + (addr & 0x00FFFFFFu));
  u8 *d=(u8*)dst;
  for(u32 i=0;i<len;i++) d[i]=s[i];
}

int flash_same(u32 addr, const void *src, u32 len)
{
  const volatile u8 *s=(const volatile u8*)(0x60000000u + (addr & 0x00FFFFFFu));
  const u8 *p=(const u8*)src;
  for(u32 i=0;i<len;i++) if(s[i]!=p[i]) return 0;
  return 1;
}

/* Write the sector only if it differs. Returns 1 if it was written. */
int flash_update4k(u32 addr, const void *src)
{
  if(flash_same(addr, src, 0x1000u)) return 0;
  flash_prog4k(addr, src);
  return 1;
}

/* DOOM: erase a sector and program small pieces (without erasing), split like in flash_prog4k */
void flash_erase4k(u32 addr)
{
  u32 a1=addr>>1, hi=(a1>>16)&0xFFFFu, lo=a1 & 0xFFFFu;
  REG(FCTL+0x008)=hi;
  REG(FCTL+0x00C)=lo;
  REG(FCTL+0x004)=0x00000400u;
  for(u32 g=0; (REG(FCTL+0x004)&0x400u) && g<60000000u; g++){ if((g & 0xFFFFu)==0) feed(); }
  for(u32 g=0; (REG(FCTL+0x004)&0x2u) && g<60000000u; g++){ if((g & 0xFFFFu)==0) feed(); }
  wait_ready();
}
void flash_prog(u32 addr, const void *src, u32 len)
{
  u32 a1=addr>>1, hi=(a1>>16)&0xFFFFu, lo=a1 & 0xFFFFu;
  REG(FCTL+0x008)=0x3000u | hi;
  REG(FCTL+0x00C)=lo;
  REG(FCTL+0x004)=0x00000400u;
  REG(FCTL+0x004)=0x00004000u;
  REG(FCTL+0x000)=0x0000d080u;
  REG(FCTL+0x020)=0;
  REG(FCTL+0x010)=0;
  REG(FCTL+0x000)=0x0000d082u;
  F_DFLA=0x60000000u | (addr & 0x00FFFFFFu);
  F_DLEN=len; F_DMEM=(u32)src;
  F_DCTL=0u;
  F_DCTL=1u;
  for(u32 g=0; (F_DCTL & 1u) && g<20000000u; g++){ if((g & 0xFFFFu)==0) feed(); }
  wait_ready();
}
