#!/usr/bin/env python3
# Macht eine ROM-lose Vorlage kompakt: schneidet den (nur aus Nullen bestehenden) reservierten
# ROM-Bereich heraus und markiert den Kopf als Version 2. p256_builder.py setzt die ROMs beim
# Bauen an dieser Stelle wieder ein. Eine kompakte Vorlage ist keine ladbare DLL.
# usage: compact_template.py <vorlage.dll> <ziel.p256tpl>
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src, "rb").read()
pos = t.find(b"NUKED-P256-ROMSLOT")
assert pos >= 0 and t.find(b"NUKED-P256-ROMSLOT", pos + 1) < 0, "ROM-Bereich nicht eindeutig"
ver, cap, cnt, filled = struct.unpack_from("<4I", t, pos + 32)
assert ver == 1 and filled == 0, "keine leere Vorlage"
area = t[pos + 256:pos + 256 + cap]
assert area.count(0) == cap, "reservierter Bereich ist nicht leer"
head = bytearray(t[:pos + 256])
struct.pack_into("<I", head, pos + 32, 2)            # Version 2 = kompakt (Bereich entfernt)
open(dst, "wb").write(bytes(head) + t[pos + 256 + cap:])
print(f"{dst}: {len(t) / 1e6:.2f} MB -> {(len(t) - cap) / 1e6:.2f} MB")
