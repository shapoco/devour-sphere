# Devour Sphere for the ESPboy (ESP8266). See ../SPEC.md.
#
#   ./build.sh            # or: . $IDF_PATH/export.sh; idf.py build
#   ./flash.sh [PORT]     # esptool over the WeMos D1 mini's USB serial
#   ./monitor.sh [PORT]   # the boot log (memory figures) at 115200
#
# An ESP8266_RTOS_SDK v3 project (the ESP-IDF-style SDK, not Arduino: the
# Arduino core keeps the WiFi stack's buffers whether WiFi is used or not
# and exposes 80 KB of the 96 KB DRAM; this SDK exposes all 96 KB, starts
# no WiFi unless asked, and its own static use is 8 KB). IDF_PATH points at
# the SDK checkout; build.sh defaults it to ~/esp/ESP8266_RTOS_SDK.
#
# The game core and ShapoGFX are out of tree, so they are wrapped as
# components under components/, as the M5StickS3 front end does.

cmake_minimum_required(VERSION 3.16)

# The SDK compiles C++ as gnu++11 (tools/cmake/build.cmake); the core is
# C++17. A later -std wins on the command line. Set before project(), when
# the build properties are read.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17" APPEND)
# One task, no threads: the guards libstdc++ puts around function-local
# statics would pull pthread_cond_* in (and the link order here does not
# resolve them)
idf_build_set_property(CXX_COMPILE_OPTIONS "-fno-threadsafe-statics" APPEND)
project(devoursphere)
