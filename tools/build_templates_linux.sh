#!/bin/bash
# Builds the ROM-free Linux x64 CLAP templates (issue #112) into out/templates-linux-x64/ of the workbench;
# copy them to templates/linux-x64/ of the repository. Same sources and ROM areas as the Windows templates of
# build_builder.sh, but native g++ (NK_TARGET=linux, see winbuild/target.sh): CLAP only, no editor yet.
# The 88emu models use the Linux lib88emu.a of build_88emu.sh (build88/; Gearmulator builds it position-independent).
# Self-test: p256_builder.py --target linux with the workbench ROMs must report "N plugin(s) created";
# then tools/test/clapcheck loads each plug-in through the CLAP entry and plays notes.
WB=/home/claude/wb; T=$WB/tools; S=$WB/src; K=$WB/out/templates-linux-x64
set -e
mkdir -p $T/winbuild/linux && cd $T/winbuild/linux
export S NK_TARGET=linux CXX=${CXX:-g++} OUTDIR=out_tpl; rm -rf out_tpl; mkdir -p gen
# helper objects as in build_tools.sh, position-independent for the shared object
ln -sfn ../inc inc; cp ../plugin.h .   # plugin.h: configured by the CMake build (build_tools.sh)
gcc -O2 -fPIC -c -DFLOATING_POINT -DUSE_SSE -DUSE_SSE2 -DEXPORT= -Iinc -Iinc/speex -I$WB/speexdsp/libspeexdsp $WB/speexdsp/libspeexdsp/resample.c -o resample.o
gcc -O2 -fPIC -c $S/src/nuked-sc55/sha/sha224-256.c -o sha.o
. ../build2.sh
NK_SLOT_CAP=3506176 build "Nuked-SC55_v121" "Nuked-SC55 v1.21" 3 0x53355033 2 "SC-55 v1.21"
NK_SLOT_CAP=3772416 build "Nuked-SC55_MkII" "Nuked-SC55 MkII" 5 0x5335504D 0 "SC-55mk2 v1.01 CTF"
LIBL=$WB/gearmulator/build88/source/ronaldo/88emu/88lib/lib88emu.a
if [ -f $LIBL ]; then
  export GM=$WB/gearmulator LIBW=$LIBL
  . ../build88.sh;   NK_SLOT_CAP=22085632 build88
  . ../build8850.sh; NK_SLOT_CAP=36765696 build8850
  . ../build88o.sh;  NK_SLOT_CAP=8912896 build88o
  . ../build8820.sh; NK_SLOT_CAP=27394048 build8820
  . ../buildmt32.sh; NK_SLOT_CAP=2621440 buildmt32
else
  echo "no Linux lib88emu.a ($LIBL): only the SC-55 templates were built" >&2
fi
rm -rf $K && mkdir -p $K
for f in out_tpl/*.so; do python3 $S/tools/p256_builder/compact_template.py "$f" "$K/$(basename "$f" .so).p256tpl"; done
echo "Templates: $K"
