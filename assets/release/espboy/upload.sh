#!/usr/bin/env bash
# Flash Devour Sphere onto an ESPboy (ESP8266 on a WeMos D1 mini).
#
#   ./upload.sh [PORT] [BAUD]
#
# PORT defaults to $PORT or /dev/ttyUSB0 (COM3 and the like on Windows),
# BAUD to $BAUD or 921600. Needs esptool (`pip install esptool`).
#
# The board's CH340 resets it into the bootloader by itself. The image is
# bootloader + partition table + app merged at their offsets, written in
# one go at 0x0.

set -eu

PORT="${1:-${PORT:-/dev/ttyUSB0}}"
BAUD="${2:-${BAUD:-921600}}"
BIN="$(cd "$(dirname "$0")" && pwd)/devour-sphere.factory.bin"

[ -f "$BIN" ] || { echo "firmware not found: $BIN" >&2; exit 1; }

# esptool is installed as `esptool`, as `esptool.py`, or as a python module
if command -v esptool >/dev/null 2>&1; then
  ESPTOOL="esptool"
elif command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL="esptool.py"
elif python3 -m esptool version >/dev/null 2>&1; then
  ESPTOOL="python3 -m esptool"
else
  echo "esptool not found. Install it with: pip install esptool" >&2
  exit 1
fi

$ESPTOOL --chip esp8266 --port "$PORT" --baud "$BAUD" \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x0 "$BIN"
