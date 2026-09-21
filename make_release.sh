#!/bin/bash
# Build the files that go into a GitHub release:
# releases/devour-sphere-YYYYMMDD.zip, one directory per target inside.
#
# Each target is built by the build script in its own directory (they source
# the SDK environment themselves, so nothing leaks between targets). The
# files that are shipped as they are (README.txt, upload.sh) live in
# assets/release/.

set -eux

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASSETS_DIR="${REPO_DIR}/assets"

TS=$(date +%Y%m%d)
RELS_NAME="devour-sphere-$TS"
RELS_DIR="${REPO_DIR}/releases/${RELS_NAME}"
RELS_ZIP="${REPO_DIR}/releases/${RELS_NAME}.zip"

COMMIT="$(git -C "${REPO_DIR}" rev-parse --short HEAD)"
if ! git -C "${REPO_DIR}" diff --quiet HEAD; then
  COMMIT="${COMMIT} (with local changes)"
fi

# Start from nothing: `zip -r` adds to an existing zip instead of replacing it
rm -rf "${RELS_DIR}" "${RELS_ZIP}"
mkdir -p "${RELS_DIR}"
mkdir -p "${RELS_DIR}/xiamocon-esp32s3"
mkdir -p "${RELS_DIR}/xiamocon-rp2350"
mkdir -p "${RELS_DIR}/picosystem"
mkdir -p "${RELS_DIR}/m5tab5"
mkdir -p "${RELS_DIR}/m5sticks3"

pushd "${REPO_DIR}/impl/xiamocon/devoursphere/"
  ./build_esp32s3.sh
  cp \
    ".pio/build/esp32s3_arduino/firmware.factory.bin" \
    "${RELS_DIR}/xiamocon-esp32s3/devour-sphere.factory.bin"
  cp \
    "${ASSETS_DIR}/release/xiamocon-esp32s3/upload.sh" \
    "${RELS_DIR}/xiamocon-esp32s3/upload.sh"
popd

pushd "${REPO_DIR}/impl/xiamocon/devoursphere/"
  ./build_rp2350.sh
  cp \
    ".cmake/devoursphere.uf2" \
    "${RELS_DIR}/xiamocon-rp2350/devour-sphere.uf2"
popd

pushd "${REPO_DIR}/impl/picosystem/"
  ./build.sh
  cp \
    "build/devoursphere.uf2" \
    "${RELS_DIR}/picosystem/devour-sphere.uf2"
popd

pushd "${REPO_DIR}/impl/m5tab5/devoursphere/"
  ./build_release.sh
  cp \
    "build/devoursphere.factory.bin" \
    "${RELS_DIR}/m5tab5/devour-sphere.factory.bin"
  cp \
    "${ASSETS_DIR}/release/m5tab5/upload.sh" \
    "${RELS_DIR}/m5tab5/upload.sh"
popd

pushd "${REPO_DIR}/impl/m5sticks3/devoursphere/"
  ./build_release.sh
  cp \
    "build/devoursphere.factory.bin" \
    "${RELS_DIR}/m5sticks3/devour-sphere.factory.bin"
  cp \
    "${ASSETS_DIR}/release/m5sticks3/upload.sh" \
    "${RELS_DIR}/m5sticks3/upload.sh"
popd

sed \
  -e "s/@DATE@/${TS}/g" \
  -e "s/@COMMIT@/${COMMIT}/g" \
  "${ASSETS_DIR}/release/README.txt" \
  > "${RELS_DIR}/README.txt"

pushd "${REPO_DIR}/releases"
  zip -r "${RELS_NAME}.zip" "${RELS_NAME}"
popd
