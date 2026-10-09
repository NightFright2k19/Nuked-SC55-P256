#!/bin/bash
# Baut die beiden ausgelieferten Ein-Datei-Plugins (ROMs eingebettet, NUR PRIVAT) nach tools/winbuild/out2/:
#   Nuked-SC55_v121.clap/.dll   und   Nuked-SC55_MkII.clap/.dll  (MkII mit CTF-rom2.bin)
#   sowie, falls 88emu gebaut ist (setup_wb.sh mit sc88pro.zip): Nuked-SC88_Pro.clap/.dll
#   und mit SC-8850-ROMs (setup_wb.sh, 4. Argument): Nuked-SC8850.clap/.dll
#   und mit SC-88-ROMs (setup_wb.sh, 5. Argument): Nuked-SC88.clap/.dll
set -e
WB=/home/claude/wb; S=$WB/src; R=$WB/roms; CXX=x86_64-w64-mingw32-g++-posix
cd $WB/tools/winbuild && mkdir -p gen out2 && . ./build2.sh
D1=$R/SC-55-v1.21; D2=$R/SC-55mk2-v1.01
build "Nuked-SC55_v121" "Nuked-SC55 v1.21" 3 0x53355033 2 "SC-55 v1.21 (embedded)" \
  0=$D1/sc55_rom1.bin 1=$D1/sc55_rom2.bin 3=$D1/sc55_waverom1.bin 4=$D1/sc55_waverom2.bin 5=$D1/sc55_waverom3.bin
build "Nuked-SC55_MkII" "Nuked-SC55 MkII" 5 0x5335504D 0 "SC-55mk2 v1.01 CTF (embedded)" \
  0=$D2/rom1.bin 1=$D2/rom2.bin 2=$D2/rom_sm.bin 3=$D2/waverom1.bin 4=$D2/waverom2.bin
if [ -f $WB/gearmulator/buildwin/source/ronaldo/88emu/88lib/lib88emu.a ] && [ -f $WB/rom88proc/control.bin ]; then
  echo "SC-88 Pro (88emu):"
  export GM=$WB/gearmulator LIBW=$WB/gearmulator/buildwin/source/ronaldo/88emu/88lib/lib88emu.a ROMPROC=$WB/rom88proc
  . ./build88.sh; build88
fi
if [ -f $WB/gearmulator/buildwin/source/ronaldo/88emu/88lib/lib88emu.a ] && [ -f $WB/rom8850/sc8850_wave.bin ]; then
  echo "SC-8850 (88emu):"
  export GM=$WB/gearmulator LIBW=$WB/gearmulator/buildwin/source/ronaldo/88emu/88lib/lib88emu.a ROM8850=$WB/rom8850
  . ./build8850.sh; build8850
fi
if [ -f $WB/gearmulator/buildwin/source/ronaldo/88emu/88lib/lib88emu.a ] && [ -f $WB/rom88sc/sc88_wave3.bin ]; then
  echo "SC-88 (88emu):"
  export GM=$WB/gearmulator LIBW=$WB/gearmulator/buildwin/source/ronaldo/88emu/88lib/lib88emu.a ROM88O=$WB/rom88sc
  . ./build88o.sh; build88o
fi
ls -la out2
