#!/bin/sh
# Cross-Build der Einzelmodell-Varianten (CLAP + VST2) mit MinGW-w64 (posix threads).
# Voraussetzung: x86_64-w64-mingw32-g++-posix, speexdsp-Quellen in ../speexdsp
set -e
CC=x86_64-w64-mingw32-gcc-posix; CXX=x86_64-w64-mingw32-g++-posix
B=build-win64; mkdir -p $B/inc/speex; S=$(pwd)
cp ../speexdsp/include/speex/*.h $B/inc/speex/
printf '#include <stdint.h>\ntypedef int16_t spx_int16_t;typedef uint16_t spx_uint16_t;typedef int32_t spx_int32_t;typedef uint32_t spx_uint32_t;\n' > $B/inc/speex/speexdsp_config_types.h
printf '#pragma once\n#define PLUGIN_VENDOR "Nuked SC-55 Poly"\n#define PLUGIN_URL "https://github.com/johnnovak/Nuked-SC55-CLAP"\n#define PLUGIN_VERSION_STRING "0.12.0-poly"\n' > $B/plugin.h
$CC -O2 -c -DFLOATING_POINT -DUSE_SSE -DUSE_SSE2 -DEXPORT= -I$B/inc -I$B/inc/speex -I../speexdsp/libspeexdsp ../speexdsp/libspeexdsp/resample.c -o $B/resample.o
$CC -O2 -c src/nuked-sc55/sha/sha224-256.c -o $B/sha.o
build() { # Ausgabename Modellindex VST2-ID VST2-Name Stimmen
  $CXX -O2 -std=c++23 -DNDEBUG -DNUKED_SC55_POLY_TARGET_VOICES=$5 -DNUKED_SC55_ONLY_MODEL=$2 \
    -DNUKED_SC55_VST2_ID=$3 "-DNUKED_SC55_VST2_NAME=\"$4\"" -ffunction-sections -fdata-sections \
    -Iinclude -Isrc -I$B -I$B/inc src/nuked-sc55/backend/*.cpp src/nuked-sc55/common/*.cpp \
    src/nuked_sc55.cpp src/plugin.cpp src/vst2/vst2_entry.cpp $B/resample.o $B/sha.o \
    -shared -o $B/$1.dll -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s
  cp $B/$1.dll $B/$1.clap
}
# Modellindex: 0=v1.00 1=v1.10 2=v1.20 3=v1.21 4=v2.00 5=mk2 v1.01
build Nuked-SC55-v121-Poly256   3 0x53355033 "SC-55 v1.21 Poly256"   256
build Nuked-SC55mk2-CTF-Poly256 5 0x5335504D "SC-55mk2 CTF Poly256"  256
