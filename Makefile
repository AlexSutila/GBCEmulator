CC ?= gcc
CXX ?= g++
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

BUILD_ROOT := build
BIN := bin
BUILD_DIR := $(BUILD_ROOT)
BUILD_TYPE ?= Release

BUILD_FULL     := $(if $(filter full,$(MAKECMDGOALS)),ON,OFF)
BUILD_SIMPLE   := $(if $(filter simple,$(MAKECMDGOALS)),ON,OFF)
BUILD_LIBRETRO := $(if $(filter libretro,$(MAKECMDGOALS)),ON,OFF)

.PHONY: full simple clean libretro clean

full simple libretro: build

build:
	mkdir -p $(BUILD_DIR) $(BIN)
	cmake -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_FULL=$(BUILD_FULL) \
		-DBUILD_SIMPLE=$(BUILD_SIMPLE) \
		-DBUILD_LIBRETRO=$(BUILD_LIBRETRO) \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(abspath $(BIN)) \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(abspath $(BIN)) \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(abspath $(BIN)) \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX)
	cmake --build $(BUILD_DIR) --parallel $(NPROC)

clean:
	rm -rf $(BUILD_ROOT) $(BIN)
