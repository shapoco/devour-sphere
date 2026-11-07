#!/bin/sh
# Build the hello program: build/hello.bin, a 64 KB PRAM image for mkrom.js.
# Needs arm-none-eabi-gcc (the Debian/Ubuntu package is enough).
set -e
cd "$(dirname "$0")"
mkdir -p build
CF="-mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections -fno-common -fno-builtin -Wall -Wno-unused-const-variable -I../hw -DFAST_LCD -DTONE_FIX -DBL_SLEEP"
arm-none-eabi-gcc $CF -c hello.c -o build/hello.o
arm-none-eabi-gcc $CF -c ../hw/hw.c -o build/hw.o
arm-none-eabi-gcc $CF -c ../hw/flash.c -o build/flash.o
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=build/hello.map \
  -T link.ld build/hello.o build/hw.o build/flash.o -lc -lgcc -o build/hello.elf
arm-none-eabi-objcopy -O binary build/hello.elf build/hello.bin
arm-none-eabi-size -A build/hello.elf | grep -E "^\.(text|data|bss)"
ls -l build/hello.bin
