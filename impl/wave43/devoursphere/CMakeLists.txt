# Devour Sphere for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 ("wave_43" in
# Kern). See ../SPEC.md.
#
#   ./build.sh            # or: source $DS_IDF_PATH/esp-idf/export.sh; idf.py build
#   ./run.sh              # build and flash
#   ./monitor.sh          # serial log
#
# A native ESP-IDF v6.1 project on the IDF's own drivers (esp_lcd for the
# MIPI-DSI panel, the PPA, I2C for the touch) with no board library in
# between: the M5 libraries the Tab5 port leans on do not build on 6.x, and
# this board is not theirs anyway.
#
# The game core and ShapoGFX are out of tree, so they are wrapped as IDF
# components under components/ rather than added with add_subdirectory():
# an IDF project may only pull sources in that way.

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(devoursphere)
