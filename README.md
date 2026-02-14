# Overview
Lolyep. This repo presents yet another GameBoy Color emulator written entirely from scratch. It serves as a spiritual successor to an earlier (and very poorly written) [DMG GameBoy Emulator](https://github.com/AlexSutila/GBEmulator), aiming to be a cleaner, more accurate, and more modular foundation for both DMG and CGB emulation.

- Try it out without compiling: [here](https://alexsutila.github.io/GBCEmulator/index.html)

## Compatability
This emulator is designed to be compatible with **GameBoy Color (CGB)** games (obviously), and also implements the backwards compatability features CGB models provide. As such, this emulator can be used to emulate **original DMG GameBoy** games as well. The games shown in the screenshot below do not define the compatability limitations of this emulator, but they are known to play reasonably well.

### GameBoy Color Compatability
![CGB Compatability](assets/cgb_demo.png)
 - Full support for RGB555 coloring CGB models use for visuals.

### DMG GameBoy Backwards Compatability
![DMG Compatability](assets/dmg_demo.png)
 - Full support for authentic re-coloring of original monochrome games through a configurable BIOS.
 - That re-coloring of DMG games can be optionally disabled (since it looks hideous for some games).

### Memory Bank Circuitry Support
TODO: We support a couple, just need to finalize before we can say we support the fully.

## Accuracy
To evaluate the accuracy of any emulator, the homebrew community has released a plethora of test ROMs that can be used to evaluate the accuracy of both basic hardware functionality and bizzare edge cases. Specifically, we leverage the following testing suites:
1. [Blargg's GB test roms](https://github.com/retrio/gb-test-roms)
2. [Dmg-Acid2](https://github.com/mattcurrie/dmg-acid2)
3. [Cgb-Acid2](https://github.com/mattcurrie/cgb-acid2)
4. [The MoonEye Test Suite](https://github.com/Gekkio/mooneye-test-suite?tab=readme-ov-file)

### Blargg's Basic Correctness Tests
![CPU and Memory Access Timing](assets/blargg_cpu_mem.png)
- Proves high level correctness of CPU instruction accuracy
- Proves accuracy of sub-instruction memory access timings

### Acid Visual Tests
![Visual Tests](assets/acid_tests.png)
- Proves high level correctness of visual capabilities for both DMG and CGB
- **Note:** An accurate pixel FIFO is not necessary for passing these tests, in fact [this older GB emulator](https://github.com/AlexSutila/GBEmulator) manages to pass DMG acid with a rudimentary scanline renderer. This emulator goes a step further and implements a full pixel FIFO, just because :)

### Mooneye Test Suite
We cannot realistically expect to pass every single one of these tests, as not all of them are designed to pass on CGB hardware. The tests we actually evaluate and their pass/fail status can be seen [here](assets/test_results.md).

## Multiple Frontends
This emulator currently supports two frontends:
1. A generic SDL3 + ImGUI frontend, which is designed to be user friendly. You can do what you would normally expect of a traditional emulator, such as remap controls, change settings, etc.
2. A simplified Raylib frontend, which is basically just the previous frontend but with a reduced feature set. This frontend compiles to a desktop usable version, and also a WASM binary which can be hosted and used natively in a browser. 
3. A comprehensive Python binding, which can be used to step the emulation and interrogate the state of the system on a per-clock cycle basis. The python bindings were also used to generate the images we display above by loading the ROM programatically, stepping the thing for a couple seconds, and rendering the pixeldata after a set amount of time using [matplotlib](https://matplotlib.org/).

This codebase was designed intentionally to make writing new frontends and ports extremely easy.

## Building
To compile a release build:
```bash
mkdir Release && cd Release
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
```

To compile a debug build:
```bash
mkdir Debug && cd Debug
cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc)
```

## Original Authors
1. Alex Sutila (https://github.com/alexsutila)
2. Xuanli Lin (https://github.com/kazum1kun)
