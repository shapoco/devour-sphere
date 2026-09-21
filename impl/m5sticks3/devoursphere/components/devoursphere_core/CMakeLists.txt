# The game core (core/) as an ESP-IDF component. The sources are listed
# rather than globbed, like every other front end's build: a glob without
# CONFIGURE_DEPENDS silently drops a new file until someone reconfigures.
set(DS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)

idf_component_register(
    SRCS
        "${DS_ROOT}/core/src/sim/fixed.cpp"
        "${DS_ROOT}/core/src/sim/entities.cpp"
        "${DS_ROOT}/core/src/sim/game.cpp"
        "${DS_ROOT}/core/src/sim/entity.cpp"
        "${DS_ROOT}/core/src/sim/combat.cpp"
        "${DS_ROOT}/core/src/sim/upgrades.cpp"
        "${DS_ROOT}/core/src/render/renderer.cpp"
        "${DS_ROOT}/core/src/render/sphere.cpp"
        "${DS_ROOT}/core/src/render/effects.cpp"
        "${DS_ROOT}/core/src/render/upgrades.cpp"
        "${DS_ROOT}/core/src/render/hud.cpp"
    INCLUDE_DIRS "${DS_ROOT}/core/include"
    REQUIRES shapogfx
)

# sim::TICK_RATE is a constexpr in a header, so every translation unit -- ours,
# the core's and ShapoGFX's -- has to agree on it. 60 Hz to start with: the
# frame here is 56% of the pixels the other ESP32S3 board draws, and there the
# simulation already hid behind the rasterizing at 30. See ../../SPEC.md; the
# on-screen overlay is what decides whether it stays.
idf_build_set_property(COMPILE_DEFINITIONS "DEVOURSPHERE_TICK_RATE=60" APPEND)

# The device is steered by tilting it, so the screen already rolls in the
# player's hand; rolling the camera into the turn as well is one roll too
# many. The body still banks -- that is the simulation's.
idf_build_set_property(COMPILE_DEFINITIONS "DEVOURSPHERE_CAMERA_ROLL=0" APPEND)

# 135 rows means UiMetrics::wireLines asks for 412 segments at most, and the
# array is 10 bytes each. The default 1100 would reserve 11 KB of the
# Renderer for lines that can never be drawn.
idf_build_set_property(COMPILE_DEFINITIONS "DEVOURSPHERE_MAX_WIRE=440" APPEND)
