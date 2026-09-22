#!/bin/bash
# What make_release.sh runs: a clean build. The RTOS SDK's esptool cannot
# merge images, so the release ships bootloader, partition table and app
# separately (assets/release/espboy/upload.sh writes them at their offsets).
set -eux
cd "$(dirname "$0")"
./build.sh
