cmake_minimum_required(VERSION 3.21)
set(CMAKE_POLICY_VERSION_MINIMUM 3.21)
project(gbc LANGUAGES CXX)


# ===========================================================================
# Global Settings
# ===========================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(BUILD_FULL       "ImGui + SDL3 frontend with debugger"       OFF)
option(BUILD_SIMPLE     "Raylib + WASM frontend (dekstop/web)"      OFF)
option(BUILD_LIBRETRO   "Libretro core for retroarch compatability" OFF)
option(BUILD_PYTHON     "Python bindings to emulator core"          OFF)


# ===========================================================================
#  Emulator core source files - Common and needed across ALL build targets
# ===========================================================================
set(GBC_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/timer.cpp
    ${CMAKE_SOURCE_DIR}/src/serial.cpp
    ${CMAKE_SOURCE_DIR}/src/gbc.cpp

    ${CMAKE_SOURCE_DIR}/src/cpu/lr35902.cpp
    ${CMAKE_SOURCE_DIR}/src/cpu/interrupts.cpp
    ${CMAKE_SOURCE_DIR}/src/cpu/instr/alu.cpp
    ${CMAKE_SOURCE_DIR}/src/cpu/instr/bitops.cpp
    ${CMAKE_SOURCE_DIR}/src/cpu/instr/branch.cpp
    ${CMAKE_SOURCE_DIR}/src/cpu/instr/control.cpp
    ${CMAKE_SOURCE_DIR}/src/cpu/instr/moves.cpp
    ${CMAKE_SOURCE_DIR}/src/cpu/registers/register.cpp
    ${CMAKE_SOURCE_DIR}/src/cpu/registers/flags.cpp

    ${CMAKE_SOURCE_DIR}/src/memory/bus.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/boot.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/mmio/joypad.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/mmio/dmg.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/mmio/cgb.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/mmio/mmio.cpp
    ${CMAKE_SOURCE_DIR}/src/memory/dma.cpp

    ${CMAKE_SOURCE_DIR}/src/ppu/ppu.cpp
    ${CMAKE_SOURCE_DIR}/src/ppu/attributes.cpp
    ${CMAKE_SOURCE_DIR}/src/ppu/palette.cpp
    ${CMAKE_SOURCE_DIR}/src/ppu/fetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/ppu/fifo.cpp
    ${CMAKE_SOURCE_DIR}/src/ppu/sprites.cpp

    ${CMAKE_SOURCE_DIR}/src/apu/apu_core.cpp
    ${CMAKE_SOURCE_DIR}/src/apu/apu_mmio.cpp
    ${CMAKE_SOURCE_DIR}/src/apu/apu_channels.cpp

    ${CMAKE_SOURCE_DIR}/src/cart/cart.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/no_mbc.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/mbc1.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/mbc2.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/mbc3.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/mbc5.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/mbc6.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/mbc7.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/mmm01.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/m161.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/huc1.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/huc3.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/tama5.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/wisdom_tree.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/ems.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/test_mbc.cpp
    ${CMAKE_SOURCE_DIR}/src/cart/mbc/mbc_factory.cpp

    ${CMAKE_SOURCE_DIR}/src/debugger/breakpoint.cpp
    ${CMAKE_SOURCE_DIR}/src/debugger/debugger.cpp
    ${CMAKE_SOURCE_DIR}/src/debugger/print.cpp
)

set(GBC_RELEASE_OPTIONS
    -O3
    -fvisibility=hidden
)
set(GBC_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
)
set(GBC_COMPILE_OPTIONS
    -Wall
    $<$<CONFIG:Debug>:-g;-O0>
    $<$<CONFIG:Release>:${GBC_RELEASE_OPTIONS}>
)

# MinGW+GCC cope with large object sizes
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND GBC_COMPILE_OPTIONS -Wa,-mbig-obj)
endif()


# ===========================================================================
# Frontend build targets
# ===========================================================================
if(BUILD_FULL)
    add_subdirectory(src/frontend/sdl3)
endif()

if(BUILD_SIMPLE)
    add_subdirectory(src/frontend/raylib)
endif()

if(BUILD_LIBRETRO)
    add_subdirectory(src/frontend/libretro)
endif()

if(BUILD_PYTHON)
    add_subdirectory(src/frontend/python)
endif()


# ===========================================================================
# Enable IPO/LTO if the compiler and the platform supports it
# --------------------------------------------------------------------------
# Note: only platform combo where it fails is MinGW + GCC, they got autistic
# bugs with LTO when certain flags are set
# ===========================================================================
if(NOT EMSCRIPTEN)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_err LANGUAGES CXX)
    message("LTO: compiler: ${CMAKE_CXX_COMPILER_ID}, IPO/LTO support: ${ipo_ok}, mingw: ${MINGW}")

    set(mingw_gcc OFF)
    if(MINGW AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        set(mingw_gcc ON)
        set(ipo_err "MinGW + GCC combo is known to have issues with LTO")
    endif()

    if(ipo_ok AND NOT mingw_gcc)
        if(BUILD_FULL)
            set_property(TARGET imgui
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            )
            set_property(TARGET gbc_full
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            )
        endif() # BUILD_FULL

        if(BUILD_SIMPLE)
            set_property(TARGET gbc_simple
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            )
        endif() # BUILD_SIMPLE

        if(BUILD_LIBRETRO)
            set_property(TARGET gbc_libretro
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            )
        endif() # BUILD_SIMPLE

        if(BUILD_PYTHON)
            set_property(TARGET gbc_py
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            )
        endif() # BUILD_PYTHON
    else()
        message(WARNING "IPO/LTO not supported: ${ipo_err}")
    endif()
endif() # NOT EMSCRIPTEN
