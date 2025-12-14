include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.12.0
)
FetchContent_MakeAvailable(pybind11)
cmake_minimum_required(VERSION 3.15)
project(gbc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Main emulator executable
add_executable(gbc
    main.cpp
    src/gbc.cpp
    src/cpu/lr35902.cpp
    src/cpu/registers/register.cpp
    src/cpu/registers/flags.cpp
    src/memory/mmio.cpp
    src/memory/bus.cpp
)
target_include_directories(gbc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/includes
)
target_compile_options(gbc
    PRIVATE
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3>
        -Wall
)

# Emulator python bindings
pybind11_add_module(gbc_py
    src/python/bindings.cpp
    src/cpu/registers/register.cpp
    src/cpu/registers/flags.cpp
    src/memory/mmio.cpp
    src/memory/bus.cpp
)
target_include_directories(gbc_py
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/includes
)
