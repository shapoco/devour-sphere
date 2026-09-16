# Devour Sphere core: the simulation (no dependencies) and the renderer
# (depends on ShapoGFX). Both are platform independent.

set(DEVOURSPHERE_SIM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sim/fixed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sim/entities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sim/game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sim/entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sim/combat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sim/upgrades.cpp
)
set(DEVOURSPHERE_RENDER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/sphere.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/upgrades.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/hud.cpp
)
# core/test builds these again at another tick rate: sim::TICK_RATE is a
# constexpr in a header, so two rates cannot share object files
set(DEVOURSPHERE_CORE_SOURCES
    ${DEVOURSPHERE_SIM_SOURCES} ${DEVOURSPHERE_RENDER_SOURCES} PARENT_SCOPE)

add_library(devoursphere_sim STATIC ${DEVOURSPHERE_SIM_SOURCES})
add_library(devoursphere::sim ALIAS devoursphere_sim)
target_include_directories(devoursphere_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(devoursphere_sim PUBLIC cxx_std_17)
target_compile_options(devoursphere_sim PRIVATE -Wall -Wextra)

add_library(devoursphere_render STATIC ${DEVOURSPHERE_RENDER_SOURCES})
add_library(devoursphere::render ALIAS devoursphere_render)
target_link_libraries(devoursphere_render PUBLIC devoursphere::sim shapoco::gfx)
target_compile_options(devoursphere_render PRIVATE -Wall -Wextra)
