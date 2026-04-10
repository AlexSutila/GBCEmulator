# ===========================================================================
# Dependencies
# ===========================================================================
project(gbc LANGUAGES C) # Allow C since API is C-based
include(FetchContent)

# ===========================================================================
# Core build
# ===========================================================================

if(EMSCRIPTEN)
  add_library(
    irogb_libretro STATIC
    ${CMAKE_SOURCE_DIR}/src/frontend/libretro/libretro_host.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/libretro/frontend.cpp ${GBC_CORE_SOURCES}
  )
elseif(VITA)
  add_library(
    irogb_libretro STATIC
    ${CMAKE_SOURCE_DIR}/src/frontend/libretro/libretro_host.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/libretro/frontend.cpp ${GBC_CORE_SOURCES}
  )
else()
  add_library(
    irogb_libretro SHARED
    ${CMAKE_SOURCE_DIR}/src/frontend/libretro/libretro_host.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/libretro/frontend.cpp ${GBC_CORE_SOURCES}
  )
endif()

set_target_properties(irogb_libretro PROPERTIES PREFIX "")

if(EMSCRIPTEN)
  set_target_properties(
    irogb_libretro PROPERTIES SUFFIX ".bc" OUTPUT_NAME "irogb_libretro_emscripten"
  )
elseif(VITA)
  set_target_properties(irogb_libretro PROPERTIES SUFFIX ".a" OUTPUT_NAME "irogb_libretro_vita")
endif()

target_compile_options(irogb_libretro PRIVATE -fPIC -DNO_CORE_FILESYSTEM=ON ${GBC_COMPILE_OPTIONS})

target_include_directories(irogb_libretro PRIVATE ${GBC_INCLUDE_DIR})
