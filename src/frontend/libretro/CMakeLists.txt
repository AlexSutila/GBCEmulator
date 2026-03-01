# ===========================================================================
# Dependencies
# ===========================================================================
project(gbc LANGUAGES C) # Allow C since API is C-based
include(FetchContent)

FetchContent_Declare(
    libretro_api
    GIT_REPOSITORY https://github.com/libretro/libretro-common
    GIT_TAG master
)
FetchContent_MakeAvailable(libretro_api)
add_library(libretro_api INTERFACE)

target_include_directories(libretro_api
    INTERFACE
        ${libretro_api_SOURCE_DIR}/include
)


# ===========================================================================
# Core build
# ===========================================================================

add_library(gbc_libretro SHARED
    ${CMAKE_SOURCE_DIR}/src/frontend/libretro/libretro_host.c
    ${CMAKE_SOURCE_DIR}/src/frontend/libretro/frontend.cpp
    ${GBC_CORE_SOURCES}
)

set_target_properties(gbc_libretro PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_compile_options(gbc_libretro
    PRIVATE ${GBC_COMPILE_OPTIONS}
)

target_include_directories(gbc_libretro
    PRIVATE ${GBC_INCLUDE_DIR}
)

target_link_libraries(gbc_libretro
    PRIVATE libretro_api
)

target_include_directories(gbc_libretro
    PRIVATE ${CMAKE_SOURCE_DIR}/include
)
