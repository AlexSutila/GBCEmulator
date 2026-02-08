cmake_minimum_required(VERSION 3.15)
# Silence old subprojects (raylib) that assume pre-3.5 policies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(gbc LANGUAGES CXX)

# Global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
include(FetchContent)

# Non-WASM build dependencies
if(NOT EMSCRIPTEN)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG         v3.0.1
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

    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.12.0
    )
    FetchContent_MakeAvailable(json)

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

else()
    # Compiles raylib for Web platform
    set(PLATFORM Web CACHE STRING "" FORCE)
endif()

# WASM and Non-WASM common build dependencies
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
)
FetchContent_MakeAvailable(raylib)


# ===========================================================================
#  Emulator core source files - Common and needed across ALL build targets
# ===========================================================================

set(GBC_CORE_SOURCES
    src/timer.cpp
    src/serial.cpp
    src/apu.cpp
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
    src/memory/mmio/joypad.cpp
    src/memory/mmio/dmg.cpp
    src/memory/mmio/cgb.cpp
    src/memory/mmio/mmio.cpp
    src/memory/dma.cpp

    src/ppu/ppu.cpp
    src/ppu/attributes.cpp
    src/ppu/palette.cpp
    src/ppu/fetcher.cpp
    src/ppu/fifo.cpp
    src/ppu/sprites.cpp

    src/cart/cart.cpp
    src/cart/mbc/no_mbc.cpp
    src/cart/mbc/mbc1.cpp
    src/cart/mbc/mbc2.cpp
    src/cart/mbc/mbc3.cpp
    src/cart/mbc/mbc5.cpp
    src/cart/mbc/mbc6.cpp
    src/cart/mbc/mbc7.cpp
    src/cart/mbc/mmm01.cpp
    src/cart/mbc/m161.cpp
    src/cart/mbc/huc1.cpp
    src/cart/mbc/huc3.cpp
    src/cart/mbc/tama5.cpp
    src/cart/mbc/wisdom_tree.cpp
    src/cart/mbc/ems.cpp
    src/cart/mbc/test_mbc.cpp
    src/cart/mbc/mbc_factory.cpp

    src/debugger/breakpoint.cpp
    src/debugger/debugger.cpp
    src/debugger/print.cpp
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

# Mingw cope with large object sizes
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND GBC_COMPILE_OPTIONS -Wa,-mbig-obj)
endif()


# ===========================================================================
#  WASM (Raylib) build target
# ===========================================================================
if(EMSCRIPTEN)
    # Raylib simplistic WASM frontend
    add_executable(gbc_wasm
        src/frontend/raylib/frontend.cpp
        src/frontend/raylib/main.cpp
        ${GBC_CORE_SOURCES}
    )
    target_link_libraries(gbc_wasm
        PRIVATE raylib
    )
    target_include_directories(gbc_wasm
        PRIVATE ${GBC_INCLUDE_DIR}
    )

    # If we ever have to debug the core, we will certainly not be doing so
    # using this build target. Hence, we go pretty heavy with compiler flags
    # here because performance on weaker machines tends to suffer.
    target_link_options(gbc_wasm
        PRIVATE
            -sUSE_GLFW=3 # Need this or else linker errors
            -sINITIAL_MEMORY=134217728
            -sTOTAL_STACK=67108864
            -sBINARYEN_ASYNC_COMPILATION=0
            -sMIN_WEBGL_VERSION=2
            -sMAX_WEBGL_VERSION=2
            -sUSE_PTHREADS=0
            -sEXPORTED_FUNCTIONS=_emscripten_start,_malloc,_free
            -sSTACK_OVERFLOW_CHECK=0
            -sFORCE_FILESYSTEM=1
            -sSAFE_HEAP=0
            -fno-rtti
            -flto
            -O3
            --shell-file ${CMAKE_SOURCE_DIR}/src/frontend/raylib/shell.html
    )
    target_compile_options(gbc_wasm
        PRIVATE
            ${GBC_COMPILE_OPTIONS}
            -sUSE_WEBGL2=1
            -flto
            -O3
    )
    set_target_properties(gbc_wasm PROPERTIES SUFFIX ".html")

# ===========================================================================
#  Non=WASM build target (including Raylib for desktop)
# ===========================================================================
else()

    # Full SDL3+ImGui feature set executable
    add_executable(gbc_full
        src/frontend/sdl3/debugger.cpp
        src/frontend/sdl3/frontend.cpp
        src/frontend/sdl3/sdl_host.cpp
        src/frontend/sdl3/main.cpp
        src/frontend/sdl3/gui.cpp
        ${GBC_CORE_SOURCES}
    )
    target_link_libraries(gbc_full
        PRIVATE SDL3::SDL3
        PRIVATE imgui
        PRIVATE nlohmann_json::nlohmann_json
    )
    target_include_directories(gbc_full
        PRIVATE ${GBC_INCLUDE_DIR}
    )
    target_compile_options(gbc_full
        PRIVATE ${GBC_COMPILE_OPTIONS}
    )

    # Simplified Raylib executable
    add_executable(gbc_simple
        src/frontend/raylib/frontend.cpp
        src/frontend/raylib/main.cpp
        ${GBC_CORE_SOURCES}
    )
    target_link_libraries(gbc_simple
        PRIVATE raylib
    )
    target_include_directories(gbc_simple
        PRIVATE ${GBC_INCLUDE_DIR}
    )
    target_compile_options(gbc_simple
        PRIVATE ${GBC_COMPILE_OPTIONS}
    )

    # Python bindings
    pybind11_add_module(gbc_py
        src/frontend/python/bindings.cpp
        src/frontend/python/frontend.cpp
        src/frontend/python/wrappers.cpp
        ${GBC_CORE_SOURCES}
    )
    target_include_directories(gbc_py
        PRIVATE ${GBC_INCLUDE_DIR}
    )
    target_compile_options(gbc_py
        PRIVATE ${GBC_COMPILE_OPTIONS}
    )
    ## Windows fix
    target_link_options(gbc_py PRIVATE
        -static-libgcc
        -static-libstdc++
        -lpthread
    )

    # TODO: Why do we need this? Is it also an mingw large objfile size cope?
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_err LANGUAGES CXX)
    if(ipo_ok AND NOT MINGW)
       set_property(TARGET imgui      PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
       set_property(TARGET gbc_full   PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
       set_property(TARGET gbc_simple PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
       set_property(TARGET gbc_py     PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
       message(WARNING "IPO/LTO not supported: ${ipo_err}")
    endif()
endif()
