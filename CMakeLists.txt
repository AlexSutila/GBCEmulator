cmake_minimum_required(VERSION 3.15)
project(gbc LANGUAGES CXX)

# Global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)

# Dependencies
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.12.0
)
FetchContent_MakeAvailable(pybind11)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.0
)
FetchContent_MakeAvailable(SDL3)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.5
)
FetchContent_MakeAvailable(imgui)
FetchContent_Declare(
    imgui_file_dialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG        v0.6.8
)
FetchContent_MakeAvailable(imgui)
FetchContent_MakeAvailable(imgui_file_dialog)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_file_dialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui
    PUBLIC ${imgui_SOURCE_DIR}
    PUBLIC ${imgui_SOURCE_DIR}/backends
    PUBLIC ${imgui_SOURCE_DIR}/misc/cpp
    PUBLIC ${imgui_file_dialog_SOURCE_DIR}
)
target_link_libraries(imgui PRIVATE SDL3::SDL3)

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

    src/memory/bus.cpp
    src/memory/boot.cpp
    src/memory/mmio/dmg.cpp
    src/memory/mmio/cgb.cpp
    src/memory/mmio/mmio.cpp
    src/memory/dma.cpp

    src/timer/timer.cpp
    src/ppu/ppu.cpp
    src/ppu/attributes.cpp
    src/ppu/palette.cpp
    src/ppu/fetcher.cpp
    src/ppu/fifo.cpp
    src/ppu/sprites.cpp

    src/cart/cart.cpp
    src/cart/cart_display.cpp
    src/cart/mbc/no_mbc.cpp
    src/cart/mbc/mbc1.cpp
    src/cart/mbc/mbc2.cpp
    src/cart/mbc/mbc3.cpp
    src/cart/mbc/mbc5.cpp
    src/cart/mbc/test_mbc.cpp
    src/cart/mbc/mbc_factory.cpp
        src/memory/mmio/joypad.cpp
        src/apu/apu.cpp
)
set(GBC_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
)

set(GBC_RELEASE_OPTIONS
    -Ofast
    -fvisibility=hidden
)

set(GBC_COMPILE_OPTIONS
    -Wall
    $<$<CONFIG:Debug>:-g;-O0>
    $<$<CONFIG:Release>:${GBC_RELEASE_OPTIONS}>
)

if (MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND GBC_COMPILE_OPTIONS -Wa,-mbig-obj)
endif()

# Emulator executable
add_executable(gbc
    src/frontend/sdl3_frontend.cpp
    main.cpp
    ${GBC_CORE_SOURCES}
)
target_link_libraries(gbc
    PRIVATE SDL3::SDL3
    PRIVATE imgui
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
    PRIVATE imgui
)
target_include_directories(gbc_py
    PRIVATE ${GBC_INCLUDE_DIR}
)
target_compile_options(gbc_py
    PRIVATE ${GBC_COMPILE_OPTIONS}
)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_err LANGUAGES CXX)

if(ipo_ok AND NOT MINGW)
  set_property(TARGET imgui  PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  set_property(TARGET gbc    PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  set_property(TARGET gbc_py PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
  message(WARNING "IPO/LTO not supported: ${ipo_err}")
endif()
