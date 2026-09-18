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
    # The wireframe's segment array: 240x240 budgets 550 lines (1100 x
    # scale), so the 1100 the reference screen needs would waste 5.4 KB
    DEVOURSPHERE_MAX_WIRE=560
    # The 2D line and point arrays (outlines, effects, far fragments): a
    # crowded 240x240 frame has about 100 lines and 30 points; what does
    # not fit takes the 3D path. 14 and 8 bytes an entry.
    DEVOURSPHERE_MAX_LINES2D=256
    DEVOURSPHERE_MAX_POINTS2D=128
    # Off-screen enemies as solid triangles rather than additive glow fans:
    # a fan costs about a millisecond here and a crowd draws twelve
    DEVOURSPHERE_SIMPLE_AURAS=1
)

# --- The shared game core -----------------------------------------------
add_subdirectory(${DS_ROOT}/submodule/shapo-gfx shapogfx)
add_subdirectory(${DS_ROOT}/core devoursphere_core)
# The simulation runs from RAM (DS_RAM_CODE below), so its code size is RAM.
# The SDK's Release default is -O3; -O2 makes the sim 10.6 KB smaller
# (50.9 KB against 61.6 KB of RAM code in all), which is what lets the sound
# effects and the flight cinematic fit in the 256 KB. The tick cost of -O2
# against -O3 is to be read on the device (../SPEC.md, "メモリ配分").
set(DS_SIM_OPT "-O2" CACHE STRING "Optimization level of the simulation library")
target_compile_options(devoursphere_sim PRIVATE ${DS_SIM_OPT})

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
# No FPU: the vertex stage, the primitive setup and the span builders in
# fixed point (see ShapoGFX's SPEC.md). Read by gfx3d.cpp only.
target_compile_definitions(shapogfx PRIVATE SHAPOGFX3D_FIXED_POINT=1)
# The span loops (render() and what it calls per span, ~14 KB) run from RAM
# through the SDK's .time_critical placement; the rest of the 3D library
# stays in flash. See ../SPEC.md "コードを RAM に置く".
target_compile_definitions(shapogfx PRIVATE
    "SHAPOGFX3D_HOT_ATTR=__attribute__((section(\".time_critical.gfx3d\")))"
    SHAPOGFX3D_HOT_INSTANTIATE=1)

# --- The sound pack --------------------------------------------------------
# materials/se/*.wav as PWM levels (core/tools/pack_se.py --pwm; needs ffmpeg
# and python3), linked into flash by src/se_data.S. The PWM period is one
# sample, so the wrap follows the system clock: 250 MHz / 22050 Hz - 1.
option(DS_OVERCLOCK "Run at 250 MHz (0: the stock 125 MHz)" ON)
set(SE_RATE 22050)
if(DS_OVERCLOCK)
    set(DS_SYS_CLOCK_HZ 250000000)
else()
    set(DS_SYS_CLOCK_HZ 125000000)
endif()
math(EXPR SE_PWM_WRAP "${DS_SYS_CLOCK_HZ} / ${SE_RATE} - 1")
# The piezo is quiet and plays nothing below a few hundred hertz: the band
# it cannot play is dropped, every sound's peak is normalized, and a common
# gain is applied through a limiter (see ../SPEC.md "効果音"). Tuning knobs.
set(SE_HIGHPASS 400 CACHE STRING "Sound pack: high-pass (Hz), 0 for none")
set(SE_PEAK_DB -1 CACHE STRING "Sound pack: peak of every sound (dBFS)")
set(SE_GAIN 2.0 CACHE STRING "Sound pack: gain on top of the peak (limited)")
set(SE_SRC_DIR ${DS_ROOT}/materials/se)
set(SE_PACK_PY ${DS_ROOT}/core/tools/pack_se.py)
set(SE_PACK ${CMAKE_CURRENT_BINARY_DIR}/se_pwm.bin)
file(GLOB SE_WAVS ${SE_SRC_DIR}/*.wav)
add_custom_command(
    OUTPUT ${SE_PACK}
    COMMAND python3 ${SE_PACK_PY} --pwm ${SE_PWM_WRAP} -r ${SE_RATE}
            --highpass ${SE_HIGHPASS} --peak ${SE_PEAK_DB} --gain ${SE_GAIN}
            -o ${SE_PACK} ${SE_SRC_DIR}
    DEPENDS ${SE_PACK_PY} ${SE_WAVS}
    COMMENT "Packing the sound effects (PWM wrap ${SE_PWM_WRAP})"
    VERBATIM
)
set_source_files_properties(src/se_data.S PROPERTIES
    COMPILE_DEFINITIONS "SE_PACK_PATH=\"${SE_PACK}\""
    OBJECT_DEPENDS ${SE_PACK}
)

# --- The firmware --------------------------------------------------------
add_executable(${APP_NAME}
    src/main.cpp
    src/display.cpp
    src/platform.cpp
    src/se_data.S
    # The piezo player is the Xiamocon one (both are RP2 chips; see
    # pwm_audio.hpp for what differs per board)
    ${XMC_APP}/src/pwm_audio.cpp
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
# The timing overlay (X cycles off / FPS / full); 0 compiles it out entirely.
target_compile_definitions(${APP_NAME} PRIVATE DS_PROFILE=1)
# The clock (ds_config.hpp reads it; the sound pack above was built for it)
target_compile_definitions(${APP_NAME} PRIVATE DS_OVERCLOCK=$<BOOL:${DS_OVERCLOCK}>)
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
    hardware_pwm              # the piezo (pwm_audio.cpp)
    hardware_vreg             # the 250 MHz overclock needs 1.20 V
    devoursphere::render      # pulls in devoursphere::sim and shapoco::gfx
)

# Which code runs from RAM. The RP2040 executes from flash through one
# 16 KB XIP cache that both cores share, so whatever both are running at
# once evicts the other's code. The choice is measured on the device (see
# ../SPEC.md, "コードを RAM に置く"):
#   sim    the simulation (core1's whole program, ~40 KB): the default
#   gfx3d  ShapoGFX's 3D rasterizer (~48 KB), what the first build used
#   none   the SDK's default placement
set(DS_RAM_CODE "sim" CACHE STRING "Code placed in RAM: sim, gfx3d or none")
if(DS_RAM_CODE STREQUAL "sim")
    set(DS_RAM_CODE_EXCLUDE "*libdevoursphere_sim.a:")
elseif(DS_RAM_CODE STREQUAL "gfx3d")
    set(DS_RAM_CODE_EXCLUDE "*libshapogfx.a:gfx3d.cpp.o")
elseif(DS_RAM_CODE STREQUAL "none")
    set(DS_RAM_CODE_EXCLUDE "")
else()
    message(FATAL_ERROR "DS_RAM_CODE must be sim, gfx3d or none (got ${DS_RAM_CODE})")
endif()
configure_file(memmap_picosystem.ld.in ${CMAKE_CURRENT_BINARY_DIR}/memmap_picosystem.ld @ONLY)
pico_set_linker_script(${APP_NAME} ${CMAKE_CURRENT_BINARY_DIR}/memmap_picosystem.ld)

pico_add_extra_outputs(${APP_NAME})   # this is what produces the .uf2
