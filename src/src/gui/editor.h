#pragma once
// Nuked SC-55 Poly – plugin editor (Windows, Win32/GDI)
//
// Shows an SC-55 style front panel: amber dot-matrix LCD with part info and
// 16 part level bars, a polyphony meter and a setup menu (max. polyphony,
// GS reset, all notes off). All plugin data is read through atomics.

class NukedSc55;

namespace editor {

#ifdef NUKED_SC55_ENGINE_88PRO
constexpr int Width  = 918; // SC-88 Pro panel (88emu art, half size)
constexpr int Height = 280;
#else
constexpr int Width  = 780;
constexpr int Height = 340;
#endif

// Opaque editor handle stored in NukedSc55::editor
void* Create(NukedSc55* plugin, const char* display_name);
bool Open(void* ed, void* parent_hwnd);
void Close(void* ed);
void Destroy(void* ed);
void Show(void* ed, bool visible);

// Test helper: render one frame into a 32-bit BGRA buffer (Width*Height).
bool RenderToBuffer(void* ed, unsigned int* pixels);

} // namespace editor
