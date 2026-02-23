# ===========================================================================
# Dependencies
# ===========================================================================
find_package(SDL3 REQUIRED CONFIG)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.5
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    imgui_file_dialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG v0.6.8
)
FetchContent_MakeAvailable(imgui)
FetchContent_MakeAvailable(imgui_file_dialog)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(json)

# SHA-256: PicoSHA2 (header-only)
FetchContent_Declare(
    picosha2
    GIT_REPOSITORY https://github.com/okdshin/PicoSHA2.git
    GIT_TAG 27fcf6979298949e8a462e16d09a0351c18fcaf2
)
FetchContent_MakeAvailable(picosha2)
target_include_directories(picosha2 INTERFACE ${picosha2_src_SOURCE_DIR})

# --- ROM I/O deps ---
# ZIP: miniz
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.1.1
)
FetchContent_MakeAvailable(miniz)
if(NOT TARGET miniz)
    add_library(miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
    target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})
endif ()

# HTTPS downloads: libcurl + mbedTLS/Schannel (should be buildable on Windows/Linux/macOS)
if(NOT WIN32)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG v4.0.0
    )
    FetchContent_MakeAvailable(mbedtls)

    # Provide what FindMbedTLS.cmake expects, using the in-tree MbedTLS build/targets
    set(MBEDTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
    set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
    set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
endif ()

set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
if(WIN32)
    set(CURL_USE_SCHANNEL ON  CACHE BOOL "" FORCE)
    set(CURL_USE_MBEDTLS  OFF CACHE BOOL "" FORCE)
else()
    set(CURL_USE_MBEDTLS  ON  CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_18_0
)
FetchContent_MakeAvailable(curl)
if(TARGET libcurl AND NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl ALIAS libcurl)
endif ()

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
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/frontend_rom_io.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/frontend_save.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/sdl_host.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/main.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/sdl3/gui.cpp
    ${GBC_CORE_SOURCES}
)

target_link_libraries(gbc_full
    PRIVATE SDL3::SDL3
    PRIVATE imgui
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE miniz
    PRIVATE picosha2
    PRIVATE CURL::libcurl
)
target_include_directories(gbc_full
    PRIVATE ${GBC_INCLUDE_DIR}
)
target_compile_options(gbc_full
    PRIVATE ${GBC_COMPILE_OPTIONS}
)
set_target_properties(gbc_full PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
