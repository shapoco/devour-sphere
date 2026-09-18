#!/usr/bin/env bash
# Flash Devour Sphere onto a XIAO ESP32S3 (Xiamocon).
#
#   ./upload.sh [PORT] [BAUD]
#
# PORT defaults to $PORT or /dev/ttyACM0 (COM3 and the like on Windows),
# BAUD to $BAUD or 921600. Needs esptool (`pip install esptool`).
#
# If the port does not appear, put the board in download mode: hold BOOT,
# tap RESET, release BOOT.

set -eu

PORT="${1:-${PORT:-/dev/ttyACM0}}"
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

# esptool 5 renamed the subcommands (write_flash -> write-flash)
MAJOR="$($ESPTOOL version 2>/dev/null | grep -oE '[0-9]+' | head -1 || true)"
if [ "${MAJOR:-4}" -ge 5 ]; then WRITE=write-flash; else WRITE=write_flash; fi

set -x
$ESPTOOL --chip esp32s3 --port "$PORT" --baud "$BAUD" "$WRITE" 0x0 "$BIN"
