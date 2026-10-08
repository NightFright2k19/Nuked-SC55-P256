# Eigene Aenderungen am Nuked-SC55-Emulationskern

`nuked-core-timers.patch` enthaelt saemtliche Aenderungen dieses Projekts an `src/nuked-sc55`
gegenueber Nuked-SC55-CLAP `de2a799` (Dateiname historisch):
- bitgenaue, lazy Auswertung der MCU-Timer (FRT/TMR) pro Timer, ca. 35-40 % weniger Rechenzeit;
- Speicher: Wave-ROMs in einem gemeinsam nutzbaren Block (`pcm_t::waverom_block`, calloc,
  ungenutzte Karten-/Erweiterungsbereiche belegen keinen Speicher, `Emulator::ShareWaveRomsFrom`),
  LCD-Pixelpuffer erst bei `LCD_Start` (Plugin: ca. 19,5 -> 1,1 MB je Einheit).
- Zustandskopie: `Emulator::CopyStateFrom()` macht einen Emulator zur exakten Kopie eines anderen
  (gleicher ROM-Satz); das Plugin bootet nur eine Einheit und kopiert sie (MkII-Laden 6,4 -> 0,6 s).
Ausgabe in beiden Faellen bitidentisch (Hash-Vergleich gegen tests/golden.json).

Der Patch ist zusaetzlich zur vollstaendigen Patch-Serie abgelegt, damit er sich bei einem neuen
Upstream-Stand einzeln pruefen laesst (`tools/upstream_check.sh trial`). Alle anderen Aenderungen
betreffen nur den Plugin-Rahmen (`src/*.cpp`, `src/gui`, `src/vst2`).
