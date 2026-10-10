# Build-Funktion fuer das Ein-Datei-Plugin "Nuked-SC88 Pro" (88emu-Kern, ROMs + Panel eingebettet).
# Erwartet: S (Plugin-Quellen), GM (Gearmulator-Checkout mit Patches), LIBW (Windows-lib88emu.a),
#           ROMPROC (aufbereitete ROMs: control.bin waveA.bin waveB.bin waveC.bin), CXX (MinGW g++-posix)
build88() {
  F="Nuked-SC88_Pro"; N="Nuked-SC88 Pro"
  # NK_SLOT_CAP gesetzt: ROM-lose Vorlage (reservierter ROM-Bereich, befuellt von tools/p256_builder)
  if [ -n "$NK_SLOT_CAP" ]; then python3 $S/tools/gen_embedded_roms.py --slot gen/roms_88.cpp 0 "SC-88 Pro" $NK_SLOT_CAP
  else python3 $S/tools/gen_embedded_roms.py gen/roms_88.cpp 0 "SC-88 Pro (embedded)" \
     0=$ROMPROC/control.bin 1=$ROMPROC/waveA.bin 2=$ROMPROC/waveB.bin 3=$ROMPROC/waveC.bin; fi
  O="${OUTDIR:-out2}"; mkdir -p "$O"
  # Panel wie beim SC-88 (editor_sc88orig.inc): Original-Grafik des SC-88 Pro, gleiche Bedienelemente
  A=$GM/source/ronaldo/88emu/88emuplayer/assets
  python3 $S/tools/make_sc88orig_panel.py --pro $A/sc88pro_panel.png gen/panel88.bgra
  python3 $S/tools/gen_blob.py gen/panel88.cpp nk_panel88o gen/panel88.bgra
  python3 $S/tools/make_sc88orig_sprites.py $A gen/sprites88.bgra
  python3 $S/tools/gen_blob.py gen/sprites88.cpp nk_sprites88o gen/sprites88.bgra
  VER_STR=$(date +%Y.%m.%d); VER_NUM=$(date +%Y,%-m,%-d,0)
  sed -e "s/@NAME@/$N/g" -e "s/@FILE@/$F.dll/g" -e "s/@VER_STR@/$VER_STR/g" -e "s/@VER_NUM@/$VER_NUM/g" -e "s/@VENDOR@/Nuked SC-88 P256/g" -e "s/@COMMENT@/Roland SC-88 Pro emulation (88emu), 256 voices./g" $S/src/vst2/version.rc.in > gen/ver_88.rc
  x86_64-w64-mingw32-windres gen/ver_88.rc -O coff -o gen/ver_88.o
  $CXX -O3 -flto -std=c++23 -DNDEBUG -DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_POLY_TARGET_VOICES=256 \
    -DNUKED_SC55_ONLY_MODEL=6 -DNUKED_SC55_EMBED_ROMS -DNUKED_SC55_VST2_ID=0x53385050 \
    "-DNUKED_SC55_VST2_NAME=\"$N\"" "-DNUKED_SC55_DISPLAY_NAME=\"$N\"" \
    -ffunction-sections -fdata-sections -I$S/include -I$S/src -I. -Iinc -I$GM/source/ronaldo/88emu/88lib \
    $S/src/nuked-sc55/backend/*.cpp $S/src/nuked-sc55/common/*.cpp \
    $S/src/nuked_sc55.cpp $S/src/plugin.cpp $S/src/vst2/vst2_entry.cpp $S/src/gui/editor_win32.cpp \
    gen/roms_88.cpp gen/panel88.cpp gen/sprites88.cpp gen/ver_88.o resample.o sha.o $LIBW \
    -O3 -flto -shared -o "$O/$F.dll" -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s \
    -lgdi32 -luser32 -lmsimg32 -lshlwapi -lole32 -luuid -lshell32 -lwinmm 2>&1 | grep -E "error|undefined" | head -20
  cp "$O/$F.dll" "$O/$F.clap"
}
