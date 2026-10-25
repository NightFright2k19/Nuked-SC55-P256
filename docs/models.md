# Models and engines

Details of the MT-32 / CM-32L, SC-88, SC-88 Pro, SC-8820 and SC-8850 engines and their panels. Part of the [documentation index](../README.md#documentation).

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

---

## SC-88 Pro engine (88emu)

The SC-88 Pro variant does not use the Nuked core. It uses **88emu**, a low-level emulation of the SC-88 / SC-88VL / SC-88Pro / SC-8820 / SC-8850 with the original firmware, written by The Usual Suspects and part of [Gearmulator](https://github.com/dsp56300/gearmulator) (marked "early access alpha" there, GPLv3).

- **Pinned version:** Gearmulator `b8e1a28d4efaa3764d875eb3118d881dba672a70`, with the submodules dsp56300 `bf222a3`, asmjit `3577608`, mc68k `cc3693a`, freetype `8289165`, lunasvg `f8aabfb` (+ plutovg `5e4712c`) and RmlUi `0c8e94e`. `tools/build_88emu.sh <target> <source>` fetches and builds exactly this state (about 8-10 min on one core).
- **Patches** (`third_party/88emu/`): `88emu-nuked-poly.patch` adds `emu88_get_active_voice_count` (voice counter), `emu88_set_sc88pro_rom_images` (a normalised ROM set from memory, bypassing the file scan), `emu88_dump_sc88pro_rom_images` (build tool), `emu88_get_display_memory` (LCD) and `emu88_get_midi_in_backlog` (MIDI not yet taken in by the firmware). `88emu-nuked-poly.patch` also makes the H8/500 bus (SC-88, SC-88 Pro) take its address space as zero pages from the OS, filled only where ROM or RAM is mapped; unmapped space reads as H'FF through the slow path, so the output is unchanged. `dsp56300-mingw.patch` restricts SEH (`__try`) to MSVC. `asmjit-thread-safe-init.patch` makes the one-time CPU and virtual-memory detection of asmjit (`CpuInfo::host()`, `VirtMem::info()`) thread-safe: the units boot in parallel in `Activate()`, and the first JIT runtime of the process was created by several threads at once (a harmless data race that ThreadSanitizer reported for the SC-8850 and MT-32).
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

---

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
- **SETUP:** max polyphony, tone map for all parts (SC-55 / SC-88 / SC-88 Pro / SC-8820, default SC-8820; the same setting as the INST MAP button), Send Reset (MT-32 / GM / GS / XG / GM2, see [MT-32 reset](panels.md#mt-32-reset) and [XG mode](#xg-mode-sc-8820)), all notes off, and below them Interface Style (SC-8820 / SC-D70, default SC-8820, stored in the state).

### XG mode (SC-8820)

The SC-8820 understands GS and GM2, not Yamaha XG (issue #95). In XG mode the plug-in translates the XG messages of the host before they reach the units, so XG files play with fitting sounds. It is **not** full XG compatibility: only what the SC-8820 has an equivalent for is translated, and XG voices play the closest SC-8820 sound, not the XG sound.

- **On and off:** SETUP > Send Reset > **XG** (between GS and GM2) sends a GS Reset and turns XG mode on. A file turns it on with an XG System On (`F0 43 1n 4C 00 00 7E 00 F7`); the plug-in then sends a GS Reset instead and holds back the next MIDI for 100 ms. Any GM On, GM Off, GM2 On or GS Reset (menu or file) turns it off. The mode is not stored in the plug-in state (like the other resets); a file that starts with XG System On turns it on again.
- **Why a GS Reset and not GM2:** GM2 System On puts the SC-8820 in its GM2 mode with the GM2 sound map, where its own sounds cannot be selected. The XG translation needs the GS mode.
- **Bank Select and Program Change:** as in XG, Bank Select is held back and applies at the next Program Change. XG bank MSB 0 = melody voices, 64 = SFX voices, 126 = SFX kit, 127 = drum kit. The unit gets the GS form instead: CC 0 = variation, CC 32 = 4 (SC-8820 map), then the program. CC 32 is never passed on unchanged, because on the SC-8820 it chooses the sound map, while in XG it is the variation. The map therefore is the SC-8820 map in XG mode, whatever INST MAP says.
- **Melody and SFX voices:** chosen by measurement (`src/src/xg_voice_table.h`). Every XG voice of the variation banks (MSB 0, LSB 1-112) and of the SFX bank (MSB 64) was recorded on the S-MU2000 (C4, one second each; the sweep MIDIs are attached to #95) and compared with SC-8820 renders of all its variations: melody voices with the variations of the same program, SFX voices with every SC-8820 voice. A voice gets the SC-8820 variation that is clearly closer than the basic voice (1146 melody voices, 125 SFX voices; the variations that do not exist on the SC-8820 are skipped). Eight more melody voices just below that margin were added because the S-MU2000 recordings of the songs that use them came closer with them (1152 in all after removing and adding voices by song recordings). All other voices, including the XG voices that sound like their basic voice (about a third), play as the basic voice (variation 0). The SC-8820 has none of the XG samples, so a chosen variation is the closest SC-8820 sound, not the same sound.
- **Drum kits:** the XG kit goes to the nearest SC-8820 drum set (Standard 1/2, Room, Rock to Power, Electro to Electronic, Analog to TR-808, Dance, Hip Hop, Jungle, Apogee / Perigee to Techno, Jazz, Brush, Symphony to Orchestra, SFX kits to SFX, other kits to Standard 1). A part other than channel 10 becomes a drum part (`40 1x 15`) when it gets a drum bank and a normal part again with a melody bank. Drum Part Protect (on by default, as in XG) makes channel 10 ignore melody banks. The note layout of the kits differs between XG and the SC-8820: the kit is chosen, the notes are not remapped.
- **SysEx:** XG System On, Master Volume (to GS `40 00 04`), Part Mode (drum or normal part) and Drum Part Protect are translated. **Effects**, calibrated with S-MU2000 recordings of every reverb and chorus type (a snare hit and a held lead, `testmidi/xg_effect_cal`):
  - **Reverb:** each XG type (Hall 1/2, Room 1-3, Stage 1/2, Plate, White Room, Tunnel, Canyon, Basement) gets the GS reverb macro (`40 01 30`) plus reverb time (`40 01 34`) and level (`40 01 33`) whose decay curve is closest to the recording (error under 1.5 dB; Tunnel 2 dB; Canyon is longer than the GS reverb can reach, 15 dB). The XG reverb return scales the level (64 = as calibrated).
  - **Chorus:** Chorus 1-4 to the GS chorus macros 1-4 (`40 01 38`), Celeste and Symphonic to Feedback Chorus, Flanger and Phaser to Flanger, Ensemble Detune to Chorus 4; the chorus return goes to `40 01 3A`. Level and modulation depth match the recordings within about 1 dB.
  - **Defaults:** after XG System On (or the menu entry) the first channel message also sends the XG defaults, Hall 1 reverb and Chorus 1, because the GS Reset leaves the GS defaults (Hall 2, Chorus 3).
  - **Part sends:** `08 nn 13` / `08 nn 12` become CC 91 / 93.
  - **Insertion effect:** the XG variation effect as insertion effect on one part (connection insertion, `02 01 5A` = 0, part set with `02 01 5B`, parts 1-16 of port A) becomes the GS insertion effect (EFX) of that part: type (`40 03 00`), its settings, the EFX sends to reverb / chorus (`40 03 17` / `18`, following the part's CC 91 / 93) and the part switch (`40 4x 22`). Type and settings were fitted to S-MU2000 recordings of each type on four voices (`testmidi/xg_insertion_cal`): Distortion, Overdrive and Amp Simulator become GS Overdrive (drive 80, 48 for Overdrive, output level 28), Auto Wah becomes GS Auto Wah (manual 30, peak 30, depth 110), Delay L,C,R / L,R and Cross Delay become Stereo Delay (balance 32), 3-Band EQ becomes Stereo-EQ with its low gain, mid frequency / gain / width, high gain and low frequency passed on. Phaser, Echo and the other types play without an insertion effect: no GS type came closer to the recording than none. Other effect parameters are not translated.
  - **Variation send (CC 94) is dropped:** the variation effect as system effect is not translated. The S-MU2000 plays no delay from CC 94 (measured with the variation effect set to Delay L,C,R / L,R / Echo / Cross Delay as system effect, and with the XG defaults), the GS delay would.
  All other XG SysEx (part parameters, displays) is dropped. SysEx that is not XG passes unchanged.
- **Test:** `tools/test/xg_translate_test.cpp` (translator alone, no ROMs) and renders of an XG file against the same file written in GS form through the Linux plug-in: bit-identical output (see [verification](verification.md#xg-mode-sc-8820)).

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

---

## SC-8850 engine (88emu)

The SC-8850 plug-in uses the same 88emu core with the SC-8850 firmware (compile-time `NUKED_SC55_DEVICE_8850` on top of `NUKED_SC55_ENGINE_88PRO`, model index 7).

- **Units:** 128 voices per unit (two XP chips), 2 units = 256 voices. The device runs at 32 kHz like the SC-88 Pro. A unit boots in about 2.7 s; with embedded ROMs the second unit boots in parallel with the first when there are enough cores, otherwise in the background.
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
- **Display SysEx** goes to unit 0 only and is paced, as on all models (see [Display SysEx](polyphony.md#display-sysex)).
- **Controls:** every switch is held while the mouse button is down (at least 80 ms). A right click holds a switch until the next left-click switch is let go, for the key combinations of the manual (PART < + >, EDIT + ..., SHIFT + ...). VALUE: drag (right / up = clockwise, about 10 px per detent) or mouse wheel; a click without moving pushes the encoder. PREVIEW plays the note chosen in the SETUP menu ("Prevw Note", default C4) on the part shown by the panel, as on the SC-88 Pro (Issue #51). GAIN: drag, wheel (0.1 dB per notch), double-click = 0 dB.
- **VOICES / UNITS:** sounding voices, peak hold and awake units like on the SC-88 Pro panel. **SETUP** (bottom right, same place as on the SC-55 / SC-55mkII): max polyphony 128 / 256, Send Reset (MT-32 / GM / GS / GM2), all notes off.
