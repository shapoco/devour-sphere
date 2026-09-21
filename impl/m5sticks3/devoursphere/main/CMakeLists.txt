set(DS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
# profiler.cpp, se_player.hpp and high_score_store.hpp are shared with the
# Xiamocon and PicoSystem front ends (that directory is on the include path
# below, and profiler.cpp takes its ds_config.hpp / ds_platform.hpp with
# angle brackets so it picks up ours).
set(XMC_APP ${DS_ROOT}/impl/xiamocon/devoursphere)

idf_component_register(
    SRCS
        "app_main.cpp"
        "panel_out.cpp"
        "attitude.cpp"
        "ds_platform.cpp"
        "se_player_s3.cpp"
        "${XMC_APP}/src/profiler.cpp"
    INCLUDE_DIRS "." "${XMC_APP}/include"
    REQUIRES
        devoursphere_core
        shapogfx
        m5unified
        m5gfx
        esp_timer
        nvs_flash
        driver
)

# The sound pack: the browser's own (signed 16-bit PCM at 24 kHz), which
# every other front end embeds too. Committed, so nothing has to run here --
# but if the material changes, `make -C impl/wasm se` rebuilds it first. The
# symbols come out as _binary_se_bin_start / _end.
target_add_binary_data(${COMPONENT_LIB} "${DS_ROOT}/docs/play/se.bin" BINARY)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
# The timing overlay (both buttons together on the title or pause screen);
# 0 compiles it out entirely.
target_compile_definitions(${COMPONENT_LIB} PRIVATE DS_PROFILE=1)
