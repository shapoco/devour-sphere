# ShapoGFX (submodule/shapo-gfx/) as an ESP-IDF component.
set(DS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)

idf_component_register(
    SRCS
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx2d/graphics2d.cpp"
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx2d/images.cpp"
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx2d/shapes.cpp"
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx3d/gfx3d.cpp"
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx3d/scene.cpp"
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx3d/shapes.cpp"
    INCLUDE_DIRS "${DS_ROOT}/submodule/shapo-gfx/include"
)

# Only RGB565_SWAPPED is ever drawn (the PPA swaps the bytes on its way to the
# panel, see panel_out.cpp), so the other formats come out of both the 2D
# blitters and the 3D span writers. A build property, not a target one: the
# format macros are read by the headers as well, so every translation unit
# has to agree.
idf_build_set_property(COMPILE_DEFINITIONS "SHAPOGFX_FORMAT_GRAY1=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "SHAPOGFX_FORMAT_RGB444=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "SHAPOGFX_FORMAT_ARGB4444=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "SHAPOGFX_FORMAT_RGB565_SWAPPED=1" APPEND)

# Nothing in this game is textured; compiling the path out takes the texture
# planes out of every primitive record and 4 bytes off a span. Verified
# bit-identical by the Xiamocon build (impl/xiamocon/SPEC.md).
target_compile_definitions(${COMPONENT_LIB} PRIVATE SHAPOGFX3D_TEXTURE=0)

# gfx2d: no color key (nothing uses one; bit-identical). The transforms stay:
# they magnify the title and the pad labels on this screen.
target_compile_definitions(${COMPONENT_LIB} PRIVATE SHAPOGFX2D_COLOR_KEY=0)
