# Native build of the WASM front end (writes frames as PPM files).
# The browser build uses the Makefile in this directory (Emscripten).
add_executable(devoursphere_native main.cpp)
target_link_libraries(devoursphere_native PRIVATE devoursphere::render)
target_compile_options(devoursphere_native PRIVATE -Wall -Wextra)
