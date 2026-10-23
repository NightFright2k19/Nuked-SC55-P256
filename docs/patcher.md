# Patcher and builder

The patcher window, its self-update and the P256 builder script. Part of the [documentation index](../README.md#documentation).

## The patcher

The patcher (`patcher.py`, texts in `patcher_i18n.py`) is a small window around the P256 builder. The releases contain it as a single executable per system, built with PyInstaller, with the ROM-less templates inside. It is not an installer: it writes nothing but the `Plugins` folder (no registry entries, nothing to uninstall).

<img width="626" height="546" alt="patcher_preview" src="https://github.com/user-attachments/assets/f6a66463-c1b6-45cc-9834-8ba7465e89fb" />

- **Releases:** the newest Autobuild is marked **Latest**; older Autobuilds stay available for comparison and regression tests.
- **ROM files:** searched in the patcher's folder and all subfolders (by SHA-256, names do not matter). If no complete set is found there, it asks for the folder; **Change…** picks another one at any time. Each model is listed as complete, incomplete (with the missing parts) or not found.
- **Patch:** builds every complete model into `Plugins/CLAP/<name>.clap` and `Plugins/VST2/<name>.dll` next to the patcher (the folder is created if needed). **Details** shows which file was used for which part.
- **Same result as the script:** the patcher calls the functions of `p256_builder.py`; the plug-ins are byte-identical to those of `python p256_builder.py`.
- **Languages:** Arabic, Bengali, Chinese (Mandarin), Czech, Danish, Dutch, English, French, German, Greek, Hebrew, Hindi, Hungarian, Indonesian, Italian, Japanese, Korean, Persian, Polish, Portuguese, Romanian, Russian, Serbian, Spanish, Swedish, Thai, Turkish, Ukrainian, Vietnamese. The system language is used if available, otherwise English; the menu at the top right switches it.
- **Update** (issue #89): a moment after it has started, the patcher asks GitHub for the number of the newest Autobuild release (the redirect of the Latest page; no login, no API, nothing else is sent). If it is newer than the running one, **A newer version (rN) is available!** appears under the buttons. **Update** (left of **Show details**) then asks once (OK / Cancel), downloads the file for your system with a progress bar, checks its size and the SHA-256 that GitHub shows, lets the new version pass its own `--selftest`, and starts it; the old window closes. The new version goes into the folder of the patcher (that is where the ROMs are searched); if that folder is read-only, into your Downloads folder.
  - **Windows:** the new `Nuked-SC55-P256-Patcher-rN-windows.exe` is placed next to the old one, and the new patcher deletes the old one as soon as it has ended. Only a file with the official name `Nuked-SC55-P256-Patcher-rN-windows.exe` is deleted; a renamed copy stays.
  - **Linux:** the program file is replaced. **macOS:** the app is replaced; the old one is deleted by the new one after it has started.
  - **Not for everyone:** start the patcher with `--no-update-check` (or set `NUKED_P256_NO_UPDATE_CHECK=1`) and it does not look at the start; **Update** still checks when you click it. A patcher run from source (`python patcher.py`) does not update itself. The patcher files are not signed (see below), so the update is as trustworthy as downloading the file yourself.
- **Without a window:** `patcher --build [folder]` builds from the given folder (default: the patcher's folder) and prints the result; `patcher --selftest` checks the bundled templates (`--selftest-out <file>` also writes the result to a file).

> [!NOTE]
> The patcher is **not signed**. On the first start Windows SmartScreen shows "Windows protected your PC": click **More info**, then **Run anyway**. macOS Gatekeeper refuses to open the app: right-click (or Control-click) it, choose **Open** and confirm; on newer macOS versions use **System Settings → Privacy & Security → Open Anyway**. Alternatively remove the quarantine flag in Terminal: `xattr -dr com.apple.quarantine "Nuked-SC55-P256-Patcher.app"`. Move the app out of the Downloads folder before starting it, so that the `Plugins` folder is created next to it. On Linux, make the file executable (`chmod +x Nuked-SC55-P256-Patcher`); it needs a desktop session.

---

## The P256 builder

`p256_builder.py` (Python 3.8 or newer, standard library only) creates the finished plug-ins from the ROM-less templates in `templates` and your ROMs in `roms`. Releases use the [patcher](#the-patcher), which runs the same code; the script stays in the repository for development and for building directly from a checkout (put your ROMs into `roms`).

```
python p256_builder.py
```

```
Nuked SC-55 / Emu SC-88 / SC-8820 / SC-8850 / MT-32 P256 - Plugin Builder

[--] MT-32 / CM-32L: no ROMs found
[ok] SC-55 v1.21: output/CLAP/Nuked-SC55_v121.clap, output/VST2/Nuked-SC55_v121.dll
[ok] SC-55mk2 v1.01 (CTF) (CTF patch will be applied): output/CLAP/Nuked-SC55_MkII.clap, ...
[--] SC-88: no ROMs found
[--] SC-88 Pro: no ROMs found
[--] SC-8820: no ROMs found
[--] SC-8850: no ROMs found

2 plugin(s) created.
```

- **Detection:** `roms` is searched recursively (symbolic links and junctions are followed, with loop protection); each file is identified by SHA-256, so names do not matter. Unknown files are skipped.
- **Per model:** a plug-in is built when its set is complete. An incomplete set is reported with the missing parts. MT-32 / CM-32L: built when at least one device set is complete, with all complete device sets in one plug-in.
- **Result:** `output/CLAP/<name>.clap` and `output/VST2/<name>.dll` per model (identical files).
- **Exit code:** 0 if something was built, 2 if nothing was built, 1 if `roms` is missing.

### What a template is

A template is the finished plug-in without ROMs. At the place where the ROMs belong, it contains a block marked `NUKED-P256-ROMSLOT` with a 256-byte header (version, capacity, number of entries, filled flag, 8 entries {slot, offset, size}, name) followed by the data area. The builder writes your ROMs into the data area, fills in the table and sets the filled flag. The code in the plug-in reads the ROMs through the same interface as the embedded builds.

- **Compact templates (header version 2):** the reserved area (only zeroes) is cut out of the file, and the builder inserts it again (`capacity` bytes) before filling it. Sizes: v1.21 and MkII 1.52 MB, SC-88 Pro 4.93 MB. Compact templates are **not loadable DLLs**.
- **Capacities:** v1.21 3,506,176 bytes, MkII 3,772,416 bytes, SC-88 Pro 22,085,632 bytes (sum of the ROMs + 64 KB).
- **No ROM data:** even though the size of a template may suggest otherwise, it does **not** contain any ROM data, only zeroes in the reserved area (verified: all zero, `filled=0`, none of 7,156 ROM chunks of 4 KB found in any template). A template that is loaded directly stays silent and does not crash.
- The generated plug-ins are byte-identical to those made from the full templates and produce the same values as the release builds (CTF test and 256-note test).

> [!WARNING]
> The generated plug-ins **contain your ROMs**. Do not share them.
