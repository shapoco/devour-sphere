#!/bin/bash
# Build and flash. Pass the port as the first argument, or let esptool find
# it (Tab5 appears as a USB-Serial/JTAG device on its USB-C port).
set -eux
DS_IDF_PATH="${DS_IDF_PATH:-${HOME}/esp/5.5}"
cd "$(dirname "$0")"
source "${DS_IDF_PATH}/esp-idf/export.sh"
idf.py build
if [ $# -ge 1 ]; then idf.py -p "$1" flash; else idf.py flash; fi
