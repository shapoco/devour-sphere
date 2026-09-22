# The ESPboy front end: the frame loop, the ST7735, the MCP23017 buttons,
# the tone player, the flash record. profiler.cpp and high_score_store.hpp
# are the Xiamocon front end's, compiled from that tree against our
# ds_config.hpp / ds_platform.hpp (angle-bracket includes, see profiler.hpp).
set(DS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
set(XMC_APP ${DS_ROOT}/impl/xiamocon/devoursphere)

idf_component_register(
    SRCS
        "app_main.cpp"
        "display.cpp"
        "input.cpp"
        "tone_player.cpp"
        "ds_platform.cpp"
        "${XMC_APP}/src/profiler.cpp"
    INCLUDE_DIRS "." "${XMC_APP}/include"
    REQUIRES
        devoursphere_core
        shapogfx
        esp8266
        freertos
        spi_flash
        # Not used here, but named so that it links: the SDK's NVS (pulled
        # in by its system API) brings libstdc++'s exception objects, whose
        # static guards want pthread_cond_*, and the archive that defines
        # them only resolves that from inside the link group
        pthread
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
# The timing overlay (the right shoulder button on the title or pause
# screen); 0 compiles it out entirely.
target_compile_definitions(${COMPONENT_LIB} PRIVATE DS_PROFILE=1)
