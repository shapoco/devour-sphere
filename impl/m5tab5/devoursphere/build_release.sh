#!/bin/bash
# Build the single image make_release.sh ships: bootloader + partition table
# + app merged at their offsets, so it is written in one go at 0x0.
set -eux
DS_IDF_PATH="${DS_IDF_PATH:-${HOME}/esp/5.5}"
cd "$(dirname "$0")"
source "${DS_IDF_PATH}/esp-idf/export.sh"
idf.py build
# merge-bin runs with the build directory as its working directory, so the
# output path has to be absolute or it lands in build/build/.
idf.py merge-bin -o "${PWD}/build/devoursphere.factory.bin"
