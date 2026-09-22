#!/bin/bash
# The boot log: the memory figures app_main prints, then nothing unless
# something goes wrong. 115200 baud (sdkconfig.defaults); the ROM's own
# boot messages come out at 74880 and look like noise here.
#
#   ./monitor.sh [PORT]           # PORT defaults to $PORT or /dev/ttyUSB0
set -eu
cd "$(dirname "$0")"
. ./env.sh
PORT="${1:-${PORT:-/dev/ttyUSB0}}"
idf.py -p "${PORT}" monitor
