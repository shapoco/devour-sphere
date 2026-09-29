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
        "${DS_ROOT}/core/src/render/benchmark.cpp"
    INCLUDE_DIRS "${DS_ROOT}/core/include"
    REQUIRES shapogfx
)

# sim::TICK_RATE is a constexpr in a header, so every translation unit --
# ours, the core's and ShapoGFX's -- has to agree on it. 60 Hz: the
# simulation runs on the core that is not rasterizing, and a tick here costs
# a few milliseconds against a frame of several tens.
idf_build_set_property(COMPILE_DEFINITIONS "DEVOURSPHERE_TICK_RATE=60" APPEND)
