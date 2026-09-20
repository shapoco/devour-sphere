# Devour Sphere for M5Stack Tab5 (ESP32-P4). See ../SPEC.md.
#
#   ./build.sh            # or: source $DS_IDF_PATH/esp-idf/export.sh; idf.py build
#   ./run.sh              # build and flash
#   ./monitor.sh          # serial log
#
# A native ESP-IDF project (not Arduino / PlatformIO): the PPA and the MIPI-DSI
# panel are IDF drivers, and building the whole tree here is what lets
# COMPILER_OPTIMIZATION_PERF reach them (the Arduino prebuilt libraries are
# always -Os).
#
# The game core and ShapoGFX are out of tree, so they are wrapped as IDF
# components under components/ rather than added with add_subdirectory():
# an IDF project may only pull sources in that way.

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(devoursphere)
