// Nuked SC-55 Poly – plugin editor, Win32/GDI implementation.
// SC-55 inspired look (no manufacturer logos): dark housing, amber
// dot-matrix LCD with part parameters and 16 level bars, polyphony meter.

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "editor.h"

// Window class name of the editor (one per editor instance)
#ifdef NUKED_SC55_ENGINE_88PRO
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
    static int de = -1;
    if (de < 0) de = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN ? 1 : 0;
    return de == 1;
}
const wchar_t* T(const wchar_t* en, const wchar_t* de) { return IsGerman() ? de : en; }

// --- Colours ---------------------------------------------------------------
constexpr COLORREF C_LCD_TOP    = RGB(0xf4, 0xae, 0x46);
constexpr COLORREF C_LCD_BOT    = RGB(0xe2, 0x94, 0x2a);
constexpr COLORREF C_DOT_OFF    = RGB(0xd9, 0x8f, 0x2f);
constexpr COLORREF C_DOT_ON     = RGB(0x2c, 0x1c, 0x08);
constexpr COLORREF C_LCD_LABEL  = RGB(0x7a, 0x4c, 0x10);
constexpr COLORREF C_LCD_TEXT   = RGB(0x24, 0x16, 0x05);
constexpr COLORREF C_PRINT      = RGB(0xd8, 0xdb, 0xe0);
constexpr COLORREF C_PRINT_DIM  = RGB(0x8e, 0x92, 0x99);
constexpr COLORREF C_ORANGE     = RGB(0xf2, 0x6a, 0x1b);
constexpr COLORREF C_LED_OFF    = RGB(0x3a, 0x2a, 0x12);
constexpr COLORREF C_LED_ON     = RGB(0xff, 0xa8, 0x2e);
constexpr COLORREF C_LED_HOT    = RGB(0xff, 0x58, 0x30);
constexpr COLORREF C_BTNLED_OFF = RGB(0x4a, 0x12, 0x10);
constexpr COLORREF C_BTNLED_ON  = RGB(0xff, 0x3a, 0x22);

// --- Layout ----------------------------------------------------------------
constexpr int EAR    = 26;                       // rack ears left/right
constexpr int CX0    = EAR + 18, CX1 = Width - EAR - 18;
constexpr int LCD_X  = CX0 + 4, LCD_Y = 64, LCD_W = 553, LCD_H = 200; // original glass 741x268 scaled
constexpr int BAR_DOT = 5, BAR_GAP = 1, BAR_ROWS = 16, BAR_COLW = 3;
constexpr int BAR_COL_SPACING = 4;
constexpr int BAR_CELL = BAR_DOT + BAR_GAP;
constexpr int BAR_W = 16 * (BAR_COLW * BAR_CELL) + 15 * BAR_COL_SPACING;
constexpr int BAR_X = LCD_X + LCD_W - 14 - BAR_W;
constexpr int BAR_Y = LCD_Y + 18;
constexpr int BTN_Y = LCD_Y + LCD_H + 16, BTN_H = 34, BTN_W = 88, BTN_GAP = 8;
constexpr int INFO_Y = Height - 22;
constexpr int VOI_X = LCD_X + LCD_W + 22, VOI_Y = LCD_Y - 8, VOI_W = CX1 - VOI_X, VOI_H = LCD_H + 16; // voices column

struct Button { int x, w, id; };
constexpr Button BUTTONS[] = {
    {CX1 - 4 * BTN_W - 3 * BTN_GAP, BTN_W, 1},
    {CX1 - 3 * BTN_W - 2 * BTN_GAP, BTN_W, 2},
    {CX1 - 2 * BTN_W - 1 * BTN_GAP, BTN_W, 3},
    {CX1 - 1 * BTN_W, BTN_W, 4},
};

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
#ifdef NUKED_SC55_ENGINE_88PRO
    bool drag_vol     = false; // GAIN knob being turned
    int drag_x = 0, drag_y = 0;
    float drag_v0     = 1.0f;
    bool preview_down = false; // PREVIEW held
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

void Gradient(HDC dc, int x, int y, int w, int h, COLORREF top, COLORREF bot)
{
    TRIVERTEX v[2] = {
        {x, y, (COLOR16)(GetRValue(top) << 8), (COLOR16)(GetGValue(top) << 8),
         (COLOR16)(GetBValue(top) << 8), 0},
        {x + w, y + h, (COLOR16)(GetRValue(bot) << 8), (COLOR16)(GetGValue(bot) << 8),
         (COLOR16)(GetBValue(bot) << 8), 0}};
    GRADIENT_RECT gr{0, 1};
    GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

void RoundFill(HDC dc, int x, int y, int w, int h, int r, COLORREF fill, COLORREF edge)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p   = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, x, y, x + w, y + h, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
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

void TextW(HDC dc, HFONT f, COLORREF c, RECT r, const wchar_t* s)
{
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

#include "lcd_render.inc"

// ---- Procedural background (built once) -----------------------------------
inline uint32_t Px(int r, int g, int b)
{
    return (uint32_t)(std::clamp(b, 0, 255) | (std::clamp(g, 0, 255) << 8) |
                      (std::clamp(r, 0, 255) << 16));
}

void PaintScrew(uint32_t* px, int cx, int cy, int rad, uint32_t& seed)
{
    for (int y = -rad - 2; y <= rad + 2; ++y)
        for (int x = -rad - 2; x <= rad + 2; ++x) {
            const int X = cx + x, Y = cy + y;
            if (X < 0 || Y < 0 || X >= Width || Y >= Height) continue;
            const float d = std::sqrt((float)(x * x + y * y));
            uint32_t& p   = px[Y * Width + X];
            if (d <= rad + 1.5f && d > rad) { // soft drop shadow ring
                const int k = 40;
                p = Px(((p >> 16) & 255) - k, ((p >> 8) & 255) - k, (p & 255) - k);
                continue;
            }
            if (d > rad) continue;
            // dome shading, light from top-left
            const float nx = x / (float)rad, ny = y / (float)rad;
            const float l  = 0.55f - 0.45f * (nx * 0.7f + ny * 0.7f);
            seed = seed * 1664525u + 1013904223u;
            const int n = (int)(seed >> 28) - 8;
            int v = (int)(70 + 110 * l) + n;
            // phillips cross slot
            const bool slot = (std::abs(x) <= 1 || std::abs(y) <= 1) && d < rad * 0.72f;
            if (slot) v = 28 + n / 2;
            if (d > rad - 1.2f) v -= 35; // rim
            p = Px(v, v, v + 4);
        }
}

void BuildBackground(Editor& e)
{
    HDC screen = GetDC(nullptr);
    e.bg_dc    = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = Width;
    bi.bmiHeader.biHeight      = -Height;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    e.bg_bmp   = CreateDIBSection(e.bg_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    e.bg_old   = SelectObject(e.bg_dc, e.bg_bmp);
    ReleaseDC(nullptr, screen);
    auto* px = static_cast<uint32_t*>(bits);

    // Brushed/powder-coated metal: fine noise + faint horizontal grain
    uint32_t seed = 0x5c55u;
    float grain[Height];
    for (int y = 0; y < Height; ++y) {
        seed     = seed * 1664525u + 1013904223u;
        grain[y] = ((seed >> 24) / 255.0f - 0.5f) * 4.0f;
    }
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const int n   = (int)((seed >> 27) & 15) - 8;
            const bool ear = x < EAR || x >= Width - EAR;
            float base    = ear ? 30.0f : 44.0f - 14.0f * y / Height; // vertical falloff
            base += grain[y] + n * (ear ? 0.6f : 0.45f);
            const int v = (int)base;
            px[y * Width + x] = Px(v, v + 1, v + 4);
        }

    // Ear edges (bevel) and panel bevels
    for (int y = 0; y < Height; ++y) {
        auto shade = [&](int x, int d) {
            uint32_t& p = px[y * Width + x];
            p = Px(((p >> 16) & 255) + d, ((p >> 8) & 255) + d, (p & 255) + d);
        };
        shade(EAR - 1, -22); shade(EAR, 26); shade(EAR + 1, 10);
        shade(Width - EAR - 1, -22); shade(Width - EAR, 18);
        shade(0, 14); shade(Width - 1, -20);
    }
    for (int x = 0; x < Width; ++x) {
        auto shade = [&](int y, int d) {
            uint32_t& p = px[y * Width + x];
            p = Px(((p >> 16) & 255) + d, ((p >> 8) & 255) + d, (p & 255) + d);
        };
        shade(0, 40); shade(1, 18); shade(Height - 1, -30); shade(Height - 2, -15);
        if (x >= EAR && x < Width - EAR) { shade(INFO_Y - 1, -20); shade(INFO_Y, 14); }
    }
    // Darker info bar
    for (int y = INFO_Y + 1; y < Height - 2; ++y)
        for (int x = EAR + 1; x < Width - EAR - 1; ++x) {
            uint32_t& p = px[y * Width + x];
            p = Px(((p >> 16) & 255) - 14, ((p >> 8) & 255) - 14, (p & 255) - 12);
        }

    // Screws in the rack ears
    for (int sx : {EAR / 2, Width - EAR / 2})
        for (int sy : {30, Height - 46}) PaintScrew(px, sx, sy, 7, seed);

    // LCD window: recessed bezel with inner shadow, then glass
    HDC dc = e.bg_dc;
    RoundFill(dc, LCD_X - 8, LCD_Y - 8, LCD_W + 16, LCD_H + 16, 12,
              RGB(0x0c, 0x0c, 0x0d), RGB(0x05, 0x05, 0x06));
    Fill(dc, LCD_X - 7, LCD_Y + LCD_H + 7, LCD_W + 14, 1, RGB(0x5a, 0x5d, 0x62)); // lower lip highlight
    {
        const RECT glass{LCD_X, LCD_Y, LCD_X + LCD_W, LCD_Y + LCD_H};
        lcdview::DrawGlass(dc, glass); // original SC-55 glass incl. printed labels
    }

    // Voices display: recessed dark window
    RoundFill(dc, VOI_X, VOI_Y, VOI_W, VOI_H, 8, RGB(0x0d, 0x0d, 0x0e), RGB(0x05, 0x05, 0x06));
    Fill(dc, VOI_X + 3, VOI_Y + VOI_H - 1, VOI_W - 6, 1, RGB(0x55, 0x58, 0x5d));

    // Printed text
    char model[40];
    std::snprintf(model, sizeof(model), "%s", e.name);
    // Title: "NUKED-SC55" white + model suffix orange
    const char* suffix = std::strstr(model, " ");
    char head[40];
    std::snprintf(head, sizeof(head), "%.*s", suffix ? (int)(suffix - model) : (int)std::strlen(model), model);
    for (char* c = head; *c; ++c) *c = (char)toupper(*c);
    Text(dc, e.f_title, C_PRINT, CX0, 12, head, DT_LEFT, 300, 34);
    SIZE sz{};
    SelectObject(dc, e.f_title);
    GetTextExtentPoint32A(dc, head, (int)std::strlen(head), &sz);
    if (suffix) Text(dc, e.f_title, C_ORANGE, CX0 + sz.cx + 8, 12, suffix + 1, DT_LEFT, 200, 34);
    Text(dc, e.f_sub, C_PRINT, CX0 + 1, 41, "SOUND MODULE EMULATOR  \xB7  POLYPHONIC EXTENSION",
         DT_LEFT, 500, 16);

    char units[64];
    std::snprintf(units, sizeof(units), "%d UNITS  \xB7  %d VOICES MAX",
                  e.plugin->NumInstances(), e.plugin->MaxSelectableVoices());
    Text(dc, e.f_sub, C_PRINT_DIM, CX1 - 300, 22, units, DT_RIGHT, 300, 16);

    // Info bar (gearmulator style)
    char info[200];
    std::snprintf(info, sizeof(info),
                  "PLUG-IN VERSION: 1.0   |   SYNTH MODEL: %s   |   ROM LOADED: EMBEDDED",
                  e.plugin->ModelName());
    Text(dc, e.f_tiny, C_PRINT_DIM, CX0, INFO_Y + 5, info, DT_LEFT, 520, 14);
    Text(dc, e.f_tiny, C_PRINT_DIM, CX1 - 260, INFO_Y + 5,
         "OPEN SOURCE PROJECT   |   LLE EMULATION", DT_RIGHT, 260, 14);
}

void LcdField(Editor& e, HDC dc, int x, int y, const char* label, const char* value,
              int w = 120)
{
    Text(dc, e.f_label, C_LCD_LABEL, x, y, label, DT_LEFT, w, 14);
    Text(dc, e.f_value, C_LCD_TEXT, x, y + 13, value, DT_LEFT, w + 200, 26);
}

void DrawButton(Editor& e, HDC dc, const Button& b)
{
    const bool hi  = e.hover_btn == b.id;
    const bool led = e.flash[b.id] > 0 || (b.id == 4 && e.menu_open);
    // rubber key: dark rounded body with soft vertical gradient
    RoundFill(dc, b.x, BTN_Y + 2, b.w, BTN_H, 9, RGB(0x08, 0x08, 0x09), RGB(0x08, 0x08, 0x09)); // shadow
    HRGN rg = CreateRoundRectRgn(b.x, BTN_Y, b.x + b.w + 1, BTN_Y + BTN_H + 1, 9, 9);
    SelectClipRgn(dc, rg);
    Gradient(dc, b.x, BTN_Y, b.w, BTN_H, hi ? RGB(0x5c, 0x5f, 0x65) : RGB(0x4a, 0x4d, 0x52),
             hi ? RGB(0x3a, 0x3c, 0x40) : RGB(0x2b, 0x2d, 0x30));
    Fill(dc, b.x + 4, BTN_Y + 1, b.w - 8, 1, RGB(0x7a, 0x7d, 0x83)); // top highlight
    SelectClipRgn(dc, nullptr);
    DeleteObject(rg);
    // small LED (JP-style) at the left
    RoundFill(dc, b.x + 9, BTN_Y + BTN_H / 2 - 3, 10, 6, 3, led ? C_BTNLED_ON : C_BTNLED_OFF,
              RGB(0x10, 0x05, 0x05));
    RECT r{b.x + 16, BTN_Y, b.x + b.w, BTN_Y + BTN_H};
    const wchar_t* lbl = b.id == 1 ? L"\u25C4 PART" : b.id == 2 ? L"PART \u25BA"
                       : b.id == 3 ? L"ALL OFF" : L"SETUP";
    if (b.id == 4) {
        r.right -= 22;
        TextW(dc, e.f_btn, C_PRINT, r, lbl);
        const int tx = b.x + b.w - 19, ty = BTN_Y + BTN_H / 2 - 2;
        POINT tri[3] = {{tx, ty}, {tx + 8, ty}, {tx + 4, ty + 5}};
        HBRUSH br = CreateSolidBrush(C_PRINT);
        HPEN pn   = CreatePen(PS_SOLID, 1, C_PRINT);
        HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pn);
        Polygon(dc, tri, 3);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(br); DeleteObject(pn);
    } else {
        TextW(dc, e.f_btn, C_PRINT, r, lbl);
    }
}

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

#ifdef NUKED_SC55_ENGINE_88PRO
#include "editor_sc88.inc"
#endif

void Draw(Editor& e, HDC dc)
{
#ifdef NUKED_SC55_ENGINE_88PRO
    Draw88(e, dc);
    return;
#endif
    NukedSc55& p = *e.plugin;
    if (!e.bg_dc) BuildBackground(e);
    BitBlt(dc, 0, 0, Width, Height, e.bg_dc, 0, 0, SRCCOPY);

    // LCD as driven by the firmware (text and bitmap SysEx messages included)
    {
        lcdview::View view;
        if (GatherLcd(p, view)) {
            const RECT glass{LCD_X, LCD_Y, LCD_X + LCD_W, LCD_Y + LCD_H};
            lcdview::DrawDots(dc, glass, view, e.lcd_cache);
        }
    }

    // Voices column (recessed window right of the LCD)
    char v[96];
    const int voices   = p.ui_voices.load();
    const int maxv     = p.max_voices.load();
    const int cap      = p.PartialsPerInstance();
    const int units    = (maxv + cap - 1) / cap;
    const int capacity = units * cap;
    if (voices >= e.peak) { e.peak = (float)voices; e.peak_hold = 45; }
    else if (e.peak_hold > 0) --e.peak_hold;
    else e.peak = std::max((float)voices, e.peak - 1.5f);
    Text(dc, e.f_tiny, C_PRINT_DIM, VOI_X, VOI_Y + 8, "VOICES", DT_CENTER, VOI_W, 14);
    std::snprintf(v, sizeof(v), "%03d", voices);
    Text(dc, e.f_big, C_LED_ON, VOI_X, VOI_Y + 22, v, DT_CENTER, VOI_W, 34);
    std::snprintf(v, sizeof(v), "of %d", capacity);
    Text(dc, e.f_tiny, C_PRINT_DIM, VOI_X, VOI_Y + 56, v, DT_CENTER, VOI_W, 14);
    {   // vertical meter
        const int segs = 20, sh = 4, sg = 1, mw = 34;
        const int mx = VOI_X + (VOI_W - mw) / 2, mb = VOI_Y + VOI_H - 46;
        const int lit = std::min(segs, (int)std::ceil(voices * (float)segs / std::max(1, capacity)));
        const int pk  = std::min(segs - 1, (int)(e.peak * segs / std::max(1, capacity)));
        for (int k = 0; k < segs; ++k) {
            COLORREF c = C_LED_OFF;
            if (k < lit) c = (k >= segs * 9 / 10) ? C_LED_HOT : C_LED_ON;
            if (k == pk && e.peak > 0) c = C_LED_HOT;
            Fill(dc, mx, mb - (k + 1) * (sh + sg), mw, sh, c);
        }
    }
    const int awake = p.ui_awake.load();
    std::snprintf(v, sizeof(v), "MAX %d / %d", maxv, capacity);
    Text(dc, e.f_tiny, C_PRINT_DIM, VOI_X, VOI_Y + VOI_H - 38, v, DT_CENTER, VOI_W, 14);
    if (awake > units) std::snprintf(v, sizeof(v), "UNITS %d->%d", awake, units);
    else std::snprintf(v, sizeof(v), "UNITS %d/%d", awake, units);
    Text(dc, e.f_tiny, C_PRINT_DIM, VOI_X, VOI_Y + VOI_H - 22, v, DT_CENTER, VOI_W, 14);

    for (const auto& b : BUTTONS) DrawButton(e, dc, b);
}

void DecayBars(Editor& e)
{
    for (int& f : e.flash) if (f > 0) --f;
    for (int c = 0; c < 16; ++c) {
        const uint8_t hit = e.plugin->ui_parts[c].hit.exchange(0);
        e.disp[c]         = std::max(e.disp[c] - 7.0f, 0.0f);
        if (hit > e.disp[c]) e.disp[c] = hit;
    }
}

int HitButton(int x, int y)
{
#ifdef NUKED_SC55_ENGINE_88PRO
    return HitButton88(x, y);
#endif
    for (const auto& b : BUTTONS)
        if (x >= b.x && x < b.x + b.w && y >= BTN_Y && y < BTN_Y + BTN_H) return b.id;
    return 0;
}

int HitPart(int x, int y)
{
#ifdef NUKED_SC55_ENGINE_88PRO
    return HitPart88(x, y);
#endif
    {
        const RECT glass{LCD_X, LCD_Y, LCD_X + LCD_W, LCD_Y + LCD_H};
        return lcdview::PartAt(glass, x, y);
    }
    if (y < BAR_Y || y > BAR_Y + BAR_ROWS * BAR_CELL + 18) return -1;
    for (int c = 0; c < 16; ++c) {
        const int cx = BAR_X + c * (BAR_COLW * BAR_CELL + BAR_COL_SPACING);
        if (x >= cx - 2 && x < cx + BAR_COLW * BAR_CELL + 2) return c;
    }
    return -1;
}

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
        if (IsGerman())
            swprintf(s, 128, L"   %d Stimmen\t(%d %ls, %d Partials)", v, units,
                     units == 1 ? L"Einheit" : L"Einheiten", units * cap);
        else
            swprintf(s, 128, L"   %d voices\t(%d %ls, %d partials)", v, units,
                     units == 1 ? L"unit" : L"units", units * cap);
        const bool checked = (cur + cap - 1) / cap == units;
        AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : 0), 100 + v, s);
    }
#ifdef NUKED_SC55_ENGINE_88PRO
    HMENU pv_menu = CreatePopupMenu(); // "Prevw Note" C-1 .. G9 (Roland numbering, C4 = 60)
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
    {
        const int tm = p.tone_map.load();
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Tone map (all parts)", L"Klang-Map (alle Parts)"));
        AppendMenuW(m, MF_STRING | (tm == 0 ? MF_CHECKED : 0), 200, L"   SC-55 Map");
        AppendMenuW(m, MF_STRING | (tm == 1 ? MF_CHECKED : 0), 201,
                    T(L"   SC-88 Map\t(default)", L"   SC-88 Map\t(Standard)"));
        AppendMenuW(m, MF_STRING | (tm == 2 ? MF_CHECKED : 0), 202,
                    T(L"   SC-88 Pro Map\t(factory setting)", L"   SC-88 Pro Map\t(Werkseinstellung)"));
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        char nb[8];
        wchar_t pl[96];
        std::swprintf(pl, 96, L"%ls: %hs", T(L"Preview note (Prevw Note)", L"Preview-Note (Prevw Note)"),
                      NoteName88(p.preview_note.load(), nb, sizeof(nb)));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)pv_menu, pl);
    }
#endif
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 2, T(L"Send GS reset", L"GS-Reset senden"));
    AppendMenuW(m, MF_STRING, 1, T(L"All notes off", L"Alle Noten aus"));

    RECT r;
    GetWindowRect(e.hwnd, &r);
    const auto& b = BUTTONS[3];
    e.menu_open = true;
    InvalidateRect(e.hwnd, nullptr, FALSE);
#ifdef NUKED_SC55_ENGINE_88PRO
    (void)b;
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                                   r.left + P88_SETUP.left, r.top + P88_SETUP.top - 2, 0, e.hwnd, nullptr);
#else
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                                   r.left + b.x + b.w, r.top + BTN_Y, 0, e.hwnd, nullptr);
#endif
    e.menu_open = false;
    DestroyMenu(m);

    if (cmd >= 300 && cmd <= 427) {
#ifdef NUKED_SC55_ENGINE_88PRO
        p.preview_note = cmd - 300; // stored in the plugin state
        p.NotifyStateChanged();
#endif
    } else if (cmd >= 200 && cmd <= 202) {
        p.tone_map = cmd - 200; // switched like the panel buttons, stored in the state
        p.NotifyStateChanged();
    } else if (cmd >= 100) {
        p.SetMaxVoices(cmd - 100); // stored in the plugin state by the host
        p.NotifyStateChanged();
    } else if (cmd == 1 || cmd == 2) {
        p.ui_command = cmd;
    }
}

const char* ClassName()
{
    static char name[64] = {};
    if (!name[0])
        std::snprintf(name, sizeof(name), NUKED_SC55_WNDCLASS_PREFIX "%p", (void*)ModuleInstance());
    return name;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* e = reinterpret_cast<Editor*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
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
#ifdef NUKED_SC55_ENGINE_88PRO
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
#endif
        if (e) {
            const int b = HitButton((short)LOWORD(lp), (short)HIWORD(lp));
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
#ifdef NUKED_SC55_ENGINE_88PRO
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (e && e->drag_vol) {
            e->drag_vol = false;
            if (msg == WM_LBUTTONUP) ReleaseCapture();
            e->plugin->NotifyStateChanged(); // GAIN is stored in the plugin state
        }
        if (e && e->preview_down) {
            e->preview_down        = false;
            e->plugin->ui_preview = 2; // note off
            if (msg == WM_LBUTTONUP) ReleaseCapture();
        }
        return 0;
    case WM_LBUTTONDBLCLK: // fast repeated presses count as presses
#endif
    case WM_LBUTTONDOWN:
        if (e) {
            const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            const int hb = HitButton(x, y);
            if (hb) e->flash[hb] = 6;
            switch (hb) {
#ifdef NUKED_SC55_ENGINE_88PRO
            // PART </>: press the device's buttons; the selected part follows the part the
            // firmware shows (A01..A16, B01..B16, no wrap-around, fast presses may be ignored)
            case 1: e->plugin->ui_command = 3; break;
            case 2: e->plugin->ui_command = 4; break;
#else
            case 1: e->part = (e->part + 15) % 16; e->plugin->ui_command = 3; break; // PART <
            case 2: e->part = (e->part + 1) % 16; e->plugin->ui_command = 4; break;  // PART >
#endif
            case 3: e->plugin->ui_command = 1; e->flash[3] = 12; break;
            case 4: ShowSetupMenu(*e); break;
#ifdef NUKED_SC55_ENGINE_88PRO
            case 5: { // SC-55 MAP: toggles SC-55 <-> SC-88 Pro like the hardware
                const int tm = e->plugin->tone_map.load();
                e->plugin->tone_map = (tm == 0) ? 2 : 0;
                e->plugin->NotifyStateChanged();
                break;
            }
            case 6: { // SC-88 MAP: toggles SC-88 <-> SC-88 Pro like the hardware
                const int tm = e->plugin->tone_map.load();
                e->plugin->tone_map = (tm == 1) ? 2 : 1;
                e->plugin->NotifyStateChanged();
                break;
            }
            case 7: ShowSetupMenu(*e); break;
            case 8: { // MUTE: selected part on/off
                const uint32_t bit = 1u << e->part;
                e->plugin->mute_mask.fetch_xor(bit);
                break;
            }
            case 9: // PREVIEW: note of the selected part while held
                e->plugin->ui_preview_part = e->part;
                e->plugin->ui_preview      = 1;
                e->preview_down            = true;
                SetCapture(hwnd);
                break;
            case 10: // GAIN knob: drag; double-click = 0 dB
                if (msg == WM_LBUTTONDBLCLK) {
                    e->plugin->gain_db = 0.0f;
                    e->plugin->NotifyStateChanged();
                    break;
                }
                e->drag_vol = true;
                e->drag_x   = x;
                e->drag_y   = y;
                e->drag_v0  = e->plugin->gain_db.load();
                SetCapture(hwnd);
                break;
#endif
            default:
                // column of the shown port (A or B, the one of the selected part)
                if (const int c = HitPart(x, y); c >= 0) e->part = e->part / 16 * 16 + c;
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
#ifdef NUKED_SC55_ENGINE_88PRO
        if (e) { // over the GAIN knob: one knob position (of 31, 0.8 dB) per notch
            POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
            ScreenToClient(hwnd, &pt);
            if (HitButton(pt.x, pt.y) == 10) {
                const int f = GainKnobFrame(e->plugin->gain_db.load()) + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1);
                e->plugin->gain_db = GainOfKnobFrame(f);
                e->plugin->NotifyStateChanged();
                return 0;
            }
        }
#endif
        if (e) e->part = (e->part + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? NukedSc55::kNumParts - 1 : 1)) %
                         NukedSc55::kNumParts;
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
    if (e->bg_dc) {
        SelectObject(e->bg_dc, e->bg_old);
        DeleteObject(e->bg_bmp);
        DeleteDC(e->bg_dc);
    }
    lcdview::FreeCache(e->lcd_cache);
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
