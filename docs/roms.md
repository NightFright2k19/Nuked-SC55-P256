# Supported models and ROM sets

Which ROM files each model needs and how they are recognised. Part of the [documentation index](../README.md#documentation).

## Supported models and ROM sets

| Model | Single-file build | Voices per unit | Units | Total |
| --- | --- | --- | --- | --- |
| MT-32 (old / new), CM-32L | yes, one plug-in | 32 | 2 | 64 |
| SC-55 v1.00 / v1.10 / v1.20 / v2.00 | source builds with external ROMs only | 24 | configurable | - |
| SC-55 v1.21 | yes | 24 | 11 | 264 |
| SC-55mk2 v1.01 (CTF) | yes | 28 | 10 | 280 |
| SC-88 | yes | 64 | 4 | 256 |
| SC-88 Pro | yes | 64 | 4 | 256 |
| SC-8820 | yes | 64 | 4 | 256 |
| SC-8850 | yes | 128 | 2 | 256 |

The ROMs are recognised **by content** (SHA-256), not by file name. Dumps of other versions are ignored. The hashes below are the first 16 digits; the full values are in `p256_builder.py`.

| Model | Role | Typical file name | SHA-256 (start) |
| --- | --- | --- | --- |
| MT-32 (old) | control 1.07 (IC27 + IC26, or both as one 64 KB file) | `mt32_1.0.7.ic27.bin` + `mt32_1.0.7.ic26.bin` | `967d2e3c782d8701` + `f2889cdd5e8dce29` (combined `a73a06c23ed38370`) |
| | control 1.06 / 1.05 / 1.04 / Blue Ridge / M-9 (used when 1.07 is missing) | `mt32_1.0.6.ic27.bin` ... | see `p256_builder.py` |
| | PCM | `r15449121.ic37.bin` (or `r15179844` + `r15179845`) | `d9164063f293410c` (`990d5bb6b6dd1ba7` + `95f312aee6f359b8`) |
| | reverb | `r15179857.ic13.bin` | `8ec50a524dde9c39` |
| MT-32 (new) | control 2.04 (2.07 when 2.04 is missing) | `mt32_2.0.4.ic28` | `a5339b966142dd2c` (2.07: `a1ee6ac3190bb94d`) |
| | PCM | as MT-32 (old) | |
| | reverb | `r15179917.ic19.bin` | `a4e146bfad60bdb4` |
| CM-32L | control 1.02 (LAPC-I 1.00 when 1.02 is missing) | `cm32l_control.rom` | `08d68695e5bff6bb` (1.00: `c8aea5fb1b281835`) |
| | PCM (1 MB, or `r15449121` + `r15179945`) | `CM32L_PCM.ROM` | `0dc50190bb7ea3fb` (`d9164063f293410c` + `5ed28de779c05b35`) |
| | reverb | as MT-32 (new) | |
| SC-55 v1.21 | rom1 | `sc55_rom1.bin` | `7e1bacd1d7c62ed6` |
| | rom2 | `sc55_rom2.bin` | `effc6132d68f7e30` |
| | wave ROM 1 / 2 / 3 | `sc55_waverom1/2/3.bin` | `5655509a531804f9` / `c655b159792d999b` / `334b2d16be3c2362` |
| SC-55mk2 v1.01 | rom1 | `rom1.bin` | `8a1eb33c7599b746` |
| | rom2 with CTF | `rom2.bin` | `10b3f09485a74bb0` |
| | rom2 original (no CTF) | `rom2.bin.old` | `a4c9fd821059054c` |
| | rom_sm | `rom_sm.bin` | `b0b5f865a403f730` |
| | wave ROM 1 / 2 | `waverom1.bin` / `waverom2.bin` | `c6429e21b9b3a02f` / `5b753f6cef4cfc7f` |
| SC-88 | control ROM (v1.01) | `sc88_control.bin` | `875f561d009fba79` |
| | wave ROM 0 / 1 / 2 / 3 | `sc88_wave0/1/2/3.bin` | `4d8fbb7f089e500a` / `c36f96c4a17a17eb` / `be62816e655cf712` / `cd2ba0643fe22fcd` |
| SC-88 Pro | control ROM (v1.02) | `sc88pro_control.bin` | `efcdbe43f5810d34` |
| | wave ROM 0 / 1 / 2 | `sc88pro_wave0/1/2.bin` | `3c6a96298e0de126` / `42bcbba9506a667c` / `db40d8624fceec5a` |
| SC-8820 | internal (CPU) | `sc8820_internal.bin` | `7f3be975fc9ada3d` (or MD5 `b03dc0c5…`) |
| | program | `sc8820_program_2000-11-28.bin` | `934df22a9056426c` |
| | wave 0 | `sc8820_wave0.bin` | `34a8a0af36fe38cb` |
| | wave 1 | `sc8820_wave1.bin` | `7284bf21923f966c` |
| SC-8850 | internal (sub CPU) | `sc8850_internal.bin` | `dc5caf0841819fce` |
| | program | `sc8850_program.bin` | `19e670a82eebe4ff` |
| | data | `sc8850_data.bin` | `48eeceb4dbba45b6` |
| | wave | `sc8850_wave.bin` | `3cfac9db381527a4` |

The SC-88 files are the raw dumps (control ROM 512 KB, four wave ROMs of 2 MB each). The SC-88 Pro files are the raw dumps (control ROM 1 MB, wave ROMs 8 + 8 + 4 MB). The SC-8820 set is the CPU ROM R02015367 (64 KB, the on-chip ROM of the SH7016 CPU; the earlier reconstruction is still accepted by its MD5 from 88emu's ROM list), the program flash of 2000-11-28 (2 MB) and the wave ROMs of 16 + 8 MB. The SC-8850 set is internal 64 KB, program 1 MB, data 2 MB and the wave ROM as one 32 MB file. The MT-32 / CM-32L plug-in is built when at least one of its three sets (MT-32 old, MT-32 new, CM-32L) is complete; devices without a set are greyed out in its SETUP menu. Per device the firmware listed first is used, and the menu shows its version.

> [!NOTE]
> **SC-55mk2:** either `rom2` works. If only the original `rom2` (512 KB) is found, the builder applies the CTF patch itself and checks the result against the known checksum (see [Capital Tone Fallback](polyphony.md#capital-tone-fallback-sc-55mk2)).

For source builds with **external** ROMs, the layout of the original plug-in applies: a `Nuked-SC55-Resources/ROMs/<model>/` folder next to the plug-in (for example `SC-55-v1.21/sc55_rom1.bin`) or one or more directories listed in the `SOUNDCANVAS_ROM_PATH` environment variable. The complete file list per model is in the [Nuked-SC55-CLAP README](https://github.com/johnnovak/Nuked-SC55-CLAP#rom-files).
