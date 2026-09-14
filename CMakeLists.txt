cmake_minimum_required(VERSION 3.13)
project(devoursphere LANGUAGES CXX)

option(DEVOURSPHERE_BUILD_TESTS "Build the core tests (run with ctest)" ON)
option(DEVOURSPHERE_SANITIZE "Build the tests with ASan/UBSan" OFF)

set(SHAPOGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SHAPOGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(submodule/shapo-gfx)
add_subdirectory(core)

if(NOT EMSCRIPTEN)
    # Native build of the WASM front end: renders frames to PPM files
    add_subdirectory(impl/wasm)
endif()

if(DEVOURSPHERE_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(core/test)
endif()
