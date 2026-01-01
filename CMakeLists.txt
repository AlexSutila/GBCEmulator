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
find_package(SDL3 REQUIRED)

# Common sources
set(GBC_CORE_SOURCES
    src/frontend/renderer.cpp
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

    src/memory/bus.cpp
    src/memory/mmio/dmg.cpp
    src/memory/mmio/cgb.cpp
    src/memory/mmio/mmio.cpp

    src/timer/timer.cpp
    src/ppu/ppu.cpp
    src/ppu/attributes.cpp
    src/ppu/palette.cpp
    src/ppu/fetcher.cpp
    src/ppu/fifo.cpp

    src/cart/cart.cpp
    src/cart/cart_display.cpp
    src/cart/mbc/no_mbc.cpp
    src/cart/mbc/mbc1.cpp
    src/cart/mbc/mbc2.cpp
    src/cart/mbc/mbc3.cpp
    src/cart/mbc/mbc5.cpp
    src/cart/mbc/test_mbc.cpp
    src/cart/mbc/mbc_factory.cpp
)
set(GBC_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
)
set(GBC_COMPILE_OPTIONS
    -Wall
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-Ofast>
)

# Emulator executable
add_executable(gbc
    main.cpp
    ${GBC_CORE_SOURCES}
)
target_link_libraries(gbc
    PRIVATE SDL3::SDL3
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
target_link_libraries(gbc_py
    PRIVATE SDL3::SDL3
)
target_include_directories(gbc_py
    PRIVATE ${GBC_INCLUDE_DIR}
)
target_compile_options(gbc_py
    PRIVATE ${GBC_COMPILE_OPTIONS}
)
