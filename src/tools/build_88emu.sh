#!/bin/bash
# Holt Gearmulator (88emu, GPLv3) im verwendeten Stand, wendet die Nuked-SC55-Poly-Patches an und baut
# lib88emu.a fuer Linux (Tests) und Windows/MinGW (Plugins) sowie das ROM-Aufbereitungswerkzeug romdump.
# usage: build_88emu.sh <Zielordner> <Plugin-Quellordner>      (Dauer ca. 8-10 min, 1 Kern)
set -e
GM=${1:?Zielordner}; S=${2:?Plugin-Quellen}
GM_COMMIT=${GM_COMMIT:-96deb437794baf109fb996a2f8806ce2ee949246} # Standard: festgehaltener Stand
fetch() { # <pfad> <url> <commit>
  rm -rf "$1"; git init -q "$1"; (cd "$1" && git remote add origin "$2" && git fetch -q --depth 1 origin "$3" && git checkout -q FETCH_HEAD)
}
if [ ! -d "$GM/.git" ]; then
  echo "[1/5] Gearmulator $GM_COMMIT (sparse)"
  git init -q "$GM"; cd "$GM"; git remote add origin https://github.com/dsp56300/gearmulator.git
  git sparse-checkout init --no-cone
  git sparse-checkout set '/CMakeLists.txt' '/LICENSE.md' '/doc/' '/scripts/' '/source/CMakeLists.txt' \
    '/source/cmake/' '/source/framework/' '/source/cpu/' '/source/3rdparty/' '/source/ronaldo/' \
    '/source/waldi/' '/source/claudia/' '!/source/**/skins/'
  git fetch -q --depth 1 --filter=blob:none origin $GM_COMMIT; git checkout -q FETCH_HEAD
  echo "[2/5] Submodule"
  fetch source/cpu/dsp56300 https://github.com/dsp56300/dsp56300 c1d6593f0c90be3c654aa1430de7738d9fc7420c
  fetch source/cpu/dsp56300/source/asmjit https://github.com/dsp56300/asmjit.git 3577608cab0bc509f856ebf6e41b2f9d9f71acc4
  fetch source/cpu/mc68k https://github.com/dsp56300/mc68k.git cc3693a23fde2ea97aec261198f9e6291ead5e5e
  git show HEAD:.gitmodules > /tmp/gm_modules
  for m in "freetype 828916527ce4f69af722bce46ce54d289001a0bd" "lunasvg f8aabfb444bb37f69df7290790f57e4a27730a93" "RmlUi d2e83ba7db630adbe6a910cbcc2f07b950676a5c"; do
    set -- $m; fetch source/3rdparty/$1 "$(git config -f /tmp/gm_modules --get submodule.source/3rdparty/$1.url)" $2
  done
  fetch source/3rdparty/lunasvg/plutovg https://github.com/sammycage/plutovg.git 5e4712cf873b0c7829a4a6157763e2ad3ac49164
  echo "[3/5] Patches (Stimmenzaehler, ROM-Satz aus dem Speicher, MinGW)"
  git apply "$S/third_party/88emu/88emu-nuked-poly.patch"
  (cd source/cpu/dsp56300 && git apply "$S/third_party/88emu/dsp56300-mingw.patch")
fi
cd "$GM"
OPTS="-DCMAKE_BUILD_TYPE=Release -Dgearmulator_BUILD_JUCEPLUGIN=off -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=off \
 -Dgearmulator_SYNTH_OSIRUS=off -Dgearmulator_SYNTH_OSTIRUS=off -Dgearmulator_SYNTH_VAVRA=off -Dgearmulator_SYNTH_XENIA=off \
 -Dgearmulator_SYNTH_NODALRED2X=off -Dgearmulator_SYNTH_JE8086=off -Dgearmulator_SYNTH_88EMU=on -Dgearmulator_COMPONENT_DSPBRIDGE=off"
echo "[4/5] Linux-Bibliothek"
cmake -S . -B build88 $OPTS >/dev/null
cmake --build build88 --target 88emu_bundle -j2 2>&1 | grep -E " error|Error [0-9]" || true
echo "[5/5] Windows-Bibliothek (MinGW: Header-Schreibweise, shlobj_core, Win10-API, cstdint)"
mkdir -p wincase && ln -sf /usr/x86_64-w64-mingw32/include/windows.h wincase/Windows.h && ln -sf /usr/x86_64-w64-mingw32/include/memory.h wincase/Memory.h
echo '#include <shlobj.h>' > wincase/shlobj_core.h
cat > mingw.cmake << 'TC'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
TC
F="-I$GM/wincase -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000005"
cmake -S . -B buildwin -DCMAKE_TOOLCHAIN_FILE=$GM/mingw.cmake $OPTS "-DCMAKE_CXX_FLAGS=$F -include cstdint" "-DCMAKE_C_FLAGS=$F" >/dev/null
cmake --build buildwin --target 88emu_bundle -j2 2>&1 | grep -E " error|Error [0-9]" || true
ls -la build88/source/ronaldo/88emu/88lib/lib88emu.a buildwin/source/ronaldo/88emu/88lib/lib88emu.a
echo "romdump (ROM-Aufbereitung)"
gcc -O2 -x c - -Isource/ronaldo/88emu/88lib -c -o /tmp/romdump.o << 'C'
#include "c_interface.h"
#include <stdio.h>
int main(int argc,char**argv){ if(argc<6){ printf("usage: romdump <romdir> <control> <waveA> <waveB> <waveC>\n"); return 2; }
  emu88_add_rom_path(argv[1]); int rc=emu88_dump_sc88pro_rom_images(argv[2],argv[3],argv[4],argv[5]); printf("romdump rc=%d\n",rc); return rc!=0; }
C
g++ /tmp/romdump.o build88/source/ronaldo/88emu/88lib/lib88emu.a -lpthread -ldl -o romdump
echo "fertig: $GM"
