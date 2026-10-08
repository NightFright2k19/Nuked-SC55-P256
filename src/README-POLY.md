# Nuked SC-55 Poly (CLAP)

Fork von [Nuked-SC55-CLAP](https://github.com/johnnovak/Nuked-SC55-CLAP) (Basis: Commit `de2a799`)
mit erweiterter Polyphonie: 128 Stimmen per Default, bis 256+ konfigurierbar.

## Prinzip

Die Polyphonie-Grenze (24 Partials beim SC-55, 28 beim SC-55mk2) steckt in der
Hardware des PCM-Chips (feste Slot-Zahl pro Sample-Takt, an die die Samplerate
gekoppelt ist) und in der Firmware. Beides lässt sich nicht sinnvoll „aufbohren“.
Stattdessen laufen N identische, bitgenaue Emulatoren parallel:

| Modell  | Partials/Instanz | 128-Ziel        | 256-Ziel         |
|---------|------------------|-----------------|------------------|
| SC-55   | 24               | 6 Inst. = 144   | 11 Inst. = 264   |
| SC-55mk2| 28               | 5 Inst. = 140   | 10 Inst. = 280   |

`src/poly_router.h` verteilt die MIDI-Daten:

- Note-On → Instanz mit der geringsten gemessenen Last (Hüllkurvenpegel aller
  PCM-Slots, `ram2[slot][10] != 0`) + Round-Robin bei Gleichstand.
- Note-Off / Poly-Aftertouch → die Instanz(en), die die Taste spielen.
- Wiederanschlag einer gehaltenen Taste → dieselbe Instanz.
- Mono-Mode (CC126) und Portamento (CC65) → Kanal wird auf eine Instanz gepinnt.
- Rhythm-Parts: Exklusiv-Gruppen (HiHat, Whistle, Guiro, Cuica, Triangle, Surdo)
  bleiben auf einer Instanz pro Kanal, damit Choke funktioniert. GS-SysEx
  „Use For Rhythm Part“ und „Rx Channel“ werden mitgelesen.
- Alles andere (CC, PC, Pitchbend, SysEx, GS/GM-Reset) geht an alle Instanzen.

Alle Instanzen booten identisch und erhalten identische Controller-/SysEx-Daten.
Dadurch laufen LFOs, Chorus und Reverb im Gleichschritt, und die Summe verhält
sich (linear) wie die Effektsektion eines einzelnen Geräts. Den konstanten
DC-Offset jeder Instanz (1/32 FS) wird für die Zusatzinstanzen kompensiert:
**Bei ≤ 24/28 Stimmen ist die Ausgabe bitidentisch mit einer einzelnen Instanz.**

## Konfiguration

- Build-Zeit: `-DNUKED_SC55_POLY_TARGET_VOICES=256` (Default 128)
- Laufzeit (überschreibt): Umgebungsvariable `NUKED_SC55_POLY_VOICES=256`
- ROMs: wie im Original, `Nuked-SC55-Resources/ROMs/...` neben dem Plugin oder
  `SOUNDCANVAS_ROM_PATH`. Neue Plugin-IDs (`net.nuked_sc55_poly_clap.*`), das
  Plugin lässt sich also parallel zum Original installieren.

## Capital Tone Fallback (CTF)

- SC-55 (alle Versionen): CTF ist Teil der Original-Firmware, immer aktiv.
- SC-55mk2: Liegt ein mit sc55mk2-ctf-patcher gepatchtes `rom2.bin` im
  ROM-Ordner, wird es bevorzugt geladen (das Original-Plugin nimmt immer das
  ungepatchte). Erzwingen einer bestimmten Variante:
  `NUKED_SC55_MK2_ROMSET=mk2-v1.01` (ohne CTF) oder z. B.
  `mk2-ctf-sc55-drum-sc55-v1.21`.

## VST2

`src/vst2/` enthält eine eigenständig geschriebene Beschreibung der VST-2.4-
Binärschnittstelle (kein Steinberg-SDK) und eine Brücke, die das CLAP-Plugin
derselben DLL als VST2 anbietet. Dafür werden Einzelmodell-Builds verwendet
(`-DNUKED_SC55_ONLY_MODEL=<0..5>`). Dieselbe Datei funktioniert als `.clap`
und als VST2-`.dll`. Siehe `build-mingw-win64.sh`.

## Dynamische Instanzen

Nur so viele Instanzen laufen, wie gebraucht werden. Schlafende Instanzen
verpassen Controller/SysEx; diese werden in `src/state_log.h` kompakt (letzter
Wert je Parameter, Reihenfolge nach letztem Auftreten) protokolliert und beim
Wecken nachgespielt. Abschalten: `NUKED_SC55_POLY_DYNAMIC=0`.

## Ein-Datei-Builds (eingebettete ROMs, nur privat)

`-DNUKED_SC55_EMBED_ROMS` + eine mit `tools/gen_embedded_roms.py` erzeugte
Quelle. Anzeigename: `-DNUKED_SC55_DISPLAY_NAME`, Windows-Versionsinfo:
`src/vst2/version.rc.in`. Siehe `tools/build-embedded-win64.sh.inc`.

## Kern-Optimierung: Timer

`src/nuked-sc55/backend/mcu_timer.cpp`: Ereignislose Timer-Ticks werden
gebündelt (nur Zähler erhöhen); Ticks mit Compare-Match, Überlauf oder nach
Registerzugriffen laufen mit der Originallogik. Bitgenau identisch zum
Originalkern, ca. 35–40 % schneller (mit -O3 -flto). Timer werden lazy pro Timer ausgewertet.

## Editor (Windows)

`src/gui/editor_win32.cpp` (Win32/GDI, prozedural gezeichnet im Stil der Gearmulator-Skins; Menüsprache nach Windows-UI-Sprache DE/EN): SC-55-artiges Panel mit Part-Parametern,
16 Part-Pegelbalken, Polyphonie-Anzeige und SETUP-Menü (Laufzeit-Stimmenlimit,
GS-Reset, alle Noten aus). Anbindung: CLAP `gui`-Extension, VST2
`effEditOpen`/`effEditClose`/`effEditGetRect`. Zustand (Limit) über CLAP `state`
bzw. VST2-Chunks; das Plugin schreibt keine Dateien. Änderungen im Menü werden dem Host gemeldet (CLAP `state.mark_dirty`, VST2 `audioMasterUpdateDisplay`).
Linken mit `-lgdi32 -luser32 -lmsimg32`.

## Build (Windows)

Wie im Original-README (vcpkg + CMake-Presets). Die Ausgabedatei heißt
`Nuked-SC55-Poly.clap`.

## CPU

Jede Instanz läuft durchgehend (die Firmware-Timer müssen weiterlaufen).
Rendering ist pro Instanz parallelisiert (Worker-Pool, Audio-Thread arbeitet mit).
Richtwert: N × die CPU-Last des Original-Plugins, verteilt auf mehrere Kerne.

## Bekannte Abweichungen zur Hardware

- Firmware-Voice-Stealing greift erst, wenn *alle* Instanzen voll sind, und
  dann innerhalb der gewählten Instanz – nicht global nach ältester Stimme.
- Interne Clipping-Grenze gilt pro Instanz, nicht für die Summe (in Float gibt
  es kein Summen-Clipping; bei sehr dichten Arrangements Pegel beachten).
- Effekte: Summen-Reverb/-Chorus ist mathematisch nahezu gleichwertig; minimale
  Unterschiede durch die Festkomma-Rundung pro Instanz sind möglich.
- Partial-Reserve (GS „Voice Reserve“) wirkt pro Instanz.
