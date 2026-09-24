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

# Only RGB565_SWAPPED is ever drawn, which is also the order the ST7789 wants on
# the wire (panel_out.cpp hands the band to M5GFX unconverted), so the other
# formats come out of both the 2D blitters and the 3D span writers. A build property, not a target one: the
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

# gfx2d: no transforms and no color key. The transform only magnifies text,
# which this screen never does (fontMult 1, core/SPEC.md), and nothing is
# drawn with a color key; the blend stays for the additive markers and dust.
# Bit-identical to the full build. PRIVATE: read by src/gfx2d only.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SHAPOGFX2D_TRANSFORM=0
    SHAPOGFX2D_COLOR_KEY=0
)
