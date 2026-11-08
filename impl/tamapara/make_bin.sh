#!/bin/bash
# Build both programs and put each into a flash dump of the Paradise:
#
#   ./make_bin.sh [dump.bin]      (default roms/golden.bin)
#
# -> roms/hello.bin (the test program) and roms/devoursphere.bin (the game).
# The game's flash part runs in place, so it is built for where this dump
# has room: mkrom.js --place reads that off the dump (Land / Water / Sky:
# the Studio's DOOM place, image 0xA0000; Glacier / Tropical: after the
# firmware's code, image 0xB7000).
set -eu
cd "$(dirname "$0")"
ROM="${1:-roms/golden.bin}"

eval "$(node tools/mkrom.js --rom "$ROM" --place)"
echo "flash part at $XIP_FLASH_OFF (from $ROM)"

(cd hello && ./build.sh)
node tools/mkrom.js --rom "$ROM" --pram hello/build/hello.bin --out roms/hello.bin

(cd devoursphere && XIP_FLASH_OFF="$XIP_FLASH_OFF" ./build.sh)
node tools/mkrom.js --rom "$ROM" --pram devoursphere/build/pram.bin --xip devoursphere/build/xip.bin --out roms/devoursphere.bin
