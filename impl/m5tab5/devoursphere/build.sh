#!/bin/bash
# Build the Tab5 firmware. DS_IDF_PATH points at the ESP-IDF installation
# directory (the one that holds esp-idf/); v5.5.x is what this project is
# built and tested against -- see ../SPEC.md for why not 6.0.
set -eux
DS_IDF_PATH="${DS_IDF_PATH:-${HOME}/esp/5.5}"
cd "$(dirname "$0")"
source "${DS_IDF_PATH}/esp-idf/export.sh"
idf.py build
