cmake_minimum_required(VERSION 3.15)
project(gbc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(gbc
    main.cpp
    src/cpu/lr35902.cpp
    src/cpu/registers/register.cpp
    src/cpu/registers/flags.cpp
    src/memory/bus.cpp
)
target_include_directories(gbc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/includes
)
