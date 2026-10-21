#!/bin/bash
# Watches the upstream repositories from upstream.lock and assesses new states.
#
#   upstream_check.sh report [name]          new commits since the recorded state, filtered to
#                                            the watched paths, classified by area (seconds,
#                                            without ROMs; also for the CI template)
#   upstream_check.sh trial  [name] [--force] [--win]
#                                            fetch the new state, apply our patches, build, check against
#                                            tests/golden.json (bit-identity, CPU), and create old/new WAV
#                                            pairs if the sound changed (about 5-15 min)
#   name: nuked-clap | gearmulator (default: both)
#   --force: trial even without relevant new commits (self-test of the check chain)
#   --win:   additionally build the Windows plug-ins with the new state (build check only)
#
# Result: report on the console and in out/upstream_<stage>.md. Exit code 0 except for
# errors of the check chain itself. In CI (UPSTREAM_CI=1) the lock file sits next to the script.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "${UPSTREAM_CI:-}" ]; then WB=$(cd "$HERE/.." && pwd); else WB=/home/claude/wb; fi
LOCK=$WB/upstream.lock; CACHE=$WB/upstream; OUT=$WB/out; mkdir -p "$CACHE" "$OUT"
MODE=${1:-report}; shift || true
ONLY=""; FORCE=0; WIN=0
for a in "$@"; do case "$a" in --force) FORCE=1;; --win) WIN=1;; *) ONLY=$a;; esac; done
REPORT=$OUT/upstream_${MODE}${ONLY:+_$ONLY}.md; : > "$REPORT"
say() { echo "$*"; echo "$*" >> "$REPORT"; }

# Area of a changed file (for the classification in the report)
category() { # <name> <path>
  case "$1:$2" in
    nuked-clap:src/nuked-sc55/*)                    echo "Emulation core (Nuked)";;
    nuked-clap:src/*)                               echo "Plug-in framework";;
    nuked-clap:*)                                   echo "Build";;
    gearmulator:source/ronaldo/custom_chips/xp/*)   echo "Sound chip XP (SC-88 Pro)";;
    gearmulator:source/ronaldo/88emu/88lib/rom/*)   echo "ROM loading (88emu)";;
    gearmulator:source/ronaldo/88emu/88lib/c_interface*) echo "C interface (88emu)";;
    gearmulator:source/ronaldo/88emu/88lib/*)       echo "Device emulation (88emu)";;
    gearmulator:*/assets/*)                         echo "Panel graphics";;
    gearmulator:source/cpu/*)                       echo "CPU cores / JIT";;
    gearmulator:source/framework/*)                 echo "Framework (synthLib/hardwareLib/baseLib)";;
    *)                                              echo "Other";;
  esac
}

# Git cache without file contents (fast, no API limits). gearmulator: an existing checkout is used.
repo_dir() { # <name> <url> <branch>
  local d=$CACHE/$1
  [ "$1" = gearmulator ] && [ -d "$WB/gearmulator/.git" ] && d=$WB/gearmulator
  if [ ! -d "$d/.git" ]; then git clone -q --filter=blob:none --no-checkout "$2" "$d" || return 1; fi
  git -C "$d" fetch -q --filter=blob:none origin "$3" || return 1
  echo "$d"
}

declare -A HEADS RELEVANT
report_one() { # <name> <url> <branch> <pin> <paths...>
  local name=$1 url=$2 br=$3 pin=$4; shift 4; local paths=("$@")
  local d; d=$(repo_dir "$name" "$url" "$br") || { say "## $name: fetch failed"; return 1; }
  local head; head=$(git -C "$d" rev-parse FETCH_HEAD)
  git -C "$d" cat-file -e "$pin^{commit}" 2>/dev/null || git -C "$d" fetch -q --filter=blob:none origin "$pin"
  local all rel
  all=$(git -C "$d" rev-list --count "$pin..$head")
  rel=$(git -C "$d" rev-list --count "$pin..$head" -- "${paths[@]}")
  HEADS[$name]=$head; RELEVANT[$name]=$rel
  say "## $name"
  say "- recorded: \`${pin:0:9}\` · current (\`$br\`): \`${head:0:9}\` · $all new commits, of which **$rel** in watched paths"
  [ "$rel" -eq 0 ] && { say "- Assessment: nothing to do"; say ""; return 0; }
  say "- Affected areas:"
  git -C "$d" diff --name-only "$pin" "$head" -- "${paths[@]}" | while read -r f; do category "$name" "$f"; done \
    | sort | uniq -c | sort -rn | while read -r n c; do say "  - $c: $n file(s)"; done
  say "- Relevant commits (newest first):"
  git -C "$d" log --format='  - `%h` %ad %s' --date=short "$pin..$head" -- "${paths[@]}" | head -25 | while read -r l; do say "$l"; done
  say "- Recommendation: \`upstream_check.sh trial $name\` (patches, build, bit-identity, CPU)"
  say ""
}

for_each_lock() { # calls <function> with the fields of every lock line
  grep -vE '^\s*(#|$)' "$LOCK" | while read -r name url br pin rest; do
    [ -n "$ONLY" ] && [ "$ONLY" != "$name" ] && continue
    # shellcheck disable=SC2086
    "$1" "$name" "$url" "$br" "$pin" $rest
  done
}

golden() { python3 -c "import json,sys; print(json.load(open('$WB/tests/golden.json'))['values'].get(sys.argv[1],{}).get(sys.argv[2],''))" "$1" "$2"; }
secs_of() { python3 -c "import sys,re; m=re.search(r'([0-9.]+)\s*s',sys.argv[1]); print(m.group(1) if m else 0)" "$1"; }
# CPU deviation in percent; up to +-10 % counts as measurement noise in the sandbox (1 core, shared)
pct() { python3 -c "import sys; a,b=float(sys.argv[1]),float(sys.argv[2]); d=(b-a)/a*100 if a>0 else 0; print(('%+.0f %%'%d)+(' (noise)' if abs(d)<=10 else (' (faster)' if d<0 else ' (slower)')) if a>0 else 'n/a')" "$1" "$2"; }
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
verdict() { VERDICT+=("$1: $2"); say "- **Result $1: $2**"; }

# --- Trial run Nuked-SC55 CLAP: new state + our complete patch series (git am -3)
trial_nuked() { # <name> <url> <branch> <pin> ...
  local name=$1 pin=$4 d head tdir
  d=$(repo_dir "$name" "$2" "$3") || return; head=${HEADS[$name]:-$(git -C "$d" rev-parse FETCH_HEAD)}
  say "## Trial $name: \`${pin:0:9}\` -> \`${head:0:9}\`"
  if [ "${RELEVANT[$name]:-1}" -eq 0 ] && [ $FORCE -eq 0 ]; then verdict "$name" "no relevant changes (no trial needed)"; return; fi
  tdir=$WB/trial/nuked-clap; rm -rf "$tdir"; git -C "$d" worktree prune
  git -C "$d" worktree add -q --detach "$tdir" "$head" || { verdict "$name" "checkout failed"; return; }
  if ! git -C "$tdir" -c user.email=t@t -c user.name=t am -q -3 "$WB"/patches/*.patch > "$OUT/trial_nuked_am.log" 2>&1; then
    local p; p=$(git -C "$tdir" am --show-current-patch=diff 2>/dev/null | head -1)
    git -C "$tdir" am --abort 2>/dev/null
    verdict "$name" "our patches do not apply (conflict, see out/trial_nuked_am.log) - adaptation needed"; return
  fi
  say "- Patch series applied ($(ls "$WB"/patches/*.patch | wc -l) patches)"
  cmake -S "$tdir" -B "$tdir/build" -DCMAKE_BUILD_TYPE=Release > "$OUT/trial_nuked_build.log" 2>&1 && \
    cmake --build "$tdir/build" -j2 >> "$OUT/trial_nuked_build.log" 2>&1 || { verdict "$name" "Linux build failed (out/trial_nuked_build.log)"; return; }
  local S=$tdir C=$tdir/src/nuked-sc55
  g++ -O2 -std=c++23 -I$S/include -I$S/src -I$S/build "$WB/tools/test/play.cpp" $(find $S/build -name '*.o') -lspeexdsp -lpthread -o "$tdir/play" 2>>"$OUT/trial_nuked_build.log" || { verdict "$name" "test host cannot be built (interface changed?)"; return; }
  g++ -O2 -std=c++20 -I$C -I$C/backend -I$S/src "$WB/tools/exact/ref.cpp" $C/backend/*.cpp $C/common/*.cpp "$WB/tools/harness/sha.o" -o "$tdir/ref" 2>>"$OUT/trial_nuked_build.log" || { verdict "$name" "core test program cannot be built"; return; }
  say "- Linux build and test hosts: ok"
  local changed=0 o h t g gt
  export SOUNDCANVAS_ROM_PATH=$WB/roms NUKED_SC55_POLY_VOICES=256
  for m in "SC-55-v1.21 mk1-v1.21 core_v121" "SC-55mk2-v1.01 mk2-ctf-sc55-drum-sc55-v1.21 core_mk2"; do set -- $m
    o=$("$tdir/ref" "$WB/roms/$1" "$2" "$WB/tools/midi/events.txt" 40 x); h=$(echo "$o" | awk '{print $3}'); t=$(echo "$o" | awk '{print $(NF-1)}')
    g=$(golden "$3" hash); gt=$(golden "$3" seconds)
    if [ "$h" = "$g" ]; then say "- $3: bit-identical, CPU $(pct "$gt" "$t") (reference ${gt}s, new ${t}s)"; else changed=1; say "- $3: **output changed** (hash $h instead of $g), CPU $(pct "$gt" "$t")"; fi
  done
  mkdir -p "$OUT/ab"
  for m in "sc55_v1_21 plugin_v121" "sc55mk2_v1_01 plugin_mk2"; do set -- $m
    o=$(cd "$WB/tools/test" && "$tdir/play" net.nuked_sc55_poly_clap.$1 "$tdir/$2.f32" | grep "compute time"); t=$(secs_of "$o")
    h=$(sha256sum "$tdir/$2.f32" | cut -c1-16); g=$(golden "$2" hash); gt=$(golden "$2" seconds)
    if [ "$h" = "$g" ]; then say "- $2: bit-identical, CPU $(pct "$gt" "$t")"
    else changed=1; say "- $2: **output changed**, CPU $(pct "$gt" "$t") -> WAV pair out/ab/${name}_$2_{old,new}.wav"
      wav "$WB/out/golden/$2.f32" "$OUT/ab/${name}_$2_old.wav"; wav "$tdir/$2.f32" "$OUT/ab/${name}_$2_new.wav"; fi
  done
  if [ $WIN -eq 1 ]; then
    ( cd "$WB/tools/winbuild" && export S=$tdir CXX=x86_64-w64-mingw32-g++-posix R=$WB/roms && . ./build2.sh && mkdir -p out_trial && sed 's#out2/#out_trial/#g' build2.sh > /tmp/b2t.sh && . /tmp/b2t.sh && \
      build "Trial_v121" "Nuked-SC55 v1.21" 3 0x53355033 2 "trial" 0=$R/SC-55-v1.21/sc55_rom1.bin 1=$R/SC-55-v1.21/sc55_rom2.bin 3=$R/SC-55-v1.21/sc55_waverom1.bin 4=$R/SC-55-v1.21/sc55_waverom2.bin 5=$R/SC-55-v1.21/sc55_waverom3.bin ) > "$OUT/trial_nuked_win.log" 2>&1
    [ -f "$WB/tools/winbuild/out_trial/Trial_v121.dll" ] && say "- Windows build: ok" || say "- Windows build: **failed** (out/trial_nuked_win.log)"
  fi
  [ $changed -eq 0 ] && verdict "$name" "can be adopted, sound bit-identical (CPU comparison see above)" \
                     || verdict "$name" "sound changed - listening comparison of the WAV pairs needed, then decide deliberately"
}

# --- Trial run Gearmulator/88emu: new state + our 88emu patches, Linux library, plug-in test
trial_gm() { # <name> <url> <branch> <pin> ...
  local name=$1 pin=$4 d head tdir
  d=$(repo_dir "$name" "$2" "$3") || return; head=${HEADS[$name]:-$(git -C "$d" rev-parse FETCH_HEAD)}
  say "## Trial $name: \`${pin:0:9}\` -> \`${head:0:9}\`"
  if [ "${RELEVANT[$name]:-1}" -eq 0 ] && [ $FORCE -eq 0 ]; then verdict "$name" "no relevant changes (no trial needed)"; return; fi
  [ -x "$WB/tools/test/play88" ] && [ -d "$WB/rom88proc" ] || { verdict "$name" "SC-88 Pro environment missing (setup_wb.sh with sc88pro.zip)"; return; }
  tdir=$WB/trial/gearmulator; rm -rf "$tdir"
  if ! GM_COMMIT=$head bash "$WB/src/tools/build_88emu.sh" "$tdir" "$WB/src" > "$OUT/trial_gm_build.log" 2>&1; then
    grep -q "patch failed\|does not apply" "$OUT/trial_gm_build.log" && verdict "$name" "88emu patches do not apply (out/trial_gm_build.log) - adaptation needed" \
      || verdict "$name" "build failed (out/trial_gm_build.log)"; return
  fi
  local L=$tdir/build88/source/ronaldo/88emu/88lib/lib88emu.a I=$tdir/source/ronaldo/88emu/88lib
  [ -f "$L" ] || { verdict "$name" "Linux library missing (out/trial_gm_build.log)"; return; }
  say "- Patches applied, Linux and Windows library built"
  mkdir -p "$tdir/rp"; "$tdir/romdump" "$WB/rom88" "$tdir/rp/control.bin" "$tdir/rp/waveA.bin" "$tdir/rp/waveB.bin" "$tdir/rp/waveC.bin" > /dev/null
  [ "$(cat "$tdir"/rp/*.bin | sha256sum | cut -c1-16)" = "$(golden romset_88pro hash)" ] && say "- ROM preparation: unchanged" || say "- ROM preparation: **changed** (embedded ROM data would have to be regenerated)"
  local S=$WB/src P=$tdir/p88 D88="-DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_ONLY_MODEL=6 -DNUKED_SC55_POLY_TARGET_VOICES=256"
  mkdir -p "$P"; for f in nuked_sc55 plugin; do g++ -O2 -std=c++23 $D88 -I$S/include -I$S/src -I$S/build -I$I -c $S/src/$f.cpp -o "$P/$f.o" || { verdict "$name" "plug-in no longer compiles against the new 88emu interface"; return; }; done
  cp $(find $S/build -name '*.o' | grep -v -E "/nuked_sc55.cpp.o|/plugin.cpp.o") "$P/"
  g++ -O2 -std=c++23 $D88 -I$S/include -I$S/src -I$S/build -I$I "$WB/tools/test/play.cpp" "$P"/*.o "$L" -lspeexdsp -lpthread -ldl -o "$tdir/play88" || { verdict "$name" "test host cannot be linked"; return; }
  local o t h g gt
  o=$(cd "$WB/tools/test" && SOUNDCANVAS_ROM_PATH=$WB/rom88 NUKED_SC55_POLY_VOICES=256 "$tdir/play88" net.nuked_sc55_poly_clap.sc88pro "$tdir/plugin_88pro.f32" | grep "compute time"); t=$(secs_of "$o")
  h=$(sha256sum "$tdir/plugin_88pro.f32" | cut -c1-16); g=$(golden plugin_88pro hash); gt=$(golden plugin_88pro seconds)
  mkdir -p "$OUT/ab"
  if [ "$h" = "$g" ]; then say "- plugin_88pro: bit-identical, CPU $(pct "$gt" "$t") (reference ${gt}s, new ${t}s)"; verdict "$name" "can be adopted, sound bit-identical"
  else say "- plugin_88pro: **output changed**, CPU $(pct "$gt" "$t") -> WAV pair out/ab/${name}_plugin_88pro_{old,new}.wav"
    wav "$WB/out/golden/plugin_88pro.f32" "$OUT/ab/${name}_plugin_88pro_old.wav"; wav "$tdir/plugin_88pro.f32" "$OUT/ab/${name}_plugin_88pro_new.wav"
    verdict "$name" "sound changed - listening comparison of the WAV pairs needed, then decide deliberately"; fi
}

say "# Upstream check ($MODE) – $(date '+%Y-%m-%d %H:%M')"
say ""
case "$MODE" in
  report) for_each_lock report_one ;;
  trial)
    [ -f "$WB/tests/golden.json" ] || { say "tests/golden.json missing: run tools/make_golden.sh first"; exit 1; }
    for_each_lock report_one
    # report_one sets HEADS/RELEVANT in a subshell (pipe) -> determine them again without a pipe
    while read -r name url br pin rest; do
      [ -n "$ONLY" ] && [ "$ONLY" != "$name" ] && continue
      d=$(repo_dir "$name" "$url" "$br") || continue
      HEADS[$name]=$(git -C "$d" rev-parse FETCH_HEAD)
      # shellcheck disable=SC2086
      RELEVANT[$name]=$(git -C "$d" rev-list --count "$pin..${HEADS[$name]}" -- $rest)
      case "$name" in nuked-clap) trial_nuked "$name" "$url" "$br" "$pin";; gearmulator) trial_gm "$name" "$url" "$br" "$pin";; esac
    done < <(grep -vE '^\s*(#|$)' "$LOCK")
    say ""; say "## Summary"; for v in "${VERDICT[@]}"; do say "- $v"; done ;;
  *) echo "usage: $0 report|trial [nuked-clap|gearmulator] [--force] [--win]"; exit 2 ;;
esac
say ""; say "(report: $REPORT)"
