# Development and test tools

Test tools and how to follow upstream changes. Part of the [documentation index](../README.md#documentation).

## Development and test tools

The development workbench contains the source, the complete patch series (`patches/`, 31 `git format-patch` files on `de2a799`), and all tools that were used for the measurements. The most important ones:

| Tool | Purpose |
| --- | --- |
| `tools/exact/ref_orig`, `ref_new`, `verify.sh` | one emulator without plug-in: renders an event list and prints an FNV hash and CPU time; same hash = bit-exact |
| `tools/test/play`, `e1`, `host`, `diag`, `peaks` | CLAP-level tests: polyphony test per round, event lists with CPU and voices per 10 s, scenarios (single note, 160-note flood, CTF, 256 retriggers, runtime limit), wake/sleep log with replay bytes and block time |
| `tools/test/diag88`, `play88`, `peaks88`, `map88`, `tools/sc88probe/` | the same against the SC-88 Pro engine, including the map transitions |
| `tools/test/reactstall88` | audio-thread stall after a re-activation (88emu models): plays silence for a given delay after each `activate()`, then a dense chord of 240 notes, and reports the activation time and the longest block; `REACTSTALL_NPROCS` pretends to have more CPUs |
| `tools/winbuild/vsthost.exe`, `hostw.exe`, `clapgui.exe`, `guihost.exe`, `lcdhost.exe`, `guiload.exe` | Windows test hosts (run under Wine): VST2 / CLAP host, editor test with screenshots and menu, LCD screenshots at song times, CPU of the interface thread |
| `tools/winbuild/polybench.exe` | Windows host for the worker pool: loads a finished plug-in (`.clap`), sets N awake units via `NUKED_SC55_POLY_VOICES` / `NUKED_SC55_POLY_DYNAMIC=0` and measures the wall time of the audio blocks against the CPU time of the process (speed-up, efficiency, loss against the ideal, blocks over budget); `--load K` adds K busy threads, `--events` plays an event list. `--soak` is a real-time long-run mode: it plays one or several plug-ins at the block rate of a host for hours and logs block times, private memory, handles, threads and GDI/USER objects once a minute, marks minutes with foreign CPU load and ends with trends per hour. Build line in the head of `polybench.cpp`. |
| `tools/winbuild/panelhost.exe`, `mt32host.exe` | Windows test hosts (run under Wine): mouse actions on the panel with screenshots at song times (`NK_CHUNK` loads a state first, for example the MT-32 device); MT-32 / CM-32L: RMS per second, device switch during playback |
| `tools/test/lcdtest`, `lcddump` | LCD renderer (incremental vs. full, must give 0 differences), character memory dump |
| `tools/test/mem`, `act`, `idle` | memory, load time and idle load of a plug-in |
| `tools/make_golden.sh`, `tests/golden.json` | reference hashes and CPU times |
| `tools/upstream_check.sh` | `report`: new relevant commits in Nuked-SC55-CLAP and Gearmulator since the pinned state (`upstream.lock`); `trial`: build the new state with our patches and compare with the golden values |
| `tools/midi/gen.py`, `smf2ev.py` | polyphony test MIDI file, SMF to event list |

### Keeping up with upstream

`upstream.lock` pins the observed states: Nuked-SC55-CLAP `de2a799` (paths `src/`, CMake, vcpkg) and Gearmulator `7f9a9806b7b3` (88lib, XP chip, panel graphics, CPU cores including dsp56300, synthLib / hardwareLib / baseLib). Our own changes to the Nuked core are also kept as a single patch, `third_party/nuked-core/nuked-core-timers.patch` (timers, memory, state copy), so that it can be checked on its own against a new upstream. A new state is taken over by changing the pin, adapting the patches if needed, rebuilding and renewing the golden values. A GitHub Actions template for a weekly report as an issue is in `ci/`.
