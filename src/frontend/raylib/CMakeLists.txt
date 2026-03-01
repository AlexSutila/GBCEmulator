# ===========================================================================
# Dependencies
# ===========================================================================
include(FetchContent)

if(EMSCRIPTEN)
    set(PLATFORM Web CACHE STRING "" FORCE)
endif() # EMSCRIPTEN

# This is a commond dep across both desktop and web builds
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.0
)
FetchContent_MakeAvailable(raylib)

if(EMSCRIPTEN)
    set(EM_SHARED_MEM_FLAGS -pthread -matomics -mbulk-memory)
    target_compile_definitions(raylib PRIVATE MA_ENABLE_AUDIO_WORKLETS)
    target_compile_options(raylib PRIVATE ${EM_SHARED_MEM_FLAGS})
    target_link_options(raylib PRIVATE ${EM_SHARED_MEM_FLAGS})
    target_compile_options(raylib PRIVATE -flto)
    target_link_options(raylib PRIVATE -flto)
endif() # EMSCRIPTEN


# ===========================================================================
# BUILD_TARGET: BUILD_SIMPLE
# ===========================================================================
if(BUILD_SIMPLE)
    if (EMSCRIPTEN) # Raylib simplistic WASM frontend
        set(SHELL_HTML "${CMAKE_SOURCE_DIR}/src/frontend/raylib/shell.html")
        add_executable(gbc_wasm
            ${CMAKE_SOURCE_DIR}/src/frontend/raylib/frontend.cpp
            ${CMAKE_SOURCE_DIR}/src/frontend/raylib/main.cpp
            ${GBC_CORE_SOURCES}
        )
        target_link_libraries(gbc_wasm
            PRIVATE raylib
        )
        target_include_directories(gbc_wasm
            PRIVATE ${GBC_INCLUDE_DIR}
        )

        # If we ever have to debug the core, we will certainly not be doing
        # so using this build target. Hence, we go pretty heavy with compiler
        # flags here because performance on weaker machines tends to suffer.
        target_link_options(gbc_wasm
            PRIVATE
            -sUSE_GLFW=3 # Need this or else linker errors
            -sINITIAL_MEMORY=134217728
            -sTOTAL_STACK=67108864
            -sBINARYEN_ASYNC_COMPILATION=0
            -sMIN_WEBGL_VERSION=2
            -sMAX_WEBGL_VERSION=2
            -sEXPORTED_FUNCTIONS=_emscripten_start,_malloc,_free
            -sEXPORTED_RUNTIME_METHODS=HEAPF32
            -sSTACK_OVERFLOW_CHECK=0
            -sFORCE_FILESYSTEM=1
            -sSAFE_HEAP=0
            -sAUDIO_WORKLET=1
            -sWASM_WORKERS=1
            -sASYNCIFY
            -fno-rtti
            -flto
            -pthread
            -O3
            --shell-file=${SHELL_HTML}
        )
        target_compile_options(gbc_wasm
            PRIVATE
            ${GBC_COMPILE_OPTIONS}
            -pthread
            -sUSE_WEBGL2=1
            -sWASM_WORKERS=1
            -flto
            -O3
        )
        set_target_properties(gbc_wasm PROPERTIES
            OUTPUT_NAME "index"
            SUFFIX ".html"
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        )

        add_custom_command(TARGET gbc_wasm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/frontend/raylib/app.css"
            "$<TARGET_FILE_DIR:gbc_wasm>/app.css"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/frontend/raylib/coi-serviceworker.js"
            "$<TARGET_FILE_DIR:gbc_wasm>/coi-serviceworker.js"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/frontend/raylib/app.js"
            "$<TARGET_FILE_DIR:gbc_wasm>/app.js"
        )
        set_property(TARGET gbc_wasm
            APPEND PROPERTY LINK_DEPENDS "${SHELL_HTML}"
        )

    else() # EMSCRIPTEN
        add_executable(gbc_simple
            ${CMAKE_SOURCE_DIR}/src/frontend/raylib/frontend.cpp
            ${CMAKE_SOURCE_DIR}/src/frontend/raylib/main.cpp
            ${GBC_CORE_SOURCES}
        )
        target_link_libraries(gbc_simple
            PRIVATE raylib
        )
        target_include_directories(gbc_simple
            PRIVATE ${GBC_INCLUDE_DIR}
        )
        target_compile_options(gbc_simple
            PRIVATE ${GBC_COMPILE_OPTIONS}
        )
        set_target_properties(gbc_simple PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif() # EMSCRIPTEN
endif() # BUILD_SIMPLE
