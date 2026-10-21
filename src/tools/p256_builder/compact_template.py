#!/usr/bin/env python3
# Makes a ROM-free template compact: cuts out the reserved ROM area (which consists of zeros only)
# and marks the header as version 2. p256_builder.py inserts the ROMs at this place again when
# building. A compact template is not a loadable DLL.
# usage: compact_template.py <template.dll> <target.p256tpl>
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src, "rb").read()
pos = t.find(b"NUKED-P256-ROMSLOT")
assert pos >= 0 and t.find(b"NUKED-P256-ROMSLOT", pos + 1) < 0, "ROM area not unique"
ver, cap, cnt, filled = struct.unpack_from("<4I", t, pos + 32)
assert ver == 1 and filled == 0, "not an empty template"
area = t[pos + 256:pos + 256 + cap]
assert area.count(0) == cap, "reserved area is not empty"
head = bytearray(t[:pos + 256])
struct.pack_into("<I", head, pos + 32, 2)            # version 2 = compact (area removed)
open(dst, "wb").write(bytes(head) + t[pos + 256 + cap:])
print(f"{dst}: {len(t) / 1e6:.2f} MB -> {(len(t) - cap) / 1e6:.2f} MB")
