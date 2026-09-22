#!/bin/bash
# Flash the build onto the ESPboy over the WeMos D1 mini's USB serial
# (CH340; the board resets into the bootloader by itself).
#
#   ./flash.sh [PORT] [BAUD]      # PORT defaults to $PORT or /dev/ttyUSB0
set -eu
cd "$(dirname "$0")"
. ./env.sh
PORT="${1:-${PORT:-/dev/ttyUSB0}}"
BAUD="${2:-${BAUD:-921600}}"
idf.py -p "${PORT}" -b "${BAUD}" flash
