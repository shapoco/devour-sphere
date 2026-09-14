# WASM build of Devour Sphere (requires Emscripten).
# Output: docs/play/devoursphere.wasm (served as a static page).

EMCC ?= emcc

REPO_ROOT := ../..
GFX_ROOT := $(REPO_ROOT)/submodule/shapo-gfx
OUT_DIR := $(REPO_ROOT)/docs/play
OUT := $(OUT_DIR)/devoursphere.wasm

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

EXPORTS := _ds_init,_ds_debug_start,_ds_tick,_ds_render,_ds_get_fb,_ds_get_width,_ds_get_height,_ds_get_state,_ds_get_tick_rate

.PHONY: all clean serve

all: $(OUT)

$(OUT): $(SRCS) $(HDRS) Makefile
	$(EMCC) $(CXXFLAGS) $(SRCS) --no-entry -sSTANDALONE_WASM=1 \
		-sEXPORTED_FUNCTIONS=$(EXPORTS) -sALLOW_MEMORY_GROWTH=1 \
		-o $@

clean:
	rm -f $(OUT)

# Serve docs/ and open http://localhost:52980/play/
serve:
	$(REPO_ROOT)/launch_web_server.sh
