# Polyphony

How the multi-instance polyphony works, display SysEx pacing and Capital Tone Fallback. Part of the [documentation index](../README.md#documentation).

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
