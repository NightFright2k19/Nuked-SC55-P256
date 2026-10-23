// Nuked SC-55 Poly – plugin editor, Win32/GDI implementation.
// Front panels from the 88emu player art (SC-55/SC-55mkII, SC-88, SC-88 Pro, SC-8850) with
// the firmware-driven LCD on top, plus the polyphony (VOICES / UNITS) display.

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "editor.h"

// Window class name of the editor (one per editor instance)
#if defined(NUKED_SC55_DEVICE_8850)
#define NUKED_SC55_WNDCLASS_PREFIX "NukedSC8850P256Editor_"
#elif defined(NUKED_SC55_DEVICE_8820)
#define NUKED_SC55_WNDCLASS_PREFIX "NukedSC8820P256Editor_"
#elif defined(NUKED_SC55_DEVICE_LA)
#define NUKED_SC55_WNDCLASS_PREFIX "NukedMT32P256Editor_"
#elif defined(NUKED_SC55_DEVICE_88)
#define NUKED_SC55_WNDCLASS_PREFIX "NukedSC88oP256Editor_"
#elif defined(NUKED_SC55_ENGINE_88PRO)
#define NUKED_SC55_WNDCLASS_PREFIX "NukedSC88P256Editor_"
#else
#define NUKED_SC55_WNDCLASS_PREFIX "NukedSC55P256Editor_"
#endif

// Character ROM of the LCD controller (nuked-sc55/backend/lcd_font.h, defined in lcd.cpp)
extern unsigned char lcd_font[240][10];
#include "nuked_sc55.h"

namespace editor {

namespace {

// --- Instrument names (General MIDI level 1 standard names) ---------------
const char* const GM_NAMES[128] = {
    "Piano 1", "Piano 2", "Piano 3", "Honky-tonk", "E.Piano 1", "E.Piano 2",
    "Harpsichord", "Clav.", "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
    "Marimba", "Xylophone", "Tubular-bell", "Santur", "Organ 1", "Organ 2",
    "Organ 3", "Church Org.1", "Reed Organ", "Accordion Fr", "Harmonica",
    "Bandneon", "Nylon-str.Gt", "Steel-str.Gt", "Jazz Gt.", "Clean Gt.",
    "Muted Gt.", "Overdrive Gt", "DistortionGt", "Gt.Harmonics", "Acoustic Bs.",
    "Fingered Bs.", "Picked Bs.", "Fretless Bs.", "Slap Bass 1", "Slap Bass 2",
    "Synth Bass 1", "Synth Bass 2", "Violin", "Viola", "Cello", "Contrabass",
    "Tremolo Str", "PizzicatoStr", "Harp", "Timpani", "Strings", "Slow Strings",
    "Syn.Strings1", "Syn.Strings2", "Choir Aahs", "Voice Oohs", "SynVox",
    "OrchestraHit", "Trumpet", "Trombone", "Tuba", "MutedTrumpet", "French Horn",
    "Brass 1", "Synth Brass1", "Synth Brass2", "Soprano Sax", "Alto Sax",
    "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Bottle Blow", "Shakuhachi",
    "Whistle", "Ocarina", "Square Wave", "Saw Wave", "Syn.Calliope",
    "Chiffer Lead", "Charang", "Solo Vox", "5th Saw Wave", "Bass & Lead",
    "Fantasia", "Warm Pad", "Polysynth", "Space Voice", "Bowed Glass",
    "Metal Pad", "Halo Pad", "Sweep Pad", "Ice Rain", "Soundtrack", "Crystal",
    "Atmosphere", "Brightness", "Goblin", "Echo Drops", "Star Theme", "Sitar",
    "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko", "Melo. Tom 1",
    "Synth Drum", "Reverse Cym.", "Gt.FretNoise", "Breath Noise", "Seashore",
    "Bird", "Telephone 1", "Helicopter", "Applause", "Gun Shot"};

const char* DrumKitName(int prog)
{
    switch (prog) {
    case 0: return "STANDARD";
    case 8: return "ROOM";
    case 16: return "POWER";
    case 24: return "ELECTRONIC";
    case 25: return "TR-808";
    case 32: return "JAZZ";
    case 40: return "BRUSH";
    case 48: return "ORCHESTRA";
    case 56: return "SFX";
    case 127: return "CM-64/32L";
    default: return "DRUM SET";
    }
}

// --- Language (menus follow the Windows UI language) -----------------------
bool IsGerman()
{
    static const bool de = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN;
    return de;
}
const wchar_t* T(const wchar_t* en, const wchar_t* de) { return IsGerman() ? de : en; }

struct Editor {
    NukedSc55* plugin = nullptr;
    char name[64]     = {};
    HWND hwnd         = nullptr;
    HFONT f_title = nullptr, f_sub = nullptr, f_small = nullptr, f_label = nullptr,
          f_value = nullptr, f_big = nullptr, f_btn = nullptr, f_tiny = nullptr;

    HDC bg_dc       = nullptr;  // cached static background (texture, ears, ...)
    HBITMAP bg_bmp  = nullptr;
    HGDIOBJ bg_old  = nullptr;

    float disp[16]  = {};
    int part        = 0;     // selected part (0-15; SC-88 Pro 0-31 = A01-B16)
    int lcd_part    = -1;    // SC-88 Pro: part last shown by the firmware LCD
    int hover_btn   = 0;
    int flash[12]   = {};    // button LED flash frames
    void* lcd_cache = nullptr; // lcdview::Cache of this editor (incremental LCD rendering)
    bool drag_vol     = false; // GAIN knob being turned
    int drag_x = 0, drag_y = 0;
    float drag_v0     = 1.0f;
    bool preview_down = false; // PREVIEW held (SC-88 Pro)
    uint32_t held85 = 0;       // panel switches held with the mouse (bit = 88emu / MCU button)
    bool release85  = false;   // mouse released, switches let go after the minimum hold
    DWORD press85   = 0;       // tick of the press
    uint32_t latch85 = 0;      // switches held by a right click (key combinations); let go with the next left-click switch
#ifdef NUKED_SC55_DEVICE_8850
    int push85      = 0;       // VALUE push pulse, timer frames left
    int value85     = 0;       // VALUE knob position (detents; 4 knurl phases)
    bool drag85     = false, moved85 = false; // VALUE being turned / moved since the press
    int steps85     = 0;       // detents passed on during this drag
    void* lcd85     = nullptr; // graphic LCD renderer
#endif
#ifdef NUKED_SC55_DEVICE_8820
    int bg_style       = -1;   // interface style the background was built for (0 Basic, 1 Pro)
    float disp32[32]   = {};   // Pro: 16-part level display (both ports)
    int pro_btn        = 0;    // edit button held (1..8), auto-repeat
    DWORD pro_btn_t    = 0;
    int pro_drag       = 0;    // value dragged (field + 1)
    int pro_drag_y     = 0, pro_drag_v0 = 0, pro_drag_last = 0;
    int pend_val[32][7]     = {}; // values just sent (PROG, MSB, LSB, LEVEL, PAN, REVERB, CHORUS) ...
    DWORD pend_until[32][7] = {}; // ... shown and stepped from until the firmware's own values follow
    HFONT f_pro        = nullptr;
    int d70_mode       = 0;    // SC-D70: display shows 0 part, 1 instrument, 2 variation
    int d70_btn        = 0;    // SC-D70: PART / INST / DEC / INC held (1..4), DEC / INC auto-repeat
    DWORD d70_btn_t    = 0;
    uint32_t d70_midi  = 0;    // SC-D70: MIDI message count last seen, lamp lit until d70_midi_until
    DWORD d70_midi_until = 0;
    float d70_lv[2]    = {-120.0f, -120.0f}; // SC-D70: level meters (dBFS, falling)
    HFONT f_d70v = nullptr, f_d70o = nullptr, f_d70u = nullptr, f_d70s = nullptr;
#endif
#ifdef NUKED_SC55_DEVICE_LA
    uint8_t la_dots[120 * 9] = {}; // MT-32 LCD dots as last read (NukedSc55::GetLaDots)
    bool drag_sel      = false;  // knob turned as the device's SELECT (right-drag)
    HFONT f_lcd        = nullptr;
#endif
    bool menu_open  = false;
    float peak      = 0;     // polyphony peak hold
    int peak_hold   = 0;     // frames
};

HINSTANCE ModuleInstance()
{
    HMODULE hm = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&ModuleInstance), &hm);
    return hm;
}

HFONT MakeFont(int px, int weight, const char* face)
{
    return CreateFontA(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, face);
}

void Fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r{x, y, x + w, y + h};
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void Text(HDC dc, HFONT f, COLORREF c, int x, int y, const char* s, UINT align = DT_LEFT,
          int w = 400, int h = 40)
{
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT r{x, y, x + w, y + h};
    DrawTextA(dc, s, -1, &r, align | DT_SINGLELINE | DT_NOPREFIX | DT_TOP);
}

#include "lcd_render.inc"

// LCD of unit 0 (text fields, L/R) with the 16x16 matrix OR-ed over all awake units: every
// unit only shows the level bars of the notes it plays; bars grow from the bottom, so the OR
// is the per-part maximum. Bitmap messages reach every unit identically.
bool GatherLcd(NukedSc55& p, lcdview::View& view)
{
    NukedSc55::LcdSnapshot snap;
    if (!p.GetLcd(0, snap)) return false;
    std::memcpy(view.dd, snap.dd, sizeof(view.dd));
    std::memcpy(view.cg, snap.cg, sizeof(view.cg));
    view.on = snap.on;
    lcdview::Matrix(snap.dd, snap.cg, view.matrix);
    const uint64_t mask = p.ui_awake_mask.load();
    for (int u = 1; u < p.NumInstances() && u < 64; ++u) {
        if (!(mask & (uint64_t{1} << u)) || !p.GetLcd(u, snap)) continue;
        uint16_t m[16];
        lcdview::Matrix(snap.dd, snap.cg, m);
        for (int r = 0; r < 16; ++r) view.matrix[r] |= m[r];
    }
    return true;
}

#include "editor_sc88.inc" // shared helpers (GAIN knob, VOICES, text boxes)
#ifndef NUKED_SC55_ENGINE_88PRO
#include "editor_sc55.inc"
#endif
#ifdef NUKED_SC55_DEVICE_8850
#include "editor_sc8850.inc"
#endif
#if defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
#include "editor_sc88orig.inc"
#endif
#ifdef NUKED_SC55_DEVICE_8820
#include "editor_sc8820.inc"
#endif
#ifdef NUKED_SC55_DEVICE_LA
#include "editor_mt32.inc"
#endif

void Draw(Editor& e, HDC dc)
{
    // The sprite sheets and lamp images are built once and shared by all editors of the plugin
    // (memory DCs, which GDI lets only one thread use at a time). Hosts that run editors on
    // different threads therefore paint one editor after the other (issue #63).
    static std::mutex shared_gdi;
    std::lock_guard lock(shared_gdi);
#if defined(NUKED_SC55_DEVICE_8850)
    Draw8850(e, dc);
#elif defined(NUKED_SC55_DEVICE_8820)
    Draw8820(e, dc);
#elif defined(NUKED_SC55_DEVICE_LA)
    DrawMt32(e, dc);
#elif defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
    Draw88o(e, dc);
#else
    Draw55(e, dc);
#endif
}

void DecayBars(Editor& e)
{
#ifdef NUKED_SC55_DEVICE_8820
    if (e.plugin->ui_style.load() == 1) {
        for (int c = 0; c < 32; ++c) { // all 32 parts (both ports) for the Pro's level display
            const uint8_t hit = e.plugin->ui_parts[c].hit.exchange(0);
            e.disp32[c]       = std::max(e.disp32[c] - 7.0f, 0.0f);
            if (hit > e.disp32[c]) e.disp32[c] = hit;
        }
        return;
    }
#endif
    for (int& f : e.flash) if (f > 0) --f;
    for (int c = 0; c < 16; ++c) {
        const uint8_t hit = e.plugin->ui_parts[c].hit.exchange(0);
        e.disp[c]         = std::max(e.disp[c] - 7.0f, 0.0f);
        if (hit > e.disp[c]) e.disp[c] = hit;
    }
}

int HitButton([[maybe_unused]] const Editor& e, int x, int y)
{
#if defined(NUKED_SC55_DEVICE_8850)
    return Hit8850(x, y);
#elif defined(NUKED_SC55_DEVICE_8820)
    return Hit8820(e, x, y);
#elif defined(NUKED_SC55_DEVICE_LA)
    return HitMt32(e, x, y);
#elif defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
    return Hit88o(x, y);
#else
    return Hit55(x, y);
#endif
}

#ifdef NUKED_SC55_ENGINE_88PRO
// "Prevw Note" submenu C-1 .. G9 (Roland numbering, C4 = 60) and the text of its Setup menu entry
HMENU BuildPreviewMenu(NukedSc55& p, wchar_t* label, size_t label_len)
{
    HMENU pv_menu = CreatePopupMenu();
    {
        const int pn = p.preview_note.load();
        for (int oct = 0; oct <= 10; ++oct) {
            HMENU sub = CreatePopupMenu();
            for (int k = 0; k < 12 && oct * 12 + k <= 127; ++k) {
                char nb[8];
                wchar_t wb[16];
                const int n = oct * 12 + k;
                std::swprintf(wb, 16, L"%hs", NoteName88(n, nb, sizeof(nb)));
                AppendMenuW(sub, MF_STRING | (n == pn ? MF_CHECKED : 0), 300 + n, wb);
            }
            wchar_t ob[16];
            std::swprintf(ob, 16, L"C%d .. B%d", oct - 1, oct - 1);
            if (oct == 10) std::swprintf(ob, 16, L"C9 .. G9");
            AppendMenuW(pv_menu, MF_POPUP | ((pn / 12) == oct ? MF_CHECKED : 0), (UINT_PTR)sub, ob);
        }
    }
    char nb[8];
    std::swprintf(label, label_len, L"%ls: %hs", T(L"Preview note (Prevw Note)", L"Preview-Note (Prevw Note)"),
                  NoteName88(p.preview_note.load(), nb, sizeof(nb)));
    return pv_menu;
}
#endif

void ShowSetupMenu(Editor& e)
{
    NukedSc55& p   = *e.plugin;
    const int cap  = p.PartialsPerInstance();
    const int maxs = p.MaxSelectableVoices();
    const int cur  = p.max_voices.load();

    const int base_mk1[] = {24, 48, 64, 96, 128, 160, 192, 224, 256};
    const int base_mk2[] = {28, 56, 64, 96, 128, 160, 192, 224, 256};
    const int* opts      = p.IsMk2() ? base_mk2 : base_mk1;
#ifdef NUKED_SC55_ENGINE_88PRO
    const int base_88pro[] = {64, 128, 192, 256, 256, 256, 256, 256, 256}; // units of 64
    opts = base_88pro;
#endif
#ifdef NUKED_SC55_DEVICE_8850
    const int base_8850[] = {128, 256, 256, 256, 256, 256, 256, 256, 256}; // units of 128
    opts = base_8850;
#endif
#ifdef NUKED_SC55_DEVICE_LA
    const int base_la[] = {32, 64, 64, 64, 64, 64, 64, 64, 64}; // units of 32
    opts = base_la;
#endif

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Max. polyphony", L"Max. Polyphonie"));
    for (int i = 0; i < 9; ++i) {
        const int v = std::min(opts[i], maxs);
        if (i > 0 && v <= opts[i - 1] && opts[i] > maxs) break;
        const int units = (v + cap - 1) / cap;
#ifdef NUKED_SC55_ENGINE_88PRO
        if (i > 0 && v == std::min(opts[i - 1], maxs)) break; // no duplicates
#endif
        wchar_t s[128];
#ifdef NUKED_SC55_DEVICE_LA
        const wchar_t* dflt = units == 1 ? T(L", default", L", Standard") : L""; // one unit, like the device
#else
        const wchar_t* dflt = L"";
#endif
        if (IsGerman())
            swprintf(s, 128, L"   %d Stimmen\t(%d %ls, %d Partials%ls)", v, units,
                     units == 1 ? L"Einheit" : L"Einheiten", units * cap, dflt);
        else
            swprintf(s, 128, L"   %d voices\t(%d %ls, %d partials%ls)", v, units,
                     units == 1 ? L"unit" : L"units", units * cap, dflt);
        const bool checked = (cur + cap - 1) / cap == units;
        AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : 0), 100 + v, s);
    }
#ifdef NUKED_SC55_DEVICE_88PRO
    {
        const int tm = p.tone_map.load();
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Tone map (all parts)", L"Klang-Map (alle Parts)"));
        AppendMenuW(m, MF_STRING | (tm == 0 ? MF_CHECKED : 0), 200, L"   SC-55 Map");
        AppendMenuW(m, MF_STRING | (tm == 1 ? MF_CHECKED : 0), 201, L"   SC-88 Map");
        AppendMenuW(m, MF_STRING | (tm == 2 ? MF_CHECKED : 0), 202,
                    T(L"   SC-88 Pro Map\t(default)", L"   SC-88 Pro Map\t(Standard)"));
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        wchar_t pl[96];
        HMENU pv_menu = BuildPreviewMenu(p, pl, 96);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)pv_menu, pl);
    }
#endif
#ifdef NUKED_SC55_DEVICE_8820
    {   // tone map (all parts): chosen here or with the INST MAP button, stored in the state
        const int tm = p.tone_map.load();
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Tone map (all parts)", L"Klang-Map (alle Parts)"));
        AppendMenuW(m, MF_STRING | (tm == 0 ? MF_CHECKED : 0), 200, L"   SC-55 Map");
        AppendMenuW(m, MF_STRING | (tm == 1 ? MF_CHECKED : 0), 201, L"   SC-88 Map");
        AppendMenuW(m, MF_STRING | (tm == 2 ? MF_CHECKED : 0), 202, L"   SC-88 Pro Map");
        AppendMenuW(m, MF_STRING | (tm == 3 ? MF_CHECKED : 0), 203,
                    T(L"   SC-8820 Map\t(default)", L"   SC-8820 Map\t(Standard)"));
    }
#endif
#ifdef NUKED_SC55_DEVICE_8850
    {   // tone map (all parts), as on the SC-8820; the panel's INST MAP still sets it per part
        const int tm = p.tone_map.load();
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Tone map (all parts)", L"Klang-Map (alle Parts)"));
        AppendMenuW(m, MF_STRING | (tm == 0 ? MF_CHECKED : 0), 200, L"   SC-55 Map");
        AppendMenuW(m, MF_STRING | (tm == 1 ? MF_CHECKED : 0), 201, L"   SC-88 Map");
        AppendMenuW(m, MF_STRING | (tm == 2 ? MF_CHECKED : 0), 202, L"   SC-88 Pro Map");
        AppendMenuW(m, MF_STRING | (tm == 3 ? MF_CHECKED : 0), 203,
                    T(L"   SC-8850 Map\t(default)", L"   SC-8850 Map\t(Standard)"));
    }
#endif
#ifdef NUKED_SC55_DEVICE_88
    {   // tone map (all parts), as on the SC-88 Pro; the panel's INST MAP still sets it per part
        const int tm = p.tone_map.load();
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Tone map (all parts)", L"Klang-Map (alle Parts)"));
        AppendMenuW(m, MF_STRING | (tm == 0 ? MF_CHECKED : 0), 200, L"   SC-55 Map");
        AppendMenuW(m, MF_STRING | (tm == 1 ? MF_CHECKED : 0), 201,
                    T(L"   SC-88 Map\t(default)", L"   SC-88 Map\t(Standard)"));
    }
#endif
#ifdef NUKED_SC55_DEVICE_LA
    {   // device (issue #69): the units are booted again as the chosen one; the panel follows.
        // The version is the firmware the plugin was built with; devices without ROMs are greyed.
        const int dev = p.la_device.load();
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Device", L"Ger\u00e4t"));
        const wchar_t* names[NukedSc55::kLaDevices] = {T(L"MT-32 (old", L"MT-32 (alt"), T(L"MT-32 (new", L"MT-32 (neu"),
                                                       L"CM-32L ("};
        for (int d = 0; d < NukedSc55::kLaDevices; ++d) {
            wchar_t s[96];
            const bool have = p.LaAvailable(d);
            std::swprintf(s, 96, L"   %ls%ls%hs)%ls", names[d], have ? (d == 2 ? L"v" : L", v") : L"", p.LaVersion(d),
                          d == 2 ? T(L"\t(default)", L"\t(Standard)") : L"");
            if (!have && d == 2) std::swprintf(s, 96, L"   CM-32L%ls", T(L"\t(default)", L"\t(Standard)"));
            AppendMenuW(m, MF_STRING | (dev == d ? MF_CHECKED : 0) | (have ? 0 : MF_GRAYED), 30 + d, s);
        }
    }
#endif
#if defined(NUKED_SC55_DEVICE_8820) || defined(NUKED_SC55_DEVICE_8850)
    {   // PREVIEW (right click on the VOLUME knob / PREVIEW knob) plays this note, as on the SC-88 Pro
        wchar_t pl[96];
        HMENU pv_menu = BuildPreviewMenu(p, pl, 96);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)pv_menu, pl);
    }
#endif
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    // "Send Reset": choosing an entry sends that reset at once (no mark, nothing stored)
    HMENU rt_menu = CreatePopupMenu();
#ifdef NUKED_SC55_DEVICE_LA
    {   // MT-32 / CM-32L: the firmware's "All parameters reset" (as after power-on)
        AppendMenuW(rt_menu, MF_STRING, 11, T(L"All parameters reset", L"Alle Parameter zur\u00fccksetzen"));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)rt_menu, T(L"Send Reset", L"Reset senden"));
    }
#else
    {
        AppendMenuW(rt_menu, MF_STRING, 10, L"MT-32");
        AppendMenuW(rt_menu, MF_STRING, 6, L"GM");
        AppendMenuW(rt_menu, MF_STRING, 2, L"GS");
        if (p.HasXg()) AppendMenuW(rt_menu, MF_STRING, 12, L"XG");
        if (p.HasGm2()) AppendMenuW(rt_menu, MF_STRING, 7, L"GM2");
        AppendMenuW(m, MF_POPUP, (UINT_PTR)rt_menu, T(L"Send Reset", L"Reset senden"));
    }
#endif
    if (p.HasSystemMode()) { // SC-88 / SC-88 Pro: System Mode Set (default Mode 1 = factory setting)
        HMENU sm_menu = CreatePopupMenu();
        const int sm  = p.system_mode.load();
        AppendMenuW(sm_menu, MF_STRING | (sm == 0 ? MF_CHECKED : 0), 8, L"Mode 1 (Single Module)");
        AppendMenuW(sm_menu, MF_STRING | (sm == 1 ? MF_CHECKED : 0), 9, L"Mode 2 (Double Module)");
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sm_menu, T(L"System Mode", L"System-Modus"));
    }
    AppendMenuW(m, MF_STRING, 1, T(L"All notes off", L"Alle Noten aus"));
#ifdef NUKED_SC55_DEVICE_8820
    {   // interface style (issue #67): the device's panel or the Pro panel, stored in the state
        const int st = p.ui_style.load();
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Interface Style", L"Oberfl\u00e4chen-Stil"));
        AppendMenuW(m, MF_STRING | (st != 2 ? MF_CHECKED : 0), 21,
                    T(L"   SC-8820\t(default)", L"   SC-8820\t(Standard)")); // issue #87: Basic removed, Pro renamed
        AppendMenuW(m, MF_STRING | (st == 2 ? MF_CHECKED : 0), 22, L"   SC-D70");
    }
#endif

    RECT r;
    GetWindowRect(e.hwnd, &r);
    e.menu_open = true;
    InvalidateRect(e.hwnd, nullptr, FALSE);
#if defined(NUKED_SC55_DEVICE_8850)
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                                   r.left + E85_SETUP.right, r.top + E85_SETUP.top - 2, 0, e.hwnd, nullptr);
#elif defined(NUKED_SC55_DEVICE_8820)
    // SC-D70: SETUP sits at the top of the panel, the menu opens downwards
    const int cmd = Style20(e) == 2
                            ? TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, r.left + Setup20(e).right,
                                             r.top + Setup20(e).bottom + 2, 0, e.hwnd, nullptr)
                            : TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, r.left + Setup20(e).right,
                                             r.top + Setup20(e).top - 2, 0, e.hwnd, nullptr);
#elif defined(NUKED_SC55_DEVICE_LA)
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                                   r.left + ELA_SETUP.right, r.top + ELA_SETUP.top - 2, 0, e.hwnd, nullptr);
#elif defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                                   r.left + E88O_SETUP.left, r.top + E88O_SETUP.top - 2, 0, e.hwnd, nullptr);
#else
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                                   r.left + E55_SETUP.right, r.top + E55_SETUP.top - 2, 0, e.hwnd, nullptr);
#endif
    e.menu_open = false;
    DestroyMenu(m);

    if (cmd >= 300 && cmd <= 427) {
#ifdef NUKED_SC55_ENGINE_88PRO
        p.preview_note = cmd - 300; // stored in the plugin state
        p.NotifyStateChanged();
#endif
    } else if (cmd >= 200 && cmd <= 200 + NukedSc55::kMaxToneMap) {
        p.tone_map = cmd - 200; // switched like the panel buttons, stored in the state
        p.NotifyStateChanged();
    } else if (cmd >= 100) {
        p.SetMaxVoices(cmd - 100); // stored in the plugin state by the host
        p.NotifyStateChanged();
    } else if (cmd == 1 || cmd == 2 || cmd == 6 || cmd == 7 || cmd == 10 || cmd == 11 || cmd == 12) { // all notes off, resets
        p.ui_command = cmd;
#ifdef NUKED_SC55_DEVICE_LA
    } else if (cmd >= 30 && cmd < 30 + NukedSc55::kLaDevices) { // device, stored in the state
        p.la_device = cmd - 30;
        p.NotifyStateChanged();
#endif
#ifdef NUKED_SC55_DEVICE_8820
    } else if (cmd >= 21 && cmd < 20 + NukedSc55::kNumStyles8820) { // interface style (1 SC-8820, 2 SC-D70), stored in the state
        p.ui_style = cmd - 20;
        p.NotifyStateChanged();
#endif
    } else if (cmd == 8 || cmd == 9) { // system mode, stored in the state
        p.system_mode = cmd - 8;
        p.ui_command  = cmd;
        p.NotifyStateChanged();
    }
}

const char* ClassName()
{
    static const std::string name = [] {
        char buf[64] = {};
        std::snprintf(buf, sizeof(buf), NUKED_SC55_WNDCLASS_PREFIX "%p", (void*)ModuleInstance());
        return std::string(buf);
    }();
    return name.c_str();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* e = reinterpret_cast<Editor*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
#ifdef NUKED_SC55_DEVICE_8850
    if (e && Mouse8850(*e, hwnd, msg, wp, lp)) return 0;
#endif
#ifdef NUKED_SC55_DEVICE_8820
    if (e && Mouse8820(*e, hwnd, msg, wp, lp)) return 0;
#endif
#ifdef NUKED_SC55_DEVICE_LA
    if (e && MouseMt32(*e, hwnd, msg, wp, lp)) return 0;
#endif
#if defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
    if (e && Mouse88o(*e, hwnd, msg, wp, lp)) return 0;
#endif
#ifndef NUKED_SC55_ENGINE_88PRO
    if (e && Mouse55(*e, hwnd, msg, wp, lp)) return 0;
#endif
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        SetTimer(hwnd, 1, 33, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (e) {
            DecayBars(*e);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (e) {
            HDC mem    = CreateCompatibleDC(dc);
            HBITMAP bm = CreateCompatibleBitmap(dc, Width, Height);
            HGDIOBJ ob = SelectObject(mem, bm);
            Draw(*e, mem);
            BitBlt(dc, 0, 0, Width, Height, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bm);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (e && e->drag_vol) { // right / up = louder, left / down = quieter
            const int dx = (short)LOWORD(lp) - e->drag_x, dy = (short)HIWORD(lp) - e->drag_y;
            // 200 px = full range; snaps to 0 dB within 0.4 dB so unity is easy to find
            const float span = NukedSc55::kGainMaxDb - NukedSc55::kGainMinDb;
            float g = std::clamp(e->drag_v0 + float(dx - dy) / 200.0f * span, NukedSc55::kGainMinDb,
                                 NukedSc55::kGainMaxDb);
            if (std::fabs(g) < 0.4f) g = 0.0f;
            e->plugin->gain_db = std::round(g * 10.0f) / 10.0f; // state stores 0.1 dB steps
            return 0;
        }
        if (e) {
            const int b = HitButton(*e, (short)LOWORD(lp), (short)HIWORD(lp));
            if (b != e->hover_btn) {
                e->hover_btn = b;
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (e) e->hover_btn = 0;
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (e && e->drag_vol) {
            e->drag_vol = false;
            if (msg == WM_LBUTTONUP) ReleaseCapture();
            e->plugin->NotifyStateChanged(); // GAIN is stored in the plugin state
        }
#ifdef NUKED_SC55_ENGINE_88PRO
        if (e && e->preview_down) {
            e->preview_down        = false;
            e->plugin->ui_preview = 2; // note off
            if (msg == WM_LBUTTONUP) ReleaseCapture();
        }
#endif
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

bool RegisterClassOnce()
{
    WNDCLASSA wc{};
    if (GetClassInfoA(ModuleInstance(), ClassName(), &wc)) return true;
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = ModuleInstance();
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = ClassName();
    return RegisterClassA(&wc) != 0;
}

} // namespace

void* Create(NukedSc55* plugin, const char* display_name)
{
    auto* e   = new Editor();
    e->plugin = plugin;
    std::snprintf(e->name, sizeof(e->name), "%s", display_name);
    e->f_title = MakeFont(28, FW_HEAVY, "Arial");
    e->f_sub   = MakeFont(11, FW_BOLD, "Arial");
    e->f_small = MakeFont(11, FW_SEMIBOLD, "Segoe UI");
    e->f_label = MakeFont(11, FW_BOLD, "Segoe UI");
    e->f_value = MakeFont(20, FW_BOLD, "Consolas");
    e->f_big   = MakeFont(30, FW_BOLD, "Consolas");
    e->f_btn   = MakeFont(13, FW_BOLD, "Segoe UI");
    e->f_tiny  = MakeFont(10, FW_BOLD, "Arial");
#ifdef NUKED_SC55_DEVICE_8820
    e->f_pro   = MakeFont(17, FW_BOLD, "Arial");
    e->f_d70v  = MakeFont(17, FW_BOLD, "Arial");   // SC-D70: voice count, "of N", units, SETUP
    e->f_d70o  = MakeFont(10, FW_NORMAL, "Arial");
    e->f_d70u  = MakeFont(14, FW_BOLD, "Arial");
    e->f_d70s  = MakeFont(12, FW_BOLD, "Arial");
#endif
#ifdef NUKED_SC55_DEVICE_LA
    e->f_lcd   = MakeFont(14, FW_BOLD, "Consolas");
#endif
    return e;
}

bool Open(void* ed, void* parent)
{
    auto* e = static_cast<Editor*>(ed);
    if (!e || e->hwnd) return e != nullptr;
    if (!RegisterClassOnce()) return false;
    e->hwnd = CreateWindowExA(0, ClassName(), "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                              0, 0, Width, Height, (HWND)parent, nullptr,
                              ModuleInstance(), e);
    return e->hwnd != nullptr;
}

void Close(void* ed)
{
    auto* e = static_cast<Editor*>(ed);
    if (e && e->hwnd) {
        DestroyWindow(e->hwnd);
        e->hwnd = nullptr;
    }
}

void Show(void* ed, bool visible)
{
    auto* e = static_cast<Editor*>(ed);
    if (e && e->hwnd) ShowWindow(e->hwnd, visible ? SW_SHOW : SW_HIDE);
}

void Destroy(void* ed)
{
    auto* e = static_cast<Editor*>(ed);
    if (!e) return;
    Close(e);
    for (HFONT f : {e->f_title, e->f_sub, e->f_small, e->f_label, e->f_value, e->f_big, e->f_btn, e->f_tiny})
        if (f) DeleteObject(f);
#ifdef NUKED_SC55_DEVICE_8820
    if (e->f_pro) DeleteObject(e->f_pro);
    for (HFONT f : {e->f_d70v, e->f_d70o, e->f_d70u, e->f_d70s}) if (f) DeleteObject(f);
#endif
#ifdef NUKED_SC55_DEVICE_LA
    if (e->f_lcd) DeleteObject(e->f_lcd);
#endif
    if (e->bg_dc) {
        SelectObject(e->bg_dc, e->bg_old);
        DeleteObject(e->bg_bmp);
        DeleteDC(e->bg_dc);
    }
    lcdview::FreeCache(e->lcd_cache);
#ifdef NUKED_SC55_DEVICE_8850
    FreeLcd85(e->lcd85);
#endif
    delete e;
}

bool RenderToBuffer(void* ed, unsigned int* pixels)
{
    auto* e = static_cast<Editor*>(ed);
    if (!e) return false;
    DecayBars(*e);
    HDC screen = GetDC(nullptr);
    HDC mem    = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = Width;
    bi.bmiHeader.biHeight      = -Height;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits                 = nullptr;
    HBITMAP bm = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(mem, bm);
    Draw(*e, mem);
    GdiFlush();
    std::memcpy(pixels, bits, (size_t)Width * Height * 4);
    SelectObject(mem, ob);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return true;
}

} // namespace editor

#endif // _WIN32
