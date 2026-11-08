#!/bin/sh
# Build the display DMA test and put it into a flash dump:
#   ./build.sh [dump.bin]   (default ../roms/golden.bin) -> ../roms/dma_test.bin
set -e
cd "$(dirname "$0")"
ROM="${1:-../roms/golden.bin}"
mkdir -p build
CF="-mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections -fno-common -fno-builtin -Wall -Wno-unused-const-variable -Wno-override-init -I../hw -DFAST_LCD -DLCD_FASTCLK -DLCD_STREAM -DTONE_FIX -DBL_SLEEP"
arm-none-eabi-gcc $CF -c dma_test.c -o build/dma_test.o
arm-none-eabi-gcc $CF -c ../hw/hw.c -o build/hw.o
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=build/dma_test.map \
  -T link.ld build/dma_test.o build/hw.o -lc -lgcc -o build/dma_test.elf
arm-none-eabi-objcopy -O binary build/dma_test.elf build/dma_test.bin
arm-none-eabi-size -A build/dma_test.elf | grep -E "^\.(text|data|bss)"
node ../tools/mkrom.js --rom "$ROM" --pram build/dma_test.bin --out ../roms/dma_test.bin
