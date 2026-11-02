#pragma once
// Nuked SC-55 Poly – plugin editor (Windows: Win32/GDI; Linux and macOS: the same code on X11 / Cocoa through
// win32_shim.h)
//
// Shows the device's front panel: firmware-driven LCD, panel switches, GAIN knob,
// a polyphony meter (VOICES / UNITS) and a setup menu (max. polyphony, GS reset,
// all notes off). All plugin data is read through atomics.

class NukedSc55;

namespace editor {

constexpr int Width  = 918; // 88emu panel art (SC-55, SC-88, SC-88 Pro, SC-8850)
constexpr int Height = 280;

// Opaque editor handle stored in NukedSc55::editor
void* Create(NukedSc55* plugin, const char* display_name);
bool Open(void* ed, void* parent_hwnd); // Linux: parent = the host's X11 window id; macOS: the host's NSView
void Close(void* ed);
void Destroy(void* ed);
void Show(void* ed, bool visible);

#ifdef NUKED_SC55_X11_EDITOR
// Linux: handles the pending window events, timers and paints (CLAP timer / fd callbacks on the main thread);
// IdleFd: file descriptor of the X connection to watch (-1 before Open).
void Idle(void* ed);
int IdleFd(void* ed);
#endif

// Test helper: render one frame into a 32-bit BGRA buffer (Width*Height).
bool RenderToBuffer(void* ed, unsigned int* pixels);

} // namespace editor
