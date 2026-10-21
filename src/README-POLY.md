# Nuked SC-55 Poly (CLAP)

Fork of [Nuked-SC55-CLAP](https://github.com/johnnovak/Nuked-SC55-CLAP) (based on commit `de2a799`)
with extended polyphony: 128 voices by default, configurable up to 256+.

## Principle

The polyphony limit (24 partials on the SC-55, 28 on the SC-55mk2) is built into the
hardware of the PCM chip (a fixed number of slots per sample clock, to which the sample rate
is coupled) and into the firmware. Neither can sensibly be "bored out".
Instead, N identical, bit-exact emulators run in parallel:

| Model   | Partials/instance | 128 target      | 256 target       |
|---------|-------------------|-----------------|------------------|
| SC-55   | 24                | 6 inst. = 144   | 11 inst. = 264   |
| SC-55mk2| 28                | 5 inst. = 140   | 10 inst. = 280   |

`src/poly_router.h` distributes the MIDI data:

- Note-on → the instance with the lowest measured load (envelope level of all
  PCM slots, `ram2[slot][10] != 0`) + round-robin on a tie.
- Note-off / poly aftertouch → the instance(s) playing the key.
- Re-striking a held key → the same instance.
- Mono mode (CC126) and portamento (CC65) → the channel is pinned to one instance.
- Rhythm parts: exclusive groups (hi-hat, whistle, guiro, cuica, triangle, surdo)
  stay on one instance per channel so that choke works. The GS SysEx
  "Use For Rhythm Part" and "Rx Channel" are read along.
- Everything else (CC, PC, pitch bend, SysEx, GS/GM reset) goes to all instances.

All instances boot identically and receive identical controller/SysEx data.
As a result, LFOs, chorus and reverb run in lockstep, and the sum behaves
(linearly) like the effects section of a single device. The constant
DC offset of each instance (1/32 FS) is compensated for the additional instances:
**With ≤ 24/28 voices the output is bit-identical to a single instance.**

## Configuration

- Build time: `-DNUKED_SC55_POLY_TARGET_VOICES=256` (default 128)
- Run time (overrides): environment variable `NUKED_SC55_POLY_VOICES=256`
- ROMs: as in the original, `Nuked-SC55-Resources/ROMs/...` next to the plug-in or
  `SOUNDCANVAS_ROM_PATH`. New plug-in IDs (`net.nuked_sc55_poly_clap.*`), so the
  plug-in can be installed alongside the original.

## Capital Tone Fallback (CTF)

- SC-55 (all versions): CTF is part of the original firmware, always active.
- SC-55mk2: if a `rom2.bin` patched with sc55mk2-ctf-patcher is in the
  ROM folder, it is preferred (the original plug-in always takes the
  unpatched one). To force a specific variant:
  `NUKED_SC55_MK2_ROMSET=mk2-v1.01` (without CTF) or e.g.
  `mk2-ctf-sc55-drum-sc55-v1.21`.

## VST2

`src/vst2/` contains an independently written description of the VST 2.4
binary interface (no Steinberg SDK) and a bridge that offers the CLAP plug-in
of the same DLL as VST2. Single-model builds are used for this
(`-DNUKED_SC55_ONLY_MODEL=<0..5>`). The same file works as `.clap`
and as a VST2 `.dll`. See `build-mingw-win64.sh`.

## Dynamic instances

Only as many instances run as are needed. Sleeping instances
miss controllers/SysEx; these are logged compactly in `src/state_log.h` (last
value per parameter, ordered by last occurrence) and replayed when the
instance wakes up. To switch this off: `NUKED_SC55_POLY_DYNAMIC=0`.

## Single-file builds (embedded ROMs, private use only)

`-DNUKED_SC55_EMBED_ROMS` + a source generated with `tools/gen_embedded_roms.py`.
Display name: `-DNUKED_SC55_DISPLAY_NAME`, Windows version info:
`src/vst2/version.rc.in`. See `tools/build-embedded-win64.sh.inc`.

## Core optimisation: timers

`src/nuked-sc55/backend/mcu_timer.cpp`: timer ticks without events are
bundled (only the counter is increased); ticks with a compare match, overflow or after
register accesses run with the original logic. Bit-exact identical to the
original core, about 35–40 % faster (with -O3 -flto). Timers are evaluated lazily, per timer.

## Editor (Windows)

`src/gui/editor_win32.cpp` (Win32/GDI, drawn procedurally in the style of the Gearmulator skins; menu language follows the Windows UI language DE/EN): SC-55-like panel with part parameters,
16 part level bars, polyphony display and a SETUP menu (run-time voice limit,
GS reset, all notes off). Integration: CLAP `gui` extension, VST2
`effEditOpen`/`effEditClose`/`effEditGetRect`. State (limit) via CLAP `state`
or VST2 chunks; the plug-in writes no files. Changes in the menu are reported to the host (CLAP `state.mark_dirty`, VST2 `audioMasterUpdateDisplay`).
Link with `-lgdi32 -luser32 -lmsimg32`.

## Build (Windows)

As in the original README (vcpkg + CMake presets). The output file is called
`Nuked-SC55-Poly.clap`.

## CPU

Every instance runs continuously (the firmware timers have to keep running).
Rendering is parallelised per instance (worker pool, the audio thread helps).
Rule of thumb: N × the CPU load of the original plug-in, spread over several cores.

## Known deviations from the hardware

- Firmware voice stealing only kicks in when *all* instances are full, and
  then within the chosen instance – not globally by oldest voice.
- The internal clipping limit applies per instance, not to the sum (in float there
  is no summing clipping; mind the level in very dense arrangements).
- Effects: the summed reverb/chorus is mathematically almost equivalent; minimal
  differences due to the fixed-point rounding per instance are possible.
- Part reserve (GS "Voice Reserve") acts per instance.
