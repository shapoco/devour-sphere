#!/usr/bin/env bash
# Build the files that go into a GitHub release.
#
#   ./make_release.sh [--date YYYYMMDD] [--clean]
#
# Builds every impl/ target except wasm (that one is published as the web page
# under docs/play/, not as a downloadable binary), lays the binaries out under
# releases/devour-sphere-YYYYMMDD/<target>/ and zips the whole directory.
# Any failing step stops the script, so a half-built zip is never produced.
#
# Needs the Xiamocon SDK environment (pico-sdk, PlatformIO, the `xmc` tool);
# the script sources setup.shrc for it. See impl/xiamocon/SPEC.md.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="devour-sphere"
DATE="$(date +%Y%m%d)"
CLEAN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --date) DATE="${2:?--date needs YYYYMMDD}"; shift 2 ;;
    --clean) CLEAN=1; shift ;;  # slow: `xmc clean` also drops the fetched picotool
    -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
  esac
done

STAGE="$REPO_ROOT/releases/$NAME-$DATE"
ZIP="$REPO_ROOT/releases/$NAME-$DATE.zip"
XMC_DIR="$REPO_ROOT/impl/xiamocon/devoursphere"

step() { printf '\n=== %s\n' "$*"; }

# The SDK paths, PlatformIO and `xmc` all come from setup.shrc. The repo copy
# is the one to use (~/.xmc/setup.shrc points at the installed SDK instead).
step "Setting up the Xiamocon SDK environment"
SETUP_SHRC="${XMC_SETUP_SHRC:-$HOME/repo/2026/xiamocon/setup.shrc}"
if [ ! -f "$SETUP_SHRC" ]; then
  echo "setup.shrc not found: $SETUP_SHRC (set XMC_SETUP_SHRC)" >&2
  exit 1
fi
set +u  # setup.shrc sources a venv activator and the completion script
# shellcheck disable=SC1090
source "$SETUP_SHRC"
set -u
command -v xmc >/dev/null || { echo "xmc not on PATH after sourcing $SETUP_SHRC" >&2; exit 1; }
echo "xmc: $(command -v xmc)"

step "Staging into $STAGE"
rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE"

# --- impl/xiamocon: one firmware per board, from the same sources -----------
cd "$XMC_DIR"
if [ "$CLEAN" = 1 ]; then
  step "Cleaning impl/xiamocon"
  rm -rf .cmake .pio
fi

step "Building Xiamocon / XIAO RP2350"
xmc build -p rp2350_pico_sdk
UF2=".cmake/devoursphere.uf2"
[ -f "$UF2" ] || { echo "missing $XMC_DIR/$UF2" >&2; exit 1; }
mkdir -p "$STAGE/xiamocon-rp2350"
cp "$UF2" "$STAGE/xiamocon-rp2350/$NAME.uf2"

step "Building Xiamocon / XIAO ESP32S3"
xmc build -p esp32s3_pio_arduino
# The factory image is bootloader + partition table + boot_app0 + app already
# merged at their offsets, so flashing it at 0x0 is what PlatformIO does in
# four pieces.
FACTORY=".pio/build/esp32s3_arduino/firmware.factory.bin"
[ -f "$FACTORY" ] || { echo "missing $XMC_DIR/$FACTORY" >&2; exit 1; }
ESP_DIR="$STAGE/xiamocon-esp32s3"
mkdir -p "$ESP_DIR"
cp "$FACTORY" "$ESP_DIR/$NAME.factory.bin"

step "Writing the upload script and the README"
cat > "$ESP_DIR/upload.sh" <<'EOF'
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
EOF
chmod +x "$ESP_DIR/upload.sh"

GIT_DESC="$(cd "$REPO_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if ! (cd "$REPO_ROOT" && git diff --quiet HEAD 2>/dev/null); then
  GIT_DESC="$GIT_DESC (with local changes)"
fi

cat > "$STAGE/README.txt" <<EOF
Devour Sphere - $DATE
Built from commit $GIT_DESC
https://github.com/shapoco/devour-sphere

Firmware for Xiamocon (a XIAO RP2350 / ESP32S3 handheld). The browser version
needs no download: see docs/play/ on the project page.

Controls: LEFT / RIGHT turn, UP dashes, DOWN brakes, A / B / X / Y fire and
confirm. Hold FUNC while playing to overlay the timing counters.

xiamocon-rp2350/  - for XIAO RP2350
  $NAME.uf2
    Put the board in mass storage mode (hold Down, hold the power button for
    3 seconds, then release Down) and copy the .uf2 onto the drive that
    appears. The board reboots into the game by itself.

xiamocon-esp32s3/ - for XIAO ESP32S3
  $NAME.factory.bin, upload.sh
    ./upload.sh [PORT] [BAUD]        (PORT defaults to /dev/ttyACM0)
    The script calls esptool ("pip install esptool" if you do not have it).
    The image is bootloader + partition table + boot_app0 + app merged at
    their offsets, so it is written in one go at 0x0. If the port does not
    appear, hold BOOT, tap RESET, release BOOT and try again.
EOF

step "Zipping"
cd "$REPO_ROOT/releases"
zip -r -q "$(basename "$ZIP")" "$NAME-$DATE"

step "Done"
ls -l "$ZIP"
(cd "$STAGE" && find . -type f | sort | sed 's|^\./|  |')
