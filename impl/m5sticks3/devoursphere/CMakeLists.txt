# Devour Sphere for M5StickS3 (ESP32-S3). See ../SPEC.md.
#
#   ./build.sh            # or: source $DS_IDF_PATH/esp-idf/export.sh; idf.py build
#   ./run.sh              # build and flash
#   ./monitor.sh          # serial log
#
# A native ESP-IDF project, like the Tab5 front end and unlike the Xiamocon
# one: building the whole tree is what lets COMPILER_OPTIMIZATION_PERF reach
# ShapoGFX's span loops and M5GFX's SPI bus (Arduino's prebuilt libraries are
# always -Os), and M5Unified is an IDF component either way.
#
# The game core and ShapoGFX are out of tree, so they are wrapped as IDF
# components under components/ rather than added with add_subdirectory():
# an IDF project may only pull sources in that way.

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(devoursphere)
