# Build-Funktion fuer das Ein-Datei-Plugin "Nuked-SC8820" (Roland SC-8820, 88emu-Kern, ROMs + Panel eingebettet).
# Erwartet: S (Plugin-Quellen), GM (Gearmulator-Checkout mit Patches), LIBW (Windows-lib88emu.a),
#           ROM8820 (sc8820_internal.bin sc8820_program.bin sc8820_wave0.bin sc8820_wave1.bin), CXX (MinGW g++-posix)
build8820() {
  F="Nuked-SC8820"; N="Nuked-SC8820"
  # NK_SLOT_CAP gesetzt: ROM-lose Vorlage (reservierter ROM-Bereich, befuellt von p256_builder)
  if [ -n "$NK_SLOT_CAP" ]; then python3 $S/tools/gen_embedded_roms.py --slot gen/roms_8820.cpp 0 "SC-8820" $NK_SLOT_CAP
  else python3 $S/tools/gen_embedded_roms.py gen/roms_8820.cpp 0 "SC-8820 (embedded)" \
     0=$ROM8820/sc8820_internal.bin 1=$ROM8820/sc8820_program.bin 2=$ROM8820/sc8820_wave0.bin 3=$ROM8820/sc8820_wave1.bin; fi
  O="${OUTDIR:-out2}"; mkdir -p "$O"
  # Panel: ohne Fotos/Fremdgrafik von Grund auf gezeichnet (Hintergrund + alle Lampen an)
  python3 $S/tools/make_sc8820_panel.py gen/panel8820.bgra --base
  python3 $S/tools/make_sc8820_panel.py gen/lit8820.bgra --all-on
  # Pro-Oberflaeche (Issue #49/#67, SETUP-Menue "Interface Style"): zweites Panel im selben Plugin
  python3 $S/tools/make_sc8820_panel.py gen/panel8820p.bgra --base --pro
  python3 $S/tools/make_sc8820_panel.py gen/lit8820p.bgra --all-on --pro
  python3 $S/tools/gen_blob.py gen/panel8820.cpp nk_panel8820 gen/panel8820.bgra
  python3 $S/tools/gen_blob.py gen/lit8820.cpp nk_lit8820 gen/lit8820.bgra
  python3 $S/tools/gen_blob.py gen/panel8820p.cpp nk_panel8820p gen/panel8820p.bgra
  python3 $S/tools/gen_blob.py gen/lit8820p.cpp nk_lit8820p gen/lit8820p.bgra
  VER_STR=$(date +%Y.%m.%d); VER_NUM=$(date +%Y,%-m,%-d,0)
  sed -e "s/@NAME@/$N/g" -e "s/@FILE@/$F.dll/g" -e "s/@VER_STR@/$VER_STR/g" -e "s/@VER_NUM@/$VER_NUM/g" -e "s/@VENDOR@/Nuked SC-8820 P256/g" -e "s/@COMMENT@/Roland SC-8820 emulation (88emu), 256 voices./g" $S/src/vst2/version.rc.in > gen/ver_8820.rc
  x86_64-w64-mingw32-windres gen/ver_8820.rc -O coff -o gen/ver_8820.o
  $CXX -O3 -flto -std=c++23 -DNDEBUG -DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_DEVICE_8820 -DNUKED_SC55_POLY_TARGET_VOICES=256 \
    -DNUKED_SC55_ONLY_MODEL=9 -DNUKED_SC55_EMBED_ROMS -DNUKED_SC55_VST2_ID=0x53383230 \
    "-DNUKED_SC55_VST2_NAME=\"$N\"" "-DNUKED_SC55_DISPLAY_NAME=\"$N\"" \
    -ffunction-sections -fdata-sections -I$S/include -I$S/src -I. -Iinc -I$GM/source/ronaldo/88emu/88lib \
    $S/src/nuked-sc55/backend/*.cpp $S/src/nuked-sc55/common/*.cpp \
    $S/src/nuked_sc55.cpp $S/src/plugin.cpp $S/src/vst2/vst2_entry.cpp $S/src/gui/editor_win32.cpp \
    gen/roms_8820.cpp gen/panel8820.cpp gen/lit8820.cpp gen/panel8820p.cpp gen/lit8820p.cpp gen/ver_8820.o resample.o sha.o $LIBW \
    -O3 -flto -shared -o "$O/$F.dll" -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s \
    -lgdi32 -luser32 -lmsimg32 -lshlwapi -lole32 -luuid -lshell32 -lwinmm 2>&1 | grep -E "error|undefined" | head -20
  cp "$O/$F.dll" "$O/$F.clap"
}
