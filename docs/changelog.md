# Version history

What changed in each [Autobuild](https://github.com/NightFright2k19/Nuked-SC55-P256/releases), newest first. Part of the [documentation index](../README.md#documentation). Each entry has the title of its release; the numbers in brackets are issues and pull requests.

**Maintaining it:** every change that triggers an Autobuild adds an entry at the top of this file in the same PR, headed `## r<N>: <title>` with `N` the number of the next Autobuild (newest release number + 1). The release workflow takes the title of the top entry for the release name (`Autobuild r<N>: <title>`) if its number matches. Releases r23 to r44 were created afterwards from the history, and r1 to r22 do not exist (the project started on 2026-10-08 with plug-in templates, before the patcher).

## r65: Thread-safe asmjit initialization (2026-10-24)

- 88emu models: the one-time CPU detection of the JIT library (asmjit) is now thread-safe. The units boot in parallel in `Activate()`, and the first JIT runtime of the process could be created by several threads at once; the values were always identical, so the race was harmless, but ThreadSanitizer reported it for the SC-8850 and MT-32 [#88].

## r64: XG effects calibrated against the S-MU2000 (2026-10-24)

- XG mode of the SC-8820: every XG reverb type now gets the GS reverb type, time and level that matched an S-MU2000 recording best, the XG defaults (Hall 1, Chorus 1) are set after XG System On, and the unsupported variation send (CC 94) is dropped as the S-MU2000 plays no delay from it [#95].

## r63: XG mode for the SC-8820 (2026-10-23)

- New **XG** entry in SETUP > Send Reset of the SC-8820: files written for Yamaha XG play with fitting sounds. Bank Select and Program Change are translated (XG drum kits to SC-8820 drum sets, drum parts on other channels), XG System On and Master Volume are understood, XG reverb / chorus type and return and the part sends are translated to the GS effects, other XG SysEx is dropped. XG variation banks play as their basic voice [#95].
- Fixed a rare hang or very long render when events held back after a reset (such as the one after XG System On) were released late: their time offset could underflow. Applies to all models.

## r62: Clearer hints for unreadable or damaged ROM files (2026-10-23)

- The patcher's details log and `--build` name ROM files that cannot be read (for example cloud files that are not downloaded) and files with a ROM size but an unknown checksum (damaged file or unknown version).
- `--build` no longer aborts on consoles whose encoding lacks characters of the path [#88].

## r60: Version history and "What's new?" link (2026-10-23)

- New version history (this file), release titles say what changed, and the patcher has a **What's new?** link to it [#94].

## r59: Documentation reorganized (2026-10-23)

- The README is now compact; technical details moved to topic files under `docs/` [#96, #97].

## r58: Fix hanging notes with 64-channel MIDI files (2026-10-22)

- SC-88, SC-88 Pro and SC-8820: port-select events from files with more than two ports no longer leave notes hanging [#92, #93].

## r57: Update Gearmulator (88emu) to b8e1a28 (2026-10-21)

- Newer 88emu core base; audio of SC-88 Pro, SC-8820 and SC-8850 is bit-identical, SK-88Pro support comes along [#91].

## r56: Patcher updates itself (2026-10-21)

- The patcher finds newer Autobuilds and offers a one-click **Update** (can be turned off) [#89, #90].

## r55: Faster, glitch-free loading of 88emu models (2026-10-21)

- All units of the 88emu models boot in parallel after a re-activation (for example a sample-rate change), so no dropout when notes follow immediately.
- Tools translated to English; long-run test mode for the polyphony benchmark [#88].

## r54: Patcher is safer with damaged files (2026-10-20)

- Plug-in pairs are replaced atomically; damaged templates are detected [#88].

## r53: Steadier audio under load on Windows (2026-10-19)

- Worker threads run with the "Pro Audio" priority, so busy programs no longer cause dropouts [#88].

## r52: SC-8820 has two interface styles (2026-10-19)

- The Basic panel is removed; the former Pro panel is now the SC-8820 interface, next to the SC-D70 one [#87].

## r51: SC-D70 panel correction (2026-10-19)

- All knobs of the SC-D70 interface point to 12 o'clock by default.

## r50: SC-D70 interface for the SC-8820 (2026-10-19)

- A third panel after the Roland SC-D70, chosen under SETUP > Interface Style or by clicking the logo [#86].

## r49: New patcher icon, ROM list reordered (2026-10-19)

- Patcher icon in all sizes; the list of ROMs is sorted differently. README screenshots show the Emu logo [#84].

## r48: Emu branding, reworked MT-32 / CM-32L panels (2026-10-18)

- Emu logo on the 88emu models, MT-32 / CM-32L panels reworked [#79, #81].

## r47: Autobuild release labels (2026-10-18)

- Only the newest Autobuild is labelled Latest; older releases are kept [#82, #83].

## r46: Older Autobuilds are kept (2026-10-18)

- Releases are no longer deleted when a new one is built [#80].

## r45: MT-32 / CM-32L panels refined (2026-10-18)

- Dot-matrix LCD, bevelled keys, ribbed knob [#77].

## r44: MT-32 / CM-32L plug-in (2026-10-18)

- New plug-in for MT-32 (old and new board) and CM-32L on the 88emu LA board [#69, #76].

## r43: SC-8820 / SC-8850 need less RAM and load without a stall (2026-10-17)

- [#75]

## r42: SC-88 / SC-88 Pro use less memory (2026-10-17)

- The H8/500 bus only takes the memory the firmware uses, about 15 MB less per unit [#74].

## r41: SC-8820 logo (2026-10-17)

- Logo at the height of the other panels; a click switches the interface [#70, #72].

## r40: One SC-8820 plug-in with an Interface Style menu (2026-10-16)

- Basic and Pro are merged into one plug-in [#67, #68].

## r39: Fix silent buffer overflows and shared state (2026-10-16)

- Stability fixes between plug-in instances [#63, #65].

## r38: SC-8820 PREVIEW by right-click (2026-10-16)

- The panel label reads "PREVIEW (RMB)" [#66].

## r37: SC-8820 Pro level display moved (2026-10-16)

- The level display sits above POWER / USB [#62].

## r36: SC-8820 Pro panel (2026-10-16)

- Extended panel variant [#49, #61].

## r35: SC-88 tone map in the SETUP menu (2026-10-16)

- [#59, #60]

## r34: 88emu core updated to Gearmulator c8047e7 (2026-10-16)

- [#58]

## r33: Tone Map for SC-8820 / SC-8850, MT-32 in Send Reset (2026-10-16)

- [#56, #57]

## r32: MT-32 reset in the SETUP menu (2026-10-16)

- [#40, #47, #55]

## r31: Templates rebuilt with Prevw Note (2026-10-16)

- Setup menu entry "Prevw Note" for SC-8820 and SC-8850 [#51, #52, #54].

## r30: GAIN knob changes by 0.1 dB per mouse-wheel notch (2026-10-15)

- On all panels [#50].

## r29: SC-8820 PREVIEW on right-clicking the GAIN knob (2026-10-15)

- Dragging or turning the knob no longer plays the PREVIEW note [#48].

## r28: SC-8820 panel tweaks (2026-10-15)

- Level dots blend from green to red.

## r27: SC-8820 panel: GAIN knob, VOICES / UNITS moved (2026-10-15)

- VOLUME becomes GAIN with its value printed below.

## r26: Fix for the SC-8820 SETUP menu (2026-10-15)

- INST map for all parts, same setting as the INST MAP button.

## r25: SC-8820 support (2026-10-14)

- New plug-in on the 88emu core with the SC-8820 firmware: 256 voices, two MIDI inputs [#45].

## r24: Patcher in 29 languages (2026-10-13)

- Sorted alphabetically.

## r23: Patcher for Windows, Linux and macOS (2026-10-13)

- Releases contain a patcher instead of template archives [#44]. First release; includes everything since the start: SC-55 and SC-55mk2 with up to 11 units, SC-88, SC-88 Pro and SC-8850 on 88emu, original front panels, SETUP menu, Send Reset, panel key combinations, steady LCD scrolling and several fixes for notes hanging after an abrupt stop [#5 to #42].
