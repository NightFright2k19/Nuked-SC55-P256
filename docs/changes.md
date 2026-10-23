# Changes compared to Nuked-SC55-CLAP

What this fork changes compared to the original Nuked-SC55-CLAP plug-in. Part of the [documentation index](../README.md#documentation).

## Changes compared to Nuked-SC55-CLAP

This project started as a fork of John Novak's [Nuked-SC55-CLAP](https://github.com/johnnovak/Nuked-SC55-CLAP) (base: commit `de2a799`, followed by 31 commits of its own). The original is a CLAP plug-in without a user interface that loads one emulated module from ROM files next to the plug-in.

### Polyphony

- **Multi-instance polyphony:** N identical, bit-exact emulators run in parallel and their outputs are summed. A note router in `src/poly_router.h` decides which instance plays which note. Result: **64 partials** (MT-32 / CM-32L, 2 units of 32), **264 voices** (SC-55 v1.21, 11 units), **280 voices** (SC-55mk2, 10 units), **256 voices** (SC-88, SC-88 Pro and SC-8820, 4 units of 64; SC-8850, 2 units of 128).
- **Dynamic instances:** only as many instances run as the music needs. Sleeping instances are brought up to date from a compact state log when they wake (`src/state_log.h`), and a background thread pre-synchronises the next sleeping units so that waking up never causes a dropout.
- **Constant DC offset compensation:** every instance outputs a constant 1/32 of full scale; it is measured after boot and subtracted from all additional instances. With 24/28 voices or fewer, the output is **bit-identical** with a single module.
- **Runtime voice limit** (SETUP menu): 24 / 48 / 64 / 96 / 128 / 160 / 192 / 224 / 256 (SC-55mk2: 28 / 56 / ...). Instances above the limit receive no new notes and go to sleep once silent.

### Models

- **MT-32 / CM-32L:** one plug-in for the MT-32 (old board, 1.x firmware), the MT-32 (new board, 2.x) and the CM-32L, chosen in the SETUP menu ("Device"). The 88emu LA board with the original firmware, the device's own panel (MT-32: LCD, PART / RHYTHM and function switches; CM-32L: MIDI MESSAGE / POWER lamps), GAIN, up to 2 units = 64 partials. See [MT-32 / CM-32L engine](models.md#mt-32--cm-32l-engine-88emu).
- **SC-55 v1.21:** the Nuked SC-55 core with the v1.21 firmware as a single-file plug-in (ROMs built in or inserted by the builder; the original plug-in loads them from a folder next to it), 11 units = 264 voices, and the original front panel driven by the firmware, with GAIN. See [SC-55 / SC-55mkII panel](panels.md#sc-55--sc-55mkii-panel-918-x-280-px).
- **SC-55mk2 with CTF:** the plug-in prefers a `rom2.bin` patched with Capital Tone Fallback (the original plug-in always loads the unpatched one). In the single-file builds, the CTF `rom2.bin` is built in.
- **SC-88:** the 88emu engine with the firmware of the first SC-88, 32 parts on two MIDI inputs like the SC-88 Pro, the original front panel driven by the firmware, and GAIN. See [SC-88 engine](models.md#sc-88-engine-88emu).
- **SC-88 Pro:** a second engine based on the 88emu core from the Gearmulator project, with the SC-55 / SC-88 / SC-88 Pro tone map switch of the hardware, gain, mute and preview, and both MIDI inputs (32 parts A01-A16 / B01-B16: port select `F5 01` / `F5 02`, or the second CLAP note port "MIDI IN B"). The SC-55 code is untouched by this (audio compared byte by byte).
- **SC-8820:** the same 88emu engine with the SC-8820 firmware, 32 parts on two MIDI inputs (A, B), a front panel drawn after the device (lamps from the firmware, INST MAP, GAIN knob, PREVIEW on a right click). See [SC-8820 engine](models.md#sc-8820-engine-88emu).
- **SC-8820 part display:** the SC-8820 panel adds what the device lacks: a 16-part level display and, in the lower strip, a part display (program, instrument, bank MSB / LSB, level, pan, reverb, chorus) with edit buttons. A second panel after the SC-D70 can be chosen in the SETUP menu ("Interface Style": SC-8820 / SC-D70). See [SC-8820 part display](models.md#sc-8820-part-display).
- **SC-D70 interface:** a third panel for the SC-8820 plug-in, after the Roland SC-D70, which has the same sound engine: part, instrument and variation on a 7-segment display with PART / INST / DEC / INC, GS / GM and MIDI lamps, output level meters; the audio interface parts of the device are drawn without a function. See [SC-D70 interface](models.md#sc-d70-interface).
- **SC-8850:** the same 88emu engine with the SC-8850 firmware, 64 parts on four MIDI inputs (A-D), the original front panel with graphic LCD driven by the firmware, and GAIN. See [SC-8850 engine](models.md#sc-8850-engine-88emu).

### Formats and packaging

- **VST2:** a bridge exposes the CLAP plug-in of the same file as a VST2 plug-in. The VST2 binary interface is described independently in `src/vst2/vst2_abi.h`; **no Steinberg SDK** is used. One file serves as `.clap` and as `.dll`.
- **Single-file builds with embedded ROMs** (private use only) and **ROM-less templates** plus a Python builder that inserts your ROMs, so no compiler is needed.
- **Settings only in the plug-in state:** no `.ini`, no registry, no file writes. The state is saved by the host (CLAP state / VST2 chunk), changes are reported to the host.

### Core and speed

- **Bit-exact lazy timer evaluation** in `mcu_timer.cpp`: event-free timer ticks only increment a counter. About **35-40 % less CPU** per instance (with `-O3 -flto`), output identical to the original core.
- **Bit-exact timer and sub-MCU shortcuts** (Issue #28): the 8-bit timer skips whole counting cycles while all its interrupts are off (the SC-55mk2 runs it as a 6-cycle loop), and the SC-55mk2 sub-MCU takes its timer prescaler and interrupt checks in bulk. SC-55mk2 about 12 % and SC-55 about 9 % less CPU under full load, output still identical to the original core.
- **Packing and wake logic:** notes fill the lowest awake instance first; a single awake instance renders directly in the audio thread without any threading overhead.
- **Memory:** wave ROMs are shared between SC-55 units (one block, untouched pages cost nothing); the unused 4 MB LCD pixel buffer per unit is gone; 88emu units share one decoded wave ROM; the SC-88 and SC-88 Pro units no longer reserve the whole 1 MB / 16 MB address space of their H8/500 CPU, only what the firmware uses (about 15 MB less per unit, measured on Linux); the SC-8820 and SC-8850 read their wave ROM directly from the embedded image (no per-unit copy).
- **Faster loading:** SC-55 units are cloned from unit 0 instead of each booting itself (SC-55mk2 load time 6.4 s to 0.6 s under Wine); the units of the 88emu models boot in parallel inside the activation when every unit gets a core of its own (about as long as one unit alone, and nothing is left to wait for); on machines with fewer cores they boot in the background after unit 0, and a unit that is needed before its boot has finished makes the audio thread wait (a dropout of up to the boot time right after a re-activation).

### User interface (Windows)

- **Editor panel** for every model, derived from the 88emu panel art: the device's own LCD and switches, GAIN knob, voice meter, SETUP menu.
- **Original LCD** rendered from the emulated LCD controller: SysEx text and bitmap messages (`45 10 00 xx`, `45 10 01 xx`) are shown exactly as the firmware displays them. On the SC-55 and SC-55mk2 a display row changes only once a firmware display refresh (every ~70 ms) matches one of the two before it, so scrolling text runs steadily instead of flickering with fragments of the instrument name (sound unchanged).
- **Menu language** follows the Windows UI language (German or English); the printed panel labels stay English like on the hardware.

### Identification

- Internal name **"Nuked SC-55 P256"** for the SC-55 models (Nuked core) and **"Emu SC-88 P256"**, **"Emu SC-8820 P256"**, **"Emu SC-8850 P256"**, **"Emu MT-32 P256"** for the models on the 88emu core (CLAP plug-in names, VST2 vendor, Windows version info). Plug-in IDs stay on `net.nuked_sc55_poly_clap.*` so existing DAW projects keep working.
- **Version number = build date** (`YYYY.MM.DD`) in the Windows version info.
