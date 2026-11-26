# Version history

What changed in each [Autobuild](https://github.com/NightFright2k19/Nuked-SC55-P256/releases), newest first. Part of the [documentation index](../README.md#documentation). Each entry has the title of its release; the numbers in brackets are issues and pull requests.

**Maintaining it:** every change that triggers an Autobuild adds an entry at the top of this file in the same PR, headed `## r<N>: <title>` with `N` the number of the next Autobuild (newest release number + 1). The release workflow takes the title of the top entry for the release name (`Autobuild r<N>: <title>`) if its number matches. Releases r23 to r44 were created afterwards from the history, and r1 to r22 do not exist (the project started on 2026-10-08 with plug-in templates, before the patcher).

## r84: MT-32 / CM-32L use much less CPU (Gearmulator update to 62f6a687) (2026-11-26)

- Newer 88emu core base (Gearmulator `62f6a687`, 3 commits after `00e1b17`). Two of them are performance work on the MT-32 / CM-32L: a faster LA32 sound generator and a faster MCS-96 CPU core and scheduler (the board itself also does less per sample). In a 12 s comparison render of seven 88emu devices (MT-32 old, MT-32 new, CM-32L, SC-88, SC-88 Pro, SC-8820, SC-8850) the output is bit-identical to the previous base, so the sound does not change. A render of the MT-32 / CM-32L alone takes about 2.7 times less time than before, and the Linux plug-in test (256 notes, 4 s of audio) needs 0.2 s of compute time instead of 0.5 s with the same RMS.
- The third commit only adds PRO-E panel switches, which no plug-in uses.
- The patch of the 88emu core was adapted to the new base (`hardwareDevice.cpp` / `hardwareDevice.h`: the SC-8820 panel buttons and the fast-boot length next to the widened PRO-E button path). The submodule pins are unchanged. The Windows and Linux templates of Emu-MT32 are rebuilt; the other four 88emu plug-ins render bit-identically and gain nothing there. The macOS templates (arm64 and Intel, all models) are rebuilt by the macOS workflow from the new base, which also brings the SC-8850 SETUP menu fix of r81 / r82 into the macOS SC-8850 templates.

## r83: SC-8850 SETUP menu marks the map of the shown part (2026-11-26)

- SC-8850: since r82 the entry "Tone map (all parts)" showed no mark at all as soon as the INST MAP button had set a part to another map. It now keeps a mark: while the parts differ, it is on the map of the part shown on the panel (Bank Select LSB 1-3 of the part's tone map, or 0 = the INST MAP map 1-4), and it changes with the part chosen with PART < >. While all parts follow the map chosen in the menu nothing changes, and picking a map in the menu still sends it to all parts [#133].
- Checked with the Linux SC-8850 test variant and the real ROMs: after a menu pick the mark is on that map; each INST MAP press moves it to the map the button selects (SC-8850, SC-88 Pro, SC-88, SC-55, SC-8850 ...). The Windows and Linux SC-8850 templates are rebuilt; the macOS ones need a Mac and keep the old behaviour until they are rebuilt.

## r82: SC-8850 SETUP menu follows the INST MAP button, templates rebuilt (2026-11-26)

- The fix of r81 (pull request #134, issue #133) was merged as source only, so no release carried it. The SC-8850 templates of Windows and Linux are rebuilt from the same sources (88emu core `00e1b17`), so the SETUP menu entry "Tone map (all parts)" now loses its mark when the panel's INST MAP button has set a single part to another map, and picking a map sends it to all parts again.
- Checked before the rebuild with the Linux test variant and the real ROMs (boot, menu pick, three INST MAP presses, same map picked again, another map picked: the state followed in every step). The other templates are unchanged. The macOS SC-8850 templates need a Mac to build and are not rebuilt yet, so the macOS plug-ins keep the old behaviour until they are.

## r81: SC-8850 SETUP menu follows the INST MAP button (2026-11-26)

- SC-8850: the INST MAP button on the panel sets the tone map of the part shown only, as on the device, but the SETUP menu entry "Tone map (all parts)" kept its mark on the map last chosen in the menu, so it no longer matched the panel [#133]. After a panel pass reads a part's tone map back and it differs from what the menu set for all parts, the menu now marks no map. Picking a map in the menu then sends it to all parts again (before, picking the already marked map did nothing). The baseline is read once, about 0.6 s after each menu pick, with 32 small data requests (2 bytes each) through unit 0.
- Checked the other models: SC-8820 already takes over the map that unit 0 shows (`E8820PanelInput`), the SC-88 Pro has no per-part INST MAP on its panel (ALL view only, the map keys are plug-in functions and set the state directly), SC-55, SC-55mk2 and MT-32 / CM-32L have no tone map menu. The SC-88 panel's INST MAP outside ALL mode is per part as well and is not covered by this change yet (see known issues).
- Only the Windows / Linux / macOS SC-8850 plug-in is affected; the other models render and behave as before.

## r80: MT-32 / CM-32L reverb and ring modulation fixes (Gearmulator update to 00e1b17) (2026-11-23)

- Newer 88emu core base (Gearmulator `00e1b17`, 38 commits plus merges after `7f9a980`). For the MT-32 / CM-32L it brings the reverb fixes made from the die shot (the tap delay summed one tap wrongly, and all modes rounded slightly differently; interpreter and both JIT paths) and a fix of the ring modulation of the LA32 sound generator. In a 12 s comparison render (all three LA devices, same MIDI sequence) the output differs from the previous state by -49 to -53 dB (RMS of the difference relative to the signal), the level by less than 0.01 dB, with no new clipping or silence.
- SC-88, SC-88 Pro, SC-8820 and SC-8850 render bit-identical to r79 in the same test; the SC-55 plug-ins do not use this code. The new base also contains the PRO-E board, the M6801 CPU core and a faster ROM scan, none of which are used by the plug-ins.
- The patch of the 88emu core was adapted to the new base (`hardwareDevice.cpp`: the SC-8820 panel buttons and lamp rows next to the new PRO-E branches). The submodule pins are unchanged. The Windows and Linux templates of the five 88emu plug-ins are rebuilt. The macOS templates need a Mac to build and are not rebuilt yet, so the macOS plug-ins keep the old behaviour until they are.

## r79: SC-55 ignores SC-8850 bitmap display data (2026-11-19)

- SC-55 v1.21 and SC-55mk2: MIDI files made for the SC-8850 that draw bitmaps on its display (such as the Bad Apple video file) sent thousands of `45 12 20 xx xx` messages, which these firmware versions do not know. After about a minute the LCD showed "Address Error!", and the notes came late (test note delay, median / 99th percentile: 98 / 337 ms, without the bitmap data 10 / 90 ms). The plug-in now drops those messages on the two models, so the delay is back to 10 / 90 ms and the error no longer appears. Text display messages are still passed on [#126].
- The SC-55 templates of Windows and Linux are rebuilt. The macOS SC-55 templates need a Mac to build and are not rebuilt yet, so the macOS plug-ins keep the old behaviour until they are.
- SC-88 and SC-88 Pro were checked with the same file: no "Address Error!", and the stream does not delay the notes (test note, median / 99th percentile: SC-88 16 / 110 ms, SC-88 Pro 8 / 121 ms, identical with and without the bitmap data). The other models are not changed.

## r78: SC-8820 takes MIDI in faster, steadier notes (2026-11-19)

- SC-8820: 88emu limited the MIDI input of the SC-8820 to 3125 bytes per second per port, the speed of a DIN cable, although the device is played over USB. Dense passages (chords, controller streams, SysEx) waited in that queue and the notes came late by an uneven amount, which sounded uneven in playback [#126]. The limit is now 31250 bytes per second (`88emu-nuked-poly.patch`, `Sc8820::reset()`). The data is not lost at any rate (the queue towards the firmware is not limited; the parameters of all 32 parts after XG and GS test files were the same at both rates). The display pacing of r77 now drains its byte count at 6250 bytes per second (`kWireRate8820`), the value with the steadiest notes in the test.
- Measured with the Bad Apple file (silent music, one audible test note every 0.5 s, 192 notes): delay of the test note, median / 99th percentile / maximum, r77: 22 / 156 / 214 ms, now: 8 / 42 / 86 ms. After Stop the sound still ends at once.
- The SC-8820 templates of Windows and Linux are rebuilt with it. The macOS SC-8820 templates need a Mac to build and are not rebuilt yet, so the macOS plug-ins keep the old behaviour until they are.
- The other models are not changed: the SC-88 and SC-88 Pro take MIDI in at the 31250 baud of the real devices inside their firmware model, the SC-8850 has no limit of this kind, and the SC-55 has none in the plug-in.

## r77: SC-8820 no longer lags behind a stream of display SysEx (2026-11-19)

- SC-8820: a MIDI file with a constant stream of SC-8850 bitmap messages (`45 20 0x 00`, for example the Bad Apple video file, about 83 messages / 9.8 kB per second) held the notes back: they came late by a growing amount, and after Stop the plug-in kept playing for as long as the file had run [#126]. The firmware of the SC-8820 works through about 3.1 kB of MIDI per second, but the backlog reported by 88emu stays at 0 to 4 bytes for it, so the display pacing of #42 never held a message back. The plug-in now counts the bytes it has sent to unit 0 and drains that count at 3125 bytes per second, and lets a display message through only while the count is below 512 bytes (newer data for the same address replaces older data in the queue, as before). Measured with that file: a note sent 3 s after the start of the stream was not heard at all within 17 s; now notes come within 0.2 s, and after Stop the sound ends as without the display messages. Only display SysEx of the SC-8820 is affected, the sound of files without them is unchanged.
- The SC-8820 templates of Windows and Linux are rebuilt with it. The macOS SC-8820 templates need a Mac to build and are not rebuilt yet, so the macOS plug-ins keep the old behaviour until they are.
- Code: `WireDecay()`, `wire_bytes` and `MidiBacklog()` in `nuked_sc55.cpp` / `nuked_sc55.h` (SC-8820 only).

## r76: XG drum notes 13, 14 and 29 for the SC-8820 (2026-11-09)

- XG mode of the SC-8820: on a drum part, XG notes 13 (Mute Surdo), 14 (Open Surdo) and 29 (Snare Roll) are played on the GS notes 86, 87 and 25, where the SC-8820 drum sets have these sounds. Measured against S-MU2000 recordings of 9 kits, the spectral distance of these notes fell from 10.7 / 11.8 / 12.0 dB to 8.6 / 5.7 / 5.4 dB; the other notes of 13-34 got further away and are not remapped [#95].
- The SC-8820 templates of Windows and Linux are rebuilt with it (checked through the real plug-in: XG notes 13 / 14 / 29 play bit-identical to the GS notes 86 / 87 / 25). The macOS SC-8820 templates need a Mac to build and are not rebuilt yet, so the macOS plug-ins play these three notes as before until they are.
- Code: `Translator::DrumNote()` (`xg_translate.h`), applied to Note On / Note Off / Poly Key Pressure in `nuked_sc55.cpp`. Test: `tools/test/xg_translate_test.cpp`.

## r75: XG reset for the SC-88 Pro (2026-11-04)

- New **XG** entry in SETUP > Send Reset of the SC-88 Pro (#120). It sends the Yamaha XG System On (`F0 43 10 4C 00 00 7E 00 F7`), like the original needs it, and the firmware switches to its unofficial XG ("MU80 compatible") fallback mode; the plug-in translates nothing. A file that starts with XG System On does the same. The reset also counts for late-waking units and the note router, like a GS Reset.
- Code: `HasXgReset()` (menu entry for the SC-8820 and the SC-88 Pro; `HasXg()` stays the SC-8820 translation), `StateLog::IsXgSystemOn`, `PolyRouter::SnoopSysEx`. Test `tools/test/xg_reset_88pro_test.cpp`. The SC-88 Pro template is rebuilt.

## r74: Editor for the macOS plug-ins (2026-11-02)

- The native **macOS CLAP** plug-ins now have the **same editor panel** as on Windows and Linux (#112): an NSView that the host embeds, 918 x 280 points, 30 frames per second, on Apple Silicon and Intel. The **SETUP menu** is a native macOS menu with the same entries.
- The text uses the embedded fonts of the Linux editor (Liberation Sans, DejaVu Sans Mono). Playback and the editor on a real Mac are not confirmed yet; the editor was tested on the GitHub macOS runners only (no ROMs there).
- Code: the Win32 shim of the Linux editor is split into `src/gui/win32_gdi.cpp` (drawing, fonts, menu model; shared) and the window parts `win32_x11.cpp` and the new `win32_cocoa.mm`; `win32_x11.h` is now `win32_shim.h`, `x11menu::SetHandler` is now `popupmenu::SetHandler`. The Linux templates are rebuilt from the split code (no change in behaviour). Test host `tools/test/clapgui_mac.mm`; the macOS workflow publishes editor screenshots to `preview/<branch>`.

## r73: Native macOS CLAP plug-ins (2026-11-02)

- The **macOS patchers** now create native **CLAP** plug-in bundles of all seven models instead of the Windows files (#112): Apple Silicon (arm64) and Intel (x86_64), each patcher for its own architecture, macOS 11 or newer. No VST2 and no editor window yet: hosts show their generic parameter view. Same CLAP IDs as on Windows and Linux.
- After inserting the ROMs the builder renews the ad-hoc code signature itself (`p256_builder.macho_resign`), so no Xcode or `codesign` is needed. The patcher window says "CLAP, macOS Apple Silicon" or "CLAP, macOS Intel" and "folder CLAP".
- Build: `NK_TARGET=macos`, `tools/build_templates_macos.sh`, run by the new workflow `macos-templates.yml` on the GitHub macOS runners; templates in `templates/macos-arm64` and `templates/macos-x64`. `clapcheck --load` checks loading without ROMs.

## r72: Editor for the Linux plug-ins (2026-11-02)

- The native **Linux CLAP** plug-ins now have the **same editor panel and SETUP menu** as on Windows (#112): an X11 window that the host embeds, 918 x 280 px, 30 frames per second. They need libX11 (part of every Linux desktop; under Wayland through XWayland).
- The text uses the embedded fonts Liberation Sans and DejaVu Sans Mono instead of Arial / Segoe UI and Consolas (licenses in `src/third_party/fonts/`). The SETUP menu is drawn by the plug-in and does not block the host.
- Code: `src/gui/win32_x11.cpp` runs the Win32 editor code on X11; CLAP `timer-support` / `posix-fd-support` drive it. Test host `tools/test/clapgui_x11.cpp`; `clapcheck` now expects the X11 editor. The Windows plug-ins are unchanged.

## r71: Native Linux CLAP plug-ins (2026-11-02)

- The **Linux patcher** now creates native **Linux x64 CLAP** plug-ins of all seven models instead of the Windows files (#112). No VST2 and no editor window yet: hosts show their generic parameter view. The plug-ins need glibc 2.35 or newer (Ubuntu 22.04, Debian 12 or newer). Same CLAP IDs as on Windows.
- The patcher window on Linux says "CLAP, Linux x64" and "folder CLAP"; the Windows patcher is unchanged. The macOS patcher still creates the Windows plug-ins.
- Builder: `p256_builder.py` builds the Linux plug-ins on Linux (`--target windows|linux`); templates in `templates/linux-x64`. Build: `NK_TARGET=linux` in the template build functions, `tools/build_templates_linux.sh`, check host `tools/test/clapcheck.cpp`.

## r70: Document that Linux and macOS plug-ins are not native yet (2026-11-02)

- Documentation only: the README, the patcher page and the Autobuild release notes now state that the Linux and macOS patchers run natively but still generate Windows x64 plug-ins; native plug-ins are tracked in issue #112.

## r69: Update Gearmulator (88emu) to 7f9a980 (2026-10-30)

- Newer 88emu core base with 22 more accuracy fixes of the XP tone generator (amp, pitch and filter ramps, TVF-F coefficient, TVA gain, wave fetch across the loop wrap, preload and FIFO refill) for the SC-88, SC-88 Pro, SC-8820 and SC-8850, and a base that already contains the SC-880 board changes (not offered as a model). In the comparison renders of E1M1 (38 s) against the previous state the level changes by at most 0.3 dB (SC-88; SC-88 Pro 0.1 dB, SC-8820 and SC-8850 0.0 dB) and the octave bands by at most 0.4 dB; no new clipping or silence. The patch of the 88emu core was adapted to the new `sc88pro` board (wave ROM sharing, voice counter). The MT-32 / CM-32L and the SC-55 plug-ins sound the same. The templates of the 88emu plug-ins are rebuilt.

## r68: Update Gearmulator (88emu) to 967ff0c (2026-10-28)

- Newer 88emu core base with more accurate sound on the SC-88, SC-88 Pro, SC-8820 and SC-8850: envelope steps, volume smoothing, effect sends and samples that loop past their end on the XP chip, delay memory precision and the effect multiplier and input on the LSP effect chip, DRAM page mode and refresh in the CPU timing of the SC-8820 and SC-8850, and the port 1 input pins of the H8/532. The sound of these models changes slightly (level within 0.3 dB, octave bands within 0.4 dB in the comparison renders); the SC-8820 needs about 12 % more CPU time, the other models are unchanged within measurement noise. The MT-32 / CM-32L and the SC-55 plug-ins sound the same. The templates of the 88emu plug-ins are rebuilt.

## r67: Update Gearmulator (88emu) to ef93259, patcher update with incomplete certificates (2026-10-27)

- Newer 88emu core base, which brings SC-880 support to the 88emu library; audio of the SC-88 Pro is bit-identical, CPU time in the polyphony test is unchanged within measurement noise. The SC-880 itself is not offered as a model yet. Patches adapted to the new `sc88pro` board (variant-dependent SRAM size).
- Patcher: the update check and the **Update** button failed with "certificate verify failed: unable to get issuer certificate" when the system certificate store lacked a root certificate of GitHub. The patcher now carries the Mozilla root certificates (certifi) in addition to the system store. The workflow also runs on changes below `src/`, so that a source-only change such as the 88emu update produces an Autobuild.

## r66: XG variation voices and insertion effects for the SC-8820 (2026-10-24)

- XG mode of the SC-8820: XG variation voices (bank LSB) and SFX voices (bank MSB 64) now play the SC-8820 variation that was measured closest to an S-MU2000 recording of the voice, where one is clearly closer than the basic voice or that made S-MU2000 recordings of songs come closer (1152 + 125 voices) [#95].
- XG mode of the SC-8820: the XG variation effect as insertion effect on a part now uses the GS insertion effect of that part. Distortion, Overdrive, Amp Simulator, Auto Wah, the delays and the 3-Band EQ get the GS type and settings that matched S-MU2000 recordings best; the default effect messages after XG System On are no longer lost when the first message is a controller that passes unchanged [#95].

## r65: Thread-safe asmjit initialization in the 88emu models (2026-10-24)

- SC-88, SC-88 Pro, SC-8820, SC-8850 and MT-32 / CM-32L: the one-time CPU detection of the JIT library (asmjit) is thread-safe now. The units boot in parallel in `Activate()`, and the first JIT runtime of the process could be created by several threads at once; the values were always identical, so the race was harmless, but ThreadSanitizer reported it for the SC-8850 and MT-32 [#88][#104]. Sanitizer runs (TSan, ASan + UBSan) of all five models are clean now, and the editors of all seven plug-ins keep their GDI and USER handles constant over 500 open / close rounds on Windows. The templates of the two Nuked SC-55 plug-ins differ from r64 only in the build date.

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
