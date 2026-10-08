build(){ F="$1"; N="$2"; M=$3; ID=$4; RS=$5; RN=$6; shift 6
 # NK_SLOT_CAP gesetzt: ROM-lose Vorlage (reservierter ROM-Bereich, befuellt von tools/p256_builder)
 if [ -n "$NK_SLOT_CAP" ]; then python3 $S/tools/gen_embedded_roms.py --slot "gen/roms_$M.cpp" $RS "$RN" $NK_SLOT_CAP
 else python3 $S/tools/gen_embedded_roms.py "gen/roms_$M.cpp" $RS "$RN" "$@"; fi
 O="${OUTDIR:-out2}"; mkdir -p "$O"
 VER_STR=$(date +%Y.%m.%d); VER_NUM=$(date +%Y,%-m,%-d,0)
 sed -e "s/@NAME@/$N/g" -e "s/@FILE@/$F.dll/g" -e "s/@VER_STR@/$VER_STR/g" -e "s/@VER_NUM@/$VER_NUM/g" -e "s/@VENDOR@/Nuked SC-55 P256/g" -e "s/@COMMENT@/Roland SC-55 emulation (Nuked SC55), 256 voices./g" $S/src/vst2/version.rc.in > gen/ver_$M.rc
 x86_64-w64-mingw32-windres gen/ver_$M.rc -O coff -o gen/ver_$M.o
 $CXX -O3 -flto -std=c++23 -DNDEBUG -DNUKED_SC55_POLY_TARGET_VOICES=256 -DNUKED_SC55_ONLY_MODEL=$M -DNUKED_SC55_EMBED_ROMS \
  -DNUKED_SC55_VST2_ID=$ID "-DNUKED_SC55_VST2_NAME=\"$N\"" "-DNUKED_SC55_DISPLAY_NAME=\"$N\"" \
  -ffunction-sections -fdata-sections -I$S/include -I$S/src -I. -Iinc $(ls $S/src/nuked-sc55/backend/*.cpp $S/src/nuked-sc55/common/*.cpp) \
  $S/src/nuked_sc55.cpp $S/src/plugin.cpp $S/src/vst2/vst2_entry.cpp $S/src/gui/editor_win32.cpp gen/roms_$M.cpp gen/ver_$M.o resample.o sha.o \
  -O3 -flto -shared -o "$O/$F.dll" -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s -lgdi32 -luser32 -lmsimg32 2>&1 | grep -iE "error" | head
 cp "$O/$F.dll" "$O/$F.clap"; }
