#pragma once
// Interface to ROM images linked into the binary (single-file builds).
// The data itself is generated per build by tools/gen_embedded_roms.py.
#include <cstddef>
#include <cstdint>

struct EmbeddedRom {
	size_t location;          // RomLocation index
	const uint8_t* begin;
	const uint8_t* end;
};

extern const EmbeddedRom g_embedded_roms[];
extern const int g_embedded_rom_count;
extern const int g_embedded_romset;       // Romset enum value
extern const char g_embedded_romset_name[];
