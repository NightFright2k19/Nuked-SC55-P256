#!/bin/bash
# Ueberwacht die Upstream-Repositories aus upstream.lock und bewertet neue Staende.
#
#   upstream_check.sh report [name]          neue Commits seit dem festgehaltenen Stand, gefiltert auf
#                                            die beobachteten Pfade, eingeordnet nach Bereich (Sekunden,
#                                            ohne ROMs; auch fuer die CI-Vorlage)
#   upstream_check.sh trial  [name] [--force] [--win]
#                                            neuen Stand holen, unsere Patches anwenden, bauen, gegen
#                                            tests/golden.json pruefen (Bitgleichheit, CPU), bei
#                                            Klangaenderung WAV-Paare alt/neu erzeugen (ca. 5-15 min)
#   name: nuked-clap | gearmulator (Standard: beide)
#   --force: Probe auch ohne relevante neue Commits (Selbsttest der Pruefkette)
#   --win:   zusaetzlich die Windows-Plugins mit dem neuen Stand bauen (nur Build-Pruefung)
#
# Ergebnis: Bericht auf der Konsole und in out/upstream_<stufe>.md. Exit-Code 0, ausser bei
# Fehlern der Pruefkette selbst. In der CI (UPSTREAM_CI=1) liegt die Lock-Datei neben dem Skript.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "${UPSTREAM_CI:-}" ]; then WB=$(cd "$HERE/.." && pwd); else WB=/home/claude/wb; fi
LOCK=$WB/upstream.lock; CACHE=$WB/upstream; OUT=$WB/out; mkdir -p "$CACHE" "$OUT"
MODE=${1:-report}; shift || true
ONLY=""; FORCE=0; WIN=0
for a in "$@"; do case "$a" in --force) FORCE=1;; --win) WIN=1;; *) ONLY=$a;; esac; done
REPORT=$OUT/upstream_${MODE}${ONLY:+_$ONLY}.md; : > "$REPORT"
say() { echo "$*"; echo "$*" >> "$REPORT"; }

# Bereich einer geaenderten Datei (fuer die Einordnung im Bericht)
category() { # <name> <pfad>
  case "$1:$2" in
    nuked-clap:src/nuked-sc55/*)                    echo "Emulationskern (Nuked)";;
    nuked-clap:src/*)                               echo "Plugin-Rahmen";;
    nuked-clap:*)                                   echo "Build";;
    gearmulator:source/ronaldo/custom_chips/xp/*)   echo "Klangchip XP (SC-88 Pro)";;
    gearmulator:source/ronaldo/88emu/88lib/rom/*)   echo "ROM-Laden (88emu)";;
    gearmulator:source/ronaldo/88emu/88lib/c_interface*) echo "C-Schnittstelle (88emu)";;
    gearmulator:source/ronaldo/88emu/88lib/*)       echo "Geraeteemulation (88emu)";;
    gearmulator:*/assets/*)                         echo "Panelgrafik";;
    gearmulator:source/cpu/*)                       echo "CPU-Kerne / JIT";;
    gearmulator:source/framework/*)                 echo "Rahmen (synthLib/hardwareLib/baseLib)";;
    *)                                              echo "Sonstiges";;
  esac
}

# Git-Cache ohne Dateiinhalte (schnell, keine API-Limits). gearmulator: vorhandener Checkout wird genutzt.
repo_dir() { # <name> <url> <branch>
  local d=$CACHE/$1
  [ "$1" = gearmulator ] && [ -d "$WB/gearmulator/.git" ] && d=$WB/gearmulator
  if [ ! -d "$d/.git" ]; then git clone -q --filter=blob:none --no-checkout "$2" "$d" || return 1; fi
  git -C "$d" fetch -q --filter=blob:none origin "$3" || return 1
  echo "$d"
}

declare -A HEADS RELEVANT
report_one() { # <name> <url> <branch> <pin> <pfade...>
  local name=$1 url=$2 br=$3 pin=$4; shift 4; local paths=("$@")
  local d; d=$(repo_dir "$name" "$url" "$br") || { say "## $name: Abruf fehlgeschlagen"; return 1; }
  local head; head=$(git -C "$d" rev-parse FETCH_HEAD)
  git -C "$d" cat-file -e "$pin^{commit}" 2>/dev/null || git -C "$d" fetch -q --filter=blob:none origin "$pin"
  local all rel
  all=$(git -C "$d" rev-list --count "$pin..$head")
  rel=$(git -C "$d" rev-list --count "$pin..$head" -- "${paths[@]}")
  HEADS[$name]=$head; RELEVANT[$name]=$rel
  say "## $name"
  say "- festgehalten: \`${pin:0:9}\` · aktuell (\`$br\`): \`${head:0:9}\` · $all neue Commits, davon **$rel** in beobachteten Pfaden"
  [ "$rel" -eq 0 ] && { say "- Bewertung: nichts zu tun"; say ""; return 0; }
  say "- Betroffene Bereiche:"
  git -C "$d" diff --name-only "$pin" "$head" -- "${paths[@]}" | while read -r f; do category "$name" "$f"; done \
    | sort | uniq -c | sort -rn | while read -r n c; do say "  - $c: $n Datei(en)"; done
  say "- Relevante Commits (neueste zuerst):"
  git -C "$d" log --format='  - `%h` %ad %s' --date=short "$pin..$head" -- "${paths[@]}" | head -25 | while read -r l; do say "$l"; done
  say "- Empfehlung: \`upstream_check.sh trial $name\` (Patches, Build, Bitgleichheit, CPU)"
  say ""
}

for_each_lock() { # ruft <funktion> mit den Feldern jeder Lock-Zeile auf
  grep -vE '^\s*(#|$)' "$LOCK" | while read -r name url br pin rest; do
    [ -n "$ONLY" ] && [ "$ONLY" != "$name" ] && continue
    # shellcheck disable=SC2086
    "$1" "$name" "$url" "$br" "$pin" $rest
  done
}

golden() { python3 -c "import json,sys; print(json.load(open('$WB/tests/golden.json'))['werte'].get(sys.argv[1],{}).get(sys.argv[2],''))" "$1" "$2"; }
secs_of() { python3 -c "import sys,re; m=re.search(r'([0-9.]+)\s*s',sys.argv[1]); print(m.group(1) if m else 0)" "$1"; }
# CPU-Abweichung in Prozent; bis +-10 % gilt in der Sandbox (1 Kern, geteilt) als Messrauschen
pct() { python3 -c "import sys; a,b=float(sys.argv[1]),float(sys.argv[2]); d=(b-a)/a*100 if a>0 else 0; print(('%+.0f %%'%d)+(' (Rauschen)' if abs(d)<=10 else (' (schneller)' if d<0 else ' (langsamer)')) if a>0 else 'n/a')" "$1" "$2"; }
wav() { # <f32 stereo 48 kHz> <wav>
  python3 - "$1" "$2" << 'PY'
import sys,struct,array
d=array.array('f'); d.frombytes(open(sys.argv[1],'rb').read())
pcm=array.array('h',(max(-32768,min(32767,int(x*32767))) for x in d))
with open(sys.argv[2],'wb') as f:
    f.write(b'RIFF'+struct.pack('<I',36+len(pcm)*2)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,2,48000,48000*4,4,16)+b'data'+struct.pack('<I',len(pcm)*2)); f.write(pcm.tobytes())
PY
}
VERDICT=()
verdict() { VERDICT+=("$1: $2"); say "- **Ergebnis $1: $2**"; }

# --- Probelauf Nuked-SC55-CLAP: neuer Stand + unsere komplette Patch-Serie (git am -3)
trial_nuked() { # <name> <url> <branch> <pin> ...
  local name=$1 pin=$4 d head tdir
  d=$(repo_dir "$name" "$2" "$3") || return; head=${HEADS[$name]:-$(git -C "$d" rev-parse FETCH_HEAD)}
  say "## Probe $name: \`${pin:0:9}\` -> \`${head:0:9}\`"
  if [ "${RELEVANT[$name]:-1}" -eq 0 ] && [ $FORCE -eq 0 ]; then verdict "$name" "keine relevanten Aenderungen (keine Probe noetig)"; return; fi
  tdir=$WB/trial/nuked-clap; rm -rf "$tdir"; git -C "$d" worktree prune
  git -C "$d" worktree add -q --detach "$tdir" "$head" || { verdict "$name" "Checkout fehlgeschlagen"; return; }
  if ! git -C "$tdir" -c user.email=t@t -c user.name=t am -q -3 "$WB"/patches/*.patch > "$OUT/trial_nuked_am.log" 2>&1; then
    local p; p=$(git -C "$tdir" am --show-current-patch=diff 2>/dev/null | head -1)
    git -C "$tdir" am --abort 2>/dev/null
    verdict "$name" "unsere Patches lassen sich nicht anwenden (Konflikt, siehe out/trial_nuked_am.log) - Anpassung noetig"; return
  fi
  say "- Patch-Serie angewendet ($(ls "$WB"/patches/*.patch | wc -l) Patches)"
  cmake -S "$tdir" -B "$tdir/build" -DCMAKE_BUILD_TYPE=Release > "$OUT/trial_nuked_build.log" 2>&1 && \
    cmake --build "$tdir/build" -j2 >> "$OUT/trial_nuked_build.log" 2>&1 || { verdict "$name" "Linux-Build fehlgeschlagen (out/trial_nuked_build.log)"; return; }
  local S=$tdir C=$tdir/src/nuked-sc55
  g++ -O2 -std=c++23 -I$S/include -I$S/src -I$S/build "$WB/tools/test/play.cpp" $(find $S/build -name '*.o') -lspeexdsp -lpthread -o "$tdir/play" 2>>"$OUT/trial_nuked_build.log" || { verdict "$name" "Test-Host nicht baubar (Schnittstelle geaendert?)"; return; }
  g++ -O2 -std=c++20 -I$C -I$C/backend -I$S/src "$WB/tools/exact/ref.cpp" $C/backend/*.cpp $C/common/*.cpp "$WB/tools/harness/sha.o" -o "$tdir/ref" 2>>"$OUT/trial_nuked_build.log" || { verdict "$name" "Kern-Testprogramm nicht baubar"; return; }
  say "- Linux-Build und Test-Hosts: ok"
  local changed=0 o h t g gt
  export SOUNDCANVAS_ROM_PATH=$WB/roms NUKED_SC55_POLY_VOICES=256
  for m in "SC-55-v1.21 mk1-v1.21 core_v121" "SC-55mk2-v1.01 mk2-ctf-sc55-drum-sc55-v1.21 core_mk2"; do set -- $m
    o=$("$tdir/ref" "$WB/roms/$1" "$2" "$WB/tools/midi/events.txt" 40 x); h=$(echo "$o" | awk '{print $3}'); t=$(echo "$o" | awk '{print $(NF-1)}')
    g=$(golden "$3" hash); gt=$(golden "$3" sekunden)
    if [ "$h" = "$g" ]; then say "- $3: bitgleich, CPU $(pct "$gt" "$t") (Referenz ${gt}s, neu ${t}s)"; else changed=1; say "- $3: **Ausgabe veraendert** (Hash $h statt $g), CPU $(pct "$gt" "$t")"; fi
  done
  mkdir -p "$OUT/ab"
  for m in "sc55_v1_21 plugin_v121" "sc55mk2_v1_01 plugin_mk2"; do set -- $m
    o=$(cd "$WB/tools/test" && "$tdir/play" net.nuked_sc55_poly_clap.$1 "$tdir/$2.f32" | grep Rechenzeit); t=$(secs_of "$o")
    h=$(sha256sum "$tdir/$2.f32" | cut -c1-16); g=$(golden "$2" hash); gt=$(golden "$2" sekunden)
    if [ "$h" = "$g" ]; then say "- $2: bitgleich, CPU $(pct "$gt" "$t")"
    else changed=1; say "- $2: **Ausgabe veraendert**, CPU $(pct "$gt" "$t") -> WAV-Paar out/ab/${name}_$2_{alt,neu}.wav"
      wav "$WB/out/golden/$2.f32" "$OUT/ab/${name}_$2_alt.wav"; wav "$tdir/$2.f32" "$OUT/ab/${name}_$2_neu.wav"; fi
  done
  if [ $WIN -eq 1 ]; then
    ( cd "$WB/tools/winbuild" && export S=$tdir CXX=x86_64-w64-mingw32-g++-posix R=$WB/roms && . ./build2.sh && mkdir -p out_trial && sed 's#out2/#out_trial/#g' build2.sh > /tmp/b2t.sh && . /tmp/b2t.sh && \
      build "Trial_v121" "Nuked-SC55 v1.21" 3 0x53355033 2 "trial" 0=$R/SC-55-v1.21/sc55_rom1.bin 1=$R/SC-55-v1.21/sc55_rom2.bin 3=$R/SC-55-v1.21/sc55_waverom1.bin 4=$R/SC-55-v1.21/sc55_waverom2.bin 5=$R/SC-55-v1.21/sc55_waverom3.bin ) > "$OUT/trial_nuked_win.log" 2>&1
    [ -f "$WB/tools/winbuild/out_trial/Trial_v121.dll" ] && say "- Windows-Build: ok" || say "- Windows-Build: **fehlgeschlagen** (out/trial_nuked_win.log)"
  fi
  [ $changed -eq 0 ] && verdict "$name" "uebernehmbar, Klang bitgleich (CPU-Vergleich siehe oben)" \
                     || verdict "$name" "Klang veraendert - Hoervergleich der WAV-Paare noetig, dann bewusst entscheiden"
}

# --- Probelauf Gearmulator/88emu: neuer Stand + unsere 88emu-Patches, Linux-Bibliothek, Plugin-Test
trial_gm() { # <name> <url> <branch> <pin> ...
  local name=$1 pin=$4 d head tdir
  d=$(repo_dir "$name" "$2" "$3") || return; head=${HEADS[$name]:-$(git -C "$d" rev-parse FETCH_HEAD)}
  say "## Probe $name: \`${pin:0:9}\` -> \`${head:0:9}\`"
  if [ "${RELEVANT[$name]:-1}" -eq 0 ] && [ $FORCE -eq 0 ]; then verdict "$name" "keine relevanten Aenderungen (keine Probe noetig)"; return; fi
  [ -x "$WB/tools/test/play88" ] && [ -d "$WB/rom88proc" ] || { verdict "$name" "SC-88-Pro-Umgebung fehlt (setup_wb.sh mit sc88pro.zip)"; return; }
  tdir=$WB/trial/gearmulator; rm -rf "$tdir"
  if ! GM_COMMIT=$head bash "$WB/src/tools/build_88emu.sh" "$tdir" "$WB/src" > "$OUT/trial_gm_build.log" 2>&1; then
    grep -q "patch failed\|does not apply" "$OUT/trial_gm_build.log" && verdict "$name" "88emu-Patches lassen sich nicht anwenden (out/trial_gm_build.log) - Anpassung noetig" \
      || verdict "$name" "Build fehlgeschlagen (out/trial_gm_build.log)"; return
  fi
  local L=$tdir/build88/source/ronaldo/88emu/88lib/lib88emu.a I=$tdir/source/ronaldo/88emu/88lib
  [ -f "$L" ] || { verdict "$name" "Linux-Bibliothek fehlt (out/trial_gm_build.log)"; return; }
  say "- Patches angewendet, Linux- und Windows-Bibliothek gebaut"
  mkdir -p "$tdir/rp"; "$tdir/romdump" "$WB/rom88" "$tdir/rp/control.bin" "$tdir/rp/waveA.bin" "$tdir/rp/waveB.bin" "$tdir/rp/waveC.bin" > /dev/null
  [ "$(cat "$tdir"/rp/*.bin | sha256sum | cut -c1-16)" = "$(golden romset_88pro hash)" ] && say "- ROM-Aufbereitung: unveraendert" || say "- ROM-Aufbereitung: **veraendert** (eingebettete ROM-Daten muessten neu erzeugt werden)"
  local S=$WB/src P=$tdir/p88 D88="-DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_ONLY_MODEL=6 -DNUKED_SC55_POLY_TARGET_VOICES=256"
  mkdir -p "$P"; for f in nuked_sc55 plugin; do g++ -O2 -std=c++23 $D88 -I$S/include -I$S/src -I$S/build -I$I -c $S/src/$f.cpp -o "$P/$f.o" || { verdict "$name" "Plugin nicht mehr kompilierbar gegen neue 88emu-Schnittstelle"; return; }; done
  cp $(find $S/build -name '*.o' | grep -v -E "/nuked_sc55.cpp.o|/plugin.cpp.o") "$P/"
  g++ -O2 -std=c++23 $D88 -I$S/include -I$S/src -I$S/build -I$I "$WB/tools/test/play.cpp" "$P"/*.o "$L" -lspeexdsp -lpthread -ldl -o "$tdir/play88" || { verdict "$name" "Test-Host nicht linkbar"; return; }
  local o t h g gt
  o=$(cd "$WB/tools/test" && SOUNDCANVAS_ROM_PATH=$WB/rom88 NUKED_SC55_POLY_VOICES=256 "$tdir/play88" net.nuked_sc55_poly_clap.sc88pro "$tdir/plugin_88pro.f32" | grep Rechenzeit); t=$(secs_of "$o")
  h=$(sha256sum "$tdir/plugin_88pro.f32" | cut -c1-16); g=$(golden plugin_88pro hash); gt=$(golden plugin_88pro sekunden)
  mkdir -p "$OUT/ab"
  if [ "$h" = "$g" ]; then say "- plugin_88pro: bitgleich, CPU $(pct "$gt" "$t") (Referenz ${gt}s, neu ${t}s)"; verdict "$name" "uebernehmbar, Klang bitgleich"
  else say "- plugin_88pro: **Ausgabe veraendert**, CPU $(pct "$gt" "$t") -> WAV-Paar out/ab/${name}_plugin_88pro_{alt,neu}.wav"
    wav "$WB/out/golden/plugin_88pro.f32" "$OUT/ab/${name}_plugin_88pro_alt.wav"; wav "$tdir/plugin_88pro.f32" "$OUT/ab/${name}_plugin_88pro_neu.wav"
    verdict "$name" "Klang veraendert - Hoervergleich der WAV-Paare noetig, dann bewusst entscheiden"; fi
}

say "# Upstream-Check ($MODE) – $(date '+%Y-%m-%d %H:%M')"
say ""
case "$MODE" in
  report) for_each_lock report_one ;;
  trial)
    [ -f "$WB/tests/golden.json" ] || { say "tests/golden.json fehlt: zuerst tools/make_golden.sh"; exit 1; }
    for_each_lock report_one
    # report_one setzt HEADS/RELEVANT in einer Subshell (Pipe) -> erneut ohne Pipe ermitteln
    while read -r name url br pin rest; do
      [ -n "$ONLY" ] && [ "$ONLY" != "$name" ] && continue
      d=$(repo_dir "$name" "$url" "$br") || continue
      HEADS[$name]=$(git -C "$d" rev-parse FETCH_HEAD)
      # shellcheck disable=SC2086
      RELEVANT[$name]=$(git -C "$d" rev-list --count "$pin..${HEADS[$name]}" -- $rest)
      case "$name" in nuked-clap) trial_nuked "$name" "$url" "$br" "$pin";; gearmulator) trial_gm "$name" "$url" "$br" "$pin";; esac
    done < <(grep -vE '^\s*(#|$)' "$LOCK")
    say ""; say "## Zusammenfassung"; for v in "${VERDICT[@]}"; do say "- $v"; done ;;
  *) echo "usage: $0 report|trial [nuked-clap|gearmulator] [--force] [--win]"; exit 2 ;;
esac
say ""; say "(Bericht: $REPORT)"
