# ===========================================================================
# Dependencies
# ===========================================================================
include(FetchContent)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v3.0.1
)
FetchContent_MakeAvailable(pybind11)


# ===========================================================================
# BUILD_TARGET: BUILD_PYTHON
# ===========================================================================
pybind11_add_module(gbc_py
    ${CMAKE_SOURCE_DIR}/src/frontend/python/bindings.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/python/frontend.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/python/wrappers.cpp
    ${GBC_CORE_SOURCES}
)
target_include_directories(gbc_py
    PRIVATE ${GBC_INCLUDE_DIR}
)
target_compile_options(gbc_py
    PRIVATE ${GBC_COMPILE_OPTIONS}
)

# Install target into proper location for usage as library
install(TARGETS gbc_py
    LIBRARY DESTINATION irogb_python
)
