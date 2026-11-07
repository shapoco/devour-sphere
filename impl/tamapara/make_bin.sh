#!/bin/bash

set -eux

node \
    tools/mkrom.js \
    --rom roms/golden.bin \
    --pram hello/build/hello.bin \
    --out roms/hello.bin

node \
    tools/mkrom.js \
    --rom roms/golden.bin \
    --pram devoursphere/build/pram.bin \
    --xip devoursphere/build/xip.bin \
    --out roms/devoursphere.bin
