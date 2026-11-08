#!/bin/bash
# Build Devour Sphere for the Tamagotchi Paradise: build/pram.bin (the 64 KB
# image the launcher loads) and build/xip.bin (the part that runs from the
# flash window), plus build/ds.elf for tools/sim.py.
#
#   ./build.sh                       # Glacier / Tropical layout (see below)
#   XIP_FLASH_OFF=0xB1000 ./build.sh # Land / Water / Sky (the DOOM places)
#
# The flash offsets are link-time constants (the code runs in place), so
# they are chosen here and written into the image's settings block, which
# tools/mkrom.js reads and checks against the ROM. Glacier and Tropical
# firmware ends at 0xB5E00: launcher 0xB6000, image 0xB7000, flash part
# 0xC7000. Land, Water and Sky have the image at
# 0xA0000 and the launcher at 0xB0000, so the flash part goes to 0xB1000.
#
# Needs arm-none-eabi-gcc (13 is what this was built with).
set -eu
cd "$(dirname "$0")"
DS_ROOT="$(cd ../../.. && pwd)"
XIP_FLASH_OFF="${XIP_FLASH_OFF:-0xC7000}"
XIP_END=0x10E000   # the Studio's settings page and what follows stay untouched
# the window shows the flash from 0x11000 at 0x10000000
XIP_ORIGIN=$(printf '0x%X' $(( 0x10000000 + XIP_FLASH_OFF - 0x11000 )))
XIP_LENGTH=$(printf '0x%X' $(( XIP_END - XIP_FLASH_OFF )))
mkdir -p build/obj
rm -f build/obj/*.o

# The core's reduced configuration: the ESPboy's values (see ../SPEC.md)
CORE_DEFS=(
  -DDEVOURSPHERE_TICK_RATE=30
  -DDEVOURSPHERE_SPHERE_RADIUS_SHIFT=16
  -DDEVOURSPHERE_INITIAL_ENTITIES=48
  -DDEVOURSPHERE_MAX_ENTITIES=64
  -DDEVOURSPHERE_INITIAL_FOOD_FRAGMENTS=80
  -DDEVOURSPHERE_FOOD_TARGET=100
  -DDEVOURSPHERE_MAX_FLOATING_FRAGMENTS=128
  -DDEVOURSPHERE_MAX_BULLETS=32
  -DDEVOURSPHERE_FRAGMENT_KEEP_LARGEST=1
  -DDEVOURSPHERE_SPHERE_TARGET_PX=64
  -DDEVOURSPHERE_MAX_WIRE=160
  -DDEVOURSPHERE_MAX_LINES2D=96
  -DDEVOURSPHERE_MAX_POINTS2D=48
  -DDEVOURSPHERE_SUPPRESS_ALPHA=1
  -DDEVOURSPHERE_NO_DEPTH=1
  -DDEVOURSPHERE_MAX_DEBRIS=16
  -DDEVOURSPHERE_MAX_DUST=16
  -DDEVOURSPHERE_MAX_GAUGES=16
  -DDEVOURSPHERE_MAX_MARKERS=16
  -DDEVOURSPHERE_MAX_ENEMY_MARKERS=12
  -DDEVOURSPHERE_MAX_STARS=48
  # the innermost loops of the core run from PRAM
  '-DDEVOURSPHERE_HOT_ATTR=__attribute__((section(".pram_text.ds")))'
)
GFX_DEFS=(
  -DSHAPOGFX_FORMAT_GRAY1=0 -DSHAPOGFX_FORMAT_RGB444=0 -DSHAPOGFX_FORMAT_ARGB4444=0
  -DSHAPOGFX_FORMAT_RGB565_SWAPPED=1
  -DSHAPOGFX3D_TEXTURE=0 -DSHAPOGFX3D_GOURAUD=0 -DSHAPOGFX3D_BLEND=0
  -DSHAPOGFX3D_FIXED_POINT=1 -DSHAPOGFX3D_VCACHE_SIZE=16 -DSHAPOGFX3D_LAYER_MAX=4
  -DSHAPOGFX2D_TRANSFORM=0 -DSHAPOGFX2D_COLOR_KEY=0
  '-DSHAPOGFX3D_HOT_ATTR=__attribute__((section(".pram_text.gfx3d")))'
  -DSHAPOGFX3D_HOT_INSTANTIATE=1
)
# This directory first: the shared profiler includes <ds_config.hpp> and
# <ds_platform.hpp> and must find ours, not the Xiamocon's next to it
INC=(-I. -I../hw -I"$DS_ROOT/core/include" -I"$DS_ROOT/submodule/shapo-gfx/include"
     -I"$DS_ROOT/impl/xiamocon/devoursphere/include")
COMMON=(-mcpu=cortex-m3 -mthumb -O2 -ffunction-sections -fdata-sections -fno-common
        -Wall -Wextra -Wno-unused-parameter "${INC[@]}" "${CORE_DEFS[@]}" "${GFX_DEFS[@]}")
CXX=(arm-none-eabi-g++ "${COMMON[@]}" -std=gnu++17 -fno-exceptions -fno-rtti
     -fno-threadsafe-statics -fno-use-cxa-atexit)
CC=(arm-none-eabi-gcc "${COMMON[@]}" -std=gnu11 -Wno-unused-const-variable -Wno-unused-function
    -DFAST_LCD -DLCD_FASTCLK -DLCD_DMA -DTONE_FIX -DBL_SLEEP)

cxx() { "${CXX[@]}" -c "$1" -o "build/obj/$2.o"; }
cc()  { "${CC[@]}"  -c "$1" -o "build/obj/$2.o"; }

for f in fixed entities game entity combat upgrades; do cxx "$DS_ROOT/core/src/sim/$f.cpp" "sim_$f"; done
for f in renderer sphere effects upgrades hud benchmark; do cxx "$DS_ROOT/core/src/render/$f.cpp" "render_$f"; done
for f in graphics2d images shapes; do cxx "$DS_ROOT/submodule/shapo-gfx/src/gfx2d/$f.cpp" "gfx2d_$f"; done
for f in gfx3d scene shapes; do cxx "$DS_ROOT/submodule/shapo-gfx/src/gfx3d/$f.cpp" "gfx3d_$f"; done
cxx "$DS_ROOT/impl/xiamocon/devoursphere/src/profiler.cpp" profiler
"${CXX[@]}" -DDS_XIP_FLASH_OFF="$XIP_FLASH_OFF" -c main.cpp -o build/obj/main.o
cxx ds_platform.cpp ds_platform
cxx tone_player.cpp tone_player
cc ../hw/hw.c hw
cc plat_c.c plat_c

arm-none-eabi-g++ -mcpu=cortex-m3 -mthumb -nostartfiles --specs=nano.specs \
  -Wl,--gc-sections -Wl,-Map=build/ds.map -Wl,--defsym=XIP_ORIGIN="$XIP_ORIGIN" -Wl,--defsym=XIP_LENGTH="$XIP_LENGTH" \
  -T link.ld build/obj/*.o -lc -lgcc -o build/ds.elf
arm-none-eabi-objcopy -O binary -j .pram -j .data build/ds.elf build/pram.bin
arm-none-eabi-objcopy -O binary -j .xip build/ds.elf build/xip.bin
echo "XIP part at flash $XIP_FLASH_OFF (window $XIP_ORIGIN)"
arm-none-eabi-size -A build/ds.elf | grep -E "^\.(pram|data|xip|bss) "
ls -l build/pram.bin build/xip.bin
PRAM_USED=$(stat -c %s build/pram.bin)
if [ "$PRAM_USED" -gt 65536 ]; then echo "build.sh: the PRAM image is over 64 KB ($PRAM_USED)"; exit 1; fi
