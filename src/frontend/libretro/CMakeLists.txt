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

  # The VITA has a literal pile of junk 333MHz clock speed (by default), so we
  # need to go pretty aggressive with platform specific compiler optimizations
  target_compile_options(
    irogb_libretro
    PRIVATE -march=armv7-a
            -mfpu=neon
            -mfloat-abi=hard
            -Ofast
            -flto # probably redundant, but gotta make sure
            -ftree-vectorize
            -funroll-loops
            -funswitch-loops
            -ftree-sra
            -ftree-ter
            -ftree-loop-im
            -ftree-loop-distribution
            -fomit-frame-pointer
            -ffunction-sections
            -fdata-sections
            -Wl,--gc-sections
  )
  target_link_options(irogb_libretro PRIVATE -flto -Wl,--gc-sections)
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
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
  set_target_properties(irogb_libretro PROPERTIES SUFFIX ".a" OUTPUT_NAME "irogb_libretro_vita")
endif()

if(NOT VITA)
  target_compile_options(irogb_libretro PRIVATE -fPIC)
endif()

target_compile_options(irogb_libretro PRIVATE -DNO_CORE_FILESYSTEM=ON ${GBC_COMPILE_OPTIONS})

target_include_directories(irogb_libretro PRIVATE ${GBC_INCLUDE_DIR})
