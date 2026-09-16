# Devour Sphere for Xiamocon (XIAO RP2350). See ../SPEC.md.
#
#   source ~/repo/2026/xiamocon/setup.shrc
#   cd impl/xiamocon/devoursphere
#   xmc build -p rp2350_pico_sdk        # -> .cmake/devoursphere.uf2
#   xmc run   -p rp2350_pico_sdk -d E   # build and flash (WSL2 drive letter)
#
# Always pass -p: without it `xmc` also attempts a PlatformIO ESP32 build and
# exits non-zero. The directory basename must stay "devoursphere" because
# `xmc run` copies ./.cmake/<basename of the working directory>.uf2.
# For quick iteration run `make -j` inside ./.cmake directly -- `xmc clean` is
# `rm -rf .cmake` and throws away the picotool it fetched and built.
#
# This is a separate top level from the repository's host CMakeLists.txt: the
# cross toolchain is installed by pico_sdk_import.cmake, which has to run
# before project().

cmake_minimum_required(VERSION 3.13)

set(APP_NAME devoursphere)
set(DS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

if(NOT DEFINED XMC_PLATFORM)
    set(XMC_PLATFORM rp2350_pico_sdk)
endif()
if(NOT XMC_PLATFORM STREQUAL "rp2350_pico_sdk")
    message(FATAL_ERROR "Devour Sphere targets RP2350 only (XMC_PLATFORM=${XMC_PLATFORM})")
endif()

# `xmc build` passes -DPICO_BOARD=pico2; this plain set() shadows that cache
# entry, the way the SDK's own examples do.
set(PICO_BOARD seeed_xiao_rp2350)
include($ENV{XMC_REPO_PATH}/cpp/pico_sdk_import.cmake)
include($ENV{XMC_REPO_PATH}/cpp/pico_extras_import.cmake)

project(${APP_NAME} C CXX ASM)
pico_sdk_init()

# Directory scope on purpose. crt0.S and multicore.c come from INTERFACE
# libraries and are compiled into whichever target links them -- xmc_pfm, not
# this executable -- so target_compile_definitions() here would miss them.
# The default 2 KB is tight for buildSphere()'s seven levels of recursion on
# top of the rasterizer's locals. The stacks live in SCRATCH_X / SCRATCH_Y
# (4 KB each) and do not come out of the 512 KB main RAM, but 4096 is the
# ceiling: more is a link error.
add_compile_definitions(
    PICO_STACK_SIZE=4096
    PICO_CORE1_STACK_SIZE=4096
)

# --- The shared game core -----------------------------------------------
# Out of tree, so the second argument (the binary directory) is required.
add_subdirectory(${DS_ROOT}/submodule/shapo-gfx shapogfx)
add_subdirectory(${DS_ROOT}/core devoursphere_core)

# Only RGB565BE is ever drawn; dropping the others takes their code out of
# both the 2D blitters and the 3D span writers.
target_compile_definitions(shapogfx PUBLIC
    SHAPOGFX_FORMAT_GRAY1=0
    SHAPOGFX_FORMAT_RGB444=0
    SHAPOGFX_FORMAT_ARGB4444=0
    SHAPOGFX_FORMAT_RGB565BE=1
)

# --- The firmware --------------------------------------------------------
add_executable(${APP_NAME})
include($ENV{XMC_REPO_PATH}/cpp/xmc_sdk_import.cmake)   # xmc_pfm and xmc_lib

# Treat the SDK's headers as system headers so our -Wextra does not report
# things we cannot fix (xmc/app.hpp drags in the whole gfx stack).
set_target_properties(xmc_lib xmc_pfm PROPERTIES SYSTEM TRUE)

# Listed rather than globbed: the SDK's template globs without
# CONFIGURE_DEPENDS, so a new file is silently dropped until someone
# reconfigures by hand.
target_sources(${APP_NAME} PRIVATE
    src/app.cpp
    src/band_writer.cpp
)
target_include_directories(${APP_NAME} PRIVATE include)
target_compile_features(${APP_NAME} PRIVATE cxx_std_17)
target_compile_options(${APP_NAME} PRIVATE -Wall -Wextra)
# No -O2 here: the pico SDK defaults CMAKE_BUILD_TYPE to Release (-O3).

target_link_libraries(${APP_NAME} PRIVATE
    xmc_lib
    xmc_pfm
    pico_rand                 # get_rand_32(), to seed the game
    devoursphere::render      # pulls in devoursphere::sim and shapoco::gfx
)

pico_add_extra_outputs(${APP_NAME})   # this is what produces the .uf2
