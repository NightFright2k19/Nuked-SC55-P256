#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Nuked SC-55 / SC-88 / SC-8820 / SC-8850 P256 - Plugin Builder

Creates the single-file plugins (CLAP + VST2, Windows x64) from the ROM-less templates in
"templates" (without ROMs and without space reserved for them; the script inserts the ROMs)
and your own ROM files in the "roms" folder (subfolders are searched as well,
filenames do not matter - identification is based on the contents via SHA-256).

  SC-55 v1.21        -> output/CLAP/Nuked-SC55_v121.clap, output/VST2/Nuked-SC55_v121.dll
  SC-55mk2 v1.01     -> output/.../Nuked-SC55_MkII.*   (always with CTF, see below)
  SC-88              -> output/.../Nuked-SC88.*
  SC-88 Pro          -> output/.../Nuked-SC88_Pro.*
  SC-8820            -> output/.../Nuked-SC8820.*
  SC-8850            -> output/.../Nuked-SC8850.*

Everything for which a complete ROM set is found will be built.
SC-55mk2: If only the original rom2 (512 KB) is available, the CTF patch (Capital Tone
Fallback) is applied during the build; the result is checked against the known CTF checksum.

Usage:  python p256_builder.py          (Python 3.8 or newer, no additional packages)

Releases use the patcher (patcher.py), which runs these same functions; this script is kept
for development and for building from the repository.
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

# Known ROM files (SHA-256 -> model, role)
KNOWN = {
    "7e1bacd1d7c62ed66e465ba05597dcd60dfc13fc23de0287fdbce6cf906c6544": ("v121", "rom1"),
    "effc6132d68f7e300aaef915ccdd08aba93606c22d23e580daf9ea6617913af1": ("v121", "rom2"),
    "5655509a531804f97ea2d7ef05b8fec20ebf46216b389a84c44169257a4d2007": ("v121", "wave1"),
    "c655b159792d999b90df9e4fa782cf56411ba1eaa0bb3ac2bdaf09e1391006b1": ("v121", "wave2"),
    "334b2d16be3c2362210fdbec1c866ad58badeb0f84fd9bf5d0ac599baf077cc2": ("v121", "wave3"),
    "8a1eb33c7599b746c0c50283e4349a1bb1773b5c0ec0e9661219bf6c067d2042": ("mk2", "rom1"),
    "10b3f09485a74bb014f1a940d5c67f380c7979b62891d540d788154c83f17430": ("mk2", "rom2"),       # with CTF
    "a4c9fd821059054c7e7681d61f49ce6f42ed2fe407a7ec1ba0dfdc9722582ce0": ("mk2", "rom2_orig"),  # without CTF
    "b0b5f865a403f7308b4be8d0ed3ba2ed1c22db881b8a8326769dea222f6431d8": ("mk2", "rom_sm"),
    "c6429e21b9b3a02fbd68ef0b2053668433bee0bccd537a71841bc70b8874243b": ("mk2", "wave1"),
    "5b753f6cef4cfc7fcafe1430fecbb94a739b874e55356246a46abe24097ee491": ("mk2", "wave2"),
    "875f561d009fba79296c745b02a83df91105346e292f575d16cf484a17b85be8": ("88", "control"),
    "4d8fbb7f089e500a5cdefcc45caaeeca3434eb2c67a3754b7a22d4b097e771e7": ("88", "wave0"),
    "c36f96c4a17a17eba668958efa95382c90f375b32fad28b7519a2f66c965debd": ("88", "wave1"),
    "be62816e655cf71244f7b06cbc06279ae424ba079a582b883c8840720e356d1e": ("88", "wave2"),
    "cd2ba0643fe22fcd5bb8121f27220f29966f146217fcda3bf206b9d80a26d86d": ("88", "wave3"),
    "efcdbe43f5810d34cb774edfdd4e785a7ce77f5646e94addcf2ee5a217e53234": ("88pro", "control"),
    "3c6a96298e0de126c885f7111c62c8cce6afe8e446d86f5d57624c9540506212": ("88pro", "wave0"),
    "42bcbba9506a667c26bed3ed02afb1f0c1d2c1a132af3f11ab439fd28aa16ae6": ("88pro", "wave1"),
    "db40d8624fceec5af4883dd2ff93c9a47acfdb3b7aed677a2eddb046db3a2a03": ("88pro", "wave2"),
    "dc5caf0841819fce6b9279af92b381a7211baa85822ef2bb1f9249296975ced0": ("8850", "internal"),
    "19e670a82eebe4ff8610aab8013470a749d31a029483ce9e46ade85acdff5aab": ("8850", "program"),
    "48eeceb4dbba45b66e0d3f3c325896bc7913ba19bdf61c4a0690ae6f936cd863": ("8850", "data"),
    "3cfac9db381527a4bc21033a297518c4122b0af392996b036bd38dbada4ba1e2": ("8850", "wave"),
    "7f3be975fc9ada3dee0c69e750380705b38aa814c3e68205dc35afba1ba113d6": ("8820", "internal"),  # CPU ROM R02015367 (SH7016 on-chip ROM)
    "934df22a9056426ce855a1a6361bab9b3fba762d736ad925d3a5742de3d6fd35": ("8820", "program"),   # 2000-11-28
    "34a8a0af36fe38cba9f442d17881cb96305ec905d5baa4cc819f10d8d4ec49f7": ("8820", "wave0"),
    "7284bf21923f966c78019a5d697c8a11bb82ff3bf7a1af95957397feb8dfc597": ("8820", "wave1"),
}
# ROMs known only by MD5 (from 88emu's ROM registry): the earlier reconstruction of the SC-8820 CPU ROM
KNOWN_MD5 = {
    "b03dc0c554fae77bb1782da4ccfadfbd": ("8820", "internal"),
}
SIZES = {32768, 262144, 524288, 4096, 1048576, 2097152, 4194304, 8388608, 65536, 33554432, 131072, 16777216}
CTF_SHA = "10b3f09485a74bb014f1a940d5c67f380c7979b62891d540d788154c83f17430"
# CTF patch for SC-55mk2 v1.01 rom2: sections (u32 offset, u16 length, bytes), zlib, Base64
CTF_PATCH = "eNrl2HdPU1EYx/Fve2kLChVliowCRcsGLYoVKSEOXNHg3nvHhMS9qyYm7r0XKu4VY4y8CF+L8SV4vFwSa5oUiTGG3++vc899cvN88tybk1xcFt2YWHjwkUYGmWSTQy555FNAIUUEKKWCEJXUUEs9DTTSRDMRWojSRjvTmM4MZtLBLOYwjwV0spBFLGYJS1nGclayijWsZT0b2MgmtrCVbexgJ7voYjd72Ms+9nOAgxziMEc4yjGOE+MEJ3tMl3PhDGc5x3kucJFLXOYKV7nGdW5wk1vc5i73uM8DHvGYp/TwjOe85BWvecNb3vGeD3zkE5/pxW3x/e+6Zw/Ivdl2bx+Ym1OcTqq+46i7jfpJErU15NQPB6BOkVR7JNVeSbVPUe1PtUil31lgrrwYaTGBoFmnY6xVxlr30xoxO26ItvXd+UXaaXZC/CZdYaSrE0ljpjpA0vPKVLUzCOuLeOu3vq57SUs635IhNt8vRj1M8lseLqlOl1RnSKr9kuoRimp3pn0q22a/WZeRwFxum6v7zWFT14Gtbk2onv8n6ph5Whf/fNojJd/xUZLqLEl1tqQ6R1KdK6nOk1Tnx6nTjTpLQD1actYFkuoxkupCSXWRpLpYUl0iqQ7Yf6STqIOm6itx7vAg3ev+E3ep5LTLJNXlkuqgpLpCUj1WUj1OUh2SVFdKqqsk1dWS6hpJda2kuk5SXS+pbpBUN0qqx0uqJ0iqw5LqJkn1REn1JEl1s6R6sqQ6IqmeIqlukVRPlVS3SqqjimpXzDKNO3E5cTuxUiw7HideJz4nqU5+AJuF72A="

# Models: template, display name, (ROM slot in plugin, role)
MODELS = [
    ("v121", "Nuked-SC55_v121", "SC-55 v1.21", [(0, "rom1"), (1, "rom2"), (3, "wave1"), (4, "wave2"), (5, "wave3")]),
    ("mk2", "Nuked-SC55_MkII", "SC-55mk2 v1.01 (CTF)", [(0, "rom1"), (1, "rom2"), (2, "rom_sm"), (3, "wave1"), (4, "wave2")]),
    ("88", "Nuked-SC88", "SC-88", [(0, "control"), (1, "wave0"), (2, "wave1"), (3, "wave2"), (4, "wave3")]),
    ("88pro", "Nuked-SC88_Pro", "SC-88 Pro", [(0, "control"), (1, "wave0"), (2, "wave1"), (3, "wave2")]),
    ("8820", "Nuked-SC8820", "SC-8820", [(0, "internal"), (1, "program"), (2, "wave0"), (3, "wave1")]),
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


def walk_files(root: Path, skip=()):
    """All files below root, including linked directories (symlink/junctions), without loops.
    Directories whose resolved path is in skip are left out."""
    seen = {os.path.realpath(p) for p in skip}
    for dirpath, dirnames, filenames in os.walk(root, followlinks=True):
        real = os.path.realpath(dirpath)
        if real in seen:
            dirnames[:] = []
            continue
        seen.add(real)
        dirnames.sort()
        for name in sorted(filenames):
            yield Path(dirpath) / name


def scan_roms(root: Path = ROMS, skip=()):
    """Known ROM files below root: {(model, role): path}."""
    found = {}
    for f in walk_files(root, skip):
        try:
            if not f.is_file() or f.stat().st_size not in SIZES:
                continue
            data = f.read_bytes()
        except OSError:
            continue
        key = KNOWN.get(hashlib.sha256(data).hexdigest()) or KNOWN_MD5.get(hashlib.md5(data).hexdigest())
        if key and key not in found:
            found[key] = f
    return found


def fill_template(template: bytes, entries, name: str) -> bytes:
    pos = template.find(MAGIC)
    if pos < 0 or template.find(MAGIC, pos + 1) >= 0:
        raise ValueError("ROM area in template could not be located unambiguously")
    version, capacity, _, _ = struct.unpack_from("<4I", template, pos + 32)
    if version == 2:
        # compact template: the reserved ROM area has been removed -> insert it again
        template = template[:pos + 256] + bytes(capacity) + template[pos + 256:]
    elif version != 1:
        raise ValueError(f"unknown template version {version}")
    out = bytearray(template)
    data_at, offset, table = pos + 256, 0, b""
    for location, blob in entries:
        if offset + len(blob) > capacity:
            raise ValueError("ROM data is larger than the reserved area")
        out[data_at + offset:data_at + offset + len(blob)] = blob
        table += struct.pack("<4I", location, offset, len(blob), 0)
        offset += (len(blob) + 63) & ~63
    struct.pack_into("<4I", out, pos + 32, 1, capacity, len(entries), 1)
    out[pos + 48:pos + 48 + 128] = table.ljust(128, b"\0")
    out[pos + 176:pos + 240] = name.encode("ascii")[:63].ljust(64, b"\0")
    return bytes(out)


def plan(found):
    """Per model: what was found. List of dicts (key, file, title, parts, have, missing, ctf)."""
    result = []
    for key, tpl_name, title, parts in MODELS:
        roles = [r for _, r in parts]
        have = {r: found.get((key, r)) for r in roles}
        ctf = False
        if key == "mk2" and have["rom2"] is None and (key, "rom2_orig") in found:
            have["rom2"] = found[(key, "rom2_orig")]
            ctf = True
        result.append(dict(key=key, file=tpl_name, title=title, parts=parts, have=have, ctf=ctf,
                           missing=[r for r in roles if have[r] is None]))
    return result


def build_model(item, templates: Path = TEMPLATES, output: Path = OUTPUT):
    """Builds one complete model -> (clap path, vst2 path). Raises ValueError on failure."""
    key, tpl_name, title, parts = item["key"], item["file"], item["title"], item["parts"]
    tpl = templates / f"{tpl_name}.p256tpl"
    if not tpl.is_file():
        raise ValueError(f"template missing ({tpl})")
    entries = []
    for location, role in parts:
        blob = item["have"][role].read_bytes()
        if key == "mk2" and role == "rom2" and hashlib.sha256(blob).hexdigest() != CTF_SHA:
            blob = apply_ctf(blob)
            if hashlib.sha256(blob).hexdigest() != CTF_SHA:
                raise ValueError("CTF patch did not produce the expected checksum")
        entries.append((location, blob))
    plugin = fill_template(tpl.read_bytes(), entries, title)
    paths = []
    for sub, ext in (("CLAP", ".clap"), ("VST2", ".dll")):
        (output / sub).mkdir(parents=True, exist_ok=True)
        path = output / sub / f"{tpl_name}{ext}"
        path.write_bytes(plugin)
        paths.append(path)
    return tuple(paths)


def main() -> int:
    print("Nuked SC-55 / SC-88 / SC-8820 / SC-8850 P256 - Plugin Builder\n")
    if not ROMS.is_dir():
        print(f"Folder missing: {ROMS}\n-> Put your ROM files there (subfolders are allowed).")
        return 1
    if (HERE / "Nuked-SC55.clap").exists():
        print("Note: Nuked-SC55.clap is not required (the P256 functions are newer code,\n"
              "      the original file cannot be patched for this purpose). The build is created from 'templates'.\n")
    built = 0
    for item in plan(scan_roms()):
        title, tpl_name = item["title"], item["file"]
        if item["missing"]:
            if any(item["have"].values()):
                print(f"[--] {title}: incomplete, missing: {', '.join(item['missing'])}")
            else:
                print(f"[--] {title}: no ROMs found")
            continue
        try:
            build_model(item)
        except (ValueError, OSError) as e:
            print(f"[!!] {title}: {e}")
            continue
        ctf_note = " (CTF patch will be applied)" if item["ctf"] else ""
        print(f"[ok] {title}{ctf_note}: output/CLAP/{tpl_name}.clap, output/VST2/{tpl_name}.dll")
        for _, role in item["parts"]:
            print(f"       {role:8} <- {item['have'][role].relative_to(HERE)}")
        built += 1
    print(f"\n{built} plugin(s) created." if built else "\nNo plugin created.")
    return 0 if built else 2


if __name__ == "__main__":
    sys.exit(main())