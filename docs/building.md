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

`tools/build_templates_linux.sh` (workbench) builds the seven ROM-free Linux x64 CLAP templates with the same build functions as the Windows templates (`tools/winbuild/build*.sh`), switched by `NK_TARGET=linux` (`tools/winbuild/target.sh`): native `g++`, `-fPIC`, libstdc++/libgcc linked statically, only `clap_entry` exported (version script), no VST2 entry point and no version resource. The editor is the Win32 editor code (`src/gui/editor_win32.cpp`) compiled with `-DNUKED_SC55_X11_EDITOR` against a small Win32 shim (`src/gui/win32_shim.h`): `win32_gdi.cpp` is the part shared with macOS (software drawing into 32-bit bitmaps, text through `stb_truetype` with the fonts of `src/third_party/fonts/`, embedded with `.incbin` and `NK_FONT_DIR`, and the menu model), `win32_x11.cpp` the X11 part: windows, mouse, the SETUP menu as its own popup windows that do not block the host (the choice arrives through `popupmenu::SetHandler`), and the 30 fps timer through the host's CLAP `timer-support` and `posix-fd-support` (`editor::Idle`). Linked with `-lX11`. Test host: `tools/test/clapgui_x11.cpp` (opens the editor in an X11 window, plays notes, optionally loads and saves the state). The 88emu models link the Linux `lib88emu.a` of `build_88emu.sh` (`build88/`, position-independent). Copy the results from `out/templates-linux-x64/` to `templates/linux-x64/`.

- **glibc baseline 2.35:** with glibc 2.38 headers, `strtol`, `strtoul` and `sscanf` become the C23 symbols `__isoc23_*`; `tools/winbuild/glibc_compat.c` forwards them to the old versions, so the plug-ins load on Ubuntu 22.04 / Debian 12 and newer. Check with `objdump -T <file> | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`.
- **Check:** `tools/test/clapcheck.cpp` (`g++ -O2 -std=c++17 -I src/include tools/test/clapcheck.cpp -ldl -o clapcheck`) loads a built `.clap` with `dlopen`, plays 256 notes, and checks the sound, that the X11 editor is offered, and the state round trip.
- The Windows build is unchanged by `NK_TARGET` (default `windows`): templates built with the old and the new scripts on the same day differ only in the PE link time stamps and checksum.

### macOS

`tools/build_templates_macos.sh` builds the seven ROM-free macOS CLAP templates for the architecture of the Mac it runs on, with the same build functions (`NK_TARGET=macos` in `tools/winbuild/target.sh`): Apple clang, `-mmacosx-version-min=11.0`, only `_clap_entry` exported, `-dead_strip`, no VST2 entry point. The editor is the same Win32 editor code with `-DNUKED_SC55_COCOA_EDITOR` against `win32_gdi.cpp` and `win32_cocoa.mm` (ARC, `-framework Cocoa`): an NSView subclass registered at run time under a name of its own per plug-in (several plug-ins in one host do not clash), the bitmap drawn as a `CGImage`, an `NSTimer` in the common run loop modes for the 30 fps timer (no host extension needed), and the SETUP menu as a native `NSMenu` (values in a second column through a tab stop; `TrackPopupMenu` delivers the choice through `popupmenu::SetHandler`). The embedded blobs go into `__TEXT,__const` (`gen_blob.py`, `gen_embedded_roms.py`). It fetches speexdsp and the DejaVu fonts of the panel graphics (`NK_DEJAVU_DIR`), and builds the 88emu library of the Mac with `build_88emu.sh` (`GM_NATIVE_ONLY=1`). There is no cross-build from Linux: the workflow `.github/workflows/macos-templates.yml` runs it on the GitHub macOS runners (`macos-15` for arm64, `macos-15-intel` for x86_64; manually or on a push to a `feat/issue-112-macos*` branch) uploads the templates as artifacts and, when both architectures pass, commits them to `templates/macos-arm64/` and `templates/macos-x64/` of the branch it ran on (a commit made with the workflow token starts no Autobuild; on `main`, push a change afterwards or start the Autobuild by hand). The workflow has no ROMs: it fills each template with random data, renews the signature with `p256_builder.macho_resign`, checks it with `codesign --verify` (on arm64; the stale signature must fail the same check) and loads the plug-in with `tools/test/clapcheck.cpp --load`. Then `tools/test/clapgui_mac.mm` opens each editor in a window of its own, saves the panel and, after a click on SETUP, a screenshot with the menu; the commit job publishes these screenshots to the branch `preview/<branch>` (replaced on each run).

- **Code signature:** the Apple linker signs arm64 output ad hoc. Inserting the ROMs changes the pages, so the builder computes the page hashes of the code directory again (same file size, same layout); x86_64 builds carry no signature.
- `build-macos.sh` comes from the original project (CMake, universal binary) and is not used for the templates.
