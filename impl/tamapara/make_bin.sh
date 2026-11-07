#!/bin/bash

set -eux

pushd hello > /dev/null
  ./build.sh
popd > /dev/null

node \
    tools/mkrom.js \
    --rom roms/golden.bin \
    --pram hello/build/hello.bin \
    --out roms/hello.bin

pushd devoursphere > /dev/null
  ./build.sh
popd > /dev/null

node \
    tools/mkrom.js \
    --rom roms/golden.bin \
    --pram devoursphere/build/pram.bin \
    --xip devoursphere/build/xip.bin \
    --out roms/devoursphere.bin
