# ShapoGFX (submodule/shapo-gfx/) as an RTOS SDK component, in its
# smallest configuration: RGB565BE only, no texture, no blending, flat
# shading, a small vertex cache, and the span loops in IRAM.
set(DS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)

idf_component_register(
    SRCS
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx2d/graphics2d.cpp"
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx3d/gfx3d.cpp"
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx3d/scene.cpp"
        "${DS_ROOT}/submodule/shapo-gfx/src/gfx3d/shapes.cpp"
    INCLUDE_DIRS "${DS_ROOT}/submodule/shapo-gfx/include"
)

# Only RGB565BE is ever drawn, which is also the byte order the ST7735
# takes on the wire (display.cpp streams the band unconverted). Build
# properties: the format macros are read by the headers as well.
idf_build_set_property(COMPILE_DEFINITIONS "-DSHAPOGFX_FORMAT_GRAY1=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DSHAPOGFX_FORMAT_RGB444=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DSHAPOGFX_FORMAT_ARGB4444=0" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DSHAPOGFX_FORMAT_RGB565BE=1" APPEND)

# Read by src/gfx3d only (PRIVATE):
#   TEXTURE=0      nothing here is textured (every front end)
#   GOURAUD=0      flat shading: a primitive takes its first vertex's color.
#                  The bodies were flat already; the auras' fans and the
#                  health warning lose their gradient. 16 bytes off a span,
#                  36 off a smoothly shaded record, and the interpolation
#                  out of the span loop.
#   BLEND=0        everything opaque (the core draws nothing translucent
#                  with DEVOURSPHERE_SUPPRESS_ALPHA either); the blend
#                  code leaves the rasterizer
#   FIXED_POINT=1  no FPU: the vertex stage in integers, as on the RP2040
#   VCACHE_SIZE=16 the fixed part of the arena: 64 entries were 2.8 KB
#   LAYER_MAX=4    the scene has two layers
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SHAPOGFX3D_TEXTURE=0
    SHAPOGFX3D_GOURAUD=0
    SHAPOGFX3D_BLEND=0
    SHAPOGFX3D_FIXED_POINT=1
    SHAPOGFX3D_VCACHE_SIZE=16
    SHAPOGFX3D_LAYER_MAX=4
)
# The span loops (render() and what it calls per span) run from IRAM: the
# SDK's linker fragments put .iram1.* there (esp8266_fragments.lf). The
# rest of the library stays in flash, behind the 16 KB cache.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "SHAPOGFX3D_HOT_ATTR=__attribute__((section(\".iram1.gfx3d\")))"
    SHAPOGFX3D_HOT_INSTANTIATE=1
)
