#!/bin/bash
# Builds the ROM-free macOS CLAP templates (issue #112) for the architecture of the Mac it runs on (arm64 or
# x86_64) into templates/macos-arm64/ or templates/macos-x64/ of the repository. Same sources, build functions
# and ROM areas as the Windows and Linux templates (tools/winbuild/build*.sh, NK_TARGET=macos in
# winbuild/target.sh): Apple clang, CLAP only, editor on Cocoa. Runs on the GitHub Actions macOS runners
# (.github/workflows/macos-templates.yml); there is no cross-build from Linux.
# Needs: Xcode command line tools, cmake, python3 with Pillow and cairosvg (cairo from Homebrew), git, curl.
# Work folder: $WORK (default build-macos/ in the repository; Gearmulator, speexdsp, fonts and objects).
# Takes about 15 min (most of it the 88emu library).
set -euo pipefail
R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
S=$R/src
W=${WORK:-$R/build-macos}
case "$(uname -m)" in
  arm64)  K=$R/templates/macos-arm64; SPX="" ;;
  x86_64) K=$R/templates/macos-x64;   SPX="-DUSE_SSE -DUSE_SSE2" ;;
  *) echo "unknown architecture $(uname -m)" >&2; exit 1 ;;
esac
export MACOSX_DEPLOYMENT_TARGET=${NK_MACOS_MIN:-11.0} NK_MACOS_MIN=${NK_MACOS_MIN:-11.0}
mkdir -p "$W" && cd "$W"

# speexdsp (resampler), the state used by the other templates
if [ ! -f speexdsp/libspeexdsp/resample.c ]; then
  rm -rf speexdsp; git init -q speexdsp
  (cd speexdsp && git remote add origin https://github.com/xiph/speexdsp.git \
     && git fetch -q --depth 1 origin 7a158783df74efe7c2d1c6ee8363c1e695c71226 && git checkout -q FETCH_HEAD)
fi
# DejaVu fonts for the panel graphics (the Linux builds use the system copy, version 2.37)
if [ ! -f fonts/DejaVuSans.ttf ]; then
  curl -sSfL -o dejavu.tar.bz2 https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2
  mkdir -p fonts && tar xjf dejavu.tar.bz2 && cp dejavu-fonts-ttf-2.37/ttf/*.ttf fonts/
fi
export NK_DEJAVU_DIR=$W/fonts
# 88emu library of this Mac (Gearmulator in the recorded state with the patches of src/third_party/88emu)
LIB=$W/gearmulator/build88/source/ronaldo/88emu/88lib/lib88emu.a
[ -f "$LIB" ] || GM_NATIVE_ONLY=1 GM_JOBS=$(sysctl -n hw.ncpu) bash "$S/tools/build_88emu.sh" "$W/gearmulator" "$S"

# helper objects and headers as in build_tools.sh
B=$W/tpl; rm -rf "$B"; mkdir -p "$B/inc/speex" "$B/gen" && cd "$B"
cp "$W"/speexdsp/include/speex/*.h inc/speex/
printf '#ifndef __SPEEX_TYPES_H__\n#define __SPEEX_TYPES_H__\n#include <stdint.h>\ntypedef int16_t spx_int16_t; typedef uint16_t spx_uint16_t;\ntypedef int32_t spx_int32_t; typedef uint32_t spx_uint32_t;\n#endif\n' > inc/speex/speexdsp_config_types.h
python3 - "$S" > plugin.h << 'PY'
# plugin.h from plugin.h.in with the values of CMakeLists.txt (what CMake's configure_file does)
import re, sys
src = sys.argv[1]
cm = open(src + "/CMakeLists.txt", encoding="utf-8").read()
v = {k: x for k, x in re.findall(r'set\((PLUGIN_\w+)\s+"([^"]*)"\)', cm)}
ver = re.search(r"project\([^)]*VERSION\s+(\d+)\.(\d+)\.(\d+)", cm).groups()
v.update(PROJECT_VERSION=".".join(ver), PROJECT_VERSION_MAJOR=ver[0], PROJECT_VERSION_MINOR=ver[1], PROJECT_VERSION_PATCH=ver[2])
print(re.sub(r"@(\w+)@", lambda m: v[m.group(1)], open(src + "/src/plugin.h.in", encoding="utf-8").read()), end="")
PY
CC=clang
$CC -O2 -c -mmacosx-version-min=$NK_MACOS_MIN -DFLOATING_POINT $SPX -DEXPORT= -Iinc -Iinc/speex -I"$W/speexdsp/libspeexdsp" "$W/speexdsp/libspeexdsp/resample.c" -o resample.o
$CC -O2 -c -mmacosx-version-min=$NK_MACOS_MIN "$S/src/nuked-sc55/sha/sha224-256.c" -o sha.o

# the build functions filter the compiler output through grep (no match = exit code 1); the count below checks
set +e +o pipefail
export S NK_TARGET=macos CXX=${CXX:-clang++} OUTDIR=out_tpl GM=$W/gearmulator LIBW=$LIB
. "$R/tools/winbuild/build2.sh"
NK_SLOT_CAP=3506176 build "Nuked-SC55_v121" "Nuked-SC55 v1.21" 3 0x53355033 2 "SC-55 v1.21"
NK_SLOT_CAP=3772416 build "Nuked-SC55_MkII" "Nuked-SC55 MkII" 5 0x5335504D 0 "SC-55mk2 v1.01 CTF"
. "$R/tools/winbuild/build88.sh";   NK_SLOT_CAP=22085632 build88
. "$R/tools/winbuild/build8850.sh"; NK_SLOT_CAP=36765696 build8850
. "$R/tools/winbuild/build88o.sh";  NK_SLOT_CAP=8912896 build88o
. "$R/tools/winbuild/build8820.sh"; NK_SLOT_CAP=27394048 build8820
. "$R/tools/winbuild/buildmt32.sh"; NK_SLOT_CAP=2621440 buildmt32

n=$(ls out_tpl/*.dylib 2>/dev/null | wc -l | tr -d ' ')
[ "$n" = 7 ] || { echo "only $n of 7 plug-ins were built" >&2; ls -la out_tpl >&2 || true; exit 1; }
rm -rf "$K" && mkdir -p "$K"
for f in out_tpl/*.dylib; do python3 "$S/tools/p256_builder/compact_template.py" "$f" "$K/$(basename "$f" .dylib).p256tpl"; done
echo "Templates: $K"
