cmake_minimum_required(VERSION 3.15)
project(gbc LANGUAGES CXX)

# Global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.12.0
)
FetchContent_MakeAvailable(pybind11)

# Common sources
set(GBC_CORE_SOURCES
    src/gbc.cpp

    src/cpu/lr35902.cpp
    src/cpu/interrupts.cpp
    src/cpu/instr/alu.cpp
    src/cpu/instr/bitops.cpp
    src/cpu/instr/branch.cpp
    src/cpu/instr/control.cpp
    src/cpu/instr/moves.cpp
    src/cpu/registers/register.cpp
    src/cpu/registers/flags.cpp

    src/memory/boot.cpp
    src/memory/bus.cpp
    src/memory/mmio.cpp

    src/ppu/ppu.cpp
    src/ppu/status.cpp

    src/cart/cart.cpp
    src/cart/cart_display.cpp
    src/cart/mbc/no_mbc.cpp
    src/cart/mbc/mbc1.cpp
    src/cart/mbc/mbc2.cpp
)
set(GBC_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
)
set(GBC_COMPILE_OPTIONS
    -Wall
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

# Emulator executable
add_executable(gbc
    main.cpp
    ${GBC_CORE_SOURCES}
)
target_include_directories(gbc
    PRIVATE ${GBC_INCLUDE_DIR}
)
target_compile_options(gbc
    PRIVATE ${GBC_COMPILE_OPTIONS}
)

# Python bindings
pybind11_add_module(gbc_py
    src/python/bindings.cpp
    ${GBC_CORE_SOURCES}
)
target_include_directories(gbc_py
    PRIVATE ${GBC_INCLUDE_DIR}
)
target_compile_options(gbc_py
    PRIVATE ${GBC_COMPILE_OPTIONS}
)
