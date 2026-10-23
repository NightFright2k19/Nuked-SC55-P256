# Plug-in reference

Plug-in names and IDs, the plug-in state and the configuration options. Part of the [documentation index](../README.md#documentation).

## The plug-ins

| Display name | File name (`.clap` / `.dll`) | Core | CLAP ID | VST2 ID | Default max voices |
| --- | --- | --- | --- | --- | --- |
| Emu-MT32 | `Emu-MT32` | 88emu | `net.nuked_sc55_poly_clap.mt32` | `MT32` (0x4D543332) | 32 (of 64) |
| Nuked-SC55 v1.21 | `Nuked-SC55_v121` | Nuked SC-55 | `net.nuked_sc55_poly_clap.sc55_v1_21` | `S5P3` (0x53355033) | 264 |
| Nuked-SC55 MkII | `Nuked-SC55_MkII` | Nuked SC-55 | `net.nuked_sc55_poly_clap.sc55mk2_v1_01` | `S5PM` (0x5335504D) | 280 |
| Emu-SC88 | `Emu-SC88` | 88emu | `net.nuked_sc55_poly_clap.sc88` | `S888` (0x53383838) | 256 |
| Emu-SC88 Pro | `Emu-SC88_Pro` | 88emu | `net.nuked_sc55_poly_clap.sc88pro` | `S8PP` (0x53385050) | 256 |
| Emu-SC8820 | `Emu-SC8820` | 88emu | `net.nuked_sc55_poly_clap.sc8820` | `S820` (0x53383230) | 256 |
| Emu-SC8850 | `Emu-SC8850` | 88emu | `net.nuked_sc55_poly_clap.sc8850` | `S885` (0x53383835) | 256 |

- The file names contain no spaces or dots (some hosts and drivers dislike them); the display names in the host stay "Nuked-SC55 v1.21", "Nuked-SC55 MkII" and "Emu-SC88 Pro". The SC-55 models keep the name "Nuked" (Nuked core); all models on the 88emu core are named "Emu" (since issue #79; before: "Nuked-SC88" etc., same plug-in IDs).
- Both formats are the **same file** with two entry points (`clap_entry` and `VSTPluginMain`). The builder writes it twice, once as `.clap` and once as `.dll`.
- Category: instrument/synth; flags: editor, replacing, program chunks. VST2 vendor `Emu MT-32 P256` (MT-32 / CM-32L), `Nuked SC-55 P256` (SC-55; SC-88 and SC-88 Pro: `Emu SC-88 P256`, SC-8820: `Emu SC-8820 P256`, SC-8850: `Emu SC-8850 P256`).
- The IDs of the plug-ins never changed since the first version, so projects stay compatible. Hosts may show a new vendor after a rescan.
- Windows version info: product name, description and internal name are the display name; file and product version are the build date (`2026.10.07` is stored as `2026,10,7,0` numerically, which Windows shows as `2026.10.7.0`).
- A DLL with ROMs is large: about 10 MB for the MT-32 / CM-32L (2.4 MB ROMs for all three devices, 4 MB panel graphics), about 14 MB for the SC-88 (8.5 MB ROMs), about 27 MB for the SC-88 Pro (21 MB ROMs, 1 MB panel graphics), about 35 MB for the SC-8820 (26 MB ROMs, 4 MB panel graphics for both interfaces), about 42 MB for the SC-8850 (35 MB ROMs).

---

## Settings and plug-in state

The DLL **never writes files**. The only state is the plug-in state, saved by the host in the CLAP `state` extension or as a VST2 chunk (opcodes 23/24). A change in the menu is reported to the host (CLAP `state.mark_dirty`, VST2 `audioMasterUpdateDisplay`). The state is a short ASCII string:

```
NSC55P1 max_voices=<n> gain=<-120..120> dev=<0..2>                      MT-32 (dev: 0 MT-32 old, 1 MT-32 new, 2 CM-32L)
NSC55P1 max_voices=<n> gain=<-120..120>                                 SC-55 plug-ins (gain in 0.1 dB)
NSC55P1 max_voices=<n> map=<0..1> gain=<-120..120> smode=<0..1>          SC-88 (map: 0 SC-55, 1 SC-88)
NSC55P1 max_voices=<n> map=<0..2> gain=<-120..120> pnote=<0..127> smode=<0..1>    SC-88 Pro
NSC55P1 max_voices=<n> map=<0..3> gain=<-120..120> style=<1..2>         SC-8820 (map: INST MAP, 0 SC-55 ... 3 SC-8820; style: 1 SC-8820, 2 SC-D70)
NSC55P1 max_voices=<n> gain=<-120..120>                                 SC-8850
```

`smode` = last system mode chosen in the SETUP menu (0 = Mode 1, 1 = Mode 2). It is only the mark in the menu: loading a state sends nothing, and the start-up stays as before (SC-55 / SC-55mkII: GS reset at boot, 88emu devices: factory state without a reset).

A new plug-in instance starts with the maximum number of voices (MT-32 / CM-32L: 32, one unit like the device). Old chunks without `map=` start with the SC-88 Pro map (before Issue #20: SC-88); chunks with `map=` keep their map. Whether a particular host or driver keeps the chunk is up to the host; if it does not, the plug-in starts with the maximum each time, which is harmless.

---

## Configuration (environment variables)

All of these are optional.

| Variable | Meaning |
| --- | --- |
| `NUKED_SC55_POLY_VOICES` | number of voices at load time (overrides the build-time target) |
| `NUKED_SC55_POLY_DYNAMIC=0` | all instances stay awake (no sleeping) |
| `NUKED_SC55_POLY_MIN_ACTIVE` | minimum number of awake instances (default 1) |
| `NUKED_SC55_MK2_ROMSET` | force a SC-55mk2 ROM set (external ROMs only) |
| `SOUNDCANVAS_ROM_PATH` | directories with ROM folders (external ROMs only; the OS path separator is the delimiter) |
| `NUKED_SC55_NO_WARMER=1` | test switch: no background synchronisation |
| `NUKED_SC55_NO_CLONE=1` | test switch (SC-55): every unit boots itself |
| `NUKED_SC55_NO_BGBOOT=1` | test switch (88emu models): all units boot inside the activation, also on machines with fewer cores than units (the default there is the background boot) |
| `NUKED_SC55_NO_DISPPACE=1` | test switch: display SysEx to all units, unpaced (old behaviour) |

Build-time options (CMake cache variable or compiler define):

| Option | Meaning |
| --- | --- |
| `NUKED_SC55_POLY_TARGET_VOICES` | polyphony target (default 128 in CMake, 256 in the shipped builds) |
| `NUKED_SC55_ONLY_MODEL=<0..10>` | single-model build: 0 = v1.00, 1 = v1.10, 2 = v1.20, 3 = v1.21, 4 = v2.00, 5 = mk2 v1.01, 6 = SC-88 Pro, 7 = SC-8850, 8 = SC-88, 9 = SC-8820, 10 = MT-32 / CM-32L |
| `NUKED_SC55_ENGINE_88PRO` | use the 88emu engine |
| `NUKED_SC55_EMBED_ROMS` | embedded ROMs (with a source from `tools/gen_embedded_roms.py`) |
| `NUKED_SC55_DISPLAY_NAME`, `NUKED_SC55_VST2_NAME`, `NUKED_SC55_VST2_ID` | names and VST2 unique ID |
