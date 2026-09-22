#!/bin/bash
# Build the ESPboy firmware with ESP8266_RTOS_SDK. IDF_PATH points at the
# SDK checkout (default ~/esp/ESP8266_RTOS_SDK); the toolchain and the
# SDK's Python environment are the ones its install.sh puts under
# ~/.espressif (xtensa-lx106-elf 8.4.0, python_env/rtos3.4_py3.12_env --
# see ../SPEC.md "ビルドと書き込み" for how they were set up).
#
#   ./build.sh            # build/devoursphere.bin (+ bootloader, partition table)
#   ./build.sh clean      # idf.py fullclean
set -eu
cd "$(dirname "$0")"
. ./env.sh
if [ "${1:-}" = "clean" ]; then
  idf.py fullclean
  exit 0
fi
idf.py build
