#!/bin/bash
# Erzeugt tests/golden.json: Referenz-Pruefsummen und CPU-Zeiten des aktuellen (gelieferten) Stands.
# Grundlage fuer tools/upstream_check.sh trial. Neu erzeugen nach jeder bewussten Aenderung am Klang.
# Dauer ca. 4 min (im Hintergrund starten).
WB=/home/claude/wb; T=$WB/tools; OUT=$WB/tests/golden.json; mkdir -p $WB/tests $WB/out/golden
export SOUNDCANVAS_ROM_PATH=$WB/roms NUKED_SC55_POLY_VOICES=256
EV=$T/midi/events.txt   # Polyphonie-Test (immer vorhanden, von gen.py erzeugt)
res=()
add() { res+=("$1|$2|$3"); echo "  $1: $2 (${3}s)"; }
secs() { python3 -c "import sys,re; m=re.search(r'([0-9.]+)\s*s',sys.argv[1]); print(m.group(1) if m else 0)" "$1"; }
echo "Kern (ein Emulator, ref_new, Polyphonie-Test 40 s)"
for m in "SC-55-v1.21 mk1-v1.21 core_v121" "SC-55mk2-v1.01 mk2-ctf-sc55-drum-sc55-v1.21 core_mk2"; do set -- $m
  o=$($T/exact/ref_new $WB/roms/$1 $2 $EV 40 x); add $3 "$(echo "$o" | awk '{print $3}')" "$(echo "$o" | awk '{print $(NF-1)}')"; done
echo "Plugin (Linux-CLAP, play, Polyphonie-Test komplett)"
cd $T/test
for m in "sc55_v1_21 plugin_v121" "sc55mk2_v1_01 plugin_mk2"; do set -- $m
  o=$(./play net.nuked_sc55_poly_clap.$1 $WB/out/golden/$2.f32 | grep "Rechenzeit"); add $2 "$(sha256sum $WB/out/golden/$2.f32 | cut -c1-16)" "$(secs "$o")"; done
if [ -x ./play88 ]; then
  o=$(SOUNDCANVAS_ROM_PATH=$WB/rom88 ./play88 net.nuked_sc55_poly_clap.sc88pro $WB/out/golden/plugin_88pro.f32 | grep "Rechenzeit")
  add plugin_88pro "$(sha256sum $WB/out/golden/plugin_88pro.f32 | cut -c1-16)" "$(secs "$o")"
  [ -f $WB/rom88proc/control.bin ] && add romset_88pro "$(cat $WB/rom88proc/*.bin | sha256sum | cut -c1-16)" 0
fi
python3 - "$OUT" "${res[@]}" << 'PY'
import json,sys,datetime
out={"erzeugt":datetime.datetime.now().isoformat(timespec="seconds"),
     "hinweis":"Pruefsummen der Audioausgabe (bitgenau) und CPU-Zeiten dieses Stands; Zeiten nur in derselben Umgebung vergleichbar.",
     "werte":{}}
for r in sys.argv[2:]:
    k,h,t=r.split("|"); out["werte"][k]={"hash":h,"sekunden":float(t)}
json.dump(out,open(sys.argv[1],"w"),indent=2,ensure_ascii=False); print("->",sys.argv[1])
PY
