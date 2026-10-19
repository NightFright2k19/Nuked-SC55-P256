#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Nuked SC-55 / Emu SC-88 / SC-8820 / SC-8850 / MT-32 P256 - Plugin Builder

Creates the single-file plugins (CLAP + VST2, Windows x64) from the ROM-less templates in
"templates" (without ROMs and without space reserved for them; the script inserts the ROMs)
and your own ROM files in the "roms" folder (subfolders are searched as well,
filenames do not matter - identification is based on the contents via SHA-256).

  SC-55 v1.21        -> output/CLAP/Nuked-SC55_v121.clap, output/VST2/Nuked-SC55_v121.dll
  SC-55mk2 v1.01     -> output/.../Nuked-SC55_MkII.*   (always with CTF, see below)
  SC-88              -> output/.../Emu-SC88.*
  SC-88 Pro          -> output/.../Emu-SC88_Pro.*
  SC-8820            -> output/.../Emu-SC8820.*    (interface Basic / Pro in the SETUP menu)
  SC-8850            -> output/.../Emu-SC8850.*
  MT-32 / CM-32L     -> output/.../Emu-MT32.*      (MT-32 old, MT-32 new and CM-32L in one plugin,
                                                    device chosen in the SETUP menu; built when at
                                                    least one of the three ROM sets is complete)

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
# MT-32 / CM-32L (LA boards, SHA-256 -> ("la", role)). Old MT-32 firmware: the two 32 KiB EPROMs
# (A = IC27, even addresses; B = IC26, odd addresses) or the combined 64 KiB image.
for _sha, _role in (
    ("a73a06c23ed38370e58a11fb1b86f7ea4c547061a60d7aa62bae446235fd2dff", "old 1.07"),
    ("967d2e3c782d87015b4a0b08348baf6bef9ad7fa7fb0d2e35d83b3158850bafd", "old 1.07 A"),
    ("f2889cdd5e8dce2989d0a63628480191eba50aa46e3ee349fcc4926045ebaf91", "old 1.07 B"),
    ("1942ad350727dad28f986f89f473e5fb86b17aabf9c9b91fd1382ad6767b83a6", "old 1.06"),
    ("17d9b8b65b92ed73a4608247858f3ffefe901934df4e14783993e49238cdc675", "old 1.06 A"),
    ("5be9dba23bebfe4b342d2b086ed5fddca1e7436eb4900b3e4e8fdd7064490c22", "old 1.06 B"),
    ("a476d2f8273c06ffba54f6c828eb8184b44d89a6aed38a26dfd9096a2c8cc11a", "old 1.05"),
    ("886433723e396c01efb5844d4edfa9caa419240a03dec2153f075048d79f3129", "old 1.05 A"),
    ("4d1a74c385abb7e2a1868c9ca1720702d1959ef60376b1f4aa3b9effac5a9c87", "old 1.05 B"),
    ("c0ea149eaadc48049572ec429e45f90ab1bb75f1ed07000c636fc0124b64da23", "old 1.04"),
    ("70fe51fef3ef16fac12f014f5c03e5cb69d9819d7568322459d9b08c675be78a", "old 1.04 A"),
    ("4ed69cdcb5a575f869f55f0751a14da7c9d31b3a6720b91c203ba7029d6cde84", "old 1.04 B"),
    ("c088c026ad99e13204ac4aa157052f443c52424cbd6394a82abd60bab4c34b91", "old Blue Ridge"),
    ("6416f335c596d853d6bb773fcdd2f575e291b57aa382092b375ba5bd1f3d8108", "old Blue Ridge A"),
    ("9da6e3f5f81915b4956ca4c9398589419b2b82d7a11ff8baae821e22566b85b2", "old Blue Ridge B"),
    ("9b16e6132fbc16e84ee74a21b3683ef60ffe6e7cbde12444e87586fbde99f7f1", "old M-9"),
    ("4b67dc6e09085b969efaf475f61fb0c5534bf78a8cfb8dbca6c1335442de9538", "old M-9 A"),
    ("d0121e2fb1fbd34ee23b56e11fdb811ebc1bd1f1c94ec3802653cc17dcd817d7", "old M-9 B"),
    ("a5339b966142dd2ca0e49b7c4cc1307510a753ea3331c85355706d306ac01d86", "new 2.04"),
    ("a1ee6ac3190bb94daeae1785b1c4b0f91dc1648880ecef0772df794f20673dfb", "new 2.07"),
    ("08d68695e5bff6bbcbb751d6e4453db9f5ead0b5f41e86480798a04376dfe090", "cm32l 1.02"),
    ("c8aea5fb1b28183523601bbc5fd7e2f48397c1cb26f8687d240333c2190e83e2", "cm32l 1.00"),  # LAPC-I 1.00
    ("d9164063f293410cf33f2f64cdcea6893b44723fe41938e07ec9aef58b406238", "R15449121"),   # PCM, both MT-32
    ("990d5bb6b6dd1ba74a009ee4712beb5cecc97b11ee63dfd78c26babd0f8e5b53", "R15179844"),   # = 1st half of it
    ("95f312aee6f359b844776c6a18d9e5ae728d295a1bd256b0a5339b58b3852899", "R15179845"),   # = 2nd half
    ("5ed28de779c05b35e456cc95bdd4f520ea1b8d3079c6809fd49ae061fee9f763", "R15179945"),   # CM-32L PCM extension
    ("0dc50190bb7ea3fb212c970346b771a511772b528801e7eda6d27efda8c40698", "R15449121+R15179945"),
    ("8ec50a524dde9c39479550ca204b5803ea4dce0bc668152983ad7f3914c2c63f", "R15179857"),   # reverb, old MT-32
    ("a4e146bfad60bdb47738b8b53292d16c612ef8edcfe710ee40ca69618421bd82", "R15179917"),   # reverb, new MT-32 + CM-32L
):
    KNOWN[_sha] = ("la", _role)
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
    # MT-32 / CM-32L: one ROM set per device (slot 0 = MT-32 old, 1 = MT-32 new, 2 = CM-32L), see la_sets()
    ("la", "Emu-MT32", "MT-32 / CM-32L", [(0, "MT-32 old"), (1, "MT-32 new"), (2, "CM-32L")]),
    ("v121", "Nuked-SC55_v121", "SC-55 v1.21", [(0, "rom1"), (1, "rom2"), (3, "wave1"), (4, "wave2"), (5, "wave3")]),
    ("mk2", "Nuked-SC55_MkII", "SC-55mk2 v1.01 (CTF)", [(0, "rom1"), (1, "rom2"), (2, "rom_sm"), (3, "wave1"), (4, "wave2")]),
    ("88", "Emu-SC88", "SC-88", [(0, "control"), (1, "wave0"), (2, "wave1"), (3, "wave2"), (4, "wave3")]),
    ("88pro", "Emu-SC88_Pro", "SC-88 Pro", [(0, "control"), (1, "wave0"), (2, "wave1"), (3, "wave2")]),
    ("8820", "Emu-SC8820", "SC-8820", [(0, "internal"), (1, "program"), (2, "wave0"), (3, "wave1")]),
    ("8850", "Emu-SC8850", "SC-8850", [(0, "internal"), (1, "program"), (2, "data"), (3, "wave")]),
]
# Firmware per LA device, preferred first (the plugin shows the version it was built with)
LA_FIRMWARE = {
    "MT-32 old": ["old 1.07", "old 1.06", "old 1.05", "old 1.04", "old Blue Ridge", "old M-9"],
    "MT-32 new": ["new 2.04", "new 2.07"],
    "CM-32L": ["cm32l 1.02", "cm32l 1.00"],
}
LA_MAGIC = b"NKLA1\0\0\0"


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


def la_sets(found):
    """MT-32 / CM-32L: per device (role) the best complete set ->
    {role: (version, control parts, wave parts, reverb)} and the missing pieces of the others."""
    sets, missing = {}, []

    def first(*names):
        for n in names:
            if ("la", n) in found:
                return [found[("la", n)]]
            if all(("la", p) in found for p in n.split("+")) and "+" in n:
                return [found[("la", p)] for p in n.split("+")]
        return None

    for role, firmwares in LA_FIRMWARE.items():
        control = version = None
        for fw in firmwares:
            if ("la", fw) in found:
                control, version = [found[("la", fw)]], fw
            elif ("la", fw + " A") in found and ("la", fw + " B") in found:
                control, version = [found[("la", fw + " A")], found[("la", fw + " B")]], fw
            if control:
                break
        if role == "CM-32L":
            wave, reverb = first("R15449121+R15179945"), first("R15179917")
            if wave is None and ("la", "R15179945") in found:
                pcm = first("R15449121", "R15179844+R15179845")
                wave = pcm + [found[("la", "R15179945")]] if pcm else None
        else:
            wave = first("R15449121", "R15179844+R15179845")
            reverb = first("R15179857" if role == "MT-32 old" else "R15179917")
        if control and wave and reverb:
            sets[role] = (version.split(" ", 1)[1], control, wave, reverb[0])
        else:
            missing += [f"{role} {what}" for what, ok in (("control", control), ("wave", wave), ("reverb", reverb))
                        if not ok]
    return sets, missing


def la_blob(version, control, wave, reverb) -> bytes:
    """One LA device set for the plugin: header (magic, version text, sizes), control, wave, reverb.
    Two control files are the old MT-32's EPROM pair: A = even, B = odd addresses."""
    if len(control) == 2:
        a, b = control[0].read_bytes(), control[1].read_bytes()
        ctl = bytearray(len(a) + len(b))
        ctl[0::2], ctl[1::2] = a, b
        ctl = bytes(ctl)
    else:
        ctl = control[0].read_bytes()
    pcm = b"".join(p.read_bytes() for p in wave)
    rev = reverb.read_bytes()
    head = LA_MAGIC + version.encode("ascii")[:15].ljust(16, b"\0") + struct.pack("<4I", len(ctl), len(pcm), len(rev), 0)
    return head + ctl + pcm + rev


def plan(found):
    """Per model: what was found. List of dicts (key, file, title, parts, have, missing, ctf)."""
    result = []
    for key, tpl_name, title, parts in MODELS:
        if key == "la":
            sets, missing = la_sets(found)
            if sets:  # complete: the devices whose set was found
                parts = [(loc, r) for loc, r in parts if r in sets]
                have = {r: sets[r][1][0] for _, r in parts}
                missing = []
            else:
                have = {r: None for _, r in parts}
                if any(k[0] == "la" for k in found):
                    have[parts[0][1]] = next(p for k, p in found.items() if k[0] == "la")
            result.append(dict(key=key, file=tpl_name, title=title, parts=parts, have=have, ctf=False,
                               missing=missing, la=sets))
            continue
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
    if key == "la":
        entries = [(location, la_blob(*item["la"][role])) for location, role in parts]
    for location, role in ([] if key == "la" else parts):
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
    print("Nuked SC-55 / Emu SC-88 / SC-8820 / SC-8850 / MT-32 P256 - Plugin Builder\n")
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