#!/bin/bash
# Build the wave_43 firmware. DS_IDF_PATH points at the ESP-IDF installation
# directory (the one that holds esp-idf/); v6.1 is what this project is
# built against (see ../SPEC.md).
set -eux
DS_IDF_PATH="${DS_IDF_PATH:-${HOME}/esp}"
cd "$(dirname "$0")"
source "${DS_IDF_PATH}/esp-idf/export.sh"
idf.py build
