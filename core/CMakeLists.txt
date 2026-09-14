# Devour Sphere core: the simulation (no dependencies) and the renderer
# (depends on ShapoGFX). Both are platform independent.

add_library(devoursphere_sim STATIC
    src/sim/fixed.cpp
    src/sim/entities.cpp
    src/sim/game.cpp
    src/sim/entity.cpp
    src/sim/combat.cpp
)
add_library(devoursphere::sim ALIAS devoursphere_sim)
target_include_directories(devoursphere_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(devoursphere_sim PUBLIC cxx_std_17)
target_compile_options(devoursphere_sim PRIVATE -Wall -Wextra)

add_library(devoursphere_render STATIC
    src/render/renderer.cpp
    src/render/sphere.cpp
    src/render/hud.cpp
)
add_library(devoursphere::render ALIAS devoursphere_render)
target_link_libraries(devoursphere_render PUBLIC devoursphere::sim shapoco::gfx)
target_compile_options(devoursphere_render PRIVATE -Wall -Wextra)
