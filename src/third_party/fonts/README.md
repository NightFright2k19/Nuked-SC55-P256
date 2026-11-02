# Fonts of the Linux editor (issue #112)

The Linux and macOS editors (`src/gui/win32_gdi.cpp`) cannot use the Windows fonts of the panels, so it embeds these
fonts unmodified (`.incbin`, about 1.1 MB together; unmodified, so the font names and licenses stay as they are).

| File | Replaces (Windows) | Source | License |
| --- | --- | --- | --- |
| `LiberationSans-Regular.ttf` | Arial / Segoe UI (normal weight, menus) | Liberation Fonts 2.1 (metric-compatible with Arial) | SIL Open Font License 1.1, `LICENSE-Liberation.txt` |
| `LiberationSans-Bold.ttf` | Arial / Segoe UI (semibold, bold, heavy) | Liberation Fonts 2.1 | SIL Open Font License 1.1, `LICENSE-Liberation.txt` |
| `DejaVuSansMono-Bold.ttf` | Consolas | DejaVu Fonts 2.37 | Bitstream Vera / public domain, `LICENSE-DejaVu.txt` |

Both licenses allow embedding the unmodified fonts in other software; the license texts above go with them.
