#!/bin/bash
# Baut die ROM-losen Vorlagen und stellt das Builder-Paket zusammen: out/Nuked-P256-Builder/
# (p256_builder.py, templates/*.p256tpl, roms/, Anleitungen). Selbsttest: Builder mit den
# Workbench-ROMs (roms/, rom88/) ausfuehren, Ergebnis muss "N Plugin(s) erzeugt" melden.
# Dauer ca. 4 min (im Hintergrund starten).
WB=/home/claude/wb; T=$WB/tools; S=$WB/src; K=$WB/out/Nuked-P256-Builder
cd $T/winbuild; export S CXX=x86_64-w64-mingw32-g++-posix OUTDIR=out_tpl; rm -rf out_tpl
. ./build2.sh
NK_SLOT_CAP=3506176 build "Nuked-SC55_v121" "Nuked-SC55 v1.21" 3 0x53355033 2 "SC-55 v1.21"
NK_SLOT_CAP=3772416 build "Nuked-SC55_MkII" "Nuked-SC55 MkII" 5 0x5335504D 0 "SC-55mk2 v1.01 CTF"
if [ -f $WB/gearmulator/buildwin/source/ronaldo/88emu/88lib/lib88emu.a ]; then
  export GM=$WB/gearmulator LIBW=$WB/gearmulator/buildwin/source/ronaldo/88emu/88lib/lib88emu.a; . ./build88.sh
  NK_SLOT_CAP=22085632 build88
fi
rm -rf $K && mkdir -p $K/templates $K/roms
cp $S/tools/p256_builder/p256_builder.py $S/tools/p256_builder/LIESMICH.txt $S/tools/p256_builder/README-EN.txt $K/
cp $S/tools/p256_builder/HIER-ROMS-ABLEGEN.txt $K/roms/
# kompakt: reservierten (leeren) ROM-Bereich herausschneiden; das Skript setzt die ROMs dort ein
for f in out_tpl/*.dll; do python3 $S/tools/p256_builder/compact_template.py "$f" "$K/templates/$(basename "$f" .dll).p256tpl"; done
# Selbsttest mit den Workbench-ROMs (Kopie, das Paket selbst bleibt ROM-frei)
X=/tmp/p256_selftest; rm -rf $X && cp -r $K $X && cp -rL $WB/roms $X/roms/sc55 && { [ -d $WB/rom88 ] && cp -r $WB/rom88/. $X/roms/sc88/ || true; }
( cd $X && python3 p256_builder.py | grep -E "^\[|erzeugt" )
echo "Paket: $K  (Selbsttest-Ausgabe: $X/output)"
