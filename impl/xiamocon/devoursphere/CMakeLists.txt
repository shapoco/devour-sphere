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

    # The simulation rate. 30 Hz was needed while one core did everything --
    # a tick costs about 7.8 ms, and three of them ate 19.9 ms of a 46.5 ms
    # frame. With the simulation on core0 and the renderer on core1 the ticks
    # hide under the rendering, so 60 Hz is affordable again and the frame
    # rate is no longer pinned to the tick rate. sim/config.hpp accepts 30 or
    # 60; the constants are derived from per-second values, so either plays
    # the same (see ../SPEC.md for the balance comparison).
    #
    # Directory scope, not target scope: sim::TICK_RATE is a constexpr in a
    # header, so every translation unit -- ours and the two core libraries --
    # has to agree on it.
    DEVOURSPHERE_TICK_RATE=60
)

# --- The shared game core -----------------------------------------------
# Out of tree, so the second argument (the binary directory) is required.
add_subdirectory(${DS_ROOT}/submodule/shapo-gfx shapogfx)
add_subdirectory(${DS_ROOT}/core devoursphere_core)

# Only RGB565_SWAPPED is ever drawn; dropping the others takes their code out of
# both the 2D blitters and the 3D span writers. PUBLIC because the format
# macros are read by the headers too, so every translation unit has to agree.
target_compile_definitions(shapogfx PUBLIC
    SHAPOGFX_FORMAT_GRAY1=0
    SHAPOGFX_FORMAT_RGB444=0
    SHAPOGFX_FORMAT_ARGB4444=0
    SHAPOGFX_FORMAT_RGB565_SWAPPED=1
)

# Nothing in this game is textured: every material comes from the three
# factories in core/src/render/renderer.cpp, none of which sets a texture or
# the TEXTURE / ENV_MAP flags, and every Vertex::uv is {0, 0}. Compiling the
# texture path out takes the texture planes out of every primitive record and
# 4 bytes off a span. Verified bit-identical over levels 0-7 at 240x240.
#
# PRIVATE, unlike the formats above: the SHAPOGFX3D_ macros are read by
# src/gfx3d/*.cpp only and change no public type.
#
# If anything here ever does grow a texture, this is what silently drops it
# -- the members of a disabled feature are ignored at run time, so it would
# still compile and just draw untextured. The WASM front end does not set
# this, so the two would disagree.
target_compile_definitions(shapogfx PRIVATE SHAPOGFX3D_TEXTURE=0)

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
    src/ds_platform.cpp
    src/profiler.cpp
    src/se_player.cpp
    asm/se_data.S
)

# --- The sound pack --------------------------------------------------------
# assets/se/*.wav as PWM levels (core/tools/pack_se.py --pwm; needs ffmpeg
# and python3), linked into flash by asm/se_data.S. The amplifier sits behind
# a 7 kHz low-pass, so the PWM runs at 244 kHz (wrap 1023 at 250 MHz: the
# carrier is 60 dB down by the time it reaches it) and a DMA pacing timer
# delivers the samples at 24000 Hz (the one rate every build uses; the
# ESP32S3's PDM hardware wants 960 / (rate / 100) to be an integer, see
# ../SPEC.md). No ramps: the output rests at the middle
# level and the amplifier is AC coupled. Out of PlatformIO's reach (asm/, not
# src/): the ESP32S3 build embeds the browser's PCM pack instead (see
# platformio.ini and se_player.cpp).
set(SE_PWM_WRAP 1023)
set(SE_RATE 24000)
set(SE_SRC_DIR ${DS_ROOT}/assets/se)
set(SE_PACK_PY ${DS_ROOT}/core/tools/pack_se.py)
set(SE_PACK ${CMAKE_CURRENT_BINARY_DIR}/se_pwm.bin)
file(GLOB SE_WAVS ${SE_SRC_DIR}/*.wav)
add_custom_command(
    OUTPUT ${SE_PACK}
    COMMAND python3 ${SE_PACK_PY} --pwm ${SE_PWM_WRAP} --ramp 0 -r ${SE_RATE}
            -o ${SE_PACK} ${SE_SRC_DIR}
    DEPENDS ${SE_PACK_PY} ${SE_WAVS}
    COMMENT "Packing the sound effects (PWM wrap ${SE_PWM_WRAP})"
    VERBATIM
)
set_source_files_properties(asm/se_data.S PROPERTIES
    COMPILE_DEFINITIONS "SE_PACK_PATH=\"${SE_PACK}\""
    OBJECT_DEPENDS ${SE_PACK}
)
target_include_directories(${APP_NAME} PRIVATE include)
target_compile_features(${APP_NAME} PRIVATE cxx_std_17)
target_compile_options(${APP_NAME} PRIVATE -Wall -Wextra)
# The FUNC-toggled timing overlay; set to 0 to compile it out entirely.
target_compile_definitions(${APP_NAME} PRIVATE DS_PROFILE=1)
# No -O2 here: the pico SDK defaults CMAKE_BUILD_TYPE to Release (-O3).

target_link_libraries(${APP_NAME} PRIVATE
    xmc_lib
    xmc_pfm
    pico_rand                 # get_rand_32(), to seed the game
    hardware_pwm              # the speaker (src/se_player.cpp)
    hardware_dma
    hardware_flash            # FLASH_PAGE_SIZE (the high score record)
    devoursphere::render      # pulls in devoursphere::sim and shapoco::gfx
)

pico_add_extra_outputs(${APP_NAME})   # this is what produces the .uf2
