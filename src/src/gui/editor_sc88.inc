// Nuked SC-88 Pro Poly - front panel (included by editor_win32.cpp in
// NUKED_SC55_ENGINE_88PRO builds only).
//
// Background: the 88emu SC-88 Pro panel art (The Usual Suspects, GPLv3),
// prepared at build time by tools/make_sc88_panel.py (918x280, branding and
// EFX labels replaced) and linked in as raw BGRA pixels. Everything dynamic
// (LCD content, value windows, map LEDs, voices) is drawn on top of it.

#if !defined(NUKED_SC55_DEVICE_8850) && !defined(NUKED_SC55_DEVICE_88) // the SC-8850 panel (editor_sc8850.inc) has its own art
extern "C" const unsigned char nk_panel88_b[] asm("nk_panel88_b");
extern "C" const unsigned char nk_panel88_e[] asm("nk_panel88_e");
// Original 88emu controls, prepared by tools/make_sc88_sprites.py (premultiplied BGRA, fixed order)
extern "C" const unsigned char nk_sprites88_b[] asm("nk_sprites88_b");
extern "C" const unsigned char nk_sprites88_e[] asm("nk_sprites88_e");
#endif

namespace {

// Geometry in panel pixels (918x280)
constexpr RECT P88_LCD       = {234, 33, 547, 146}; // 313x113 = original glass 741x268 scaled
constexpr int P88_BAR_X0     = 363;   // centre of part 1 column
constexpr double P88_BAR_DX  = 10.93; // column pitch (parts 1..16)
constexpr int P88_BAR_TOP    = 40, P88_BAR_BOTTOM = 132;
constexpr RECT P88_VOICES    = {29, 38, 142, 145};
constexpr RECT P88_UNITS     = {44, 151, 128, 171};
struct P88Box { RECT r; };
constexpr RECT P88_INSTR     = {804, 24, 893, 41};
constexpr RECT P88_LEVEL     = {695, 63, 783, 80};
constexpr RECT P88_PAN       = {804, 63, 893, 80};
constexpr RECT P88_REVERB    = {694, 102, 783, 119};
constexpr RECT P88_CHORUS    = {804, 102, 893, 119};
constexpr RECT P88_KSHIFT    = {694, 141, 783, 158};
constexpr RECT P88_MIDICH    = {804, 141, 892, 158};
constexpr RECT P88_SETUP     = {464, 247, 551, 263};
constexpr RECT P88_TONEMAP   = {585, 247, 673, 263};
constexpr RECT P88_UNITSBOX  = {695, 248, 782, 263};
constexpr RECT P88_MAXBOX    = {804, 248, 892, 263};
// Controls at the positions of the 88emu player (dp x 1.5); centres and radii
constexpr POINT P88_BTN_ALL  = {642, 33}, P88_BTN_MUTE = {642, 72};
constexpr POINT P88_BTN_55   = {642, 111}, P88_BTN_88 = {642, 150};
constexpr POINT P88_PART_L   = {718, 31}, P88_PART_R = {760, 31};
constexpr POINT P88_PREVIEW  = {189, 132}, P88_GAIN = {189, 57};
constexpr int P88_BTN_R = 12, P88_PART_RAD = 10, P88_PREVIEW_R = 15, P88_GAIN_R = 21;
constexpr int P88_GAIN_TEXT_Y = 81; // GAIN readout (dB) under the knob

// GAIN knob: 31 sprite positions across kGainMinDb..kGainMaxDb, 0 dB = middle position (15)
int GainKnobFrame(float db)
{
    const float span = NukedSc55::kGainMaxDb - NukedSc55::kGainMinDb;
    return std::clamp(static_cast<int>(std::lround((db - NukedSc55::kGainMinDb) / span * 30.0f)), 0, 30);
}
float GainOfKnobFrame(int frame)
{
    const float span = NukedSc55::kGainMaxDb - NukedSc55::kGainMinDb;
    return NukedSc55::kGainMinDb + std::clamp(frame, 0, 30) * span / 30.0f;
}

// Sprite sheet (one DIB, sprites stacked): id -> (y offset, size)
enum SpriteId88 { SP_ALL, SP_ROUND, SP_LED, SP_PART_L, SP_PART_R, SP_PREVIEW, SP_KNOB };
constexpr int SP_SIZE[]  = {24, 24, 24, 20, 20, 30, 42};
constexpr int SP_Y[]     = {0, 24, 48, 72, 92, 112, 142};     // knob frame f at 142 + 42 f
constexpr int SP_SHEET_W = 42, SP_SHEET_H = 142 + 42 * 31;
HDC g_sp_dc = nullptr;

constexpr COLORREF C88_LCD_TEXT = RGB(0x2a, 0x12, 0x00);
constexpr COLORREF C88_LCD_OFF  = RGB(0xe8, 0x62, 0x0c); // unlit bar dots on the orange LCD
constexpr COLORREF C88_LED      = RGB(0xff, 0x9a, 0x2e);
constexpr COLORREF C88_LED_DIM  = RGB(0x3a, 0x24, 0x10);
constexpr COLORREF C88_MAP_LED  = RGB(0x5c, 0xff, 0x7a);

bool InRect(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
bool InCircle(POINT c, int rad, int x, int y) { return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= rad * rad; }

void Box88(Editor& e, HDC dc, const RECT& r, const char* s, COLORREF col = C88_LED, bool centered = false)
{
    RECT t = r;
    if (!centered) t.left += 4;
    SelectObject(dc, e.f_small);
    SetTextColor(dc, col);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s, -1, &t, (centered ? DT_CENTER : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                                 DT_END_ELLIPSIS);
}

#if !defined(NUKED_SC55_DEVICE_8850) && !defined(NUKED_SC55_DEVICE_88)
void BuildSprites88()
{
    if (g_sp_dc) return;
    HDC screen = GetDC(nullptr);
    g_sp_dc    = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = SP_SHEET_W;
    bi.bmiHeader.biHeight      = -SP_SHEET_H;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(g_sp_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(g_sp_dc, bmp);
    auto* dst            = static_cast<uint8_t*>(bits);
    std::memset(dst, 0, size_t(SP_SHEET_W) * SP_SHEET_H * 4);
    const uint8_t* src   = nk_sprites88_b;
    const uint8_t* end   = nk_sprites88_e;
    const auto copy = [&](int y0, int size) {
        for (int y = 0; y < size && src + size * 4 <= end; ++y, src += size * 4)
            std::memcpy(dst + (size_t(y0 + y) * SP_SHEET_W) * 4, src, size_t(size) * 4);
    };
    for (int i = SP_ALL; i <= SP_PREVIEW; ++i) copy(SP_Y[i], SP_SIZE[i]);
    for (int f = 0; f < 31; ++f) copy(SP_Y[SP_KNOB] + 42 * f, 42);
}

// Draw a sprite centred on c; dimmed while pressed (like the player's pressed tint)
void Sprite88(HDC dc, SpriteId88 id, POINT c, bool pressed = false, int frame = 0)
{
    BuildSprites88();
    const int sz = SP_SIZE[id];
    BLENDFUNCTION bf{AC_SRC_OVER, 0, static_cast<BYTE>(pressed ? 170 : 255), AC_SRC_ALPHA};
    AlphaBlend(dc, c.x - sz / 2, c.y - sz / 2, sz, sz, g_sp_dc, 0, SP_Y[id] + frame * sz, sz, sz, bf);
}

#endif

const char* NoteName88(int n, char* buf, size_t len) // Roland: C-1 = 0, C4 = 60, G9 = 127
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    std::snprintf(buf, len, "%s%d", names[n % 12], n / 12 - 1);
    return buf;
}

void Flash88(HDC dc, POINT c, int rad)
{
    HPEN p     = CreatePen(PS_SOLID, 2, C88_LED);
    HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, c.x - rad, c.y - rad, c.x + rad, c.y + rad);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(p);
}

// Voices window (the player's playlist) and the UNITS slot under it
void DrawVoices88(Editor& e, HDC dc, const RECT& win, const RECT& units_box)
{
    NukedSc55& p = *e.plugin;
    char v[32];
    const int voices   = p.ui_voices.load();
    const int maxv     = p.max_voices.load();
    const int cap      = p.PartialsPerInstance();
    const int units    = (maxv + cap - 1) / cap;
    const int capacity = units * cap;
    if (voices >= e.peak) { e.peak = (float)voices; e.peak_hold = 45; }
    else if (e.peak_hold > 0) --e.peak_hold;
    else e.peak = std::max((float)voices, e.peak - 1.5f);
    std::snprintf(v, sizeof(v), "%03d", voices);
    Text(dc, e.f_big, C88_LED, win.left, win.top + 8, v, DT_CENTER, win.right - win.left, 34);
    std::snprintf(v, sizeof(v), "of %d", capacity);
    Text(dc, e.f_tiny, RGB(0x9a, 0x9e, 0xa6), win.left, win.top + 44, v, DT_CENTER, win.right - win.left, 14);
    {   // vertical-ish meter: 20 segments across the window
        const int segs = 20, mx = win.left + 8, my = win.top + 66, sw = 4, sg = 1;
        const int lit  = std::min(segs, (int)std::ceil(voices * (float)segs / std::max(1, capacity)));
        const int pk   = std::min(segs - 1, (int)(e.peak * segs / std::max(1, capacity)));
        for (int s = 0; s < segs; ++s) {
            COLORREF c = C88_LED_DIM;
            if (s < lit) c = (s >= segs * 9 / 10) ? RGB(0xff, 0x58, 0x30) : C88_LED;
            if (s == pk && e.peak > 0) c = RGB(0xff, 0x58, 0x30);
            Fill(dc, mx + s * (sw + sg), my, sw, 22, c);
        }
    }
    const int awake = p.ui_awake.load();
    if (awake > units) std::snprintf(v, sizeof(v), "UNITS %d->%d", awake, units);
    else std::snprintf(v, sizeof(v), "UNITS %d/%d", awake, units);
    Box88(e, dc, units_box, v, C88_LED, true);
}

#if !defined(NUKED_SC55_DEVICE_8850) && !defined(NUKED_SC55_DEVICE_88)
void BuildBackground88(Editor& e)
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
    const size_t need = static_cast<size_t>(Width) * Height * 4;
    const size_t have = static_cast<size_t>(nk_panel88_e - nk_panel88_b);
    std::memcpy(bits, nk_panel88_b, std::min(need, have));
    lcdview::DrawGlass(e.bg_dc, P88_LCD); // original LCD glass with printed labels
}

// Part shown in the firmware's part field (DDRAM 0..2, "A01".."B16"), else -1
int LcdPart88(const uint8_t* dd)
{
    if ((dd[0] != 'A' && dd[0] != 'B') || dd[1] < '0' || dd[1] > '1' || dd[2] < '0' || dd[2] > '9') return -1;
    const int n = (dd[1] - '0') * 10 + (dd[2] - '0');
    if (n < 1 || n > 16) return -1;
    return (dd[0] == 'B' ? 16 : 0) + n - 1;
}

void Draw88(Editor& e, HDC dc)
{
    NukedSc55& p = *e.plugin;
    if (!e.bg_dc) BuildBackground88(e);
    BitBlt(dc, 0, 0, Width, Height, e.bg_dc, 0, 0, SRCCOPY);

    // --- LCD as driven by the firmware (text and bitmap SysEx messages included). The
    // selected part follows the part the firmware shows ("A01".."B16"), so the value windows
    // match the LCD after the PART buttons; a click on a matrix column selects a part of the
    // shown port without changing the LCD.
    {
        lcdview::View view;
        if (GatherLcd(p, view)) {
            lcdview::DrawDots(dc, P88_LCD, view, e.lcd_cache);
            if (const int lp = LcdPart88(view.dd); lp >= 0 && lp != e.lcd_part) {
                e.lcd_part = lp;
                e.part     = lp;
            }
        }
    }
    const auto& pt    = p.ui_parts[e.part];
    const bool rhythm = pt.rhythm.load();
    const int prog = pt.prog.load(), bank = pt.bank.load();
    char v[96];
    const int pan = pt.pan.load() - 64;
    const int ks  = pt.key_shift.load() - 0x40;

    // --- Value windows (right block)
    if (rhythm) std::snprintf(v, sizeof(v), "%s", DrumKitName(prog));
    else std::snprintf(v, sizeof(v), "%s", GM_NAMES[prog]);
    Box88(e, dc, P88_INSTR, v, C88_LED, true);
    std::snprintf(v, sizeof(v), "%d", pt.vol.load());
    Box88(e, dc, P88_LEVEL, v, C88_LED, true);
    if (pan == 0) std::snprintf(v, sizeof(v), "0");
    else std::snprintf(v, sizeof(v), "%c%d", pan < 0 ? 'L' : 'R', std::abs(pan));
    Box88(e, dc, P88_PAN, v, C88_LED, true);
    std::snprintf(v, sizeof(v), "%d", pt.rev.load());
    Box88(e, dc, P88_REVERB, v, C88_LED, true);
    std::snprintf(v, sizeof(v), "%d", pt.cho.load());
    Box88(e, dc, P88_CHORUS, v, C88_LED, true);
    std::snprintf(v, sizeof(v), ks == 0 ? "0" : "%+d", ks);
    Box88(e, dc, P88_KSHIFT, v, C88_LED, true);
    std::snprintf(v, sizeof(v), "%c%02d", e.part < 16 ? 'A' : 'B', e.part % 16 + 1);
    Box88(e, dc, P88_MIDICH, v, C88_LED, true);

    // --- Original controls: lit map / MUTE buttons, pressed tint, GAIN knob position
    const int tm      = p.tone_map.load();
    const bool muted  = (p.mute_mask.load() >> e.part) & 1u;
    Sprite88(dc, SP_ALL, P88_BTN_ALL, e.flash[3] > 0);
    Sprite88(dc, muted ? SP_LED : SP_ROUND, P88_BTN_MUTE, e.flash[8] > 0);
    Sprite88(dc, tm == 0 ? SP_LED : SP_ROUND, P88_BTN_55, e.flash[5] > 0);
    Sprite88(dc, tm == 1 ? SP_LED : SP_ROUND, P88_BTN_88, e.flash[6] > 0);
    Sprite88(dc, SP_PART_L, P88_PART_L, e.flash[1] > 0);
    Sprite88(dc, SP_PART_R, P88_PART_R, e.flash[2] > 0);
    Sprite88(dc, SP_PREVIEW, P88_PREVIEW, e.preview_down);
    const float gain = p.gain_db.load();
    Sprite88(dc, SP_KNOB, P88_GAIN, false, GainKnobFrame(gain));
    {   // GAIN readout under the knob
        char g[16];
        std::snprintf(g, sizeof(g), "%+.1f dB", std::fabs(gain) < 0.05f ? 0.0f : gain);
        Text(dc, e.f_tiny, RGB(0xd8, 0xdc, 0xe2), P88_GAIN.x - 30, P88_GAIN_TEXT_Y, g, DT_CENTER, 60, 12);
    }

    // --- Voices window (former playlist)
    DrawVoices88(e, dc, P88_VOICES, P88_UNITS);
    const int maxv     = p.max_voices.load();
    const int cap      = p.PartialsPerInstance();
    const int units    = (maxv + cap - 1) / cap;
    const int capacity = units * cap;
    const int awake    = p.ui_awake.load();

    // --- Status windows (former EFX section)
    Box88(e, dc, P88_SETUP, e.menu_open ? "SETUP  \x85" : "SETUP  >", C88_LED, true);
    const char* maps[3] = {"SC-55", "SC-88", "SC-88 Pro"};
    Box88(e, dc, P88_TONEMAP, maps[std::clamp(tm, 0, 2)], C88_LED, true);
    std::snprintf(v, sizeof(v), "%d / %d", awake, units);
    Box88(e, dc, P88_UNITSBOX, v, C88_LED, true);
    std::snprintf(v, sizeof(v), "%d (%d)", maxv, capacity);
    Box88(e, dc, P88_MAXBOX, v, C88_LED, true);
}

#endif // !NUKED_SC55_DEVICE_8850 && !NUKED_SC55_DEVICE_88

// 1/2 = part left/right, 3 = ALL (all notes off), 4 = SETUP, 5 = SC-55 MAP,
// 6 = SC-88 MAP, 7 = tone map window (opens the menu as well), 8 = MUTE,
// 9 = PREVIEW, 10 = GAIN knob
int HitButton88(int x, int y)
{
    if (InCircle(P88_PART_L, P88_PART_RAD, x, y)) return 1;
    if (InCircle(P88_PART_R, P88_PART_RAD, x, y)) return 2;
    if (InCircle(P88_BTN_ALL, P88_BTN_R, x, y)) return 3;
    if (InRect(P88_SETUP, x, y)) return 4;
    if (InCircle(P88_BTN_55, P88_BTN_R, x, y)) return 5;
    if (InCircle(P88_BTN_88, P88_BTN_R, x, y)) return 6;
    if (InRect(P88_TONEMAP, x, y)) return 7;
    if (InCircle(P88_BTN_MUTE, P88_BTN_R, x, y)) return 8;
    if (InCircle(P88_PREVIEW, P88_PREVIEW_R, x, y)) return 9;
    if (InCircle(P88_GAIN, P88_GAIN_R, x, y)) return 10;
    return 0;
}

int HitPart88(int x, int y)
{
    return lcdview::PartAt(P88_LCD, x, y);
    if (y < P88_BAR_TOP || y > P88_LCD.bottom) return -1;
    for (int c = 0; c < 16; ++c) {
        const int cx = static_cast<int>(P88_BAR_X0 + c * P88_BAR_DX);
        if (x >= cx - 5 && x < cx + 6) return c;
    }
    return -1;
}

} // namespace
