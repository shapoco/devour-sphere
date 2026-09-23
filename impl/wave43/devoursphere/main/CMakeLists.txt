set(DS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
# profiler.cpp, se_player.hpp and high_score_store.hpp are shared with the
# Xiamocon and PicoSystem front ends (that directory is on the include path
# below, and profiler.cpp takes its ds_config.hpp / ds_platform.hpp with
# angle brackets so it picks up ours).
set(XMC_APP ${DS_ROOT}/impl/xiamocon/devoursphere)

idf_component_register(
    SRCS
        "app_main.cpp"
        "board.cpp"
        "panel_st7701.c"
        "panel_out.cpp"
        "touch_pad.cpp"
        "ds_platform.cpp"
        "se_player_none.cpp"
        "${XMC_APP}/src/profiler.cpp"
    INCLUDE_DIRS "." "${XMC_APP}/include"
    REQUIRES
        devoursphere_core
        shapogfx
        esp_driver_ppa
        esp_driver_i2c
        esp_driver_ledc
        esp_driver_gpio
        esp_lcd
        esp_hw_support    # esp_ldo_regulator
        esp_mm            # esp_cache_msync
        esp_timer
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
# The FUNC-less timing overlay (UP on the title or pause screen); 0 compiles
# it out entirely.
target_compile_definitions(${COMPONENT_LIB} PRIVATE DS_PROFILE=1)
