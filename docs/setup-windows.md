## Windows Setup

This repo has two common Windows build paths:

- `gbc_full`: the main SDL3 + ImGui desktop app
- `gbc_wasm`: the raylib + Emscripten web build

`git` must be available during the first CMake configure because several dependencies are downloaded with `FetchContent`.

Do not edit `CMakeLists.txt` to enable frontends. Use CMake options such as `-DBUILD_FULL=ON` and `-DBUILD_SIMPLE=ON`.

### Desktop build (`gbc_full`)

1. Install MSYS2.
2. Open the `MSYS2 CLANG64` terminal and fully update it:

```bash
pacman -Syu
pacman -Syu
```

If the first update asks you to restart the shell, close it, reopen `MSYS2 CLANG64`, and run the second command.

3. Install the required tools and packages:

```bash
pacman -S --needed git mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-sdl3 mingw-w64-clang-x86_64-curl mingw-w64-clang-x86_64-nlohmann-json
```

4. From the repository root, configure the build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_FULL=ON -DCMAKE_PREFIX_PATH=C:/msys64/clang64
```

5. Build the executable:

```bash
cmake --build build --target gbc_full
```

Notes:

- You may need to add `C:\msys64\clang64\bin` to `PATH` so the SDL3 and libcurl DLLs can be found.


### Web build (`gbc_wasm`)

1. Install the latest `emsdk`.
2. In a fresh terminal, run:

```bat
emsdk install latest
emsdk activate latest
emsdk_env.bat
```

3. Make sure `cmake`, `ninja`, and `git` are on `PATH`.
4. From the repository root, configure the build:

```bat
emcmake cmake -S . -B build-emscripten -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SIMPLE=ON
```

5. Build the web target:

```bat
cmake --build build-emscripten --target gbc_wasm
```

The web output is written to `build-emscripten/` and you can use any HTTP server to host it.

If you want the native raylib desktop frontend instead of the web build, configure with `-DBUILD_SIMPLE=ON` on a normal desktop toolchain and build `gbc_simple`.
