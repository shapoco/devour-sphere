# Devour Sphere in a terminal. `make` builds build/devoursphere through cmake;
# `make run` starts it. See SPEC.md.

BUILD_DIR ?= build
BUILD_TYPE ?= Release
# Through the shell: GNU make's own PATH search stops at a *directory*
# named cmake (Emscripten's SDK has one on the PATH), `command -v` does not
CMAKE ?= $(shell command -v cmake 2>/dev/null || echo cmake)

.PHONY: all run clean

all: $(BUILD_DIR)/devoursphere

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

$(BUILD_DIR)/devoursphere: $(BUILD_DIR)/CMakeCache.txt $(wildcard src/*) FORCE
	$(CMAKE) --build $(BUILD_DIR)

run: all
	./$(BUILD_DIR)/devoursphere $(ARGS)

clean:
	rm -rf $(BUILD_DIR)

FORCE:
