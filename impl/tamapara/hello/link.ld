/* A PRAM-only program for the Paradise: one 64 KB image, loaded to 0 by the
 * launcher. The vectors first (80 words), the settings block at 0x140, then
 * the code. Read-write data goes to the SRAM (writing PRAM is unreliable on
 * the device, Paramix DOOM v0.1.4). */
ENTRY(Reset_Handler)
MEMORY {
  PRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  SRAM (rw)  : ORIGIN = 0x18000000, LENGTH = 0x1F000
}
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    KEEP(*(.pmxcfg))
    *(.text*) *(.rodata*) *(.initcode*) *(.initdata*) *(.pram_text*) *(.pram_rodata*)
    . = ALIGN(4);
  } > PRAM
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > SRAM AT > PRAM
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > SRAM
  /DISCARD/ : { *(.init) *(.fini) *(.init_array*) *(.fini_array*) *(.eh_frame) *(.comment) *(.ARM.attributes) *(.ARM.exidx*) }
}
