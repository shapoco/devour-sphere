#!/bin/bash
# Build the M5StickS3 firmware. DS_IDF_PATH points at the ESP-IDF
# installation directory (the one that holds esp-idf/); v5.5.x is what this
# project is built against, the same as the Tab5 front end.
set -eux
DS_IDF_PATH="${DS_IDF_PATH:-${HOME}/esp/5.5}"
cd "$(dirname "$0")"
source "${DS_IDF_PATH}/esp-idf/export.sh"
idf.py build
