#!/bin/bash
# What make_release.sh runs: the build, whose last step merges bootloader,
# partition table and app into build/devoursphere.factory.bin (the one file
# the release ships).
set -eux
cd "$(dirname "$0")"
./build.sh
