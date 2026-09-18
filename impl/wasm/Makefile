# WASM build of Devour Sphere (requires Emscripten).
# Output: docs/play/devoursphere.wasm (served as a static page).

EMCC ?= emcc

REPO_ROOT := ../..
GFX_ROOT := $(REPO_ROOT)/submodule/shapo-gfx
OUT_DIR := $(REPO_ROOT)/docs/play
OUT := $(OUT_DIR)/devoursphere.wasm
# Sound effects: assets/se/*.wav packed into one PCM file (needs ffmpeg)
SE_DIR := $(REPO_ROOT)/assets/se
SE_PACK := $(REPO_ROOT)/core/tools/pack_se.py
SE_OUT := $(OUT_DIR)/se.bin

SRCS := main.cpp \
        $(wildcard $(REPO_ROOT)/core/src/sim/*.cpp) \
        $(wildcard $(REPO_ROOT)/core/src/render/*.cpp) \
        $(wildcard $(GFX_ROOT)/src/gfx2d/*.cpp) \
        $(wildcard $(GFX_ROOT)/src/gfx3d/*.cpp)
HDRS := $(wildcard $(REPO_ROOT)/core/include/devoursphere/*.hpp) \
        $(wildcard $(REPO_ROOT)/core/include/devoursphere/*/*.hpp) \
        $(wildcard $(GFX_ROOT)/include/shapoco/*/*.hpp)

CXXFLAGS := -O2 -std=c++17 -Wall -Wextra \
            -I $(REPO_ROOT)/core/include -I $(GFX_ROOT)/include

EXPORTS := _ds_init,_ds_set_screen,_ds_get_max_pixels,_ds_debug_start,_ds_debug_auto,_ds_tick,_ds_render,_ds_get_fb,_ds_get_width,_ds_get_height,_ds_get_state,_ds_get_tick_rate,_ds_get_score,_ds_set_high_score,_ds_set_debug,_ds_debug_key,_ds_get_sounds,_ds_set_muted,_ds_get_muted,_ds_get_paused

.PHONY: all clean serve se

all: $(OUT) $(SE_OUT)

se: $(SE_OUT)

$(SE_OUT): $(wildcard $(SE_DIR)/*.wav) $(SE_PACK)
	python3 $(SE_PACK) -r 24000 -o $@ $(SE_DIR)

$(OUT): $(SRCS) $(HDRS) Makefile
	$(EMCC) $(CXXFLAGS) $(SRCS) --no-entry -sSTANDALONE_WASM=1 \
		-sEXPORTED_FUNCTIONS=$(EXPORTS) -sALLOW_MEMORY_GROWTH=1 \
		-o $@

clean:
	rm -f $(OUT) $(SE_OUT)

# Serve docs/ and open http://localhost:52980/play/
serve:
	$(REPO_ROOT)/launch_web_server.sh
