#!/usr/bin/env python3
# Erzeugt eine C++-Quelle, die ROM-Dateien per .incbin in die Binary einbettet.
# Aufruf: gen_embedded_roms.py <out.cpp> <romset-enum> <name> <loc>=<datei> ...
#    oder: gen_embedded_roms.py --slot <out.cpp> <romset-enum> <name> <kapazitaet>
#          (ROM-lose Vorlage: reservierter Bereich, den tools/p256_builder befuellt)
import sys, os, struct
if sys.argv[1] == "--slot":
    out, romset, name, cap = sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
    blob = os.path.splitext(out)[0] + "_slot.bin"
    # Kopf (256 Byte): Kennung, Version, Kapazitaet, Anzahl, befuellt, 8 Eintraege, Name; dann Datenbereich
    head = b"NUKED-P256-ROMSLOT".ljust(32, b"\0") + struct.pack("<4I", 1, cap, 0, 0)
    head += b"\0" * 128 + b"(empty template)".ljust(64, b"\0")
    head = head.ljust(256, b"\0")
    with open(blob, "wb") as f:
        f.write(head); f.write(b"\0" * cap)
    path = os.path.abspath(blob).replace("\\", "/")
    with open(out, "w") as f:
        f.write(f"""// ROM-lose Vorlage: ROMs werden nachtraeglich in den Bereich nk_romslot geschrieben
// (tools/p256_builder). Der Inhalt ist fuer den Compiler unbekannt (.incbin).
#include "embedded_roms.h"
#include <cstdint>
#ifdef _WIN32
#define NK_SECTION ".section .rdata,\\"dr\\"\\n"
#else
#define NK_SECTION ".section .rodata\\n"
#endif
asm(NK_SECTION ".balign 64\\n" ".globl nk_romslot_b\\n" "nk_romslot_b:\\n" ".incbin \\"{path}\\"\\n" ".text\\n");
extern "C" const uint8_t nk_romslot_b[] asm("nk_romslot_b");
namespace {{
struct SlotEntry {{ uint32_t location, offset, size, reserved; }};
struct SlotHeader {{ char magic[32]; uint32_t version, capacity, count, filled; SlotEntry e[8]; char name[64]; }};
const SlotHeader& Head() {{ return *reinterpret_cast<const SlotHeader*>(nk_romslot_b); }}
EmbeddedRom Entry(int k)
{{
	const SlotHeader& h = Head();
	if (!h.filled || k >= static_cast<int>(h.count)) return {{0, nullptr, nullptr}};
	const uint8_t* data = nk_romslot_b + 256;
	return {{h.e[k].location, data + h.e[k].offset, data + h.e[k].offset + h.e[k].size}};
}}
}} // namespace
const EmbeddedRom g_embedded_roms[8] = {{Entry(0), Entry(1), Entry(2), Entry(3), Entry(4), Entry(5), Entry(6), Entry(7)}};
const int g_embedded_rom_count = Head().filled ? static_cast<int>(Head().count) : 0;
const int g_embedded_romset    = {romset};
const char g_embedded_romset_name[] = "{name} (P256 builder)";
""")
    sys.exit(0)
out, romset, name, *pairs = sys.argv[1:]
asm, decl, table = [], [], []
for k, p in enumerate(pairs):
    loc, path = p.split("=", 1)
    path = os.path.abspath(path).replace("\\", "/")
    asm += [f".balign 64", f".globl nk_rom{k}_b", f"nk_rom{k}_b:",
            f'.incbin "{path}"', f".globl nk_rom{k}_e", f"nk_rom{k}_e:"]
    decl += [f'extern "C" const uint8_t nk_rom{k}_b[] asm("nk_rom{k}_b");',
             f'extern "C" const uint8_t nk_rom{k}_e[] asm("nk_rom{k}_e");']
    table.append(f"\t{{{loc}, nk_rom{k}_b, nk_rom{k}_e}},")
with open(out, "w") as f:
    f.write('#include "embedded_roms.h"\n\n')
    f.write('#ifdef _WIN32\n#define NK_SECTION ".section .rdata,\\"dr\\"\\n"\n#else\n#define NK_SECTION ".section .rodata\\n"\n#endif\n')
    f.write("asm(NK_SECTION\n" + "".join(f'    "{l.replace(chr(34), chr(92)+chr(34))}\\n"\n' for l in asm) + '    ".text\\n");\n\n')
    f.write("\n".join(decl) + "\n\n")
    f.write("const EmbeddedRom g_embedded_roms[] = {\n" + "\n".join(table) + "\n};\n")
    f.write(f"const int g_embedded_rom_count = {len(pairs)};\n")
    f.write(f"const int g_embedded_romset = {romset};\n")
    f.write(f'const char g_embedded_romset_name[] = "{name}";\n')
