# Building from source

Requirements and build steps for the plug-ins. Part of the [documentation index](../README.md#documentation).

## Building from source

### Requirements

- **Cross-build on Linux** (what the single-file plug-ins use): `x86_64-w64-mingw32-g++-posix` (MinGW-w64 with **posix threads**, needed for `std::thread`), a C++23 compiler, the SpeexDSP sources, Python 3 (for `gen_embedded_roms.py`). Link statically with `-lgdi32 -luser32 -lmsimg32` (the last one for `GradientFill`).
- **CMake and vcpkg** as in the original plug-in (CMake 3.25 or newer, vcpkg; Visual Studio 2022 on Windows, Clang 17 or newer and Ninja on macOS and Linux). The presets are the same as in the [Nuked-SC55-CLAP README](https://github.com/johnnovak/Nuked-SC55-CLAP#building) (`release-windows-x64`, `release-linux-x64`, ...). `-DNUKED_SC55_POLY_TARGET_VOICES=256` sets the voice target.

### Single-model VST2 / CLAP build (external ROMs)

`build-mingw-win64.sh` builds single-model variants (`-DNUKED_SC55_ONLY_MODEL=<n>`). The resulting file works as `.clap` and as VST2 `.dll`.

### Single-file build with embedded ROMs (private use only)

1. `tools/gen_embedded_roms.py` creates a C++ source that includes each ROM with `.incbin` for its slot (SC-55: 0 = rom1, 1 = rom2, 3/4/5 = wave ROMs; SC-55mk2: 0 = rom1, 1 = rom2 (CTF), 2 = rom_sm, 3/4 = wave ROMs).
2. Compile with `-DNUKED_SC55_EMBED_ROMS`, `-DNUKED_SC55_ONLY_MODEL=<n>` and the display name / VST2 ID, `-O3 -flto`, and link `src/gui/editor_win32.cpp` and the version resource (`src/vst2/version.rc.in` with the placeholders `@NAME@`, `@FILE@`, `@VER_STR@`, `@VER_NUM@`, `@VENDOR@`, `@COMMENT@`). `tools/build-embedded-win64.sh.inc` contains the build function.
3. For a **template** instead, call `gen_embedded_roms.py --slot <out.cpp> <romset> <name> <capacity>` (the build function does this when `NK_SLOT_CAP` is set) and run `src/tools/p256_builder/compact_template.py` on the result.

The wave ROMs must be **descrambled** like in the file loader (`LoadRomset(info, nullptr)` from `rom_io.h` before `LoadRoms`). Without that, the level is wrong by a factor of 15-20.

### SC-88 Pro

```
tools/build_88emu.sh <target folder> <plug-in source folder>
```

fetches Gearmulator in the pinned state, applies the patches in `third_party/88emu/`, and builds `lib88emu.a` for Linux and Windows/MinGW plus the ROM preparation tool `romdump`. The Windows build needs several MinGW adjustments, which the script handles (`wincase/Windows.h`, `Memory.h`, a replacement for `shlobj_core.h`, `-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000005`, `-include cstdint`). Boot of the units is parallel with embedded ROMs; with ROM files it is serial because the file scan is not thread-safe.

### Native Linux templates (issue #112)

`tools/build_templates_linux.sh` (workbench) builds the seven ROM-free Linux x64 CLAP templates with the same build functions as the Windows templates (`tools/winbuild/build*.sh`), switched by `NK_TARGET=linux` (`tools/winbuild/target.sh`): native `g++`, `-fPIC`, libstdc++/libgcc linked statically, only `clap_entry` exported (version script), no editor, no VST2 entry point and no version resource. The 88emu models link the Linux `lib88emu.a` of `build_88emu.sh` (`build88/`, position-independent). Copy the results from `out/templates-linux-x64/` to `templates/linux-x64/`.

- **glibc baseline 2.35:** with glibc 2.38 headers, `strtol`, `strtoul` and `sscanf` become the C23 symbols `__isoc23_*`; `tools/winbuild/glibc_compat.c` forwards them to the old versions, so the plug-ins load on Ubuntu 22.04 / Debian 12 and newer. Check with `objdump -T <file> | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`.
- **Check:** `tools/test/clapcheck.cpp` (`g++ -O2 -std=c++17 -I src/include tools/test/clapcheck.cpp -ldl -o clapcheck`) loads a built `.clap` with `dlopen`, plays 256 notes, and checks the sound, that no GUI is offered, and the state round trip.
- The Windows build is unchanged by `NK_TARGET` (default `windows`): templates built with the old and the new scripts on the same day differ only in the PE link time stamps and checksum.

### macOS

`build-macos.sh` comes from the original project; macOS has not been tested with the P256 changes. The editor (Win32/GDI) is Windows-only, and there are no macOS single-file templates yet (issue #112).
