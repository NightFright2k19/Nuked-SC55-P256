# Nuked SC-55 P256

<p align="center">
<img width="587" height="256" alt="sc55_v121" src="https://github.com/user-attachments/assets/8edc288c-b546-45f8-a545-86653f200ef6" />
<img width="588" height="257" alt="sc55_mk2" src="https://github.com/user-attachments/assets/c82088b1-2c79-4ec0-87df-da444537797b" />
<img width="691" height="210" alt="sc88_pro" src="https://github.com/user-attachments/assets/6c130f6c-574c-44d2-804c-e4a0ee6d53d3" />
</p>

Single-file **CLAP** and **VST2** plug-ins (Windows x64) for the Roland **SC-55 (v1.21)**, **SC-55mk2 (v1.01, with Capital Tone Fallback)** and **SC-88 Pro**, built on the low-level emulation of [Nuked SC-55](https://github.com/nukeykt/Nuked-SC55) and [88emu](https://github.com/dsp56300/gearmulator). The original modules stop at 24 (SC-55) or 28 (SC-55mk2) simultaneous voices. These plug-ins run several bit-identical emulator instances side by side and distribute the MIDI notes among them, which raises the limit to **256-280 voices** without changing how a single module sounds.

This document is the reference for the current state of the project: what changed compared to Nuked-SC55-CLAP, supported models and ROM sets, how to get the plug-ins (with the builder or from source), how the polyphony engine works, what the panels do, how the results were checked, and what is still open.

> [!IMPORTANT]
> The plug-ins aim to preserve an important part of DOS gaming history for all to enjoy. They are only intended for **personal use** (retro gaming, writing music as a hobby) and **research purposes**. **No ROM files are included in this repository.** Plug-ins that contain your ROMs (everything the builder produces) are for **private use only** and must not be shared.

## Table of contents

- [Changes compared to Nuked-SC55-CLAP](#changes-compared-to-nuked-sc55-clap)
- [Quick start](#quick-start)
- [Supported models and ROM sets](#supported-models-and-rom-sets)
- [The plug-ins](#the-plug-ins)
- [How the polyphony works](#how-the-polyphony-works)
- [Capital Tone Fallback (SC-55mk2)](#capital-tone-fallback-sc-55mk2)
- [SC-88 Pro engine (88emu)](#sc-88-pro-engine-88emu)
- [Panels and controls](#panels-and-controls)
- [Settings and plug-in state](#settings-and-plug-in-state)
- [Configuration (environment variables)](#configuration-environment-variables)
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

- **Multi-instance polyphony:** N identical, bit-exact emulators run in parallel and their outputs are summed. A note router in `src/poly_router.h` decides which instance plays which note. Result: **264 voices** (SC-55 v1.21, 11 units), **280 voices** (SC-55mk2, 10 units), **256 voices** (SC-88 Pro, 4 units of 64).
- **Dynamic instances:** only as many instances run as the music needs. Sleeping instances are brought up to date from a compact state log when they wake (`src/state_log.h`), and a background thread pre-synchronises the next sleeping units so that waking up never causes a dropout.
- **Constant DC offset compensation:** every instance outputs a constant 1/32 of full scale; it is measured after boot and subtracted from all additional instances. With 24/28 voices or fewer, the output is **bit-identical** with a single module.
- **Runtime voice limit** (SETUP menu): 24 / 48 / 64 / 96 / 128 / 160 / 192 / 224 / 256 (SC-55mk2: 28 / 56 / ...). Instances above the limit receive no new notes and go to sleep once silent.

### Models

- **SC-55mk2 with CTF:** the plug-in prefers a `rom2.bin` patched with Capital Tone Fallback (the original plug-in always loads the unpatched one). In the single-file builds, the CTF `rom2.bin` is built in.
- **SC-88 Pro** (new): a second engine based on the 88emu core from the Gearmulator project, with the SC-55 / SC-88 / SC-88 Pro tone map switch of the hardware, volume, mute and preview. The SC-55 code is untouched by this (audio compared byte by byte).

### Formats and packaging

- **VST2** (new): a bridge exposes the CLAP plug-in of the same file as a VST2 plug-in. The VST2 binary interface is described independently in `src/vst2/vst2_abi.h`; **no Steinberg SDK** is used. One file serves as `.clap` and as `.dll`.
- **Single-file builds with embedded ROMs** (new, private use only) and **ROM-less templates** plus a Python builder that inserts your ROMs, so no compiler is needed.
- **Settings only in the plug-in state:** no `.ini`, no registry, no file writes. The state is saved by the host (CLAP state / VST2 chunk), changes are reported to the host.

### Core and speed

- **Bit-exact lazy timer evaluation** in `mcu_timer.cpp`: event-free timer ticks only increment a counter. About **35-40 % less CPU** per instance (with `-O3 -flto`), output identical to the original core.
- **Packing and wake logic:** notes fill the lowest awake instance first; a single awake instance renders directly in the audio thread without any threading overhead.
- **Memory:** wave ROMs are shared between SC-55 units (one block, untouched pages cost nothing); the unused 4 MB LCD pixel buffer per unit is gone; 88emu units share one decoded wave ROM.
- **Faster loading:** SC-55 units are cloned from unit 0 instead of each booting itself (SC-55mk2 load time 6.4 s to 0.6 s under Wine); SC-88 Pro units boot in the background.

### User interface (Windows)

- **Editor panel** for both device families, drawn procedurally with Win32/GDI (SC-55) or derived from the 88emu panel art (SC-88 Pro): part parameters, 16 part level meters, voice meter, SETUP menu.
- **Original LCD** rendered from the emulated LCD controller: SysEx text and bitmap messages (`45 10 00 xx`, `45 10 01 xx`) are shown exactly as the firmware displays them.
- **Menu language** follows the Windows UI language (German or English); the printed panel labels stay English like on the hardware.

### Identification

- Internal name **"Nuked SC-55 P256"** / **"Nuked SC-88 P256"** (CLAP plug-in names, VST2 vendor, Windows version info, editor window class). Plug-in IDs stay on `net.nuked_sc55_poly_clap.*` so existing DAW projects keep working.
- **Version number = build date** (`YYYY.MM.DD`) in the Windows version info.

---

## Quick start

There are two ways to get the plug-ins. The first needs only Python.

### Option A: the P256 builder (no compiler)

1. Install Python 3.8 or newer (no extra packages).
2. Download the **P256 Builder** package from the Releases page and unpack it. It contains `p256_builder.py`, the `templates` folder (the finished plug-ins **without** ROMs) and an empty `roms` folder.
3. Put your ROM files into `roms` (subfolders are searched, file names do not matter, see [Supported models](#supported-models-and-rom-sets)).
4. Run:

   ```
   python p256_builder.py
   ```

5. Copy the results from `output\CLAP\*.clap` and/or `output\VST2\*.dll` to your plug-in folders (see [Using the plug-ins](#using-the-plug-ins)).

Everything for which a complete ROM set is found is built (SC-55 v1.21, SC-55mk2 v1.01, SC-88 Pro). More in [The P256 builder](#the-p256-builder).

### Option B: build from source

See [Building from source](#building-from-source).

> [!NOTE]
> An original `Nuked-SC55.clap` is **not** needed. The P256 features are new code and cannot be patched into the original binary; the builder works from its own templates.

---

## Supported models and ROM sets

| Model | Single-file build | Voices per unit | Units | Total |
| --- | --- | --- | --- | --- |
| SC-55 v1.21 | yes | 24 | 11 | 264 |
| SC-55mk2 v1.01 (CTF) | yes | 28 | 10 | 280 |
| SC-88 Pro | yes | 64 | 4 | 256 |
| SC-55 v1.00 / v1.10 / v1.20 / v2.00 | source builds with external ROMs only | 24 | configurable | - |

The ROMs are recognised **by content** (SHA-256), not by file name. Dumps of other versions are ignored. The hashes below are the first 16 digits; the full values are in `tools/p256_builder/p256_builder.py`.

| Model | Role | Typical file name | SHA-256 (start) |
| --- | --- | --- | --- |
| SC-55 v1.21 | rom1 | `sc55_rom1.bin` | `7e1bacd1d7c62ed6` |
| | rom2 | `sc55_rom2.bin` | `effc6132d68f7e30` |
| | wave ROM 1 / 2 / 3 | `sc55_waverom1/2/3.bin` | `5655509a531804f9` / `c655b159792d999b` / `334b2d16be3c2362` |
| SC-55mk2 v1.01 | rom1 | `rom1.bin` | `8a1eb33c7599b746` |
| | rom2 with CTF | `rom2.bin` | `10b3f09485a74bb0` |
| | rom2 original (no CTF) | `rom2.bin.old` | `a4c9fd821059054c` |
| | rom_sm | `rom_sm.bin` | `b0b5f865a403f730` |
| | wave ROM 1 / 2 | `waverom1.bin` / `waverom2.bin` | `c6429e21b9b3a02f` / `5b753f6cef4cfc7f` |
| SC-88 Pro | control ROM (v1.02) | `sc88pro_control.bin` | `efcdbe43f5810d34` |
| | wave ROM 0 / 1 / 2 | `sc88pro_wave0/1/2.bin` | `3c6a96298e0de126` / `42bcbba9506a667c` / `db40d8624fceec5a` |

The SC-88 Pro files are the raw dumps (control ROM 1 MB, wave ROMs 8 + 8 + 4 MB).

> [!NOTE]
> **SC-55mk2:** either `rom2` works. If only the original `rom2` (512 KB) is found, the builder applies the CTF patch itself and checks the result against the known checksum (see [Capital Tone Fallback](#capital-tone-fallback-sc-55mk2)).

For source builds with **external** ROMs, the layout of the original plug-in applies: a `Nuked-SC55-Resources/ROMs/<model>/` folder next to the plug-in (for example `SC-55-v1.21/sc55_rom1.bin`) or one or more directories listed in the `SOUNDCANVAS_ROM_PATH` environment variable. The complete file list per model is in the [Nuked-SC55-CLAP README](https://github.com/johnnovak/Nuked-SC55-CLAP#rom-files).

---

## The plug-ins

| Display name | File name (`.clap` / `.dll`) | Core | CLAP ID | VST2 ID | Default max voices |
| --- | --- | --- | --- | --- | --- |
| Nuked-SC55 v1.21 | `Nuked-SC55_v121` | Nuked SC-55 | `net.nuked_sc55_poly_clap.sc55_v1_21` | `S5P3` (0x53355033) | 264 |
| Nuked-SC55 MkII | `Nuked-SC55_MkII` | Nuked SC-55 | `net.nuked_sc55_poly_clap.sc55mk2_v1_01` | `S5PM` (0x5335504D) | 280 |
| Nuked-SC88 Pro | `Nuked-SC88_Pro` | 88emu | `net.nuked_sc55_poly_clap.sc88pro` | `S8PP` (0x53385050) | 256 |

- The file names contain no spaces or dots (some hosts and drivers dislike them); the display names in the host stay "Nuked-SC55 v1.21", "Nuked-SC55 MkII" and "Nuked-SC88 Pro".
- Both formats are the **same file** with two entry points (`clap_entry` and `VSTPluginMain`). The builder writes it twice, once as `.clap` and once as `.dll`.
- Category: instrument/synth; flags: editor, replacing, program chunks. VST2 vendor `Nuked SC-55 P256` (SC-88 Pro: `Nuked SC-88 P256`).
- The IDs of the plug-ins never changed since the first version, so projects stay compatible. Hosts may show a new vendor after a rescan.
- Windows version info: product name, description and internal name are the display name; file and product version are the build date (`2026.10.07` is stored as `2026,10,7,0` numerically, which Windows shows as `2026.10.7.0`).
- A DLL with ROMs is large: about 27 MB for the SC-88 Pro (21 MB ROMs, 1 MB panel graphics).

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
| Everything else | CC, program change, pitch bend, pressure, SysEx and resets go to **all awake** instances. |

**Load measurement:** the load of an instance is its number of sounding partials plus 2 per note sent since the last measurement. A slot counts as active when its TVA envelope level (`pcm.ram2[slot][10]`) is non-zero; the voice mask of the chip is always full and cannot be used. For the 88emu core, a voice is active when `combinedAmp_2300 != 0`.

### DC offset

Every SC-55 instance outputs a constant 1/32 of full scale. It is measured after boot and subtracted from all instances except instance 0. With up to 24/28 voices, the output is **bit-identical** to a single module (checked by hash).

### Dynamic instances

After boot, everything except `min_active` (1) sleeps.

- **Going to sleep:** the output of an awake unit must stay below 2.5e-4 (about -72 dBFS after DC removal, which also covers reverb tails) for 1 s, **and** the remaining awake units must keep at least a quarter of a unit in reserve (or the unit is above the voice limit). The SC-55 mk1 emulation (v1.xx) toggles its output by one step (1.2e-4) even without a sounding voice, so the threshold has to be above that; the MkII does not show this residual noise. When a unit falls asleep, its note assignments are cleared.
- **State log** (`src/state_log.h`): messages that a sleeping unit misses are logged compactly (last value per parameter, ordered by last occurrence) and replayed when it wakes. GS reset / GM on clears the log and gets 60 ms of settle time. Program changes are stored with the bank that was valid at the time, RPN/NRPN as a sequence; increment/decrement (96/97) and the "notes off" controllers are not logged.
- **Replay speed:** 3000 MCU cycles per byte on the emulated UART (about 2.5 times MIDI speed), then the buffer is drained and 5 ms are added.
- **Background warmer:** every 250 ms the audio thread hands the lowest of the **next two** sleeping units that is behind the state log, together with the missing log entries, to a separate thread (`try_lock` only, the audio thread never blocks). The warmer replays them. Result in the test song "grabbag": wake-up takes 0.3-0.9 ms with 0 bytes left to replay; without the warmer the first wake-up replayed 450 bytes (7.6 ms, more than the 11.6 ms block budget allowed).
- **Rendering:** only awake instances are rendered and measured; one awake instance renders directly in the audio thread, several use a worker pool (the audio thread works with it).

### Voice limit

`max_voices` determines the allowed instances: ceil(max / capacity). The default for a new plug-in instance is the maximum (264 / 280 / 256). Instances above the limit get no new notes and sleep as soon as they are silent. The limit is part of the plug-in state.

---

## Capital Tone Fallback (SC-55mk2)

With CTF, a module falls back to the capital (basic) tone of a program when the selected variation tone does not exist, instead of staying silent. The SC-55 firmware does this by itself; the original SC-55mk2 firmware does not, which is why the mk2 ROM gets patched.

- **SC-55 (all versions):** CTF is part of the original firmware and always active.
- **SC-55mk2:** the single-file MkII plug-in contains a `rom2.bin` patched with [sc55mk2-ctf-patcher](https://github.com/shingo45endo/sc55mk2-ctf-patcher) (variant "Tone SC-55, Drum SC-55 v1.21"). In source builds with external ROMs, a CTF-patched `rom2.bin` is **preferred** over the original one. Force a specific set with `NUKED_SC55_MK2_ROMSET=mk2-v1.01` (no CTF) or for example `mk2-ctf-sc55-drum-sc55-v1.21`.
- **SC-88 Pro:** CTF is built in (bank 1/5 fall back to the basic tone), no patch needed.
- **Builder:** the CTF patch is embedded in `p256_builder.py` (77 sections, 15,561 bytes raw, zlib + Base64 808 characters). The result is checked against the SHA-256 of the known CTF `rom2`.
- **Quick test:** bank 1, program 17. The SC-55 plays it, the original SC-55mk2 stays silent, the CTF SC-55mk2 plays it.

---

## SC-88 Pro engine (88emu)

The SC-88 Pro variant does not use the Nuked core. It uses **88emu**, a low-level emulation of the SC-88 / SC-88VL / SC-88Pro / SC-8850 with the original firmware, written by The Usual Suspects and part of [Gearmulator](https://github.com/dsp56300/gearmulator) (marked "early access alpha" there, GPLv3).

- **Pinned version:** Gearmulator `96deb437794baf109fb996a2f8806ce2ee949246`, with the submodules dsp56300 `c1d6593`, asmjit `3577608`, mc68k `cc3693a`, freetype `8289165`, lunasvg `f8aabfb` (+ plutovg `5e4712c`) and RmlUi `d2e83ba`. `tools/build_88emu.sh <target> <source>` fetches and builds exactly this state (about 8-10 min on one core).
- **Patches** (`third_party/88emu/`): `88emu-nuked-poly.patch` adds `emu88_get_active_voice_count` (voice counter), `emu88_set_sc88pro_rom_images` (a normalised ROM set from memory, bypassing the file scan), `emu88_dump_sc88pro_rom_images` (build tool) and `emu88_get_display_memory` (LCD). `dsp56300-mingw.patch` restricts SEH (`__try`) to MSVC.
- **Engine variant:** compile-time `NUKED_SC55_ENGINE_88PRO` (model index 6). All differences sit in `#ifdef` blocks at about ten coupling points, so the SC-55 code is unchanged.
- **Units:** 64 voices per unit, 4 units. 88emu renders at the device rate of 32 kHz, which goes through the existing resampler. Boot takes about 0.7 s per unit.
- **Port:** only port A (16 parts). The real SC-88 Pro has 32 parts on two ports; VST2 offers only one.

### Tone map switch

Like the hardware, the map is switched with emulated panel keys. Both map keys **toggle**: SC-55 MAP toggles 55 <-> Pro, SC-88 MAP toggles 88 <-> Pro. **ALL latches** and makes the key press valid for all parts. The map LEDs of the emulation do not show the map reliably, so the plug-in tracks the map of every unit itself (`applied_map`, boot state = Pro). The sequence is ALL on, one map key, ALL off, running in the background while rendering (press 0.15 s, release 0.35 s); only during boot and in the warmer it runs synchronously. The map survives GS resets like on the hardware; SysEx `40 1x 7F` has no effect. All 15 transitions were tested from every state.

| `map` | Tone map |
| --- | --- |
| 0 | SC-55 |
| 1 | **SC-88 (default)** |
| 2 | SC-88 Pro (factory setting of the hardware) |

The panel keys toggle like on the device, the SETUP menu selects directly.

---

## Panels and controls

Both panels are Windows-only (Win32/GDI), 30 frames per second, and read the engine only through atomics.

### SC-55 panel (780 x 340 px)

- **Style:** drawn procedurally: rack ears with screws, metal texture, recessed orange dot-matrix LCD, rubber keys with LEDs, info bar. It is inspired by the look of the Gearmulator skins; **no graphics of Gearmulator and no Roland or Sound Canvas logos are used**.
- **LCD:** the contents of the emulated HD44780 controller (DDRAM 80 bytes, CGRAM 64 bytes) shown on the original glass. Scrolling text, display time and the return to the normal display are handled by the firmware itself. It also shows the 16-part level matrix.
- **Voices window:** sounding voices as segment bar with peak hold; `MAX / CAP / UNITS`; above the limit "UNITS 6->2".
- **Buttons:** PART left / PART right (press the device's PART keys on unit 0), ALL OFF, SETUP. A click on a matrix column selects the part for the value windows; the mouse wheel changes the part.
- **Value windows:** part parameters (GM names or GS kit names, `*` for bank not 0).
- **Window class:** contains the module handle, so v1.21 and MkII can run in the same host.

### SC-88 Pro panel

- **Graphics:** derived from the panel art of 88emu (GPLv3, The Usual Suspects). `tools/make_sc88_panel.py` halves it, replaces the 88emu branding by "NUKED-SC88 PRO" and the playlist / EFX fields by VOICES, SETUP, TONE MAP, UNITS and MAX VOICES. The controls (ALL, MUTE, SC-55, SC-88, PART arrows, PREVIEW, VOLUME knob with 31 positions) come from the 88emu player assets (`tools/make_sc88_sprites.py`).
- **TONE MAP keys:** SC-55 / SC-88; the green LED shows the active map, both off means SC-88 Pro.
- **VOLUME:** 0..1, characteristic off / -60 ... 0 dB, applied at the output after resampling with a ramp per block; at 1.0 there is no multiplication (bit-identical). Drag the knob or use the mouse wheel.
- **MUTE:** per part (bit = MIDI channel). Note ons of that channel are dropped, a newly muted part gets CC 120. The mute state is **not** part of the plug-in state (like on the device).
- **PREVIEW:** note on while pressed, note off when released, on the selected part, velocity 100, note from the menu "Prevw Note" (C-1 ... G9, Roland numbering, C4 = 60, default C4). It goes through the normal routing path.

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
| Max polyphony | 24 / 48 / 64 / 96 / 128 / 160 / 192 / 224 / 256 (SC-55mk2: 28 / 56 / ...) |
| GS reset | sends a GS reset to all units |
| All notes off | stops all sounding notes |
| Tone map (SC-88 Pro) | SC-55 / SC-88 / SC-88 Pro |
| Prevw Note (SC-88 Pro) | submenu per octave |

The menu language (German or English) follows `GetUserDefaultUILanguage()`; every other Windows language gives English.

---

## Settings and plug-in state

The DLL **never writes files**. The only state is the plug-in state, saved by the host in the CLAP `state` extension or as a VST2 chunk (opcodes 23/24). A change in the menu is reported to the host (CLAP `state.mark_dirty`, VST2 `audioMasterUpdateDisplay`). The state is a short ASCII string:

```
NSC55P1 max_voices=<n>                                    SC-55 plug-ins
NSC55P1 max_voices=<n> map=<0..2> vol=<0..1000> pnote=<0..127>    SC-88 Pro
```

A new plug-in instance starts with the maximum number of voices. Old chunks without `map=` start with the SC-88 map. Whether a particular host or driver keeps the chunk is up to the host; if it does not, the plug-in starts with the maximum each time, which is harmless.

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

Build-time options (CMake cache variable or compiler define):

| Option | Meaning |
| --- | --- |
| `NUKED_SC55_POLY_TARGET_VOICES` | polyphony target (default 128 in CMake, 256 in the shipped builds) |
| `NUKED_SC55_ONLY_MODEL=<0..6>` | single-model build: 0 = v1.00, 1 = v1.10, 2 = v1.20, 3 = v1.21, 4 = v2.00, 5 = mk2 v1.01, 6 = SC-88 Pro |
| `NUKED_SC55_ENGINE_88PRO` | use the 88emu engine |
| `NUKED_SC55_EMBED_ROMS` | embedded ROMs (with a source from `tools/gen_embedded_roms.py`) |
| `NUKED_SC55_DISPLAY_NAME`, `NUKED_SC55_VST2_NAME`, `NUKED_SC55_VST2_ID` | names and VST2 unique ID |

---

## The P256 builder

`tools/p256_builder/p256_builder.py` (Python 3.8 or newer, standard library only) creates the finished plug-ins from the ROM-less templates in `templates` and your ROMs in `roms`.

```
python p256_builder.py
```

```
Nuked SC-55 / SC-88 P256 - Plugin Builder

[ok] SC-55 v1.21: output/CLAP/Nuked-SC55_v121.clap, output/VST2/Nuked-SC55_v121.dll
[ok] SC-55mk2 v1.01 (CTF) (CTF patch will be applied): output/CLAP/Nuked-SC55_MkII.clap, ...
[--] SC-88 Pro: no ROMs found

2 plugin(s) created.
```

- **Detection:** `roms` is searched recursively (symbolic links and junctions are followed, with loop protection); each file is identified by SHA-256, so names do not matter. Unknown files are skipped.
- **Per model:** a plug-in is built when its set is complete. An incomplete set is reported with the missing parts.
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

A short German version of the instructions is in `tools/p256_builder/LIESMICH.txt`, the English one in `tools/p256_builder/README-EN.txt`.

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
3. For a **template** instead, call `gen_embedded_roms.py --slot <out.cpp> <romset> <name> <capacity>` (the build function does this when `NK_SLOT_CAP` is set) and run `tools/p256_builder/compact_template.py` on the result.

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
| SC-55 v1.21 | 236 MB | **30 MB** |
| SC-55mk2 | 216 MB | **29 MB** |
| SC-88 Pro | 247 MB | **166 MB** |

Each SC-55 unit held 19.5 MB of arrays (wave ROMs 5 MB + card 2 MB + expansion 8 MB + a 4 MB LCD buffer), of which about 3.5 MB were used. Now the wave ROMs are one `calloc` block shared by all units (untouched pages cost nothing) and the LCD buffer is only created on demand. In the SC-88 Pro, the decoded wave ROM is shared process-wide (cache by FNV hash of the raw data); the rest (about 35 MB per unit) is the internal state of 88emu and JIT code.

### Load time (Wine, one core)

| Plug-in | Before | After |
| --- | --- | --- |
| SC-55 v1.21 | 0.37 s | **0.04 s** |
| SC-55mk2 | 6.41 s | **0.58 s** |
| SC-88 Pro | 3.56 s | **1.06 s** |

Only SC-55 unit 0 boots; the others take a bit-exact copy of its state (`Emulator::CopyStateFrom`). SC-88 Pro units 1..N boot in background threads, and a unit that is needed before it has booted makes the wake-up wait. The warmer also waits for a booting unit it needs, because otherwise the warm-up time would depend on the boot time and the output would not be reproducible.

---

## Verification

### Method

- **Reference:** the unchanged upstream core (`tools/exact/core_orig`) and recorded **golden values** (`tests/golden.json`): hashes and CPU times for the core of v1.21 and mk2, for the three plug-ins on the plug-in level, and for the prepared SC-88 Pro ROM set. They are generated with `tools/make_golden.sh` and renewed after every deliberate change of the sound.
- **Test material:** the polyphony test `SC55-Polyphonie-Test.mid` (generated by `tools/midi/gen.py`), game music (E1M1, Animus, Duke Nukem 3D "grabbag"), and LCD test files (StarGame, 3X3EYES).
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
- **16 parts only.** The SC-55 has one MIDI port; the SC-88 Pro has 32 parts on two ports, but VST2 only offers one. A second port for parts B01-B16 (CLAP only) is possible.
- **SC-88 Pro:** uses much more memory (166 MB) and larger files (about 27 MB) than the SC-55 variants. 88emu is an early access alpha and models the analog output stages for SC-55 mk1/mk2 too (`AnalogOutputMode`), which might become a sound option later.
- **64-bit only, Windows only for the editor and single-file builds.** There is no 32-bit build yet; 32-bit programs can use a MIDI driver that hosts VST plug-ins (see [Using the plug-ins](#using-the-plug-ins)).
- **Not yet decided:** a language selection in the SETUP menu (automatic / English / German, stored in the state); a 48-voice variant for lower peak load (available today through SETUP); other 88emu devices (SC-88, SC-8850); more CPU work (resampler, about 11 % of the plug-in share, not bit-exact; mixing overhead with several instances); HiDPI scaling and fonts of the editor.

---

## Known issues

### SC-88 Pro

- **The two parts of the GUI can get out of sync.** The PART left / PART right buttons press the device's PART keys, so the internal (firmware) LCD follows them. The plug-in only offers port A (16 parts), but the firmware LCD can be stepped past part A16 into the port B domain (parts B01-B16) by pressing the buttons several times. The plug-in's own display (the instrument shown on the right-hand side and the part indication of the value windows) does not know about this, so it keeps showing a port A part while the LCD shows a port B part.

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
| `tools/test/lcdtest`, `lcddump` | LCD renderer (incremental vs. full, must give 0 differences), character memory dump |
| `tools/test/mem`, `act`, `idle` | memory, load time and idle load of a plug-in |
| `tools/make_golden.sh`, `tests/golden.json` | reference hashes and CPU times |
| `tools/upstream_check.sh` | `report`: new relevant commits in Nuked-SC55-CLAP and Gearmulator since the pinned state (`upstream.lock`); `trial`: build the new state with our patches and compare with the golden values |
| `tools/midi/gen.py`, `smf2ev.py` | polyphony test MIDI file, SMF to event list |

### Keeping up with upstream

`upstream.lock` pins the observed states: Nuked-SC55-CLAP `de2a799` (paths `src/`, CMake, vcpkg) and Gearmulator `96deb437794b` (88lib, XP chip, panel graphics, CPU cores including dsp56300, synthLib / hardwareLib / baseLib). Our own changes to the Nuked core are also kept as a single patch, `third_party/nuked-core/nuked-core-timers.patch` (timers, memory, state copy), so that it can be checked on its own against a new upstream. A new state is taken over by changing the pin, adapting the patches if needed, rebuilding and renewing the golden values. A GitHub Actions template for a weekly report as an issue is in `ci/`.

---

## Credits and license

- **NukeYKT** - [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55), the low-level SC-55 emulation this project is built on
- **John Novak** - [Nuked-SC-55-CLAP](https://github.com/johnnovak/Nuked-SC55-CLAP), the CLAP plug-in this project forked from
- **dsp56300** - [Gearmulator](https://github.com/dsp56300/gearmulator), including the 88emu core used for the SC-88 Pro
- **shingo45endo** - [SC55MK2-CTF-Patcher](https://github.com/shingo45endo/sc55mk2-ctf-patcher), tool to modify the SC-55mkII firmware for CTF support
- **Falcosoft** - Falcosoft MIDI Player and Falcosoft VST MIDI Driver ([falcosoft.hu](https://falcosoft.hu/))
- **Roland** - [Roland](https://www.roland.com), the SC-55, SC-55mk2 and SC-88 Pro sound modules

### License

This project is licensed under the **GNU General Public License v3.0** (see [LICENSE](LICENSE.md).

Parts of the code come from other projects and keep their own notices:

- The files of the emulation core in `src/nuked-sc55/` carry the notice of nukeykt: redistribution is allowed, but not for sale and not in a commercial product or activity, and modified redistributions must include the complete source code.
- The original Nuked-SC55-CLAP plug-in code is distributed under the GPL v2.0 or later, as stated in its README.
- 88emu and the SC-88 Pro panel art are by The Usual Suspects and part of Gearmulator (GPLv3).
- The CLAP headers in `include/clap` are from the CLAP project. The VST2 interface in `src/vst2/vst2_abi.h` is an independent description of the binary interface and contains no Steinberg SDK code ("VST" is a trademark of Steinberg Media Technologies GmbH).

**ROMs are not part of this repository and are not covered by this license.** They are the property of Roland. Plug-ins built with the builder or with embedded ROMs contain them and are for private use only.

This software comes with no warranty (see sections 15 and 16 of the license). "Roland", "Sound Canvas" and the model names are trademarks of their owners and are only used to say which hardware is emulated.
