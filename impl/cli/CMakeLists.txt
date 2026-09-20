# Devour Sphere in a terminal: the frame is rendered at 320x240 and turned
# into colored text (ASCII art by default, braille or half blocks on
# request). See SPEC.md. Built by `make` in this directory, which calls:
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build                     # -> build/devoursphere
#
# A separate top level from the repository's CMakeLists.txt, like the
# handheld builds: nothing but the game core and ShapoGFX is pulled in, and
# only POSIX (termios, poll, ioctl) is used, so it builds wherever a C++17
# compiler and cmake exist.
cmake_minimum_required(VERSION 3.13)
project(devoursphere_cli LANGUAGES CXX)

set(DS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(SHAPOGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SHAPOGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${DS_ROOT}/submodule/shapo-gfx shapogfx)
add_subdirectory(${DS_ROOT}/core devoursphere_core)

add_executable(devoursphere src/main.cpp src/term.cpp src/aa.cpp)
target_link_libraries(devoursphere PRIVATE devoursphere::render)
target_compile_features(devoursphere PRIVATE cxx_std_17)
target_compile_options(devoursphere PRIVATE -Wall -Wextra)
