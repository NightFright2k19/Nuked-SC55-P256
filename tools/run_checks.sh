#!/bin/bash
# Regressionstests. usage: run_checks.sh quick|exact|poly|wine|gui|full|sc88|sc88poly|lcd
#   quick = exact-kurz + wine + gui (je < 5 min).  full = verify.sh + poly (lang, im Hintergrund starten!)
WB=/home/claude/wb; T=$WB/tools; R=$WB/roms
export SOUNDCANVAS_ROM_PATH=$R WINEPREFIX=$WB/.wine WINEDEBUG=-all
W64=/usr/lib/wine/wine64; SOLO=/tmp/solo; WSOLO='Z:\tmp\solo'
xvfb() { pgrep Xvfb >/dev/null || (setsid nohup Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 </dev/null &); sleep 1; export DISPLAY=:99; }
stage() { rm -rf $SOLO && mkdir -p $SOLO && cp $T/winbuild/out2/* $SOLO/; }

exact_short() {
  echo "== Bitgenauigkeit (E1M1 40 s, SC-55 v1.21) – Hashes muessen gleich sein"
  cd $T/exact; [ -f $T/midi/e1m1.txt ] && E=$T/midi/e1m1.txt || E=$T/midi/events.txt
  ./ref_orig $R/SC-55-v1.21 mk1-v1.21 $E 40 orig; ./ref_new $R/SC-55-v1.21 mk1-v1.21 $E 40 neu
}
wine_audio() {
  echo "== VST2 unter Wine (CTF-Test: v1.21 RMS 0.0432, MkII RMS 0.0301)"; stage
  for f in "Nuked-SC55_v121" "Nuked-SC55_MkII"; do timeout 140 $W64 $T/winbuild/vsthost.exe "$WSOLO\\$f.dll" 2 2>&1 | grep -E "Name|CTF"; done
}
gui() {
  echo "== GUI-Screenshot (VST2-Editor, Menue, Chunk) -> $WB/out/"; xvfb; stage; mkdir -p $WB/out; rm -f /tmp/shot*.bmp /tmp/menu.bmp
  for L in en_US.UTF-8 de_DE.UTF-8; do
    LANG=$L LC_ALL=$L timeout 200 $W64 $T/winbuild/guihost.exe "$WSOLO\\Nuked-SC55_v121.dll" 2>&1 | grep -E "EditOpen|Chunk"
    python3 -c "from PIL import Image; Image.open('/tmp/shot2.bmp').save('$WB/out/panel_$L.png'); Image.open('/tmp/menu.bmp').save('$WB/out/menu_$L.png')"
  done
  timeout 120 $W64 $T/winbuild/clapgui.exe "$WSOLO\\Nuked-SC55_v121.clap" 2>&1 | grep -E "set_parent|state"
}
poly() {
  echo "== Polyphonie-Test durchs Linux-Plugin (Erwartung: hoerbar bis Runde 8, Runde 9 STILLE) ~2 min"
  cd $T/test; NUKED_SC55_POLY_VOICES=256 timeout 280 ./play net.nuked_sc55_poly_clap.sc55_v1_21 | tail -10
}
sc88() {
  echo "== SC-88 Pro unter Wine (Name/ID S8PP, CTF-Test hoerbar ~0.0109, 256 Noten)"; stage
  for m in 2 1; do timeout 250 $W64 $T/winbuild/vsthost.exe "$WSOLO\\Nuked-SC88_Pro.dll" $m 2>&1 | grep -E "Name|CTF|256"; done
  echo "== SC-88 Pro Linux: Map-Umschaltung + Einheiten (erwartet: Start SC-88; Verhaeltnisse ~1.38/1.73; alle Einheiten folgen)"
  cd $T/test && SOUNDCANVAS_ROM_PATH=$WB/rom88 timeout 250 ./map88 | grep -vE "^nach Umschalten"
}
sc88poly() {
  echo "== SC-88 Pro Polyphonie-Test (erwartet: hoerbar bis Runde 8, Runde 9 STILLE, Einheiten 1..4)"
  cd $T/test; SOUNDCANVAS_ROM_PATH=$WB/rom88 timeout 280 ./play88 net.nuked_sc55_poly_clap.sc88pro | tail -10
}
lcd() {
  echo "== LCD (SysEx-Text/Bitmap) -> $WB/out/lcd_*.png (Erwartung: StarGame Bitmap-Animation + Pegel, 3X3EYES Text '3X3EYES'/'NIHON CREATE' + Bitmaps)"
  xvfb; stage; mkdir -p $WB/out
  for f in "Nuked-SC55_v121:55" "Nuked-SC88_Pro:88"; do [ -f "$SOLO/${f%%:*}.dll" ] || continue
    for m in StarGame:1.45,2.0,6.0,12.0 3X3EYES_mod:0.95,1.4,2.4,3.0; do n=${m%%:*}; [ -f $T/midi/lcd/$n.txt ] || continue; cp $T/midi/lcd/$n.txt /tmp/
      timeout 280 $W64 $T/winbuild/lcdhost.exe "$WSOLO\\${f%%:*}.dll" "Z:\\tmp\\$n.txt" "Z:\\tmp\\lcd${f##*:}_$n" ${m#*:} | grep -c Screenshot
      for b in /tmp/lcd${f##*:}_${n}_*.bmp; do python3 -c "from PIL import Image; Image.open('$b').save('$WB/out/'+'$(basename $b .bmp)'+'.png')"; done
    done
  done
}
case "$1" in
  quick) exact_short; wine_audio; gui ;;
  exact) exact_short ;; wine) wine_audio ;; gui) gui ;; poly) poly ;; sc88) sc88 ;; sc88poly) sc88poly ;; lcd) lcd ;;
  full) (cd $T/exact && sh verify.sh; cat /tmp/verify.txt); poly ;;
  *) echo "usage: $0 quick|exact|poly|wine|gui|full|sc88|sc88poly|lcd" ;;
esac
