#!/bin/sh
R=/home/claude/wb/roms; E=/home/claude/wb/tools/midi/e1m1.txt; T=/home/claude/wb/tools/midi/events.txt
for cfg in "SC-55-v1.21 mk1-v1.21 $T 85 Polytest-mk1" "SC-55mk2-v1.01 mk2-ctf-sc55-drum-sc55-v1.21 $T 85 Polytest-mk2" \
           "SC-55-v1.00 mk1-v1.00 $E 96 E1M1-v1.00" "SC-55-v2.00 mk1-v2.00 $E 96 E1M1-v2.00" "SC-55-v1.21 mk1-v1.21 $E 96 E1M1-v121-voll"; do
  set -- $cfg; [ -d $R/$1 ] || continue
  a=$(./ref_orig $R/$1 $2 $3 $4 $5); b=$(./ref_new $R/$1 $2 $3 $4 $5)
  ha=$(echo "$a" | awk '{print $3}'); hb=$(echo "$b" | awk '{print $3}'); ta=$(echo "$a" | awk '{print $(NF-1)}'); tb=$(echo "$b" | awk '{print $(NF-1)}')
  [ "$ha" = "$hb" ] && ok=IDENTISCH || ok=ABWEICHUNG
  echo "$5: $ok  ($ha)  alt ${ta}s -> neu ${tb}s"
done > /tmp/verify.txt 2>&1; touch /tmp/verifydone
