// Nuked SC-55 Poly - front panel of the SC-55 / SC-55mkII (included by editor_win32.cpp in
// the Nuked-core builds, after editor_sc88.inc).
//
// Background: the 88emu SC-55 / SC-55mkII panel art (The Usual Suspects, GPLv3), prepared at
// build time by tools/make_sc55_panel.py (918x280, branding replaced, playlist -> VOICES,
// VOLUME -> GAIN). The controls are the player's sprites (tools/make_sc55_sprites.py) at the
// positions of its skin. All switches drive unit 0's own panel: the firmware runs the LCD and
// the ALL / MUTE lamps, and the plugin passes panel edits on to the other units
// (NukedSc55::E55PanelPump).

extern "C" const unsigned char nk_panel55_b[] asm("nk_panel55_b");
extern "C" const unsigned char nk_panel55_e[] asm("nk_panel55_e");
extern "C" const unsigned char nk_sprites55_b[] asm("nk_sprites55_b");
extern "C" const unsigned char nk_sprites55_e[] asm("nk_sprites55_e");

void ShowSetupMenu(Editor& e); // editor_win32.cpp

namespace {

constexpr RECT E55_SETUP = P88_SETUP_RIGHT;
constexpr DWORD E55_MIN_HOLD_MS = 80; // a short click still reaches the panel scan

// Sprite order of make_sc55_sprites.py
constexpr int S55_MODE = 0, S55_PART = 8, S55_ROCKER = 12, S55_KNOB = 40, S55_COUNT = 71;

// Panel switches: box (dp, player skin), Nuked MCU button bit, lamp bit (-1 = none),
// shape (0 = round, 1 = rocker half / plain key), first sprite (normal; +1 pressed)
struct E55Btn { float l, t, w, h; uint8_t bit; int8_t led; uint8_t rect; uint8_t sprite; };
constexpr E55Btn E55_BTNS[] = {
    {420, 14, 15, 15, 6, 0, 0, S55_MODE + 0},   // ALL
    {420, 40, 15, 15, 5, 1, 0, S55_MODE + 4},   // MUTE
    {472, 14, 13, 13, 22, -1, 0, S55_PART + 0}, // PART <
    {500, 14, 13, 13, 14, -1, 0, S55_PART + 2}, // PART >
    {538, 17, 27, 9, 3, -1, 1, S55_ROCKER + 0},  {566, 17, 27, 9, 4, -1, 1, S55_ROCKER + 2},   // INSTRUMENT
    {465, 43, 27, 9, 20, -1, 1, S55_ROCKER + 4}, {493, 43, 27, 9, 21, -1, 1, S55_ROCKER + 6},  // LEVEL
    {538, 43, 27, 9, 12, -1, 1, S55_ROCKER + 8}, {566, 43, 27, 9, 13, -1, 1, S55_ROCKER + 10}, // PAN
    {465, 69, 27, 9, 18, -1, 1, S55_ROCKER + 12}, {493, 69, 27, 9, 19, -1, 1, S55_ROCKER + 14}, // REVERB
    {538, 69, 27, 9, 10, -1, 1, S55_ROCKER + 16}, {566, 69, 27, 9, 11, -1, 1, S55_ROCKER + 18}, // CHORUS
    {465, 95, 27, 9, 16, -1, 1, S55_ROCKER + 20}, {493, 95, 27, 9, 17, -1, 1, S55_ROCKER + 22}, // KEY SHIFT
    {538, 95, 27, 9, 8, -1, 1, S55_ROCKER + 24},  {566, 95, 27, 9, 9, -1, 1, S55_ROCKER + 26},  // MIDI CH
};
constexpr int E55_NBTN = sizeof(E55_BTNS) / sizeof(E55_BTNS[0]);
// Hit ids: 1..E55_NBTN switches, then:
constexpr int E55_HIT_GAIN = E55_NBTN + 1, E55_HIT_SETUP = E55_NBTN + 2;

// Sprite sheet: the blob's sprites stacked in one DIB
struct Sprite55 { short x, y, w, h; int sy; };
Sprite55 g_sp55[S55_COUNT];
HDC g_sp55_dc = nullptr;

void BuildSprites55()
{
    if (g_sp55_dc) return;
    int n = 0, sheet_w = 1, sheet_h = 0;
    for (const uint8_t* p = nk_sprites55_b; p + 8 <= nk_sprites55_e && n < S55_COUNT; ++n) {
        Sprite55& s = g_sp55[n];
        std::memcpy(&s.x, p, 2);
        std::memcpy(&s.y, p + 2, 2);
        uint16_t w = 0, h = 0;
        std::memcpy(&w, p + 4, 2);
        std::memcpy(&h, p + 6, 2);
        s.w = static_cast<short>(w);
        s.h = static_cast<short>(h);
        s.sy = sheet_h;
        sheet_h += h;
        sheet_w = std::max(sheet_w, int(w));
        p += 8 + size_t(w) * h * 4;
    }
    HDC screen = GetDC(nullptr);
    g_sp55_dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = sheet_w;
    bi.bmiHeader.biHeight      = -std::max(sheet_h, 1);
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits  = nullptr;
    HBITMAP bmp = CreateDIBSection(g_sp55_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(g_sp55_dc, bmp);
    auto* dst = static_cast<uint8_t*>(bits);
    std::memset(dst, 0, size_t(sheet_w) * std::max(sheet_h, 1) * 4);
    const uint8_t* p = nk_sprites55_b;
    for (int i = 0; i < n; ++i) {
        const Sprite55& s = g_sp55[i];
        p += 8;
        for (int y = 0; y < s.h; ++y, p += size_t(s.w) * 4)
            std::memcpy(dst + (size_t(s.sy + y) * sheet_w) * 4, p, size_t(s.w) * 4);
    }
    for (int i = n; i < S55_COUNT; ++i) g_sp55[i] = {};
}

void Blend55(HDC dc, int id)
{
    BuildSprites55();
    const Sprite55& s = g_sp55[id];
    if (s.w <= 0 || s.h <= 0) return;
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, s.x, s.y, s.w, s.h, g_sp55_dc, 0, s.sy, s.w, s.h, bf);
}

int Hit55(int x, int y)
{
    for (int i = 0; i < E55_NBTN; ++i) {
        const auto& b = E55_BTNS[i];
        const double l = b.l * 1.5, t = b.t * 1.5, w = b.w * 1.5, h = b.h * 1.5;
        if (b.rect) {
            if (x >= l && x < l + w && y >= t - 2 && y < t + h + 2) return i + 1;
        } else {
            const double cx = l + w / 2, cy = t + h / 2, r = w / 2 + 1;
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) return i + 1;
        }
    }
    if (InCircle(P88_GAIN, P88_GAIN_R, x, y)) return E55_HIT_GAIN;
    if (InRect(E55_SETUP, x, y)) return E55_HIT_SETUP;
    return 0;
}

void BuildBackground55(Editor& e)
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
    const size_t have = static_cast<size_t>(nk_panel55_e - nk_panel55_b);
    std::memcpy(bits, nk_panel55_b, std::min(need, have));
    lcdview::DrawGlass(e.bg_dc, P88_LCD); // LCD glass with its printed labels
}

// Dark recessed slot (like the UNITS slot of the art) with centred orange text
void Slot55(Editor& e, HDC dc, const RECT& r, const char* s)
{
    Fill(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, RGB(0x1c, 0x1d, 0x20));
    Fill(dc, r.left, r.top, r.right - r.left, 1, RGB(0x0a, 0x0a, 0x0c));
    Fill(dc, r.left, r.bottom - 1, r.right - r.left, 1, RGB(0x8c, 0x8e, 0x92));
    Box88(e, dc, r, s, C88_LED, true);
}

void Draw55(Editor& e, HDC dc)
{
    NukedSc55& p = *e.plugin;
    if (!e.bg_dc) BuildBackground55(e);
    BitBlt(dc, 0, 0, Width, Height, e.bg_dc, 0, 0, SRCCOPY);
    {   // LCD as driven by unit 0's firmware; level meters of all units
        lcdview::View view;
        if (GatherLcd(p, view)) lcdview::DrawDots(dc, P88_LCD, view, e.lcd_cache);
    }

    // Switches and lamps as the firmware lights them, darker while held
    const uint32_t leds = p.PanelLeds();
    for (const auto& b : E55_BTNS) {
        const bool held = (e.held85 >> b.bit) & 1u;
        const int lit   = (b.led >= 0 && ((leds >> b.led) & 1u)) ? 2 : 0;
        Blend55(dc, b.sprite + lit + (held ? 1 : 0));
    }
    const float gain = p.gain_db.load();
    Blend55(dc, S55_KNOB + GainKnobFrame(gain));
    {   // GAIN readout under the knob
        char g[16];
        std::snprintf(g, sizeof(g), "%+.1f dB", std::fabs(gain) < 0.05f ? 0.0f : gain);
        Text(dc, e.f_tiny, RGB(0xd8, 0xdc, 0xe2), P88_GAIN.x - 30, P88_GAIN_TEXT_Y, g, DT_CENTER, 60, 12);
    }

    DrawVoices88(e, dc, P88_VOICES, P88_UNITS);
    Slot55(e, dc, E55_SETUP, e.menu_open ? "SETUP  \x85" : "SETUP  >");
}

// Mouse input of the SC-55 panel; true = handled. Switches are held while the mouse
// button is down (the firmware sees press and release and repeats like the hardware).
bool Mouse55(Editor& e, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NukedSc55& p = *e.plugin;
    const auto send = [&] { p.ui_panel_buttons = e.held85; };
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        const int h = Hit55(x, y);
        if (h >= 1 && h <= E55_NBTN) {
            // a button still held only for the minimum time is released by the new press
            e.held85 = (e.release85 ? 0u : e.held85) | 1u << E55_BTNS[h - 1].bit;
            e.release85 = false;
            e.press85   = GetTickCount();
            send();
            SetCapture(hwnd);
        } else if (h == E55_HIT_GAIN) { // drag; double-click = 0 dB
            if (msg == WM_LBUTTONDBLCLK) {
                p.gain_db = 0.0f;
                p.NotifyStateChanged();
            } else {
                e.drag_vol = true;
                e.drag_x   = x;
                e.drag_y   = y;
                e.drag_v0  = p.gain_db.load();
                SetCapture(hwnd);
            }
        } else if (h == E55_HIT_SETUP) {
            ShowSetupMenu(e);
        }
        return true;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (e.held85 && !e.release85) { // released by the timer once held long enough
            e.release85 = true;
            if (GetTickCount() - e.press85 >= E55_MIN_HOLD_MS) {
                e.held85 = 0;
                send();
            }
            if (msg == WM_LBUTTONUP) ReleaseCapture();
        }
        return false; // GAIN drag: shared code
    case WM_TIMER:
        if (e.release85 && e.held85 && GetTickCount() - e.press85 >= E55_MIN_HOLD_MS) {
            e.held85 = 0;
            send();
        }
        return false;
    case WM_MOUSEWHEEL: {
        POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(hwnd, &pt);
        if (Hit55(pt.x, pt.y) == E55_HIT_GAIN) { // one knob position (of 31, 0.8 dB) per notch
            p.gain_db = GainOfKnobFrame(GainKnobFrame(p.gain_db.load()) + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1));
            p.NotifyStateChanged();
        }
        return true;
    }
    }
    return false;
}

} // namespace
