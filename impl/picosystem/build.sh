#!/bin/bash

set -eux

cmake -S . -B build
cmake --build build -j
