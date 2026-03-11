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

set_target_properties(irogb_libretro PROPERTIES PREFIX "")

target_compile_options(irogb_libretro PRIVATE ${GBC_COMPILE_OPTIONS} -fPIC)

target_include_directories(irogb_libretro PRIVATE ${GBC_INCLUDE_DIR})
