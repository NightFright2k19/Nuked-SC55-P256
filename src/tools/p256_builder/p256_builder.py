#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Nuked SC-55 / SC-88 / SC-8850 P256 - Plugin-Builder

Erzeugt die Ein-Datei-Plugins (CLAP + VST2, Windows x64) aus den ROM-losen Vorlagen in
"templates" (ohne ROMs und ohne Platz dafuer; das Skript setzt die ROMs ein) und deinen eigenen ROM-Dateien im Ordner "roms" (Unterordner werden mit
durchsucht, Dateinamen sind egal - erkannt wird am Inhalt per SHA-256).

  SC-55 v1.21        -> output/CLAP/Nuked-SC55_v121.clap, output/VST2/Nuked-SC55_v121.dll
  SC-55mk2 v1.01     -> output/.../Nuked-SC55_MkII.*   (immer mit CTF, s. u.)
  SC-88 Pro          -> output/.../Nuked-SC88_Pro.*
  SC-8850            -> output/.../Nuked-SC8850.*

Gebaut wird alles, wofuer ein vollstaendiger ROM-Satz gefunden wird.
SC-55mk2: Liegt nur das originale rom2 (512 KB) vor, wird der CTF-Patch (Capital Tone
Fallback) beim Bauen angewendet; das Ergebnis wird gegen die bekannte CTF-Pruefsumme geprueft.

Aufruf:  python p256_builder.py          (Python 3.8 oder neuer, keine Zusatzpakete)
"""
import base64
import hashlib
import os
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROMS, TEMPLATES, OUTPUT = HERE / "roms", HERE / "templates", HERE / "output"
MAGIC = b"NUKED-P256-ROMSLOT"

# Bekannte ROM-Dateien (SHA-256 -> Modell, Rolle)
KNOWN = {
    "7e1bacd1d7c62ed66e465ba05597dcd60dfc13fc23de0287fdbce6cf906c6544": ("v121", "rom1"),
    "effc6132d68f7e300aaef915ccdd08aba93606c22d23e580daf9ea6617913af1": ("v121", "rom2"),
    "5655509a531804f97ea2d7ef05b8fec20ebf46216b389a84c44169257a4d2007": ("v121", "wave1"),
    "c655b159792d999b90df9e4fa782cf56411ba1eaa0bb3ac2bdaf09e1391006b1": ("v121", "wave2"),
    "334b2d16be3c2362210fdbec1c866ad58badeb0f84fd9bf5d0ac599baf077cc2": ("v121", "wave3"),
    "8a1eb33c7599b746c0c50283e4349a1bb1773b5c0ec0e9661219bf6c067d2042": ("mk2", "rom1"),
    "10b3f09485a74bb014f1a940d5c67f380c7979b62891d540d788154c83f17430": ("mk2", "rom2"),       # mit CTF
    "a4c9fd821059054c7e7681d61f49ce6f42ed2fe407a7ec1ba0dfdc9722582ce0": ("mk2", "rom2_orig"),  # ohne CTF
    "b0b5f865a403f7308b4be8d0ed3ba2ed1c22db881b8a8326769dea222f6431d8": ("mk2", "rom_sm"),
    "c6429e21b9b3a02fbd68ef0b2053668433bee0bccd537a71841bc70b8874243b": ("mk2", "wave1"),
    "5b753f6cef4cfc7fcafe1430fecbb94a739b874e55356246a46abe24097ee491": ("mk2", "wave2"),
    "efcdbe43f5810d34cb774edfdd4e785a7ce77f5646e94addcf2ee5a217e53234": ("88pro", "control"),
    "3c6a96298e0de126c885f7111c62c8cce6afe8e446d86f5d57624c9540506212": ("88pro", "wave0"),
    "42bcbba9506a667c26bed3ed02afb1f0c1d2c1a132af3f11ab439fd28aa16ae6": ("88pro", "wave1"),
    "db40d8624fceec5af4883dd2ff93c9a47acfdb3b7aed677a2eddb046db3a2a03": ("88pro", "wave2"),
    "dc5caf0841819fce6b9279af92b381a7211baa85822ef2bb1f9249296975ced0": ("8850", "internal"),
    "19e670a82eebe4ff8610aab8013470a749d31a029483ce9e46ade85acdff5aab": ("8850", "program"),
    "48eeceb4dbba45b66e0d3f3c325896bc7913ba19bdf61c4a0690ae6f936cd863": ("8850", "data"),
    "3cfac9db381527a4bc21033a297518c4122b0af392996b036bd38dbada4ba1e2": ("8850", "wave"),
}
SIZES = {32768, 262144, 524288, 4096, 1048576, 2097152, 4194304, 8388608, 65536, 33554432}
CTF_SHA = "10b3f09485a74bb014f1a940d5c67f380c7979b62891d540d788154c83f17430"
# CTF-Patch fuer SC-55mk2 v1.01 rom2: Abschnitte (Versatz u32, Laenge u16, Bytes), zlib, Base64
CTF_PATCH = "eNrl2HdPU1EYx/Fve2kLChVliowCRcsGLYoVKSEOXNHg3nvHhMS9qyYm7r0XKu4VY4y8CF+L8SV4vFwSa5oUiTGG3++vc899cvN88tybk1xcFt2YWHjwkUYGmWSTQy555FNAIUUEKKWCEJXUUEs9DTTSRDMRWojSRjvTmM4MZtLBLOYwjwV0spBFLGYJS1nGclayijWsZT0b2MgmtrCVbexgJ7voYjd72Ms+9nOAgxziMEc4yjGOE+MEJ3tMl3PhDGc5x3kucJFLXOYKV7nGdW5wk1vc5i73uM8DHvGYp/TwjOe85BWvecNb3vGeD3zkE5/pxW3x/e+6Zw/Ivdl2bx+Ym1OcTqq+46i7jfpJErU15NQPB6BOkVR7JNVeSbVPUe1PtUil31lgrrwYaTGBoFmnY6xVxlr30xoxO26ItvXd+UXaaXZC/CZdYaSrE0ljpjpA0vPKVLUzCOuLeOu3vq57SUs635IhNt8vRj1M8lseLqlOl1RnSKr9kuoRimp3pn0q22a/WZeRwFxum6v7zWFT14Gtbk2onv8n6ph5Whf/fNojJd/xUZLqLEl1tqQ6R1KdK6nOk1Tnx6nTjTpLQD1actYFkuoxkupCSXWRpLpYUl0iqQ7Yf6STqIOm6itx7vAg3ev+E3ep5LTLJNXlkuqgpLpCUj1WUj1OUh2SVFdKqqsk1dWS6hpJda2kuk5SXS+pbpBUN0qqx0uqJ0iqw5LqJkn1REn1JEl1s6R6sqQ6IqmeIqlukVRPlVS3SqqjimpXzDKNO3E5cTuxUiw7HideJz4nqU5+AJuF72A="

# Modelle: Vorlage, Anzeigename, (ROM-Platz im Plugin, Rolle)
MODELS = [
    ("v121", "Nuked-SC55_v121", "SC-55 v1.21", [(0, "rom1"), (1, "rom2"), (3, "wave1"), (4, "wave2"), (5, "wave3")]),
    ("mk2", "Nuked-SC55_MkII", "SC-55mk2 v1.01 (CTF)", [(0, "rom1"), (1, "rom2"), (2, "rom_sm"), (3, "wave1"), (4, "wave2")]),
    ("88pro", "Nuked-SC88_Pro", "SC-88 Pro", [(0, "control"), (1, "wave0"), (2, "wave1"), (3, "wave2")]),
    ("8850", "Nuked-SC8850", "SC-8850", [(0, "internal"), (1, "program"), (2, "data"), (3, "wave")]),
]


def apply_ctf(rom2: bytes) -> bytes:
    data, raw, p = bytearray(rom2), zlib.decompress(base64.b64decode(CTF_PATCH)), 0
    while p < len(raw):
        off, n = struct.unpack_from("<IH", raw, p)
        p += 6
        data[off:off + n] = raw[p:p + n]
        p += n
    return bytes(data)


def walk_files(root: Path):
    """Alle Dateien unter root, auch in verknuepften Ordnern (Symlink/Junction), ohne Schleifen."""
    seen = set()
    for dirpath, dirnames, filenames in os.walk(root, followlinks=True):
        real = os.path.realpath(dirpath)
        if real in seen:
            dirnames[:] = []
            continue
        seen.add(real)
        dirnames.sort()
        for name in sorted(filenames):
            yield Path(dirpath) / name


def scan_roms():
    found = {}
    for f in walk_files(ROMS):
        if not f.is_file() or f.stat().st_size not in SIZES:
            continue
        sha = hashlib.sha256(f.read_bytes()).hexdigest()
        if sha in KNOWN and KNOWN[sha] not in found:
            found[KNOWN[sha]] = f
    return found


def fill_template(template: bytes, entries, name: str) -> bytes:
    pos = template.find(MAGIC)
    if pos < 0 or template.find(MAGIC, pos + 1) >= 0:
        raise ValueError("ROM-Bereich in der Vorlage nicht eindeutig gefunden")
    version, capacity, _, _ = struct.unpack_from("<4I", template, pos + 32)
    if version == 2:
        # kompakte Vorlage: der reservierte ROM-Bereich ist herausgeschnitten -> wieder einsetzen
        template = template[:pos + 256] + bytes(capacity) + template[pos + 256:]
    elif version != 1:
        raise ValueError(f"unbekannte Vorlagen-Version {version}")
    out = bytearray(template)
    data_at, offset, table = pos + 256, 0, b""
    for location, blob in entries:
        if offset + len(blob) > capacity:
            raise ValueError("ROM-Daten groesser als der reservierte Bereich")
        out[data_at + offset:data_at + offset + len(blob)] = blob
        table += struct.pack("<4I", location, offset, len(blob), 0)
        offset += (len(blob) + 63) & ~63
    struct.pack_into("<4I", out, pos + 32, 1, capacity, len(entries), 1)
    out[pos + 48:pos + 48 + 128] = table.ljust(128, b"\0")
    out[pos + 176:pos + 240] = name.encode("ascii")[:63].ljust(64, b"\0")
    return bytes(out)


def main() -> int:
    print("Nuked SC-55 / SC-88 / SC-8850 P256 - Plugin-Builder\n")
    if not ROMS.is_dir():
        print(f"Ordner fehlt: {ROMS}\n-> Lege dort deine ROM-Dateien ab (Unterordner erlaubt).")
        return 1
    if (HERE / "Nuked-SC55.clap").exists():
        print("Hinweis: Nuked-SC55.clap wird nicht benoetigt (die P256-Funktionen sind neuer Programmcode,\n"
              "         die Original-Datei laesst sich nicht dahin patchen). Gebaut wird aus 'templates'.\n")
    found = scan_roms()
    built = 0
    for key, tpl_name, title, parts in MODELS:
        roles = [r for _, r in parts]
        have = {r: found.get((key, r)) for r in roles}
        ctf_note = ""
        if key == "mk2" and have["rom2"] is None and (key, "rom2_orig") in found:
            have["rom2"] = found[(key, "rom2_orig")]
            ctf_note = " (CTF-Patch wird angewendet)"
        missing = [r for r in roles if have[r] is None]
        if missing:
            if any(have.values()):
                print(f"[--] {title}: unvollstaendig, es fehlen: {', '.join(missing)}")
            else:
                print(f"[--] {title}: keine ROMs gefunden")
            continue
        tpl = TEMPLATES / f"{tpl_name}.p256tpl"
        if not tpl.is_file():
            print(f"[!!] {title}: Vorlage fehlt ({tpl})")
            continue
        entries = []
        for location, role in parts:
            blob = have[role].read_bytes()
            if key == "mk2" and role == "rom2" and hashlib.sha256(blob).hexdigest() != CTF_SHA:
                blob = apply_ctf(blob)
                if hashlib.sha256(blob).hexdigest() != CTF_SHA:
                    print(f"[!!] {title}: CTF-Patch ergab nicht die erwartete Pruefsumme")
                    entries = None
                    break
            entries.append((location, blob))
        if entries is None:
            continue
        try:
            plugin = fill_template(tpl.read_bytes(), entries, title)
        except ValueError as e:
            print(f"[!!] {title}: {e}")
            continue
        for sub, ext in (("CLAP", ".clap"), ("VST2", ".dll")):
            (OUTPUT / sub).mkdir(parents=True, exist_ok=True)
            (OUTPUT / sub / f"{tpl_name}{ext}").write_bytes(plugin)
        print(f"[ok] {title}{ctf_note}: output/CLAP/{tpl_name}.clap, output/VST2/{tpl_name}.dll")
        for _, role in parts:
            print(f"       {role:8} <- {have[role].relative_to(HERE)}")
        built += 1
    print(f"\n{built} Plugin(s) erzeugt." if built else "\nKein Plugin erzeugt.")
    return 0 if built else 2


if __name__ == "__main__":
    sys.exit(main())
