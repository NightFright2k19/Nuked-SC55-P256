# Build function for the single-file plug-in "Emu-MT32" (Roland MT-32 old / new and CM-32L in one plug-in,
# 88emu core, ROMs + panels embedded; issue #69).
# Expects: S (plug-in sources), GM (Gearmulator checkout with patches), LIBW (Windows lib88emu.a), CXX (MinGW g++-posix),
#           ROMLA (folder with the MT-32/CM-32L ROMs; the per-device sets are assembled by p256_builder.la_blob())
buildmt32() {
  F="Emu-MT32"; N="Emu-MT32"
  # NK_SLOT_CAP set: ROM-free template (reserved ROM area, filled by p256_builder)
  if [ -n "$NK_SLOT_CAP" ]; then python3 $S/tools/gen_embedded_roms.py --slot gen/roms_mt32.cpp 0 "MT-32 / CM-32L" $NK_SLOT_CAP
  else
    # one set per device (0 = MT-32 old, 1 = MT-32 new, 2 = CM-32L), as the builder inserts it
    python3 -I -c "import sys; sys.path.insert(0, sys.argv[1]); import p256_builder as b; from pathlib import Path
s = b.la_sets(b.scan_roms(Path(sys.argv[2])))[0]
for loc, (_, r) in enumerate(next(m for m in b.MODELS if m[0] == 'la')[3]):
    if r in s: open(f'gen/la_{loc}.bin', 'wb').write(b.la_blob(*s[r])); print(f'{loc}=gen/la_{loc}.bin')" \
      "${P256:-$S/..}" "$ROMLA" > gen/la_list.txt
    python3 $S/tools/gen_embedded_roms.py gen/roms_mt32.cpp 0 "MT-32 / CM-32L (embedded)" $(cat gen/la_list.txt)
  fi
  O="${OUTDIR:-out2}"; mkdir -p "$O"
  # Panels: drawn from scratch without photos or third-party graphics (background + all lamps on, glass over the LCD)
  python3 $S/tools/make_la_panel.py gen/panelmt32.bgra --base
  python3 $S/tools/make_la_panel.py gen/litmt32.bgra --all-on
  python3 $S/tools/make_la_panel.py gen/panelcm32l.bgra --base --cm32l
  python3 $S/tools/make_la_panel.py gen/litcm32l.bgra --all-on --cm32l
  python3 $S/tools/make_la_panel.py gen/glassmt32.bgra --glass
  python3 $S/tools/make_la_panel.py gen/namemt32v2.bgra --base --v2 --name
  for b in panelmt32 litmt32 panelcm32l litcm32l glassmt32 namemt32v2; do python3 $S/tools/gen_blob.py gen/$b.cpp nk_$b gen/$b.bgra; done
  VER_STR=$(date +%Y.%m.%d); VER_NUM=$(date +%Y,%-m,%-d,0)
  sed -e "s/@NAME@/$N/g" -e "s/@FILE@/$F.dll/g" -e "s/@VER_STR@/$VER_STR/g" -e "s/@VER_NUM@/$VER_NUM/g" -e "s/@VENDOR@/Emu MT-32 P256/g" -e "s/@COMMENT@/Roland MT-32 \/ CM-32L emulation (88emu), 64 voices./g" $S/src/vst2/version.rc.in > gen/ver_mt32.rc
  x86_64-w64-mingw32-windres gen/ver_mt32.rc -O coff -o gen/ver_mt32.o
  $CXX -O3 -flto -std=c++23 -DNDEBUG -DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_DEVICE_LA -DNUKED_SC55_POLY_TARGET_VOICES=64 \
    -DNUKED_SC55_ONLY_MODEL=10 -DNUKED_SC55_EMBED_ROMS -DNUKED_SC55_VST2_ID=0x4D543332 \
    "-DNUKED_SC55_VST2_NAME=\"$N\"" "-DNUKED_SC55_DISPLAY_NAME=\"$N\"" \
    -ffunction-sections -fdata-sections -I$S/include -I$S/src -I. -Iinc -I$GM/source/ronaldo/88emu/88lib \
    $S/src/nuked-sc55/backend/*.cpp $S/src/nuked-sc55/common/*.cpp \
    $S/src/nuked_sc55.cpp $S/src/plugin.cpp $S/src/vst2/vst2_entry.cpp $S/src/gui/editor_win32.cpp \
    gen/roms_mt32.cpp gen/panelmt32.cpp gen/litmt32.cpp gen/panelcm32l.cpp gen/litcm32l.cpp gen/glassmt32.cpp gen/namemt32v2.cpp \
    gen/ver_mt32.o resample.o sha.o $LIBW \
    -O3 -flto -shared -o "$O/$F.dll" -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -s \
    -lgdi32 -luser32 -lmsimg32 -lshlwapi -lole32 -luuid -lshell32 -lwinmm 2>&1 | grep -E "error|undefined" | head -20
  cp "$O/$F.dll" "$O/$F.clap"
}
