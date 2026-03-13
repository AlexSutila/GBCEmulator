# ===========================================================================
# Dependencies
# ===========================================================================
project(gbc LANGUAGES C) # Allow C since API is C-based
include(FetchContent)

# ===========================================================================
# Core build
# ===========================================================================

add_library(
  irogb_libretro SHARED ${CMAKE_SOURCE_DIR}/src/frontend/libretro/libretro_host.cpp
                        ${CMAKE_SOURCE_DIR}/src/frontend/libretro/frontend.cpp ${GBC_CORE_SOURCES}
)

set_target_properties(
  irogb_libretro
  PROPERTIES PREFIX ""
             RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
             ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
             LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

target_compile_options(irogb_libretro PRIVATE -fPIC ${GBC_COMPILE_OPTIONS})

target_include_directories(irogb_libretro PRIVATE ${GBC_INCLUDE_DIR})
