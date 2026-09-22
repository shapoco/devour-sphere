# The game core (core/) as an RTOS SDK component, built in the ESPboy's
# reduced configuration. The sources are listed rather than globbed, like
# every other front end's build. Every constant below is a compile-time
# override the core offers (DEVOURSPHERE_*, see core/SPEC.md); the reasoning
# is in ../../../SPEC.md "縮小構成".
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

# Build properties, not target ones: these constants live in headers that
# every translation unit -- the core's, ShapoGFX's and main/ -- reads.
# (This SDK passes the property verbatim, hence the -D.)
set(DS_CORE_DEFS
    # 30 Hz: one core, no FPU, no divide; a tick costs about twice what it
    # does on the RP2040 and has to share the core with the frame
    DEVOURSPHERE_TICK_RATE=30

    # --- The simulation (sim/config.hpp) ---
    # A 256 FU sphere (a quarter of the 512 FU surface) with 48 enemies
    # keeps 96% of the reference density on a quarter of the entities. The
    # entity loop is 80% of a tick, and 200 entities would be a 31 ms tick
    # here -- the CPU, not the RAM, is what sets this number.
    DEVOURSPHERE_SPHERE_RADIUS_SHIFT=16
    DEVOURSPHERE_INITIAL_ENTITIES=48
    DEVOURSPHERE_MAX_ENTITIES=64        # 48 + the player + a respawn pack (4)
    DEVOURSPHERE_INITIAL_FOOD_FRAGMENTS=80
    DEVOURSPHERE_FOOD_TARGET=100
    DEVOURSPHERE_MAX_FLOATING_FRAGMENTS=128
    DEVOURSPHERE_MAX_BULLETS=32
    # A full fragment array drops its smallest, not its oldest
    DEVOURSPHERE_FRAGMENT_KEEP_LARGEST=1

    # --- The renderer (render/renderer.hpp) ---
    # Faces of the sphere's mesh 48 px across on a 128 px screen (the
    # PicoSystem's value on 240 px). The first device reading (32 px, 45
    # lines) spent 22 ms a frame walking the mesh -- a third of the frame
    # -- and the user asked for one step coarser. 10 bytes a segment.
    DEVOURSPHERE_SPHERE_TARGET_PX=48
    DEVOURSPHERE_MAX_WIRE=160
    # The 2D line and point arrays (outlines, effects, far fragments); what
    # does not fit takes the 3D path. 14 and 8 bytes an entry.
    DEVOURSPHERE_MAX_LINES2D=96
    DEVOURSPHERE_MAX_POINTS2D=48
    # No blending anywhere (the PicoSystem's choice too: a blended pixel
    # reads the frame back) and no depth in the world layer either: the
    # records lose their depth plane and the spans their depth compare
    DEVOURSPHERE_SUPPRESS_ALPHA=1
    DEVOURSPHERE_NO_DEPTH=1
    # The effects and the overlay, sized for 64 entities on 128 px
    DEVOURSPHERE_MAX_DEBRIS=16
    DEVOURSPHERE_MAX_DUST=16
    DEVOURSPHERE_MAX_GAUGES=16
    DEVOURSPHERE_MAX_MARKERS=16
    DEVOURSPHERE_MAX_ENEMY_MARKERS=12
    DEVOURSPHERE_MAX_STARS=48
)
foreach(def ${DS_CORE_DEFS})
    idf_build_set_property(COMPILE_DEFINITIONS "-D${def}" APPEND)
endforeach()
# The core's innermost loops are NOT placed in IRAM here, although the
# hook exists (DEVOURSPHERE_HOT_ATTR, core/SPEC.md): the third device
# reading tried it with the wireframe DDA, the 2D line pass, the mesh walk,
# normalize and the square roots (7.6 KB at -Os) and neither the mesh walk
# (S 12.5 -> 12.1 ms) nor the 2D pass (U 15 -> 16 ms) moved -- with the
# 32 KB cache they are compute bound, not fetch bound -- and at -O2 they
# would not fit next to the SDK's 20 KB and ShapoGFX's span loops anyway.
#   idf_build_set_property(COMPILE_DEFINITIONS
#       "-DDEVOURSPHERE_HOT_ATTR=__attribute__((section(\"\.iram1.ds\")))" APPEND)

# -O2 over the SDK's -Os (the later flag wins): with the 32 KB cache the
# code is no longer starved, and every phase left is compute bound
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -O2)
