# Build-Funktion fuer das Ein-Datei-Plugin "Nuked-SC8850" (88emu-Kern, ROMs + Panel eingebettet).
# Erwartet: S (Plugin-Quellen), GM (Gearmulator-Checkout mit Patches), LIBW (Windows-lib88emu.a),
#           ROM8850 (sc8850_internal.bin sc8850_program.bin sc8850_data.bin sc8850_wave.bin), CXX (MinGW g++-posix)
build8850() {
  F="Nuked-SC8850"; N="Nuked-SC8850"
  # NK_SLOT_CAP gesetzt: ROM-lose Vorlage (reservierter ROM-Bereich, befuellt von tools/p256_builder)
  if [ -n "$NK_SLOT_CAP" ]; then python3 $S/tools/gen_embedded_roms.py --slot gen/roms_8850.cpp 0 "SC-8850" $NK_SLOT_CAP
  else python3 $S/tools/gen_embedded_roms.py gen/roms_8850.cpp 0 "SC-8850 (embedded)" \
     0=$ROM8850/sc8850_internal.bin 1=$ROM8850/sc8850_program.bin 2=$ROM8850/sc8850_data.bin 3=$ROM8850/sc8850_wave.bin; fi
  O="${OUTDIR:-out2}"; mkdir -p "$O"
  A=$GM/source/ronaldo/88emu/88emuplayer/assets
  python3 $S/tools/make_sc8850_panel.py $A/sc8850_panel.png gen/panel8850.bgra
  python3 $S/tools/gen_blob.py gen/panel8850.cpp nk_panel8850 gen/panel8850.bgra
  python3 $S/tools/make_sc8850_sprites.py $A gen/sprites8850.bgra
  python3 $S/tools/gen_blob.py gen/sprites8850.cpp nk_sprites8850 gen/sprites8850.bgra
  VER_STR=$(date +%Y.%m.%d); VER_NUM=$(date +%Y,%-m,%-d,0)
  sed -e "s/@NAME@/$N/g" -e "s/@FILE@/$F.dll/g" -e "s/@VER_STR@/$VER_STR/g" -e "s/@VER_NUM@/$VER_NUM/g" -e "s/@VENDOR@/Nuked SC-8850 P256/g" -e "s/@COMMENT@/Roland SC-8850 emulation (88emu), 256 voices./g" $S/src/vst2/version.rc.in > gen/ver_8850.rc
  x86_64-w64-mingw32-windres gen/ver_8850.rc -O coff -o gen/ver_8850.o
  $CXX -O3 -flto -std=c++23 -DNDEBUG -DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_DEVICE_8850 -DNUKED_SC55_POLY_TARGET_VOICES=256 \
    -DNUKED_SC55_ONLY_MODEL=7 -DNUKED_SC55_EMBED_ROMS -DNUKED_SC55_VST2_ID=0x53383835 \
    "-DNUKED_SC55_VST2_NAME=\"$N\"" "-DNUKED_SC55_DISPLAY_NAME=\"$N\"" \
    -ffunction-sections -fdata-sections -I$S/include -I$S/src -I. -Iinc -I$GM/source/ronaldo/88emu/88lib \
    $S/src/nuked-sc55/backend/*.cpp $S/src/nuked-sc55/common/*.cpp \
    $S/src/nuked_sc55.cpp $S/src/plugin.cpp $S/src/vst2/vst2_entry.cpp $S/src/gui/editor_win32.cpp \
    gen/roms_8850.cpp gen/panel8850.cpp gen/sprites8850.cpp gen/ver_8850.o resample.o sha.o $LIBW \
    -O3 -flto -shared -o "$O/$F.dll" -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s \
    -lgdi32 -luser32 -lmsimg32 -lshlwapi -lole32 -luuid -lshell32 -lwinmm 2>&1 | grep -E "error|undefined" | head -20
  cp "$O/$F.dll" "$O/$F.clap"
}
