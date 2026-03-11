CC ?= gcc
CXX ?= g++
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

BUILD_ROOT := build
BIN := bin

FULL_BUILD := $(BUILD_ROOT)/full
SIMPLE_BUILD := $(BUILD_ROOT)/simple

BOOTROMS_DIR ?= bootroms
WARNINGS ?=

.PHONY: all full simple clean
all:
	$(MAKE) -f Makefile.libretro


$(FULL_BUILD)/CMakeCache.txt:
	mkdir -p $(FULL_BUILD) $(BIN)
	cmake -S . -B $(FULL_BUILD) \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_FULL=ON \
		-DBUILD_SIMPLE=OFF \
		-DBUILD_LIBRETRO=OFF \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(abspath $(BIN)) \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX)

$(SIMPLE_BUILD)/CMakeCache.txt:
	mkdir -p $(SIMPLE_BUILD) $(BIN)
	cmake -S . -B $(SIMPLE_BUILD) \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_FULL=OFF \
		-DBUILD_SIMPLE=ON \
		-DBUILD_LIBRETRO=OFF \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(abspath $(BIN)) \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX)


simple: $(SIMPLE_BUILD)/CMakeCache.txt
	cmake --build $(SIMPLE_BUILD) -- -j$(NPROC)

full: $(FULL_BUILD)/CMakeCache.txt
	cmake --build $(FULL_BUILD) -- -j$(NPROC)

clean:
	rm -rf $(BUILD_ROOT) $(BIN)
