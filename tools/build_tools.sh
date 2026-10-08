#!/bin/bash
# Baut alle Test- und Messwerkzeuge. usage: build_tools.sh [Ordner mit D_E1M1.mid/Animus.mid]
set -e
WB=/home/claude/wb; S=$WB/src; C=$S/src/nuked-sc55; T=$WB/tools; MIDIDIR=${1:-/mnt/user-data/uploads}

echo "  Linux-Plugin (CMake, liefert die Objekte fuer die Test-Hosts)"
cmake -S $S -B $S/build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build $S/build -j2 2>&1 | grep -E "error|Built target" || true

echo "  harness/ (einzelner Emulator, aktueller Kern)"
cd $T/harness && gcc -O2 -c $C/sha/sha224-256.c -o sha.o
for t in bench probe partials ctf locs which; do
  g++ -O2 -std=c++20 -I$S/src -I$C -I$C/backend $t.cpp $C/backend/*.cpp $C/common/*.cpp sha.o -o $t
done

echo "  exact/ (ref_orig = Upstream-Kern, ref_new = aktueller Kern, beide -O2)"
cd $T/exact
g++ -O2 -std=c++20 -Icore_orig -Icore_orig/backend -I$S/src ref.cpp core_orig/backend/*.cpp core_orig/common/*.cpp ../harness/sha.o -o ref_orig
g++ -O2 -std=c++20 -I$C -I$C/backend -I$S/src ref.cpp $C/backend/*.cpp $C/common/*.cpp ../harness/sha.o -o ref_new

echo "  test/ (CLAP-Ebene, linkt die Objekte des Linux-Builds)"
cd $T/test; OBJ=$(find $S/build -name '*.o')
for t in host play e1 diag peaks lcddump mem act idle; do
  g++ -O2 -std=c++23 -I$S/include -I$S/src -I$S/build $t.cpp $OBJ -lspeexdsp -lpthread -o $t
done

echo "  winbuild/ (MinGW-Hilfsobjekte + Windows-Test-Hosts)"
cd $T/winbuild; mkdir -p inc/speex gen out2
cp $WB/speexdsp/include/speex/*.h inc/speex/
printf '#ifndef __SPEEX_TYPES_H__\n#define __SPEEX_TYPES_H__\n#include <stdint.h>\ntypedef int16_t spx_int16_t; typedef uint16_t spx_uint16_t;\ntypedef int32_t spx_int32_t; typedef uint32_t spx_uint32_t;\n#endif\n' > inc/speex/speexdsp_config_types.h
cp $S/build/plugin.h .
x86_64-w64-mingw32-gcc-posix -O2 -c -DFLOATING_POINT -DUSE_SSE -DUSE_SSE2 -DEXPORT= -Iinc -Iinc/speex -I$WB/speexdsp/libspeexdsp $WB/speexdsp/libspeexdsp/resample.c -o resample.o
x86_64-w64-mingw32-gcc-posix -O2 -c $C/sha/sha224-256.c -o sha.o
x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/src vsthost.cpp -o vsthost.exe -static
x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/include hostw.cpp -o hostw.exe -static
x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/src guihost.cpp -o guihost.exe -static -lgdi32 -luser32
x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/include clapgui.cpp -o clapgui.exe -static -lgdi32 -luser32
x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/src lcdhost.cpp -o lcdhost.exe -static -lgdi32 -luser32
x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/src memhost.cpp -o memhost.exe -static -lpsapi
x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/src guiload.cpp -o guiload.exe -static -lgdi32 -luser32

echo "  midi/ (Polyphonie-Test neu erzeugen, Eventlisten)"
cd $T/midi && python3 gen.py 2> info.txt
for m in D_E1M1:e1m1 Animus:animus grabbag:grabbag; do
  f=$MIDIDIR/${m%%:*}.mid; [ -f "$f" ] && python3 smf2ev.py "$f" ${m##*:}.txt || echo "  (optional fehlt: $f)"
done
# LCD-Testdateien des Nutzers (LCD_Test_Midis.zip: StarGame.mid, 3X3EYES_mod.mid)
if [ -f "$MIDIDIR/LCD_Test_Midis.zip" ]; then
  mkdir -p lcd && unzip -q -o "$MIDIDIR/LCD_Test_Midis.zip" -d lcd
  for f in lcd/*.mid; do python3 smf2ev.py "$f" "${f%.mid}.txt"; done
fi

if [ -f $WB/gearmulator/build88/source/ronaldo/88emu/88lib/lib88emu.a ]; then
  echo "  SC-88 Pro: Linux-Testvariante des Plugins + diag88/play88/peaks88/map88"
  I88=$WB/gearmulator/source/ronaldo/88emu/88lib; L88=$WB/gearmulator/build88/source/ronaldo/88emu/88lib/lib88emu.a
  D88="-DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_ONLY_MODEL=6 -DNUKED_SC55_POLY_TARGET_VOICES=256"
  P=$T/test/build88p; rm -rf $P; mkdir -p $P
  for f in nuked_sc55 plugin; do g++ -O2 -std=c++23 $D88 -I$S/include -I$S/src -I$S/build -I$I88 -c $S/src/$f.cpp -o $P/$f.o; done
  cp $(find $S/build -name '*.o' | grep -v -E "/nuked_sc55.cpp.o|/plugin.cpp.o") $P/
  cd $T/test; for t in diag peaks play map88 ks88 panel88; do n=$t; [ $t = map88 ] || [ $t = ks88 ] || [ $t = panel88 ] || n=${t}88
    g++ -O2 -std=c++23 $D88 -I$S/include -I$S/src -I$S/build -I$I88 $t.cpp $P/*.o $L88 -lspeexdsp -lpthread -ldl -o $n; done
  cd $T/sc88probe; for t in probe voices loop; do gcc -O2 -c $t.c -I$I88 -o $t.o && g++ $t.o $L88 -lpthread -ldl -o $t; done
fi

# Display-Renderer: Teilaktualisierung gegen Vollberechnung (pixelgleich?) + Rechenzeit, ohne Windows
( cd $T/test && g++ -O2 -std=c++20 -I$S/src -I$S/src/gui lcdtest.cpp -o lcdtest )
