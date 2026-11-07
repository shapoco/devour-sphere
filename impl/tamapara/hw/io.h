#ifndef IO_H
#define IO_H
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#define REG(a) (*(volatile u32*)(a))
#define UART  0x4000B000u
#define FCTL  0x40022000u
#define CLK   0x45000000u
static inline void dsb(void){ __asm__ volatile("dsb":::"memory"); }

#ifndef TC_INIT
#define TC_INIT __attribute__((section(".initcode"), noinline))
#endif
#endif
