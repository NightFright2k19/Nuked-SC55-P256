# Build-Funktion fuer das Ein-Datei-Plugin "Nuked-SC88" (Roland SC-88, 88emu-Kern, ROMs + Panel eingebettet).
# Erwartet: S (Plugin-Quellen), GM (Gearmulator-Checkout mit Patches), LIBW (Windows-lib88emu.a),
#           ROM88O (sc88_control.bin sc88_wave0..3.bin, Roh-Dumps), CXX (MinGW g++-posix)
build88o() {
  F="Nuked-SC88"; N="Nuked-SC88"
  # NK_SLOT_CAP gesetzt: ROM-lose Vorlage (reservierter ROM-Bereich, befuellt von tools/p256_builder)
  if [ -n "$NK_SLOT_CAP" ]; then python3 $S/tools/gen_embedded_roms.py --slot gen/roms_88o.cpp 0 "SC-88" $NK_SLOT_CAP
  else python3 $S/tools/gen_embedded_roms.py gen/roms_88o.cpp 0 "SC-88 (embedded)" \
     0=$ROM88O/sc88_control.bin 1=$ROM88O/sc88_wave0.bin 2=$ROM88O/sc88_wave1.bin 3=$ROM88O/sc88_wave2.bin 4=$ROM88O/sc88_wave3.bin; fi
  O="${OUTDIR:-out2}"; mkdir -p "$O"
  A=$GM/source/ronaldo/88emu/88emuplayer/assets
  python3 $S/tools/make_sc88orig_panel.py $A/sc88_panel.png gen/panel88o.bgra
  python3 $S/tools/gen_blob.py gen/panel88o.cpp nk_panel88o gen/panel88o.bgra
  python3 $S/tools/make_sc88orig_sprites.py $A gen/sprites88o.bgra
  python3 $S/tools/gen_blob.py gen/sprites88o.cpp nk_sprites88o gen/sprites88o.bgra
  VER_STR=$(date +%Y.%m.%d); VER_NUM=$(date +%Y,%-m,%-d,0)
  sed -e "s/@NAME@/$N/g" -e "s/@FILE@/$F.dll/g" -e "s/@VER_STR@/$VER_STR/g" -e "s/@VER_NUM@/$VER_NUM/g" -e "s/@VENDOR@/Nuked SC-88 P256/g" -e "s/@COMMENT@/Roland SC-88 emulation (88emu), 256 voices./g" $S/src/vst2/version.rc.in > gen/ver_88o.rc
  x86_64-w64-mingw32-windres gen/ver_88o.rc -O coff -o gen/ver_88o.o
  $CXX -O3 -flto -std=c++23 -DNDEBUG -DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_DEVICE_88 -DNUKED_SC55_POLY_TARGET_VOICES=256 \
    -DNUKED_SC55_ONLY_MODEL=8 -DNUKED_SC55_EMBED_ROMS -DNUKED_SC55_VST2_ID=0x53383838 \
    "-DNUKED_SC55_VST2_NAME=\"$N\"" "-DNUKED_SC55_DISPLAY_NAME=\"$N\"" \
    -ffunction-sections -fdata-sections -I$S/include -I$S/src -I. -Iinc -I$GM/source/ronaldo/88emu/88lib \
    $S/src/nuked-sc55/backend/*.cpp $S/src/nuked-sc55/common/*.cpp \
    $S/src/nuked_sc55.cpp $S/src/plugin.cpp $S/src/vst2/vst2_entry.cpp $S/src/gui/editor_win32.cpp \
    gen/roms_88o.cpp gen/panel88o.cpp gen/sprites88o.cpp gen/ver_88o.o resample.o sha.o $LIBW \
    -O3 -flto -shared -o "$O/$F.dll" -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s \
    -lgdi32 -luser32 -lmsimg32 -lshlwapi -lole32 -luuid -lshell32 -lwinmm 2>&1 | grep -E "error|undefined" | head -20
  cp "$O/$F.dll" "$O/$F.clap"
}
