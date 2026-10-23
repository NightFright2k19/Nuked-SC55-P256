# Known limitations and issues

Limits of the approach and known issues per model. Part of the [documentation index](../README.md#documentation).

## Known limitations and open points

These are limits of the approach, or things that are not done yet.

- **Voice stealing differs from hardware:** the firmware only steals voices once **all** instances are full, and then inside the chosen instance, not globally by oldest voice.
- **Clipping:** the internal clipping limit applies per instance, not to the sum (in float there is no sum clipping). Watch the level of very dense arrangements.
- **Effects:** summed reverb and chorus are mathematically almost equivalent to one effect section, but fixed-point rounding per instance can cause minimal differences.
- **Partial reserve** (GS "Voice Reserve") works per instance.
- **Retrigger special case:** if the owning instance is full, a retrigger goes to another instance. In the extreme test (256 retriggers) more voices sound than with a static assignment; level deviation 1-4 %. Accepted.
- **VST2 has one MIDI input.** The SC-88, SC-88 Pro, SC-8820 (32 parts) and SC-8850 (64 parts) reach their other ports there only through the port select messages `F5 0n`; CLAP offers one note port per MIDI input. On the 32-part models, `F5 03` / `F5 04` (sent by players such as the Falcosoft MIDI Player for files with up to 64 channels) select IN A / IN B, so parts of port C and D are played by those of port A and B.
- **SC-88 Pro:** uses much more memory (166 MB) and larger files (about 27 MB) than the SC-55 variants. 88emu is an early access alpha and models the analog output stages for SC-55 mk1/mk2 too (`AnalogOutputMode`), which might become a sound option later.
- **64-bit only, Windows only for the editor and single-file builds.** There is no 32-bit build yet; 32-bit programs can use a MIDI driver that hosts VST plug-ins (see [Using the plug-ins](../README.md#using-the-plug-ins)).
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
