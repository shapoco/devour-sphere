/* Devour Sphere on the Paradise: a 64 KB PRAM image (loaded to 0 by the
 * launcher) and a part that runs in the cached flash window (1 MB at
 * 0x10000000, showing the flash from 0x11000). The SRAM holds the data and
 * the stack. XIP_ORIGIN comes from the build (--defsym): the window address
 * of the flash offset mkrom.js writes the .xip part to. */
ENTRY(Reset_Handler)
MEMORY {
  PRAM  (rwx) : ORIGIN = 0x00000000, LENGTH = 0x10000
  XIP   (rx)  : ORIGIN = XIP_ORIGIN, LENGTH = XIP_LENGTH
  SRAM  (rw)  : ORIGIN = 0x18000000, LENGTH = 0x1C000   /* the top 16 KB are the stack */
}
SECTIONS {
  .pram : {
    KEEP(*(.vectors))          /* 80 words */
    KEEP(*(.pmxcfg))           /* 0x140: the launcher's block */
    KEEP(*(.dscfg))            /* 0x1D8: ours */
    *(.pram_text*) *(.pram_rodata*) *(.initcode*) *(.initdata*)
    *hw.o(.text* .rodata*) *flash.o(.text* .rodata*) *plat_c.o(.text* .rodata*)
    INCLUDE pram_hot.ld
    *libgcc.a:*(.text* .rodata*)
    *libc_nano.a:*memcpy*(.text*) *libc_nano.a:*memset*(.text*) *libc_nano.a:*memmove*(.text*)
    . = ALIGN(4);
  } > PRAM
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > SRAM AT > PRAM
  __data_load = LOADADDR(.data);
  __pram_end = LOADADDR(.data) + SIZEOF(.data);
  .xip : {
    *(.text*) *(.rodata*)
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(.init_array*))
    __init_array_end = .;
    . = ALIGN(4);
  } > XIP
  __xip_size = SIZEOF(.xip);
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end = .; } > SRAM
  /DISCARD/ : { *(.init) *(.fini) *(.fini_array*) *(.eh_frame*) *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.ARM.attributes) }
}
