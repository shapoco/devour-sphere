# Devour Sphere for PicoSystem (Pimoroni, RP2040). See ../SPEC.md.
#
#   cmake -S . -B build -DPICO_SDK_PATH=~/pico/pico-sdk
#   cmake --build build -j                       # -> build/devoursphere.uf2
#
# A plain pico-sdk project: the PicoSystem SDK is not used, because it owns a
# static 115,200-byte frame buffer (picosystem.cpp, `_fb`) and this game draws
# in 40-row bands precisely so that it never has to hold a whole frame. What
# the SDK would have provided -- the ST7789 bring-up, the button pins and the
# 250 MHz overclock -- is a few dozen lines in src/, and the pin numbers come
# from pico-sdk's own board file (pimoroni_picosystem.h).
#
# This is a separate top level from the repository's host CMakeLists.txt, like
# the Xiamocon build: the cross toolchain is installed by pico_sdk_import.cmake,
# which has to run before project().

cmake_minimum_required(VERSION 3.13)

set(APP_NAME devoursphere)
set(DS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(XMC_APP ${DS_ROOT}/impl/xiamocon/devoursphere)  # the profiler is shared

# The board file selects rp2040, the 16 MB flash and the flash clock divider.
# A plain set() shadows a cache entry the way the SDK's own examples do.
set(PICO_BOARD pimoroni_picosystem)
include(pico_sdk_import.cmake)

project(${APP_NAME} C CXX ASM)
pico_sdk_init()

# Directory scope on purpose: crt0.S comes from an INTERFACE library and is
# compiled into whichever target links it. The stacks live in SCRATCH_X /
# SCRATCH_Y (4 KB each) and do not come out of the 256 KB main RAM; 4096 is
# the ceiling and the same value the Xiamocon build uses.
#
# 30 Hz simulation: a tick costs several times what it does on the RP2350's
# Cortex-M33 (no 64-bit multiply, no divide, no FPU on the M0+), and the
# ticks have to fit under the frame on core1. sim/config.hpp accepts 30 or
# 60 and derives its constants from per-second values, so either plays the
# same. Directory scope because sim::TICK_RATE is a constexpr in a header
# that every translation unit -- ours and the two core libraries -- reads.
add_compile_definitions(
    PICO_STACK_SIZE=4096
    PICO_CORE1_STACK_SIZE=4096
    DEVOURSPHERE_TICK_RATE=30
)

# --- The shared game core -----------------------------------------------
add_subdirectory(${DS_ROOT}/submodule/shapo-gfx shapogfx)
add_subdirectory(${DS_ROOT}/core devoursphere_core)

# Only RGB565BE is ever drawn. The panel is written in its 16-bit mode over
# the hardware SPI, and the DMA byte-swaps each 16-bit word on the way out,
# so the band buffers are the same big-endian format the Xiamocon build uses
# (see src/display.cpp). PUBLIC because the format macros are read by the
# headers too.
target_compile_definitions(shapogfx PUBLIC
    SHAPOGFX_FORMAT_GRAY1=0
    SHAPOGFX_FORMAT_RGB444=0
    SHAPOGFX_FORMAT_ARGB4444=0
    SHAPOGFX_FORMAT_RGB565BE=1
)
# Nothing in this game is textured (verified in the Xiamocon build, see
# impl/xiamocon/devoursphere/CMakeLists.txt). PRIVATE: read by src/gfx3d only.
target_compile_definitions(shapogfx PRIVATE SHAPOGFX3D_TEXTURE=0)

# --- The firmware --------------------------------------------------------
add_executable(${APP_NAME}
    src/main.cpp
    src/display.cpp
    src/platform.cpp
    # The timing overlay is the Xiamocon one, compiled from its own tree: it
    # only needs ds_config.hpp / ds_platform.hpp, which this front end
    # provides under the same names.
    ${XMC_APP}/src/profiler.cpp
)
# Our include directory must come first: the Xiamocon one is listed only for
# profiler.hpp, and it also holds a ds_config.hpp / ds_platform.hpp of its
# own (which include the xmc SDK) that must never be picked up here.
target_include_directories(${APP_NAME} PRIVATE
    include
    ${XMC_APP}/include
)
target_compile_features(${APP_NAME} PRIVATE cxx_std_17)
target_compile_options(${APP_NAME} PRIVATE -Wall -Wextra)
# The Y-toggled timing overlay; set to 0 to compile it out entirely.
target_compile_definitions(${APP_NAME} PRIVATE DS_PROFILE=1)
# No -O2 here: the pico SDK defaults CMAKE_BUILD_TYPE to Release (-O3).

# No stdio at all: USB would cost RAM and interrupt load, and the UART pins
# are not brought out on the PicoSystem.
pico_enable_stdio_usb(${APP_NAME} 0)
pico_enable_stdio_uart(${APP_NAME} 0)

target_link_libraries(${APP_NAME} PRIVATE
    pico_stdlib
    pico_multicore            # the simulation runs on core1
    pico_rand                 # get_rand_32(), to seed the game
    hardware_spi
    hardware_dma
    hardware_vreg             # the 250 MHz overclock needs 1.20 V
    devoursphere::render      # pulls in devoursphere::sim and shapoco::gfx
)

pico_add_extra_outputs(${APP_NAME})   # this is what produces the .uf2
