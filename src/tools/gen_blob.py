#!/usr/bin/env python3
# Creates a C++ source that includes a binary file with .incbin (symbols <name>_b / <name>_e).
# usage: gen_blob.py <out.cpp> <name> <file>
import sys, os
out, name, path = sys.argv[1:4]
p = os.path.abspath(path).replace("\\", "/")
with open(out, "w") as f:
    f.write('#ifdef _WIN32\n#define NK_SECTION ".section .rdata,\\"dr\\"\\n"\n#else\n#define NK_SECTION ".section .rodata\\n"\n#endif\n')
    f.write(f'asm(NK_SECTION ".balign 64\\n" ".globl {name}_b\\n" "{name}_b:\\n" ".incbin \\"{p}\\"\\n" ".globl {name}_e\\n" "{name}_e:\\n" ".text\\n");\n')
