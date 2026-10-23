# Panels, controls and the SETUP menu

Panels of the SC-55 models, LCD details and the complete SETUP menu reference. Part of the [documentation index](../README.md#documentation).

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
| Send Reset | submenu MT-32 / GM / GS / GM2 (SC-8850), MT-32 / GM / GS / XG / GM2 (SC-8820), MT-32 / GM / GS (others): clicking an entry sends that reset to all units (nothing is marked or stored). XG sends a GS Reset and turns on the [XG mode](models.md#xg-mode-sc-8820) of the SC-8820 |
| System Mode (SC-88, SC-88 Pro) | submenu Mode 1 (Single Module) / Mode 2 (Double Module): sends System Mode Set (`F0 41 10 42 12 00 00 7F 0n ..`) to all units and marks it (default Mode 1 = factory setting, stored in the state) |
| All notes off | stops all sounding notes |
| Tone map (SC-88) | SC-55 / SC-88 (default SC-88) for all parts: ALL + INST MAP on every unit; stored in the state (`map=0..1`), kept over GS resets |
| Tone map (SC-88 Pro) | SC-55 / SC-88 / SC-88 Pro |
| Tone map (SC-8850) | SC-55 / SC-88 / SC-88 Pro / SC-8850 (default SC-8850) for all parts: sends Bank Select LSB (CC#32) and the part's bank and program again to every part; stored in the state (`map=0..3`), a GS / GM reset returns to the SC-8850 map. The panel's INST MAP still sets it per part |
| Prevw Note (SC-88 Pro, SC-8820, SC-8850) | submenu per octave |
| Interface Style (SC-8820) | SC-8820 (default) / SC-D70: the [SC-8820 panel](models.md#sc-8820-part-display) or the [SC-D70 interface](models.md#sc-d70-interface); last entry of the menu, stored in the state (`style=1..2`, a former `style=0` opens as SC-8820); a left click on the logo switches too |

After a reset from the menu, the plug-in holds back the host's next MIDI events for 100 ms (System Mode: 200 ms) and then plays them in their original order, so that the firmware has finished the reset (measured with `tools/test/reset26.cpp`: notes sound exactly as after boot from 50-100 ms after GM / GS / GM2 and from 150 ms after a System Mode Set on the SC-88 Pro). Resets that come from the host or a MIDI file are passed on unchanged, as before.

<a id="mt-32-reset"></a>**MT-32 reset:** a GS Reset followed by Falcosoft's "GS MT-32 Mode" SysEx: the MT-32 tones of bank 127 on parts 1-9, bank 126 on parts 11-16, the MT-32 / CM-64 drum set 128 on part 10 and the pitch bend range of the MT-32. On the 88emu models every part also gets Bank LSB 1 (SC-55 map), because the SC-88 map has no bank 127 / 126 and the LCD then shows the MT-32 names. Like GM / GS it is a one-time command and goes through the state log, so units that wake later play the same tones. Limitation: MT-32 games that send their own MT-32 SysEx (LA timbres, patch memory) are not understood by a Sound Canvas; only titles that use the preset sounds play correctly.

**System Mode (SC-88, SC-88 Pro, see the SC-88 Pro manual p.116 and p.194):** Mode 1 (Single Module, factory setting) is one 32-part module with one set of system effects (reverb, chorus, delay, EQ). Mode 2 (Double Module) works as two 16-part modules on MIDI IN A and IN B, each with its own reverb, chorus, master level / pan / key shift / tune; delay and EQ are not available and the insertion effect works only on part group A. A System Mode Set initialises everything except the tone maps (the firmware is busy for about 150 ms). In Mode 2, GM On and GS Reset initialise only the part group of the MIDI IN they arrive on (checked in the emulation with `tools/test/mode2.cpp`); the plug-in follows this in its state log and note router, so units that wake up later get the same state. GM / GS from the Send Reset menu are therefore sent on IN A and IN B in Mode 2, so the whole module is reset. A System Mode Set from the host also moves the check mark in the menu.

The menu language (German or English) follows `GetUserDefaultUILanguage()`; every other Windows language gives English.
