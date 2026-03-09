# ===========================================================================
# Dependencies
# ===========================================================================

find_package(SDL3 REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

# We are trying to avoid usage of git submodules
include(FetchContent)


# ===========================================================================
# Vendored header-only deps (bundling since these deps are small)
# ===========================================================================

FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.1.1
)
FetchContent_MakeAvailable(miniz)

if(NOT TARGET miniz)
    add_library(miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
    target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})
endif()

FetchContent_Declare(
    picosha2 # I do not believe this is vendored
    GIT_REPOSITORY https://github.com/okdshin/PicoSHA2.git
    GIT_TAG 27fcf6979298949e8a462e16d09a0351c18fcaf2
)
FetchContent_MakeAvailable(picosha2)


# ===========================================================================
# ImGui + ImGuiFileDialog
# ===========================================================================

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.6
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    imgui_file_dialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG v0.6.8
)
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


# ===========================================================================
# Build target
# ===========================================================================

add_executable(gbc_full
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/debugger.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/frontend.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/frontend_emulation.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/frontend_input.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/frontend_render.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/frontend_rom_io.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/frontend_save.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/sdl_host.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/main.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/gui.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/gui_context.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/gui_main.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/gui_tools.cpp
    ${GBC_CORE_SOURCES}
)

target_link_libraries(gbc_full
    PRIVATE
        SDL3::SDL3
        imgui
        miniz
        picosha2
        nlohmann_json::nlohmann_json
        CURL::libcurl
)

target_include_directories(gbc_full
    PRIVATE
        ${GBC_INCLUDE_DIR}
        ${PICOSHA2_INCLUDE_DIR}
)

target_compile_options(gbc_full
    PRIVATE ${GBC_COMPILE_OPTIONS}
)

set_target_properties(gbc_full PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

install(TARGETS gbc_full
        RUNTIME_DEPENDENCIES
        PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-"
        POST_EXCLUDE_REGEXES ".*/System32/.*\\.dll"
        RUNTIME DESTINATION bin
)
