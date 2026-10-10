Nuked SC-55 / SC-88 / SC-8850 P256 - Plugin Builder
====================================================

Creates the single-file plugins (CLAP + VST2, Windows x64) from your own ROM files. The
templates in "templates" are the finished plugins without ROMs (and without space for them);
the script inserts the ROMs at the intended place. No compiler needed.

1. Install Python 3.8 or newer (no extra packages).
2. Put your ROM files into "roms" (subfolders are searched, file names do not matter -
   files are recognised by content, SHA-256).
3. Run:  python p256_builder.py
4. Result: output\CLAP\*.clap and output\VST2\*.dll

Everything with a complete ROM set is built: SC-55 v1.21, SC-55mk2 v1.01,
SC-88 (control 512 KB, wave0-3 2 MB each), SC-88 Pro, SC-8850 (internal 64 KB, program 1 MB, data 2 MB, wave 32 MB).
SC-55mk2 always includes CTF (capital tone fallback): if only the original rom2 is present,
the script applies the CTF patch and verifies the result against the known checksum.
An original "Nuked-SC55.clap" is not needed (the P256 features are new code and cannot be
patched into the original file). The generated plugins contain your ROMs: private use only.
88emu and the SC-88 / SC-88 Pro / SC-8850 panel art (The Usual Suspects) are GPLv3.
