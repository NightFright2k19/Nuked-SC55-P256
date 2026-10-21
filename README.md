# Nuked SC-55 P256

<p align="center">
<img width="918" height="280" alt="mt32" src="docs/images/mt32_panel.png" />
<img width="918" height="280" alt="cm32l" src="docs/images/cm32l_panel.png" />
<img width="918" height="280" alt="sc55" src="https://github.com/user-attachments/assets/b166fa58-a380-4987-9cbe-c36dcb0e3541" />
<img width="918" height="280" alt="sc55_mk2" src="https://github.com/user-attachments/assets/2ad02a8b-aef4-44f3-b5ed-8b1a7bf05e6b" />
<img width="918" height="280" alt="sc_88" src="docs/images/sc88_panel.png" />
<img width="918" height="280" alt="sc_88pro" src="docs/images/sc88pro_panel.png" />
<img width="918" height="280" alt="sc8820" src="docs/images/sc8820_panel.png" />
<img width="918" height="280" alt="sc8820d70" src="docs/images/sc8820d70_panel.png" />
<img width="918" height="280" alt="sc_8850" src="docs/images/sc8850_panel.png" />
</p>

Single-file **CLAP** and **VST2** plug-ins (Windows x64) for the Roland **MT-32 / CM-32L**, **SC-55 (v1.21)**, **SC-55mk2 (v1.01, with Capital Tone Fallback)**, **SC-88**, **SC-88 Pro**, **SC-8820** and **SC-8850**, built on the low-level emulation of [Nuked SC-55](https://github.com/nukeykt/Nuked-SC55) and [88emu](https://github.com/dsp56300/gearmulator). The original modules stop at 32 (MT-32, CM-32L), 24 (SC-55), 28 (SC-55mk2), 64 (SC-88, SC-88 Pro and SC-8820) or 128 (SC-8850) simultaneous voices. These plug-ins run several bit-identical emulator instances side by side and distribute the MIDI notes among them, which raises the limit to **64 voices** (MT-32 / CM-32L), **264 voices** (SC-55), **280 voices** (SC-55mk2), **256 voices** (SC-88, SC-88 Pro, SC-8820 and SC-8850) without changing how a single module sounds.

> [!IMPORTANT]
> The plug-ins aim to preserve an important part of DOS gaming history for all to enjoy. They are only intended for **personal use** (retro gaming, writing music as a hobby) and **research purposes**. **No ROM files are included in this repository.** Plug-ins that contain your ROMs (everything the builder produces) are for **private use only** and must not be shared.

## Table of contents

- [Changes compared to Nuked-SC55-CLAP](#changes-compared-to-nuked-sc55-clap)
- [Quick start](#quick-start)
- [Supported models and ROM sets](#supported-models-and-rom-sets)
- [The plug-ins](#the-plug-ins)
- [MT-32 / CM-32L engine (88emu)](#mt-32--cm-32l-engine-88emu)
- [How the polyphony works](#how-the-polyphony-works)
- [Capital Tone Fallback (SC-55mk2)](#capital-tone-fallback-sc-55mk2)
- [SC-88 Pro engine (88emu)](#sc-88-pro-engine-88emu)
- [SC-88 engine (88emu)](#sc-88-engine-88emu)
- [SC-8820 engine (88emu)](#sc-8820-engine-88emu)
  - [SC-8820 part display](#sc-8820-part-display)
  - [SC-D70 interface](#sc-d70-interface)
- [SC-8850 engine (88emu)](#sc-8850-engine-88emu)
- [Panels and controls](#panels-and-controls)
- [Settings and plug-in state](#settings-and-plug-in-state)
- [Configuration (environment variables)](#configuration-environment-variables)
- [The patcher](#the-patcher)
- [The P256 builder](#the-p256-builder)
- [Building from source](#building-from-source)
- [Performance and memory](#performance-and-memory)
- [Verification](#verification)
- [Known limitations and open points](#known-limitations-and-open-points)
- [Known issues](#known-issues)
- [Using the plug-ins](#using-the-plug-ins)
- [Development and test tools](#development-and-test-tools)
- [Credits and license](#credits-and-license)

---

## Changes compared to Nuked-SC55-CLAP

This project started as a fork of John Novak's [Nuked-SC55-CLAP](https://github.com/johnnovak/Nuked-SC55-CLAP) (base: commit `de2a799`, followed by 31 commits of its own). The original is a CLAP plug-in without a user interface that loads one emulated module from ROM files next to the plug-in.

### Polyphony

- **Multi-instance polyphony:** N identical, bit-exact emulators run in parallel and their outputs are summed. A note router in `src/poly_router.h` decides which instance plays which note. Result: **64 partials** (MT-32 / CM-32L, 2 units of 32), **264 voices** (SC-55 v1.21, 11 units), **280 voices** (SC-55mk2, 10 units), **256 voices** (SC-88, SC-88 Pro and SC-8820, 4 units of 64; SC-8850, 2 units of 128).
- **Dynamic instances:** only as many instances run as the music needs. Sleeping instances are brought up to date from a compact state log when they wake (`src/state_log.h`), and a background thread pre-synchronises the next sleeping units so that waking up never causes a dropout.
- **Constant DC offset compensation:** every instance outputs a constant 1/32 of full scale; it is measured after boot and subtracted from all additional instances. With 24/28 voices or fewer, the output is **bit-identical** with a single module.
- **Runtime voice limit** (SETUP menu): 24 / 48 / 64 / 96 / 128 / 160 / 192 / 224 / 256 (SC-55mk2: 28 / 56 / ...). Instances above the limit receive no new notes and go to sleep once silent.

### Models

- **MT-32 / CM-32L:** one plug-in for the MT-32 (old board, 1.x firmware), the MT-32 (new board, 2.x) and the CM-32L, chosen in the SETUP menu ("Device"). The 88emu LA board with the original firmware, the device's own panel (MT-32: LCD, PART / RHYTHM and function switches; CM-32L: MIDI MESSAGE / POWER lamps), GAIN, up to 2 units = 64 partials. See [MT-32 / CM-32L engine](#mt-32--cm-32l-engine-88emu).
- **SC-55 v1.21:** the Nuked SC-55 core with the v1.21 firmware as a single-file plug-in (ROMs built in or inserted by the builder; the original plug-in loads them from a folder next to it), 11 units = 264 voices, and the original front panel driven by the firmware, with GAIN. See [SC-55 / SC-55mkII panel](#sc-55--sc-55mkii-panel-918-x-280-px).
- **SC-55mk2 with CTF:** the plug-in prefers a `rom2.bin` patched with Capital Tone Fallback (the original plug-in always loads the unpatched one). In the single-file builds, the CTF `rom2.bin` is built in.
- **SC-88:** the 88emu engine with the firmware of the first SC-88, 32 parts on two MIDI inputs like the SC-88 Pro, the original front panel driven by the firmware, and GAIN. See [SC-88 engine](#sc-88-engine-88emu).
- **SC-88 Pro:** a second engine based on the 88emu core from the Gearmulator project, with the SC-55 / SC-88 / SC-88 Pro tone map switch of the hardware, gain, mute and preview, and both MIDI inputs (32 parts A01-A16 / B01-B16: port select `F5 01` / `F5 02`, or the second CLAP note port "MIDI IN B"). The SC-55 code is untouched by this (audio compared byte by byte).
- **SC-8820:** the same 88emu engine with the SC-8820 firmware, 32 parts on two MIDI inputs (A, B), a front panel drawn after the device (lamps from the firmware, INST MAP, GAIN knob, PREVIEW on a right click). See [SC-8820 engine](#sc-8820-engine-88emu).
- **SC-8820 part display:** the SC-8820 panel adds what the device lacks: a 16-part level display and, in the lower strip, a part display (program, instrument, bank MSB / LSB, level, pan, reverb, chorus) with edit buttons. A second panel after the SC-D70 can be chosen in the SETUP menu ("Interface Style": SC-8820 / SC-D70). See [SC-8820 part display](#sc-8820-part-display).
- **SC-D70 interface:** a third panel for the SC-8820 plug-in, after the Roland SC-D70, which has the same sound engine: part, instrument and variation on a 7-segment display with PART / INST / DEC / INC, GS / GM and MIDI lamps, output level meters; the audio interface parts of the device are drawn without a function. See [SC-D70 interface](#sc-d70-interface).
- **SC-8850:** the same 88emu engine with the SC-8850 firmware, 64 parts on four MIDI inputs (A-D), the original front panel with graphic LCD driven by the firmware, and GAIN. See [SC-8850 engine](#sc-8850-engine-88emu).

### Formats and packaging

- **VST2:** a bridge exposes the CLAP plug-in of the same file as a VST2 plug-in. The VST2 binary interface is described independently in `src/vst2/vst2_abi.h`; **no Steinberg SDK** is used. One file serves as `.clap` and as `.dll`.
- **Single-file builds with embedded ROMs** (private use only) and **ROM-less templates** plus a Python builder that inserts your ROMs, so no compiler is needed.
- **Settings only in the plug-in state:** no `.ini`, no registry, no file writes. The state is saved by the host (CLAP state / VST2 chunk), changes are reported to the host.

### Core and speed

- **Bit-exact lazy timer evaluation** in `mcu_timer.cpp`: event-free timer ticks only increment a counter. About **35-40 % less CPU** per instance (with `-O3 -flto`), output identical to the original core.
- **Bit-exact timer and sub-MCU shortcuts** (Issue #28): the 8-bit timer skips whole counting cycles while all its interrupts are off (the SC-55mk2 runs it as a 6-cycle loop), and the SC-55mk2 sub-MCU takes its timer prescaler and interrupt checks in bulk. SC-55mk2 about 12 % and SC-55 about 9 % less CPU under full load, output still identical to the original core.
- **Packing and wake logic:** notes fill the lowest awake instance first; a single awake instance renders directly in the audio thread without any threading overhead.
- **Memory:** wave ROMs are shared between SC-55 units (one block, untouched pages cost nothing); the unused 4 MB LCD pixel buffer per unit is gone; 88emu units share one decoded wave ROM; the SC-88 and SC-88 Pro units no longer reserve the whole 1 MB / 16 MB address space of their H8/500 CPU, only what the firmware uses (about 15 MB less per unit, measured on Linux); the SC-8820 and SC-8850 read their wave ROM directly from the embedded image (no per-unit copy).
- **Faster loading:** SC-55 units are cloned from unit 0 instead of each booting itself (SC-55mk2 load time 6.4 s to 0.6 s under Wine); SC-88 Pro units boot in the background.

### User interface (Windows)

- **Editor panel** for every model, derived from the 88emu panel art: the device's own LCD and switches, GAIN knob, voice meter, SETUP menu.
- **Original LCD** rendered from the emulated LCD controller: SysEx text and bitmap messages (`45 10 00 xx`, `45 10 01 xx`) are shown exactly as the firmware displays them. On the SC-55 and SC-55mk2 a display row changes only once a firmware display refresh (every ~70 ms) matches one of the two before it, so scrolling text runs steadily instead of flickering with fragments of the instrument name (sound unchanged).
- **Menu language** follows the Windows UI language (German or English); the printed panel labels stay English like on the hardware.

### Identification

- Internal name **"Nuked SC-55 P256"** for the SC-55 models (Nuked core) and **"Emu SC-88 P256"**, **"Emu SC-8820 P256"**, **"Emu SC-8850 P256"**, **"Emu MT-32 P256"** for the models on the 88emu core (CLAP plug-in names, VST2 vendor, Windows version info). Plug-in IDs stay on `net.nuked_sc55_poly_clap.*` so existing DAW projects keep working.
- **Version number = build date** (`YYYY.MM.DD`) in the Windows version info.

---

## Quick start

There are two ways to get the plug-ins. The first needs nothing but your ROM files.

### Option A: the patcher (no compiler, no Python)

1. Download the **patcher** for your system from the [Releases](https://github.com/NightFright2k19/Nuked-SC55-P256/releases) page (Windows, Linux, macOS Apple Silicon or macOS Intel). The newest Autobuild is at the top and marked **Latest**; older Autobuilds stay available for comparison and regression tests.
2. Put it in a folder together with your ROM files (subfolders are searched, file names do not matter, see [Supported models](#supported-models-and-rom-sets)).
3. Start it, check the list of found models and click **Patch**.
4. Copy the results from `Plugins/CLAP/*.clap` and/or `Plugins/VST2/*.dll` to your plug-in folders (see [Using the plug-ins](#using-the-plug-ins)).

Everything for which a complete ROM set is found is built (MT-32 / CM-32L, SC-55 v1.21, SC-55mk2 v1.01, SC-88, SC-88 Pro, SC-8820, SC-8850). More in [The patcher](#the-patcher).

### Option B: build from source

See [Building from source](#building-from-source).

> [!NOTE]
> An original `Nuked-SC55.clap` is **not** needed. The P256 features are separate code and cannot be patched into the original binary; the builder works from its own templates.

---

## Supported models and ROM sets

| Model | Single-file build | Voices per unit | Units | Total |
| --- | --- | --- | --- | --- |
| MT-32 (old / new), CM-32L | yes, one plug-in | 32 | 2 | 64 |
| SC-55 v1.00 / v1.10 / v1.20 / v2.00 | source builds with external ROMs only | 24 | configurable | - |
| SC-55 v1.21 | yes | 24 | 11 | 264 |
| SC-55mk2 v1.01 (CTF) | yes | 28 | 10 | 280 |
| SC-88 | yes | 64 | 4 | 256 |
| SC-88 Pro | yes | 64 | 4 | 256 |
| SC-8820 | yes | 64 | 4 | 256 |
| SC-8850 | yes | 128 | 2 | 256 |

The ROMs are recognised **by content** (SHA-256), not by file name. Dumps of other versions are ignored. The hashes below are the first 16 digits; the full values are in `p256_builder.py`.

| Model | Role | Typical file name | SHA-256 (start) |
| --- | --- | --- | --- |
| MT-32 (old) | control 1.07 (IC27 + IC26, or both as one 64 KB file) | `mt32_1.0.7.ic27.bin` + `mt32_1.0.7.ic26.bin` | `967d2e3c782d8701` + `f2889cdd5e8dce29` (combined `a73a06c23ed38370`) |
| | control 1.06 / 1.05 / 1.04 / Blue Ridge / M-9 (used when 1.07 is missing) | `mt32_1.0.6.ic27.bin` ... | see `p256_builder.py` |
| | PCM | `r15449121.ic37.bin` (or `r15179844` + `r15179845`) | `d9164063f293410c` (`990d5bb6b6dd1ba7` + `95f312aee6f359b8`) |
| | reverb | `r15179857.ic13.bin` | `8ec50a524dde9c39` |
| MT-32 (new) | control 2.04 (2.07 when 2.04 is missing) | `mt32_2.0.4.ic28` | `a5339b966142dd2c` (2.07: `a1ee6ac3190bb94d`) |
| | PCM | as MT-32 (old) | |
| | reverb | `r15179917.ic19.bin` | `a4e146bfad60bdb4` |
| CM-32L | control 1.02 (LAPC-I 1.00 when 1.02 is missing) | `cm32l_control.rom` | `08d68695e5bff6bb` (1.00: `c8aea5fb1b281835`) |
| | PCM (1 MB, or `r15449121` + `r15179945`) | `CM32L_PCM.ROM` | `0dc50190bb7ea3fb` (`d9164063f293410c` + `5ed28de779c05b35`) |
| | reverb | as MT-32 (new) | |
| SC-55 v1.21 | rom1 | `sc55_rom1.bin` | `7e1bacd1d7c62ed6` |
| | rom2 | `sc55_rom2.bin` | `effc6132d68f7e30` |
| | wave ROM 1 / 2 / 3 | `sc55_waverom1/2/3.bin` | `5655509a531804f9` / `c655b159792d999b` / `334b2d16be3c2362` |
| SC-55mk2 v1.01 | rom1 | `rom1.bin` | `8a1eb33c7599b746` |
| | rom2 with CTF | `rom2.bin` | `10b3f09485a74bb0` |
| | rom2 original (no CTF) | `rom2.bin.old` | `a4c9fd821059054c` |
| | rom_sm | `rom_sm.bin` | `b0b5f865a403f730` |
| | wave ROM 1 / 2 | `waverom1.bin` / `waverom2.bin` | `c6429e21b9b3a02f` / `5b753f6cef4cfc7f` |
| SC-88 | control ROM (v1.01) | `sc88_control.bin` | `875f561d009fba79` |
| | wave ROM 0 / 1 / 2 / 3 | `sc88_wave0/1/2/3.bin` | `4d8fbb7f089e500a` / `c36f96c4a17a17eb` / `be62816e655cf712` / `cd2ba0643fe22fcd` |
| SC-88 Pro | control ROM (v1.02) | `sc88pro_control.bin` | `efcdbe43f5810d34` |
| | wave ROM 0 / 1 / 2 | `sc88pro_wave0/1/2.bin` | `3c6a96298e0de126` / `42bcbba9506a667c` / `db40d8624fceec5a` |
| SC-8820 | internal (CPU) | `sc8820_internal.bin` | `7f3be975fc9ada3d` (or MD5 `b03dc0c5…`) |
| | program | `sc8820_program_2000-11-28.bin` | `934df22a9056426c` |
| | wave 0 | `sc8820_wave0.bin` | `34a8a0af36fe38cb` |
| | wave 1 | `sc8820_wave1.bin` | `7284bf21923f966c` |
| SC-8850 | internal (sub CPU) | `sc8850_internal.bin` | `dc5caf0841819fce` |
| | program | `sc8850_program.bin` | `19e670a82eebe4ff` |
| | data | `sc8850_data.bin` | `48eeceb4dbba45b6` |
| | wave | `sc8850_wave.bin` | `3cfac9db381527a4` |

The SC-88 files are the raw dumps (control ROM 512 KB, four wave ROMs of 2 MB each). The SC-88 Pro files are the raw dumps (control ROM 1 MB, wave ROMs 8 + 8 + 4 MB). The SC-8820 set is the CPU ROM R02015367 (64 KB, the on-chip ROM of the SH7016 CPU; the earlier reconstruction is still accepted by its MD5 from 88emu's ROM list), the program flash of 2000-11-28 (2 MB) and the wave ROMs of 16 + 8 MB. The SC-8850 set is internal 64 KB, program 1 MB, data 2 MB and the wave ROM as one 32 MB file. The MT-32 / CM-32L plug-in is built when at least one of its three sets (MT-32 old, MT-32 new, CM-32L) is complete; devices without a set are greyed out in its SETUP menu. Per device the firmware listed first is used, and the menu shows its version.

> [!NOTE]
> **SC-55mk2:** either `rom2` works. If only the original `rom2` (512 KB) is found, the builder applies the CTF patch itself and checks the result against the known checksum (see [Capital Tone Fallback](#capital-tone-fallback-sc-55mk2)).

For source builds with **external** ROMs, the layout of the original plug-in applies: a `Nuked-SC55-Resources/ROMs/<model>/` folder next to the plug-in (for example `SC-55-v1.21/sc55_rom1.bin`) or one or more directories listed in the `SOUNDCANVAS_ROM_PATH` environment variable. The complete file list per model is in the [Nuked-SC55-CLAP README](https://github.com/johnnovak/Nuked-SC55-CLAP#rom-files).

---

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

## MT-32 / CM-32L engine (88emu)

The MT-32 plug-in uses the 88emu LA board (the 8095 / 8098 CPU with the original firmware, LA32 sound generator, reverb chip) in three variants (compile-time `NUKED_SC55_DEVICE_LA` on top of `NUKED_SC55_ENGINE_88PRO`, model index 10).

- **Devices (SETUP > Device):** MT-32 (old board, 1.x firmware), MT-32 (new board, 2.x firmware) and CM-32L, default CM-32L. The menu shows the firmware version the plug-in was built with; devices whose ROM set was missing at build time are greyed out. Choosing a device boots the units again as that device (about 0.3 s, with a short dropout), like swapping the module: what the host sent before (patches, timbres) is gone. The panel follows the device. Stored in the state (`dev=0..2`).

- **Boot:** each unit runs the firmware for 3 emulated seconds at start instead of 88emu's usual 10 s (all three devices are past their intro and take MIDI after 2 s), so loading and a device switch take about 0.3 s instead of 1 s (Wine, 4 cores).

- **Units:** 32 partials per unit, as the device. **One unit is the default** (32 voices); SETUP offers 64 voices (2 units). Unlike the Sound Canvas models, all units run from the start and receive every message: a sleeping unit would have to be brought up to date by replaying the SysEx state at MIDI speed (faster corrupts the firmware's patch memory), which would take seconds. One unit costs about 15 % of one core.
- **MIDI:** one MIDI input (parts 1-8 on channels 2-9, rhythm on channel 10, as the device; channels can be changed by SysEx). The firmware takes MIDI in at wire speed (3125 bytes/s), as on the device. The old firmware needs pauses between large SysEx blocks ("Exc. Buffer overflow" on the LCD otherwise), as on a real MT-32.
- **Voice counter:** a partial counts while its pitch is set and one of its envelope ramps is above zero (the firmware leaves the pitch of finished partials in place).
- **GAIN:** -12 ... +12 dB, 0 dB at 12 o'clock (default). The LA output is used as it is (RMS of E1M1 on the CM-32L about the level of the SC-55 plug-ins).
- **Send Reset:** "All parameters reset" (the firmware's own initialization, as after power-on). **All notes off** releases all notes and the hold pedal on every unit.
- **Patches:** `88emu-nuked-poly.patch` also adds `emu88_set_la_rom_images` (one ROM set per device from memory), the LA partial counter, the LA MIDI input backlog and `emu88_set_midi_in_rate`.
- **Builds:** `tools/winbuild/buildmt32.sh` (single file / template; ROM slots 0 = MT-32 old, 1 = MT-32 new, 2 = CM-32L, each a block of control, PCM and reverb ROM written by `p256_builder.la_blob`).

### MT-32 / CM-32L panels (918 x 280 px)

- **Graphics:** drawn from scratch by `tools/make_la_panel.py` after photos of the devices (no photo or third-party art is used); the maker's logos are replaced by the plug-in's own branding. VOICES (count, bar) and UNITS on the left, the name ("Emu-MT32", "Emu-MT32 v2" for the new MT-32, "Emu-CM32L") and SETUP (bottom right) in the strip below, as on the other panels. **A left click on the name switches the device** in the order CM-32L, MT-32 (old), MT-32 (new), CM-32L (devices without ROMs are skipped); like SETUP > Device, the units boot again as that device.
- **MT-32:** the dark front with the display window: the 20-character LCD modelled on the MT-32's display (vivid green, square dots of 5 x 8 per character with the character ROM of the LCD controller, bold, unlit dots and the cursor line visible, glass) shows unit 0's display text (a part that plays shows a block, as on the device); the MIDI MESSAGE lamp (green, right of the LCD) lights for notes of any unit. PART 1-5, RHYTHM, SOUND GROUP, VOLUME, SOUND and MASTER VOLUME are the device's switches, pressed on all units alike (held while the mouse button is down, at least 120 ms; a right click holds a switch until the next left-click switch, for key combinations). The knob is labelled SELECT/GAIN: left-drag or mouse wheel = GAIN (0.1 dB per notch, double-click = 0 dB), right-drag or Shift + wheel = the device's own knob (SELECT, for example in the SOUND menu; in play mode it is the device's master volume, starting at "vol:100").
- **CM-32L:** the aged ivory front with "Emu CM-32L / LA SOUND MODULE" in the device's squared lettering, the dark knob (GAIN) above a ribbed column cut into the ribbed strip, the GAIN value above it, the MIDI MESSAGE (green) and POWER (red) lamps across the full height of the ribbed strip and the grey power switch.

---

## How the polyphony works

### Why several instances

The limit of 24 partials (SC-55) or 28 (SC-55mk2) is built into the PCM chip, which processes a fixed number of slots per sample clock (the sample rate is tied to that number), and into the firmware. Neither can be extended in a faithful way. So instead of changing a module, the plug-in runs **N identical emulators**, each with its own 24/28 partials, and sums their outputs.

All instances boot identically and receive identical controller and SysEx data. LFOs, chorus and reverb therefore run in lockstep, and the sum behaves (linearly) like the effect section of a single module.

### Note router (`src/poly_router.h`)

| Event | Handling |
| --- | --- |
| Note on | **Lowest awake instance with room** (load <= capacity - 2). If none has room, the next sleeping instance is woken (synchronously, before the note is routed). |
| Note off, poly aftertouch | The owning instance(s). Note offs of unknown notes are broadcast (harmless). |
| Retrigger of a held key | Same instance if at least 2 partials are free there, otherwise a normal pick. |
| Mono mode (CC 126/127) and portamento (CC 65) | The channel is pinned to one instance. |
| Rhythm parts | Exclusive groups (hi-hat, whistle, guiro, cuica, triangle, surdo; notes 27-29, 42/44/46, 71/72, 73/74, 78/79, 80/81, 86/87) stay on one instance per channel so that choke groups work. GS SysEx "Use for Rhythm Part" (`40 1x 15`) and "Rx Channel" (`40 1x 02`) are tracked. |
| Everything else | CC, program change, pitch bend, pressure, SysEx and resets go to **all awake** instances. Exception: display SysEx goes to unit 0 only (see [Display SysEx](#display-sysex)). |

**Load measurement:** the load of an instance is its number of sounding partials plus 2 per note sent since the last measurement. A slot counts as active when its TVA envelope level (`pcm.ram2[slot][10]`) is non-zero; the voice mask of the chip is always full and cannot be used. For the 88emu core, a voice is active when `combinedAmp_2300 != 0`.

### DC offset

Every SC-55 instance outputs a constant 1/32 of full scale. It is measured after boot and subtracted from all instances except instance 0. With up to 24/28 voices, the output is **bit-identical** to a single module (checked by hash).

### Dynamic instances

After boot, everything except `min_active` (1) sleeps.

- **Going to sleep:** the output of an awake unit must stay below 2.5e-4 (about -72 dBFS after DC removal, which also covers reverb tails) for 1 s, **and** the remaining awake units must keep at least a quarter of a unit in reserve (or the unit is above the voice limit). The SC-55 mk1 emulation (v1.xx) toggles its output by one step (1.2e-4) even without a sounding voice, so the threshold has to be above that; the MkII does not show this residual noise. When a unit falls asleep, its note assignments are cleared.
- **State log** (`src/state_log.h`): messages that a sleeping unit misses are logged compactly (last value per parameter, ordered by last occurrence) and replayed when it wakes. GS reset / GM on / GM2 on (and System Mode Set on the 88emu devices) clears the log and gets 60 ms of settle time (System Mode Set: 200 ms). Program changes are stored with the bank that was valid at the time, RPN/NRPN as a sequence; increment/decrement (96/97) and the "notes off" controllers are not logged.
- **Replay speed:** 3000 MCU cycles per byte on the emulated UART (about 2.5 times MIDI speed), then the buffer is drained and 5 ms are added.
- **Background warmer:** every 250 ms the audio thread hands the lowest of the **next two** sleeping units that is behind the state log, together with the missing log entries, to a separate thread (`try_lock` only, the audio thread never blocks). The warmer replays them. Result in the test song "grabbag": wake-up takes 0.3-0.9 ms with 0 bytes left to replay; without the warmer the first wake-up replayed 450 bytes (7.6 ms, more than the 11.6 ms block budget allowed).
- **Rendering:** only awake instances are rendered and measured; one awake instance renders directly in the audio thread, several use a worker pool (the audio thread works with it). On Windows the pool's worker threads join the "Pro Audio" MMCSS class like a host's audio thread (THREAD_PRIORITY_HIGHEST where MMCSS is not available), because the audio thread waits for them at the end of every block: with normal priority, other busy threads could hold a worker back for a scheduler time slice and the block missed its deadline. Measured with `tools/winbuild/polybench.exe` (a host running the plug-in with N awake units and, optionally, busy threads competing for the CPU) on an i5-10300H (4 cores, 8 threads) with 4 busy threads: blocks over budget went from 3 of 1292 to 0 (SC-55 v1.21, 8 units), 1 to 0 (SC-88 Pro, 2-6 units) and 8 to 0 (SC-8850, 2 units), maxima from 14-16 ms to 4-9 ms; without competing load nothing changed and the CPU time per block stayed the same.

### Voice limit

`max_voices` determines the allowed instances: ceil(max / capacity). The default for a new plug-in instance is the maximum (264 / 280 / 256). Instances above the limit get no new notes and sleep as soon as they are silent. The limit is part of the plug-in state.

---

### Display SysEx

Display messages (Roland model `45`: text `45 10 00 xx`, 16 x 16 bitmaps `45 10 01 xx`, the SC-8850's 160 x 64 bitmaps `45 20 0x 00`) only change the LCD, which the panel shows from unit 0. On all models they go to unit 0 alone and are not logged for waking other units, so songs full of bitmaps cost no CPU on the other units.

The firmware takes MIDI in at its own pace (SC-55: emulated UART, SC-88 / SC-88 Pro: MIDI wire speed, SC-8820 / SC-8850: USB; the SC-8850 is slowed down further by drawing the bitmaps). When the host sends a large burst, for example all SysEx up to the new position after seeking, display data would queue up and hold back the notes behind it for seconds (on the SC-55 the 8 KB input buffer could even overflow). So a display message goes straight to unit 0, sample-accurate, as long as nothing is waiting and unit 0 has less than 512 bytes not yet taken in; otherwise it waits in a short queue (at most 64; newer data for the same address replaces older data) that is handed on as unit 0 catches up. GS Reset / GM On empties the queue. In normal playback the LCD shows exactly what it showed before.

## Capital Tone Fallback (SC-55mk2)

With CTF, a module falls back to the capital (basic) tone of a program when the selected variation tone does not exist, instead of staying silent. The SC-55 firmware does this by itself; the original SC-55mk2 firmware does not, which is why the mk2 ROM gets patched.

- **SC-55 (all versions):** CTF is part of the original firmware and always active.
- **SC-55mk2:** the single-file MkII plug-in contains a `rom2.bin` patched with [sc55mk2-ctf-patcher](https://github.com/shingo45endo/sc55mk2-ctf-patcher) (variant "Tone SC-55, Drum SC-55 v1.21"). In source builds with external ROMs, a CTF-patched `rom2.bin` is **preferred** over the original one. Force a specific set with `NUKED_SC55_MK2_ROMSET=mk2-v1.01` (no CTF) or for example `mk2-ctf-sc55-drum-sc55-v1.21`.
- **SC-88 Pro:** CTF is built in (bank 1/5 fall back to the basic tone), no patch needed.
- **Builder:** the CTF patch is embedded in `p256_builder.py` (77 sections, 15,561 bytes raw, zlib + Base64 808 characters). The result is checked against the SHA-256 of the known CTF `rom2`.
- **Quick test:** bank 1, program 17. The SC-55 plays it, the original SC-55mk2 stays silent, the CTF SC-55mk2 plays it.


## SC-88 engine (88emu)

The SC-88 plug-in uses the same 88emu core with the firmware of the first SC-88 (compile-time `NUKED_SC55_DEVICE_88` on top of `NUKED_SC55_ENGINE_88PRO`, model index 8).

- **Units:** 64 voices per unit, 4 units = 256 voices, like the SC-88 Pro.
- **Two MIDI inputs, 32 parts (A01-B16):** CLAP note ports "MIDI IN A" and "MIDI IN B"; on VST2 the port select messages `F5 01` / `F5 02`.
- **GAIN:** -12 ... +12 dB, 0 dB in the middle (default). At 0 dB the output is raised by 5 dB like the SC-88 Pro; the measured level then matches the SC-88 Pro within 0.1 dB (RMS of E1M1, Animus and grabbag).
- **Front panel = the device's own panel:** all switches go to unit 0, so the firmware's LCD, LEDs and functions work as on the hardware (part and instrument selection, level / pan / reverb / chorus / key shift / MIDI channel, the SELECT rows vibrato / TVF / envelope, USER INST EDIT, ALL mode, PREVIEW). The SC-88 firmware writes every panel edit into a small log in its work RAM; the plug-in reads that log and sends each edit as a GS DT1 message to the other units and into the state log, so all units (including those that wake later) play the same sounds. Checked by comparing the parameter memory of the units.
- **ALL mode:** ALL + MUTE mutes all units (taken from the firmware's panel flag); ALL + EQ switches the EQ of all 32 parts on all units. ALL + INST MAP and ALL + MIDI CH have no GS equivalent and are disabled on the panel; the tone map for all parts is chosen in the SETUP menu instead.
- **Tone map (SETUP menu, all parts):** SC-55 / SC-88 (default SC-88, see the SC-88 manual p.1-4). The plug-in runs ALL + INST MAP (+ ALL) on every unit, like the SC-88 Pro's map keys, tracks the map of each unit (boot state = SC-88) and stores it in the plug-in state (`map=0..1`). Units that sleep follow in the background or when they wake. The map survives GS resets like on the device. While the SC-55 map is set for all parts, the firmware also plays the SC-55 sounds for parts that select bank LSB 2; the panel's INST MAP still switches single parts. Checked by `tools/test/map88o.cpp` (sound of parts 1-4 and B01 against SC-55 / SC-88 references, all units after waking compared by parameter memory).
- **Panel functions without a GS parameter:** when ALL confirms Initialize GM (SELECT + PART >), Set Mode 1 / Mode 2 (SELECT + KEY SHIFT) or Initialize CM-64 (SELECT + INST <) on unit 0, the plug-in sends GM On or System Mode Set to the other units (the SETUP menu mark follows), or runs the same panel steps on them; units that wake later get it from the state log. MASTER TUNE from the menu is passed on as GS DT1.
- **Patches:** `88emu-nuked-poly.patch` also adds `emu88_set_sc88_rom_images` (ROM set from memory; the raw dumps are normalised inside 88emu), the SC-88 voice counter and work RAM access.
- **Builds:** `tools/winbuild/build88o.sh` (single file / template, ROM slots 0 = control, 1-4 = wave ROMs 0-3).

### SC-88 panel (918 x 280 px)

- **Graphics:** the 88emu SC-88 panel art (GPLv3, The Usual Suspects) by `tools/make_sc88orig_panel.py`. Only the branding ("Emu-SC88"), the playlist header (VOICES) and the VOLUME label (GAIN) are replaced; everything else is unchanged. Switches, rockers, LEDs, PREVIEW and the GAIN knob are rendered from the player's own vector graphics at the positions of its skin (`tools/make_sc88orig_sprites.py`, with the pressed tint).
- **LCD:** the firmware's character LCD on the panel's glass, rendered like on the SC-88 Pro.
- **Controls:** every switch is held while the mouse button is down (at least 80 ms), so holding a rocker repeats like on the device. A right click holds a switch until the next left-click switch is let go, for the key combinations of the manual (SELECT + ..., USER INST + ..., ALL + ...). GAIN: drag, wheel (0.1 dB per notch), double-click = 0 dB.
- **VOICES / UNITS** as on the other 88emu panels. **SETUP** (bottom, between the logo and the EFX lamp; same place on the SC-88 Pro): max polyphony 64 / 128 / 192 / 256, tone map (SC-55 / SC-88), Send Reset (MT-32 / GM / GS), System Mode (Mode 1 / Mode 2), all notes off.


## SC-88 Pro engine (88emu)

The SC-88 Pro variant does not use the Nuked core. It uses **88emu**, a low-level emulation of the SC-88 / SC-88VL / SC-88Pro / SC-8820 / SC-8850 with the original firmware, written by The Usual Suspects and part of [Gearmulator](https://github.com/dsp56300/gearmulator) (marked "early access alpha" there, GPLv3).

- **Pinned version:** Gearmulator `c8047e7a61a9a466e3cd9192157965856e774786`, with the submodules dsp56300 `da2ff23`, asmjit `3577608`, mc68k `cc3693a`, freetype `8289165`, lunasvg `f8aabfb` (+ plutovg `5e4712c`) and RmlUi `df1b338`. `tools/build_88emu.sh <target> <source>` fetches and builds exactly this state (about 8-10 min on one core).
- **Patches** (`third_party/88emu/`): `88emu-nuked-poly.patch` adds `emu88_get_active_voice_count` (voice counter), `emu88_set_sc88pro_rom_images` (a normalised ROM set from memory, bypassing the file scan), `emu88_dump_sc88pro_rom_images` (build tool), `emu88_get_display_memory` (LCD) and `emu88_get_midi_in_backlog` (MIDI not yet taken in by the firmware). `88emu-nuked-poly.patch` also makes the H8/500 bus (SC-88, SC-88 Pro) take its address space as zero pages from the OS, filled only where ROM or RAM is mapped; unmapped space reads as H'FF through the slow path, so the output is unchanged. `dsp56300-mingw.patch` restricts SEH (`__try`) to MSVC.
- **Engine variant:** compile-time `NUKED_SC55_ENGINE_88PRO` (model index 6). All differences sit in `#ifdef` blocks at about ten coupling points, so the SC-55 code is unchanged.
- **Units:** 64 voices per unit, 4 units. 88emu renders at the device rate of 32 kHz, which goes through the existing resampler. Boot takes about 0.7 s per unit.
- **Ports:** both MIDI inputs, 32 parts (A01-B16): CLAP note ports "MIDI IN A" and "MIDI IN B"; on VST2 (one input) the port select messages `F5 01` / `F5 02`.

### Tone map switch

Like the hardware, the map is switched with emulated panel keys. Both map keys **toggle**: SC-55 MAP toggles 55 <-> Pro, SC-88 MAP toggles 88 <-> Pro. **ALL latches** and makes the key press valid for all parts. The map LEDs of the emulation do not show the map reliably, so the plug-in tracks the map of every unit itself (`applied_map`, boot state = Pro). The sequence is ALL on, one map key, ALL off (only the map key while the ALL view chosen on the panel is on), running in the background while rendering (press 0.15 s, release 0.35 s); only during boot and in the warmer it runs synchronously. The map survives GS resets like on the hardware; SysEx `40 1x 7F` has no effect. All 15 transitions were tested from every state.

| `map` | Tone map |
| --- | --- |
| 0 | SC-55 |
| 1 | SC-88 |
| 2 | **SC-88 Pro (default, factory setting of the hardware)** |

The panel keys toggle like on the device, the SETUP menu selects directly.


## SC-8820 engine (88emu)

The SC-8820 plug-in uses the 88emu core with the SC-8820 firmware (compile-time `NUKED_SC55_DEVICE_8820` on top of `NUKED_SC55_ENGINE_88PRO`, model index 9).

- **Units:** 64 voices per unit (one XP chip), 4 units = 256 voices. 32 kHz like the SC-88 Pro and the SC-8850; light on the CPU (E1M1 about a tenth of one core, one unit).
- **Two MIDI inputs, 32 parts (A01-B16):** CLAP note ports "MIDI IN A" and "MIDI IN B"; on VST2 the port select messages `F5 01` / `F5 02` choose the port.
- **GAIN:** -12 ... +12 dB, 0 dB in the middle. At 0 dB the level matches the SC-88 Pro (RMS of E1M1, Animus and grabbag within 1 dB).
- **Tone maps:** per part with bank select LSB (CC#32) as on the device: 0 = the map chosen with INST MAP, 1 = SC-55, 2 = SC-88, 3 = SC-88Pro, 4 = SC-8820. **INST MAP** (or SETUP) chooses the map for all parts. The button works as on the device: the first press after the map indicator has gone out selects the SC-8820 map, each further press within about 4 s the next one (SC-88Pro, SC-88, SC-55, SC-8820, ...); the PART B indicator shows it. The device starts with the SC-8820 map. The plug-in adopts the map unit 0 shows after a press in the GUI, keeps the other units on it (each presses its own INST MAP until its indicator shows the same map) and stores it in the plug-in state (`map=0..3`: SC-55, SC-88, SC-88 Pro, SC-8820).
- **Patches:** `88emu-nuked-poly.patch` also adds `emu88_set_sc8820_rom_images` (ROM set from memory), the SC-8820 voice counter, its USB input backlog, `emu88_peek_work_ram` for the SC-8820, and the INST MAP / VOLUME push switches and lamps (`emu88_set_panel_buttons` bit 0 / 1, `emu88_get_panel_leds`).
- **Builds:** `tools/winbuild/build8820.sh` (single file / template, ROM slots 0 = internal, 1 = program, 2 = wave 0, 3 = wave 1).

### SC-8820 panel (918 x 280 px)

- **Graphics:** drawn from scratch by `tools/make_sc8820_panel.py` after photos of the device (no photo or third-party art is used): the champagne metal front, the smoked display window with the POWER (red) and USB (amber) symbols, the PART A / PART B level indicators (12 x 3 dots each, green through yellow and orange to red, each dot in a fixed colour), the map labels, the INST MAP button and the knob (labelled GAIN). The maker's logos are replaced by the plug-in's own branding. VOICES (count, bar) and UNITS sit left of INST MAP as on the other panels, the GAIN value under the knob; the strip below holds the name, the [part display](#sc-8820-part-display) and SETUP.
- **Lamps:** from unit 0's firmware. It lights the level indicators in 4 blocks of 3 columns per part group and multiplexes them, so the panel combines them over 40 ms. After INST MAP the PART B block above the chosen map lights, as on the device. POWER and USB are always lit (the emulation has no USB host; the firmware would blink USB).
- **Controls:** INST MAP is the device's button (held while the mouse button is down, at least 120 ms). The knob in place of the device's VOLUME turns GAIN (left-button drag, mouse wheel = 0.1 dB per notch, double-click = 0 dB; its marker follows GAIN), always silently. **Deviation from the device:** there pushing the knob plays the PREVIEW note, but here the left button is needed for turning, so PREVIEW is a **right click** on the knob (held while the button is down). It plays the note chosen in the SETUP menu ("Prevw Note", default C4) on part A01 (Issue #51). A left click on the "Emu-SC8820" logo switches to the [SC-D70 interface](#sc-d70-interface) and back (the same as SETUP > Interface Style).
- **SETUP:** max polyphony, tone map for all parts (SC-55 / SC-88 / SC-88 Pro / SC-8820, default SC-8820; the same setting as the INST MAP button), Send Reset (MT-32 / GM / GS / GM2, see [MT-32 reset](#mt-32-reset)), all notes off, and below them Interface Style (SC-8820 / SC-D70, default SC-8820, stored in the state).

### SC-8820 part display

The device has no display and almost no controls; the SC-8820 panel adds them in the display window and the lower strip, inspired by the part view of Sound Canvas VA. This is **customized and less faithful to the original**: everything the panel shows or changes goes through normal MIDI messages, the firmware and the sound are not affected. Until issue #87 there were two styles, **Basic** (only the device's front) and **Pro** (this panel); Basic was removed and Pro is now the SC-8820 interface. A project saved with Basic (`style=0`) opens with it.

- **16-part level display:** 16 columns x 8 dots in the style of the device's lamps, for the 16 parts of the port (A or B) the selected part belongs to. The levels come from the notes (velocity x volume x expression, like the SC-55 / SC-88 bar display). Clicking a column selects that part; the marker under the columns shows the selected part.
- **Part display:** PART, PROG (001-128), INSTRUMENT (the tone name, or the drum set name on a rhythm part), MSB and LSB (bank select; LSB 0 = the INST MAP map, 1-4 = SC-55, SC-88, SC-88Pro, SC-8820), LEVEL, PAN (L63-0-R63, RND), REVERB and CHORUS. The values are read about 30 times a second from unit 0's work RAM, so they also follow SysEx, resets and the host's own messages.
- **Edit buttons:** PART, PROGRAM, BANK MSB and BANK LSB (held = repeat). The mouse wheel changes the value under the pointer (also PART and INSTRUMENT); LEVEL, PAN, REVERB and CHORUS also follow a vertical drag. A change of program, MSB or LSB sends CC#0, CC#32 and the program change of the part; the others send CC#7, CC#10, CC#91 and CC#93. They go to all units like the host's messages (sleeping units get them when they wake).
- **State:** only the interface style (`style=1` SC-8820, `style=2` SC-D70); the selected part is not stored.

### SC-D70 interface

The Roland SC-D70 has the sound engine of the SC-8820 and adds an audio interface (USB audio, digital and analog inputs). **SC-D70** in SETUP > Interface Style (or a left click on the logo) shows the `Emu-SC8820` plug-in with a panel after the SC-D70, drawn from scratch by `tools/make_scd70_panel.py` after photos and the owner's manual (silver front, smoked display window, no maker's logos). Firmware and sound are the same as with the SC-8820 interface.

- **Display and buttons:** the 7-segment display shows the part (A01-A16, b01-b16), the instrument (001-128) or its variation (bank MSB, 000-127); the PART and INST lamps show which. PART selects the part view, INST the instrument view; DEC / INC (held = repeat) and the mouse wheel over the display change the shown value. A change of instrument or variation sends CC#0, CC#32 and the program change of the part, as on the Pro interface. **Deviation from the device:** there DEC and INC pressed together show the variation; here a second press on INST does (INST blinks), a click on the display switches between part and instrument.
- **Lamps:** GS or GM shows the last reset received (GM On / GM2 On = GM; GS Reset, System Mode Set or GM Off = GS; GS at start), MIDI lights when the host sends MIDI, USB is always lit (as on the SC-8820 panel). Of the recording source lamps INST+WAVE is lit: the sound generator is the only source in the plug-in.
- **Level meters:** USB REC LEVEL and OUTPUT LEVEL both show the plug-in's output (after GAIN), left and right, 9 segments up to OL.
- **Knobs:** VOLUME and INPUT GAIN both turn the plug-in's GAIN (-12 to +12 dB; left-button drag, mouse wheel = 0.1 dB per notch, double-click = 0 dB), their markers move together and the value is shown under INPUT GAIN. A right click on INST LEVEL plays the PREVIEW note (held while the button is down), as the knob's push does on the device.
- **Without a function:** POWER, the MIC/GUITAR jack and switch, REC SOURCE, USB AUDIO LEVEL, SAMPLE RATES, INPUT MONITOR and the headphone jack (the plug-in has no audio inputs or USB audio).
- **Plug-in additions:** VOICES, UNITS and SETUP at the top right of the display window; the SETUP menu opens downwards.


## SC-8850 engine (88emu)

The SC-8850 plug-in uses the same 88emu core with the SC-8850 firmware (compile-time `NUKED_SC55_DEVICE_8850` on top of `NUKED_SC55_ENGINE_88PRO`, model index 7).

- **Units:** 128 voices per unit (two XP chips), 2 units = 256 voices. The device runs at 32 kHz like the SC-88 Pro. A unit boots in about 2.7 s; with embedded ROMs the second unit boots in the background.
- **Four MIDI inputs, 64 parts (A01-D16):** CLAP note ports "MIDI IN A" to "MIDI IN D"; on VST2 (one input) the port select messages `F5 01` ... `F5 04` choose the port, as with the SC-88 Pro.
- **GAIN:** -12 ... +12 dB, 0 dB in the middle (default). At 0 dB the output is raised by 7.5 dB to the level of the other plug-ins (RMS of E1M1, Animus and grabbag measured against SC-55 and SC-88 Pro).
- **Front panel = the device's own panel:** all switches and the VALUE encoder go to unit 0, so the firmware's menus, LCD and LEDs work as on the hardware (EDIT, effects, drum edit, INST MAP, ...). 0.4 s after the last panel input that can edit something (not PART < >, EXIT, SHIFT, MUTE, SOLO or PREVIEW) the plug-in reads back the system and effect settings and the parts shown while editing (RQ1; all parts after ENTER, the drum maps only if DRUM was open) and sends what changed as DT1 to the other units and into the state log, so units that wake later play the same sounds. The requests go through unit 0's MIDI input, so a pass is kept small; reading all 64 parts took about 6 s and disturbed playback (Issue #13). Multi-byte parameters are always sent whole. A changed tone map (INST MAP) is followed by the part's tone number, because the firmware applies a tone map written by DT1 only when the part selects its tone again (Issue #17). Host data for the same part within 0.1 s wins.
- **MUTE / SOLO** come from the firmware (its part records in RAM) and act on all units: note-ons of muted parts are dropped, a newly muted part gets CC 120.
- **Patches:** `88emu-nuked-poly.patch` also adds `emu88_set_sc8850_rom_images` / `emu88_dump_sc8850_rom_images` (ROM set from memory), `emu88_get_display_pixels` (graphic LCD), `emu88_peek_work_ram`, `emu88_capture_midi_out` / `emu88_read_midi_out` (RQ1 answers), the SC-8850 voice counter and a shared wave ROM.
- **Builds:** `tools/winbuild/build8850.sh` (single file / template, ROM slots 0 = internal, 1 = program, 2 = data, 3 = wave).

### SC-8850 panel (918 x 280 px)

- **Graphics:** the 88emu SC-8850 panel art (GPLv3, The Usual Suspects), halved by `tools/make_sc8850_panel.py`. Only the branding ("Emu-SC8850"), the playlist header (VOICES) and the VOLUME label (GAIN) are replaced; everything else is unchanged. The switches, LEDs, PREVIEW, VALUE encoder (4 knurl phases) and the GAIN knob are the player's sprites at the positions of its skin (`tools/make_sc8850_sprites.py`, with half-pixel phases and the pressed tint).
- **LCD:** the 160 x 64 dots of the firmware's graphic LCD, black on the orange glass like in the player, area-averaged to 262 x 105 px.
- **SysEx display messages:** text messages (`45 10 00 xx`) are shown by the firmware itself. The SC-8850 firmware ignores the 16 x 16 bitmaps (`45 10 01 00` … `45 10 05 7F`, pages 1-10, page select `45 10 20 00`), so the plug-in keeps the pages and lays the shown one over the level meters of the play screen for 3.2 s after the last update, as on the SC-55 / SC-88 (one bar = one column, two dots per row). Menus are left alone. The panel size does not change. The SC-8850's own 160 x 64 bitmap messages (`45 20 0x 00`) are drawn by the firmware.
- **Display SysEx** goes to unit 0 only and is paced, as on all models (see [Display SysEx](#display-sysex)).
- **Controls:** every switch is held while the mouse button is down (at least 80 ms). A right click holds a switch until the next left-click switch is let go, for the key combinations of the manual (PART < + >, EDIT + ..., SHIFT + ...). VALUE: drag (right / up = clockwise, about 10 px per detent) or mouse wheel; a click without moving pushes the encoder. PREVIEW plays the note chosen in the SETUP menu ("Prevw Note", default C4) on the part shown by the panel, as on the SC-88 Pro (Issue #51). GAIN: drag, wheel (0.1 dB per notch), double-click = 0 dB.
- **VOICES / UNITS:** sounding voices, peak hold and awake units like on the SC-88 Pro panel. **SETUP** (bottom right, same place as on the SC-55 / SC-55mkII): max polyphony 128 / 256, Send Reset (MT-32 / GM / GS / GM2), all notes off.

---

## Panels and controls

All panels are Windows-only (Win32/GDI), 918 x 280 px, 30 frames per second, and read the engine only through atomics.

### SC-55 / SC-55mkII panel (918 x 280 px)

- **Graphics:** the 88emu SC-55 and SC-55mkII panel art (GPLv3, The Usual Suspects), halved by `tools/make_sc55_panel.py`. Only the branding ("Nuked SC-55" / "Nuked SC-55mkII"), the playlist header (VOICES) and the VOLUME label (GAIN) are replaced. Switches, rockers, lamps and the GAIN knob are rendered from the player's own graphics at the positions of its skin (`tools/make_sc55_sprites.py`, with the pressed tint). The art is fetched from Gearmulator at build time (pinned commit).
- **LCD:** the firmware's character LCD on the original glass (313 x 113 px, rendered at full size and reduced so the dots stay even). Text and bitmap SysEx messages are shown by the firmware itself.
- **Front panel = the device's own panel:** ALL, MUTE, PART < >, INSTRUMENT, LEVEL, PAN, REVERB, CHORUS, KEY SHIFT and MIDI CH go to unit 0, so the LCD, the ALL / MUTE lamps and the key repeat work as on the hardware. Every switch is held while the mouse button is down (at least 80 ms). A right click holds a switch until the next left-click switch is let go, for the key combinations of the manual (PART < + > opens the part menu, with ALL the system menu; INST < + > ...). Until 0.6 s after the last switch the plug-in compares unit 0's parameter memory and sends every change as GS DT1 (instrument as CC 0 + program change) to the other units and into the state log, so units that wake later play the same sounds; this covers the menus too (master tune, reverb / chorus type, part mode, bend range, modulation depth, key range, velocity, partial reserve, mono / poly, vibrato / TVF / envelope). Part MUTE flags, the system menu's Rx switches and the mk2 part fine tune have no simple GS form and are copied to all awake units. ALL + MUTE silences the output. ALL + MIDI CH (device ID) is disabled like on the SC-88, since unit 0 would then ignore the host's GS messages.
- **GAIN:** -12 ... +12 dB, 0 dB in the middle (default) and bit-identical to the previous output. Drag (right / up = louder), mouse wheel (0.1 dB per notch), double-click = 0 dB.
- **VOICES / UNITS:** sounding voices with peak hold and awake units like on the other 88emu panels. **SETUP** (bottom right, same place as on the SC-8850): max polyphony, Send Reset (MT-32 / GM / GS), all notes off.
- **Window class:** contains the module handle, so v1.21 and MkII can run in the same host.

### SC-88 Pro panel (918 x 280 px)

- **Same panel code as the SC-88** (`editor_sc88orig.inc`): the 88emu SC-88 Pro panel art (GPLv3, The Usual Suspects) by `tools/make_sc88orig_panel.py --pro`, branding "Emu-SC88 Pro". All controls of the device are real switches: PART < >, INSTRUMENT, LEVEL, PAN, REVERB, CHORUS, KEY SHIFT and MIDI CH rockers, ALL, the bottom row EFX ON/OFF (USER INST / SELECT), EFX TYPE, EFX PARAM, EFX VALUE, and the EFX lamp (green / red / amber like on the device).
- **Firmware panel:** these switches go to unit 0. The SC-88 Pro firmware keeps the same panel edit log as the SC-88; the plug-in forwards every edit as GS DT1 to the other units and into the state log, so all units (also those that wake later) play the same sounds. ALL + INSTRUMENT and ALL + MIDI CH have no GS equivalent and are disabled. Initialize GM / CM-64, Set Mode 1 / 2 and MASTER TUNE reach all units as on the SC-88. Checked by `tools/test/panel88p.cpp` (30 panel steps, ALL mode, tone map change, key repeat; parameter memory of all units compared, with a negative control).
- **Handled by the plug-in:** the tone map keys SC-55 / SC-88 (lamp = active map, both off = SC-88 Pro; also in the SETUP menu), MUTE per part (note ons dropped, CC 120 on muting; not stored in the state, like on the device) and PREVIEW (note on while pressed, on the shown part, note from the menu "Prevw Note").
- **GAIN, VOICES / UNITS, SETUP:** as on the SC-88, SETUP between the logo and the EFX lamp.

### LCD details

- Text (`45 10 00 xx`) and bitmap messages (`45 10 01 xx`, 64 bytes) are shown by the emulated firmware; the plug-in does not interpret them.
- The renderer (`src/gui/lcd_render.inc`) is shared by both device families. The glass (741 x 268 px from Nuked's `lcd_back.h`) is drawn once. Characters are 5 x 7 dots on a grid of 6, the L/R lamp comes from DDRAM 58, the 16 x 16 matrix from DDRAM 20-23 / 60-63. Lit dots are `#000000`, unlit ones `#e2600a`.
- To keep the dots evenly thick at small sizes, the complete LCD is drawn at 741 x 268 and reduced with a separable box filter. Only changed characters, matrix dots and the lamp are redrawn and reduced again; partial and full updates use the same function and are pixel-identical (600 frames compared).
- With several units: text and fields come from unit 0, the matrix is the OR over all awake units, which equals the peak level per part.
- Nuked discards LCD writes without a registered backend, so the plug-in registers an empty one (the sound is unchanged).
- **Key shift:** the SC-88 value windows show the key shift from GS SysEx `40 1x 16` (0x40 = 0). RPN coarse tune is not a key shift, and the device does not show it as one either.

### SETUP menu

| Entry | Meaning |
| --- | --- |
| Max polyphony (MT-32) | 32 (1 unit, default) / 64 (2 units) |
| Device (MT-32) | MT-32 (old, v1.xx) / MT-32 (new, v2.xx) / CM-32L (v1.xx, default): boots the units again as that device; stored in the state (`dev=0..2`). Devices without ROMs are greyed out. A left click on the name in the panel switches too |
| Send Reset (MT-32) | All parameters reset |
| Max polyphony | 24 / 48 / 64 / 96 / 128 / 160 / 192 / 224 / 256 (SC-55mk2: 28 / 56 / ...) |
| Send Reset | submenu MT-32 / GM / GS / GM2 (SC-8850, SC-8820), MT-32 / GM / GS (others): clicking an entry sends that reset to all units (nothing is marked or stored) |
| System Mode (SC-88, SC-88 Pro) | submenu Mode 1 (Single Module) / Mode 2 (Double Module): sends System Mode Set (`F0 41 10 42 12 00 00 7F 0n ..`) to all units and marks it (default Mode 1 = factory setting, stored in the state) |
| All notes off | stops all sounding notes |
| Tone map (SC-88) | SC-55 / SC-88 (default SC-88) for all parts: ALL + INST MAP on every unit; stored in the state (`map=0..1`), kept over GS resets |
| Tone map (SC-88 Pro) | SC-55 / SC-88 / SC-88 Pro |
| Tone map (SC-8850) | SC-55 / SC-88 / SC-88 Pro / SC-8850 (default SC-8850) for all parts: sends Bank Select LSB (CC#32) and the part's bank and program again to every part; stored in the state (`map=0..3`), a GS / GM reset returns to the SC-8850 map. The panel's INST MAP still sets it per part |
| Prevw Note (SC-88 Pro, SC-8820, SC-8850) | submenu per octave |
| Interface Style (SC-8820) | SC-8820 (default) / SC-D70: the [SC-8820 panel](#sc-8820-part-display) or the [SC-D70 interface](#sc-d70-interface); last entry of the menu, stored in the state (`style=1..2`, a former `style=0` opens as SC-8820); a left click on the logo switches too |

After a reset from the menu, the plug-in holds back the host's next MIDI events for 100 ms (System Mode: 200 ms) and then plays them in their original order, so that the firmware has finished the reset (measured with `tools/test/reset26.cpp`: notes sound exactly as after boot from 50-100 ms after GM / GS / GM2 and from 150 ms after a System Mode Set on the SC-88 Pro). Resets that come from the host or a MIDI file are passed on unchanged, as before.

<a id="mt-32-reset"></a>**MT-32 reset:** a GS Reset followed by Falcosoft's "GS MT-32 Mode" SysEx: the MT-32 tones of bank 127 on parts 1-9, bank 126 on parts 11-16, the MT-32 / CM-64 drum set 128 on part 10 and the pitch bend range of the MT-32. On the 88emu models every part also gets Bank LSB 1 (SC-55 map), because the SC-88 map has no bank 127 / 126 and the LCD then shows the MT-32 names. Like GM / GS it is a one-time command and goes through the state log, so units that wake later play the same tones. Limitation: MT-32 games that send their own MT-32 SysEx (LA timbres, patch memory) are not understood by a Sound Canvas; only titles that use the preset sounds play correctly.

**System Mode (SC-88, SC-88 Pro, see the SC-88 Pro manual p.116 and p.194):** Mode 1 (Single Module, factory setting) is one 32-part module with one set of system effects (reverb, chorus, delay, EQ). Mode 2 (Double Module) works as two 16-part modules on MIDI IN A and IN B, each with its own reverb, chorus, master level / pan / key shift / tune; delay and EQ are not available and the insertion effect works only on part group A. A System Mode Set initialises everything except the tone maps (the firmware is busy for about 150 ms). In Mode 2, GM On and GS Reset initialise only the part group of the MIDI IN they arrive on (checked in the emulation with `tools/test/mode2.cpp`); the plug-in follows this in its state log and note router, so units that wake up later get the same state. GM / GS from the Send Reset menu are therefore sent on IN A and IN B in Mode 2, so the whole module is reset. A System Mode Set from the host also moves the check mark in the menu.

The menu language (German or English) follows `GetUserDefaultUILanguage()`; every other Windows language gives English.

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
| `NUKED_SC55_NO_BGBOOT=1` | test switch (SC-88 Pro): all units boot before start |
| `NUKED_SC55_NO_DISPPACE=1` | test switch: display SysEx to all units, unpaced (old behaviour) |

Build-time options (CMake cache variable or compiler define):

| Option | Meaning |
| --- | --- |
| `NUKED_SC55_POLY_TARGET_VOICES` | polyphony target (default 128 in CMake, 256 in the shipped builds) |
| `NUKED_SC55_ONLY_MODEL=<0..10>` | single-model build: 0 = v1.00, 1 = v1.10, 2 = v1.20, 3 = v1.21, 4 = v2.00, 5 = mk2 v1.01, 6 = SC-88 Pro, 7 = SC-8850, 8 = SC-88, 9 = SC-8820, 10 = MT-32 / CM-32L |
| `NUKED_SC55_ENGINE_88PRO` | use the 88emu engine |
| `NUKED_SC55_EMBED_ROMS` | embedded ROMs (with a source from `tools/gen_embedded_roms.py`) |
| `NUKED_SC55_DISPLAY_NAME`, `NUKED_SC55_VST2_NAME`, `NUKED_SC55_VST2_ID` | names and VST2 unique ID |

---

## The patcher

The patcher (`patcher.py`, texts in `patcher_i18n.py`) is a small window around the P256 builder. The releases contain it as a single executable per system, built with PyInstaller, with the ROM-less templates inside. It is not an installer: it writes nothing but the `Plugins` folder (no registry entries, nothing to uninstall).

<img width="626" height="546" alt="patcher_preview" src="https://github.com/user-attachments/assets/f6a66463-c1b6-45cc-9834-8ba7465e89fb" />

- **ROM files:** searched in the patcher's folder and all subfolders (by SHA-256, names do not matter). If no complete set is found there, it asks for the folder; **Change…** picks another one at any time. Each model is listed as complete, incomplete (with the missing parts) or not found.
- **Patch:** builds every complete model into `Plugins/CLAP/<name>.clap` and `Plugins/VST2/<name>.dll` next to the patcher (the folder is created if needed). **Details** shows which file was used for which part.
- **Same result as the script:** the patcher calls the functions of `p256_builder.py`; the plug-ins are byte-identical to those of `python p256_builder.py`.
- **Languages:** Arabic, Bengali, Chinese (Mandarin), Czech, Danish, Dutch, English, French, German, Greek, Hebrew, Hindi, Hungarian, Indonesian, Italian, Japanese, Korean, Persian, Polish, Portuguese, Romanian, Russian, Serbian, Spanish, Swedish, Thai, Turkish, Ukrainian, Vietnamese. The system language is used if available, otherwise English; the menu at the top right switches it.
- **Without a window:** `patcher --build [folder]` builds from the given folder (default: the patcher's folder) and prints the result; `patcher --selftest` checks the bundled templates.

> [!NOTE]
> The patcher is **not signed**. On the first start Windows SmartScreen shows "Windows protected your PC": click **More info**, then **Run anyway**. macOS Gatekeeper refuses to open the app: right-click (or Control-click) it, choose **Open** and confirm; on newer macOS versions use **System Settings → Privacy & Security → Open Anyway**. Alternatively remove the quarantine flag in Terminal: `xattr -dr com.apple.quarantine "Nuked-SC55-P256-Patcher.app"`. Move the app out of the Downloads folder before starting it, so that the `Plugins` folder is created next to it. On Linux, make the file executable (`chmod +x Nuked-SC55-P256-Patcher`); it needs a desktop session.

---

## The P256 builder

`p256_builder.py` (Python 3.8 or newer, standard library only) creates the finished plug-ins from the ROM-less templates in `templates` and your ROMs in `roms`. Releases use the [patcher](#the-patcher), which runs the same code; the script stays in the repository for development and for building directly from a checkout (put your ROMs into `roms`).

```
python p256_builder.py
```

```
Nuked SC-55 / Emu SC-88 / SC-8820 / SC-8850 / MT-32 P256 - Plugin Builder

[--] MT-32 / CM-32L: no ROMs found
[ok] SC-55 v1.21: output/CLAP/Nuked-SC55_v121.clap, output/VST2/Nuked-SC55_v121.dll
[ok] SC-55mk2 v1.01 (CTF) (CTF patch will be applied): output/CLAP/Nuked-SC55_MkII.clap, ...
[--] SC-88: no ROMs found
[--] SC-88 Pro: no ROMs found
[--] SC-8820: no ROMs found
[--] SC-8850: no ROMs found

2 plugin(s) created.
```

- **Detection:** `roms` is searched recursively (symbolic links and junctions are followed, with loop protection); each file is identified by SHA-256, so names do not matter. Unknown files are skipped.
- **Per model:** a plug-in is built when its set is complete. An incomplete set is reported with the missing parts. MT-32 / CM-32L: built when at least one device set is complete, with all complete device sets in one plug-in.
- **Result:** `output/CLAP/<name>.clap` and `output/VST2/<name>.dll` per model (identical files).
- **Exit code:** 0 if something was built, 2 if nothing was built, 1 if `roms` is missing.

### What a template is

A template is the finished plug-in without ROMs. At the place where the ROMs belong, it contains a block marked `NUKED-P256-ROMSLOT` with a 256-byte header (version, capacity, number of entries, filled flag, 8 entries {slot, offset, size}, name) followed by the data area. The builder writes your ROMs into the data area, fills in the table and sets the filled flag. The code in the plug-in reads the ROMs through the same interface as the embedded builds.

- **Compact templates (header version 2):** the reserved area (only zeroes) is cut out of the file, and the builder inserts it again (`capacity` bytes) before filling it. Sizes: v1.21 and MkII 1.52 MB, SC-88 Pro 4.93 MB. Compact templates are **not loadable DLLs**.
- **Capacities:** v1.21 3,506,176 bytes, MkII 3,772,416 bytes, SC-88 Pro 22,085,632 bytes (sum of the ROMs + 64 KB).
- **No ROM data:** even though the size of a template may suggest otherwise, it does **not** contain any ROM data, only zeroes in the reserved area (verified: all zero, `filled=0`, none of 7,156 ROM chunks of 4 KB found in any template). A template that is loaded directly stays silent and does not crash.
- The generated plug-ins are byte-identical to those made from the full templates and produce the same values as the release builds (CTF test and 256-note test).

> [!WARNING]
> The generated plug-ins **contain your ROMs**. Do not share them.

---

## Building from source

### Requirements

- **Cross-build on Linux** (what the single-file plug-ins use): `x86_64-w64-mingw32-g++-posix` (MinGW-w64 with **posix threads**, needed for `std::thread`), a C++23 compiler, the SpeexDSP sources, Python 3 (for `gen_embedded_roms.py`). Link statically with `-lgdi32 -luser32 -lmsimg32` (the last one for `GradientFill`).
- **CMake and vcpkg** as in the original plug-in (CMake 3.25 or newer, vcpkg; Visual Studio 2022 on Windows, Clang 17 or newer and Ninja on macOS and Linux). The presets are the same as in the [Nuked-SC55-CLAP README](https://github.com/johnnovak/Nuked-SC55-CLAP#building) (`release-windows-x64`, `release-linux-x64`, ...). `-DNUKED_SC55_POLY_TARGET_VOICES=256` sets the voice target.

### Single-model VST2 / CLAP build (external ROMs)

`build-mingw-win64.sh` builds single-model variants (`-DNUKED_SC55_ONLY_MODEL=<n>`). The resulting file works as `.clap` and as VST2 `.dll`.

### Single-file build with embedded ROMs (private use only)

1. `tools/gen_embedded_roms.py` creates a C++ source that includes each ROM with `.incbin` for its slot (SC-55: 0 = rom1, 1 = rom2, 3/4/5 = wave ROMs; SC-55mk2: 0 = rom1, 1 = rom2 (CTF), 2 = rom_sm, 3/4 = wave ROMs).
2. Compile with `-DNUKED_SC55_EMBED_ROMS`, `-DNUKED_SC55_ONLY_MODEL=<n>` and the display name / VST2 ID, `-O3 -flto`, and link `src/gui/editor_win32.cpp` and the version resource (`src/vst2/version.rc.in` with the placeholders `@NAME@`, `@FILE@`, `@VER_STR@`, `@VER_NUM@`, `@VENDOR@`, `@COMMENT@`). `tools/build-embedded-win64.sh.inc` contains the build function.
3. For a **template** instead, call `gen_embedded_roms.py --slot <out.cpp> <romset> <name> <capacity>` (the build function does this when `NK_SLOT_CAP` is set) and run `src/tools/p256_builder/compact_template.py` on the result.

The wave ROMs must be **descrambled** like in the file loader (`LoadRomset(info, nullptr)` from `rom_io.h` before `LoadRoms`). Without that, the level is wrong by a factor of 15-20.

### SC-88 Pro

```
tools/build_88emu.sh <target folder> <plug-in source folder>
```

fetches Gearmulator in the pinned state, applies the patches in `third_party/88emu/`, and builds `lib88emu.a` for Linux and Windows/MinGW plus the ROM preparation tool `romdump`. The Windows build needs several MinGW adjustments, which the script handles (`wincase/Windows.h`, `Memory.h`, a replacement for `shlobj_core.h`, `-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000005`, `-include cstdint`). Boot of the units is parallel with embedded ROMs; with ROM files it is serial because the file scan is not thread-safe.

### macOS

`build-macos.sh` comes from the original project; macOS has not been tested with the P256 changes. The editor (Win32/GDI) and the single-file builds are Windows-only.

---

## Performance and memory

### CPU (author's Windows machine, peak, original Nuked-SC55 for comparison)

| Song | Original | Poly before optimisation | Current |
| --- | --- | --- | --- |
| E1M1 (Doom) | 4.6 % | 12.4 % | **3.8 %** |
| Animus | 5.4 % | 16.6 % | **12.3 %** |

"Animus" needs 32-41 voices, which means 2-3 units (the original steals the rest). One SC-55 instance took about 19 % (v1.21) / 30 % (MkII) of one core before the optimisation (sandbox, one core). An SC-88 Pro unit costs about 11-12 % of one core. The interface thread with the editor open takes 3.3 % (SC-55) or 2.9 % (SC-88 Pro) of a core under Wine, while a song plays in real time.

### Core optimisation (bit-exact)

The four timers (3 x FRT, 1 x TMR) are evaluated lazily, one by one. Ticks without an event only increase the counter; ticks with a compare match, an overflow or after a register access (`dirty`) run through the original logic. Every timer register access synchronises everything; divisions are shifts. This is bit-exact because the timers are independent and only set interrupt pending bits, which are evaluated before the next instruction and cleared only through `TIMER_Write`. The pure core (E1M1, v1.21, `-O2`) went from 24.8 s to 15.1 s (-38 %).

Tried and rejected: polling the peripherals less often (< 5 %), skipping silent PCM slots (only helps at low load and risks bit-exactness), PGO (bit-exact, but about 10 % slower), `-march=x86-64-v2/v3` (no gain), hand-written SIMD for the PCM slot loop (too costly for bit-exact special cases, estimated 15-25 %). The big remaining lever would be a JIT for the SC-55 CPU, which is a project of its own.

### Memory (Wine, embedded ROMs, units active)

| Plug-in | Before | After |
| --- | --- | --- |
| MT-32 / CM-32L (2 units) | - | about 3 MB per unit on top of the plug-in (measured on Linux) |
| SC-55 v1.21 | 236 MB | **30 MB** |
| SC-55mk2 | 216 MB | **29 MB** |
| SC-88 Pro | 247 MB | **166 MB** |
| SC-8820 (4 units) | 213 MB | **81 MB** |
| SC-8850 (2 units) | 160 MB | **129 MB** |

Each SC-55 unit held 19.5 MB of arrays (wave ROMs 5 MB + card 2 MB + expansion 8 MB + a 4 MB LCD buffer), of which about 3.5 MB were used. Now the wave ROMs are one `calloc` block shared by all units (untouched pages cost nothing) and the LCD buffer is only created on demand. In the SC-88 Pro, the decoded wave ROM is shared process-wide (cache by FNV hash of the raw data); the rest (about 35 MB per unit) is the internal state of 88emu and JIT code. The SC-8820 and SC-8850 map their wave ROM straight from the embedded image instead of copying it (the SC-8820 used to copy 24 MB per unit, the SC-8850 a shared 32 MB block that was also hashed at every boot), so a unit costs about 13 MB (SC-8820) or 34 MB (SC-8850) in the Linux measurement; the output is bit-identical.

### Load time (Wine, one core)

| Plug-in | Before | After |
| --- | --- | --- |
| SC-55 v1.21 | 0.37 s | **0.04 s** |
| SC-55mk2 | 6.41 s | **0.58 s** |
| SC-88 Pro | 3.56 s | **1.06 s** |

Only SC-55 unit 0 boots; the others take a bit-exact copy of its state (`Emulator::CopyStateFrom`). SC-88 Pro units 1..N boot in background threads, and a unit that is needed before it has booted makes the wake-up wait. The warmer thread (not the audio thread) waits for a booting unit it needs, and it schedules by the logical position in the state log, because otherwise the warm-up would depend on the boot time and the output would not be reproducible. So playing right after loading no longer stalls the first block (SC-8820 1.7 s to 2 ms, SC-8850 2.8 s to 4 ms in the sandbox).

---

## Verification

### Method

- **Reference:** the unchanged upstream core (`tools/exact/core_orig`) and recorded **golden values** (`tests/golden.json`): hashes and CPU times for the core of v1.21 and mk2, for the three plug-ins on the plug-in level, and for the prepared SC-88 Pro ROM set. They are generated with `tools/make_golden.sh` and renewed after every deliberate change of the sound.
- **Test material:** the polyphony test `SC55-Polyphony-Test.mid` (generated by `tools/midi/gen.py`), game music (E1M1, Animus, Duke Nukem 3D "grabbag"), and LCD test files (StarGame, 3X3EYES).
- **Environment:** the automated tests ran under Wine on Linux. The author confirmed playback on Windows (CPU measurements above, glitch-free playback of the "grabbag" song with both SC-55 variants, running back to a single unit); the SC-88 Pro and its map switch run on the author's machine. Wider testing in different DAWs is still open.

### Polyphony test

Nine rounds with 20 / 40 / 64 / 96 / 128 / 160 / 200 / 240 / 300 organ notes (one partial per note). The test tone is the **oldest** note; if it is still audible, nothing was stolen. Voice Reserve is set to 0 by SysEx (`F0 41 10 42 12 40 01 10` + 16 x `00` + `2F F7`), otherwise the firmware protects a test tone on its own channel and the test shows nothing.

| Configuration | Test tone audible until | Silent from |
| --- | --- | --- |
| Original 24 / 28 | round 1 | round 2 |
| Poly 128 (144) | round 5 | round 6 |
| Poly 256 (264 / 280) | round 8 | round 9 |

Awake instances per round (v1.21, dynamic): 1 / 2 / 3 / 5 / 6 / 7 / 9 / 11 / 11. SC-88 Pro: audible until round 8, round 9 silent, units 1 -> 4, every wake-up 0.4-0.6 ms without replay, units go to sleep after every round.

### Bit-exactness

- E1M1 on v1.00 / v1.21 / v2.00 and the polyphony test on v1.21 / MkII: identical to the original core (`tools/exact/verify.sh`, 5 combinations).
- The timer optimisation, the memory changes (shared wave ROM, LCD buffer), the state copy, the volume stage at 1.0 and the incremental LCD are checked against `tests/golden.json`: identical.
- With 24 / 28 voices or fewer, plug-in output equals a single module (hash).
- The SC-55 code with the SC-88 Pro engine added is byte-identical to the version before it.
- SC-88 Pro: three runs with embedded ROMs equal the golden value; the voice counter and the map switch were tested separately.

### Other checks

- **Windows (Wine):** CTF test (bank 1, program 17): RMS 0.0432 (v1.21), 0.0301 (MkII), about 0.0109 (SC-88 Pro); VST2 and CLAP load, editor opens, state is saved and loaded, host change notification arrives.
- **Languages:** menu language checked with `de_DE`, `en_US`, `fr_FR` (French gives English).
- **LCD:** SysEx text and bitmap animation (StarGame 0.13 s; 3X3EYES text + bitmaps) on SC-55 v1.21 and SC-88 Pro; key shift test +6 / -12 matches plug-in data and firmware LCD.
- **Builder:** nested folders, arbitrary file names, foreign files, original `rom2` only (CTF applied), one set only (only that plug-in), MkII from CTF `rom2` byte-identical to MkII from the patched original, junction / symlink folders.

---

## Known limitations and open points

These are limits of the approach, or things that are not done yet.

- **Voice stealing differs from hardware:** the firmware only steals voices once **all** instances are full, and then inside the chosen instance, not globally by oldest voice.
- **Clipping:** the internal clipping limit applies per instance, not to the sum (in float there is no sum clipping). Watch the level of very dense arrangements.
- **Effects:** summed reverb and chorus are mathematically almost equivalent to one effect section, but fixed-point rounding per instance can cause minimal differences.
- **Partial reserve** (GS "Voice Reserve") works per instance.
- **Retrigger special case:** if the owning instance is full, a retrigger goes to another instance. In the extreme test (256 retriggers) more voices sound than with a static assignment; level deviation 1-4 %. Accepted.
- **VST2 has one MIDI input.** The SC-88, SC-88 Pro, SC-8820 (32 parts) and SC-8850 (64 parts) reach their other ports there only through the port select messages `F5 0n`; CLAP offers one note port per MIDI input.
- **SC-88 Pro:** uses much more memory (166 MB) and larger files (about 27 MB) than the SC-55 variants. 88emu is an early access alpha and models the analog output stages for SC-55 mk1/mk2 too (`AnalogOutputMode`), which might become a sound option later.
- **64-bit only, Windows only for the editor and single-file builds.** There is no 32-bit build yet; 32-bit programs can use a MIDI driver that hosts VST plug-ins (see [Using the plug-ins](#using-the-plug-ins)).
- **Not yet decided:** a language selection in the SETUP menu (automatic / English / German, stored in the state); a 48-voice variant for lower peak load (available today through SETUP); other 88emu devices; more CPU work (resampler, about 11 % of the plug-in share, not bit-exact; mixing overhead with several instances); HiDPI scaling and fonts of the editor.

---

## Known issues

### MT-32 / CM-32L

- **Device switch:** booting the units again takes about 0.3 s in the audio thread (a short dropout); what the host sent before is gone, as after swapping the module.
- **Firmware choice:** per device the plug-in contains the one firmware the builder found first (MT-32 old: 1.07 before 1.06, 1.05, 1.04, Blue Ridge, M-9; MT-32 new: 2.04 before 2.07; CM-32L: 1.02 before LAPC-I 1.00). Other versions cannot be chosen in the plug-in.
- **Units never sound sample-identical:** the LA32 phases run freely, so two units with the same memory differ slightly in phase. Whether they hold the same patches can only be compared through their memory (RQ1), not by their sound.

### SC-88

- **ALL + INST MAP and ALL + MIDI CH** do nothing (there is no GS message for them that the other units could follow). Panel edits are passed on from the firmware's edit log, whose address belongs to control ROM v1.01.

### SC-8820

- **CPU ROM:** the file `7f3be975fc9ada3d` is the original dump R02015367. The earlier reconstruction (MD5 `b03dc0c5…`) still boots as in 88emu; rare firmware paths may behave differently from the device with it.
- **Level indicators:** the emulation gives the firmware's lamps in four blocks per part group (3 columns each); finer steps that the device may show are not available.
- **SC-8820 part display:** the work RAM addresses it reads belong to the accepted program ROM (2000-11-28). Some tone names end in a marker character of the firmware (for example `Violin    :`).

### SC-8850

- **Panel edits of user tones and user drum sets** (stored in the firmware's user memory) are not passed on to the other units; part, system, effect and drum map settings are. The firmware RAM addresses the plug-in reads (current part, MUTE / SOLO) belong to the accepted program ROM.

---

## Using the plug-ins

- **Install the CLAP file** into one of the standard locations, for example `C:\Program Files\Common Files\CLAP\` or `%LOCALAPPDATA%\Programs\CLAP\`. **VST2:** copy the `.dll` into the VST2 folder of your host.
- **DAW:** load the plug-in as an instrument and send MIDI to it. For GM/GS/XG songs, a GS reset (or GM on) at the start is sent to all units as usual.
- **Falcosoft:** the plug-ins are meant to work with the [Falcosoft MIDI Player](https://falcosoft.hu/) and the Falcosoft VST MIDI Driver, which makes a VST instrument available as a system MIDI device (DOS games in DOSBox, Windows games). The MIDI Player is also a good second opinion for the LCD: its "Midi Text" and "Visualization" dialogs show the same text and bitmap messages.
- **Which one:** SC-55 v1.21 is the classic DOS game sound (Doom, Duke Nukem 3D and many others), SC-55mk2 with CTF is the later model with the SC-55 tones as fallback, SC-88 Pro offers the larger sound set and can switch back to the SC-55 and SC-88 maps.
- **Loudness:** every unit adds its notes to one mix. With very dense music, lower the host fader or use the SC-88 Pro volume knob.

---

## Development and test tools

The development workbench contains the source, the complete patch series (`patches/`, 31 `git format-patch` files on `de2a799`), and all tools that were used for the measurements. The most important ones:

| Tool | Purpose |
| --- | --- |
| `tools/exact/ref_orig`, `ref_new`, `verify.sh` | one emulator without plug-in: renders an event list and prints an FNV hash and CPU time; same hash = bit-exact |
| `tools/test/play`, `e1`, `host`, `diag`, `peaks` | CLAP-level tests: polyphony test per round, event lists with CPU and voices per 10 s, scenarios (single note, 160-note flood, CTF, 256 retriggers, runtime limit), wake/sleep log with replay bytes and block time |
| `tools/test/diag88`, `play88`, `peaks88`, `map88`, `tools/sc88probe/` | the same against the SC-88 Pro engine, including the map transitions |
| `tools/winbuild/vsthost.exe`, `hostw.exe`, `clapgui.exe`, `guihost.exe`, `lcdhost.exe`, `guiload.exe` | Windows test hosts (run under Wine): VST2 / CLAP host, editor test with screenshots and menu, LCD screenshots at song times, CPU of the interface thread |
| `tools/winbuild/polybench.exe` | Windows host for the worker pool: loads a finished plug-in (`.clap`), sets N awake units via `NUKED_SC55_POLY_VOICES` / `NUKED_SC55_POLY_DYNAMIC=0` and measures the wall time of the audio blocks against the CPU time of the process (speed-up, efficiency, loss against the ideal, blocks over budget); `--load K` adds K busy threads, `--events` plays an event list. `--soak` is a real-time long-run mode: it plays one or several plug-ins at the block rate of a host for hours and logs block times, private memory, handles, threads and GDI/USER objects once a minute, marks minutes with foreign CPU load and ends with trends per hour. Build line in the head of `polybench.cpp`. |
| `tools/winbuild/panelhost.exe`, `mt32host.exe` | Windows test hosts (run under Wine): mouse actions on the panel with screenshots at song times (`NK_CHUNK` loads a state first, for example the MT-32 device); MT-32 / CM-32L: RMS per second, device switch during playback |
| `tools/test/lcdtest`, `lcddump` | LCD renderer (incremental vs. full, must give 0 differences), character memory dump |
| `tools/test/mem`, `act`, `idle` | memory, load time and idle load of a plug-in |
| `tools/make_golden.sh`, `tests/golden.json` | reference hashes and CPU times |
| `tools/upstream_check.sh` | `report`: new relevant commits in Nuked-SC55-CLAP and Gearmulator since the pinned state (`upstream.lock`); `trial`: build the new state with our patches and compare with the golden values |
| `tools/midi/gen.py`, `smf2ev.py` | polyphony test MIDI file, SMF to event list |

### Keeping up with upstream

`upstream.lock` pins the observed states: Nuked-SC55-CLAP `de2a799` (paths `src/`, CMake, vcpkg) and Gearmulator `c8047e7a61a9` (88lib, XP chip, panel graphics, CPU cores including dsp56300, synthLib / hardwareLib / baseLib). Our own changes to the Nuked core are also kept as a single patch, `third_party/nuked-core/nuked-core-timers.patch` (timers, memory, state copy), so that it can be checked on its own against a new upstream. A new state is taken over by changing the pin, adapting the patches if needed, rebuilding and renewing the golden values. A GitHub Actions template for a weekly report as an issue is in `ci/`.

---

## Credits and license

- **NukeYKT** - [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55), the low-level SC-55 emulation this project is built on
- **John Novak** - [Nuked-SC-55-CLAP](https://github.com/johnnovak/Nuked-SC55-CLAP), the CLAP plug-in this project forked from
- **dsp56300** - [Gearmulator](https://github.com/dsp56300/gearmulator), including the 88emu core used for the MT-32, CM-32L, SC-88, SC-88 Pro, SC-8820 and SC-8850
- **shingo45endo** - [SC55MK2-CTF-Patcher](https://github.com/shingo45endo/sc55mk2-ctf-patcher), tool to modify the SC-55mkII firmware for CTF support
- **Falcosoft** - Falcosoft MIDI Player and Falcosoft VST MIDI Driver ([falcosoft.hu](https://falcosoft.hu/))
- **Magnific** - the patcher icon is a VST icon from [magnific.com/icons/vst](https://www.magnific.com/icons/vst)
- **Roland** - [Roland](https://www.roland.com), the MT-32, the CM-32L and the SC-55, SC-55mk2, SC-88, SC-88 Pro, SC-8820 and SC-8850 sound modules

### License

This project is licensed under the **GNU General Public License v3.0** (see [LICENSE](LICENSE.md).

Parts of the code come from other projects and keep their own notices:

- The files of the emulation core in `src/nuked-sc55/` carry the notice of nukeykt: redistribution is allowed, but not for sale and not in a commercial product or activity, and modified redistributions must include the complete source code.
- The original Nuked-SC55-CLAP plug-in code is distributed under the GPL v2.0 or later, as stated in its README.
- 88emu and the SC-88 Pro panel art are by The Usual Suspects and part of Gearmulator (GPLv3).
- The CLAP headers in `include/clap` are from the CLAP project. The VST2 interface in `src/vst2/vst2_abi.h` is an independent description of the binary interface and contains no Steinberg SDK code ("VST" is a trademark of Steinberg Media Technologies GmbH).

**ROMs are not part of this repository and are not covered by this license.** They are the property of Roland. Plug-ins built with the builder or with embedded ROMs contain them and are for private use only.

This software comes with no warranty (see sections 15 and 16 of the license). "Roland", "Sound Canvas" and the model names are trademarks of their owners and are only used to say which hardware is emulated.
