// Emu-MT32 Poly - front panels of the MT-32 and the CM-32L (included by editor_win32.cpp in
// NUKED_SC55_DEVICE_LA builds, after editor_sc88.inc; issue #69).
//
// Background and lamp art: tools/make_la_panel.py, drawn from scratch after photos of the devices
// (918x280; --base = lamps off, --all-on = every lamp lit, --glass = the glass over the MT-32's LCD).
// The panel follows the device the units run as (SETUP menu "Device"): the MT-32 panel for both
// MT-32 boards ("v2" after the name for the new one: a small patch over the name), the CM-32L panel for
// the CM-32L. A left click on the name switches the device: CM-32L -> MT-32 (old) -> MT-32 (new) ->
// CM-32L (issue #79; devices without ROMs are skipped).
//
// MT-32: the 20-character LCD shows unit 0's display in square dots (a lit dot also lights the gaps
// to its neighbours, as make_la_panel.py draws it); the glass is laid over the characters.
// PART 1-5, RHYTHM, SOUND GROUP, VOLUME, SOUND and MASTER VOLUME press the device's switches on all
// units (left click; a right click keeps a switch held for key combinations until the next left
// click). The knob is labelled SELECT/GAIN: left-drag, wheel = the plugin's output gain (double-click
// = 0 dB), right-drag or Shift + wheel = the device's own knob (SELECT, e.g. in the SOUND menu).
// CM-32L: no switches; the knob is GAIN, MIDI MESSAGE and POWER lamps on the grooved strip.

extern "C" const unsigned char nk_panelmt32_b[] asm("nk_panelmt32_b");
extern "C" const unsigned char nk_panelmt32_e[] asm("nk_panelmt32_e");
extern "C" const unsigned char nk_litmt32_b[] asm("nk_litmt32_b");
extern "C" const unsigned char nk_litmt32_e[] asm("nk_litmt32_e");
extern "C" const unsigned char nk_panelcm32l_b[] asm("nk_panelcm32l_b");
extern "C" const unsigned char nk_panelcm32l_e[] asm("nk_panelcm32l_e");
extern "C" const unsigned char nk_litcm32l_b[] asm("nk_litcm32l_b");
extern "C" const unsigned char nk_litcm32l_e[] asm("nk_litcm32l_e");
extern "C" const unsigned char nk_glassmt32_b[] asm("nk_glassmt32_b");
extern "C" const unsigned char nk_glassmt32_e[] asm("nk_glassmt32_e");
extern "C" const unsigned char nk_namemt32v2_b[] asm("nk_namemt32v2_b");
extern "C" const unsigned char nk_namemt32v2_e[] asm("nk_namemt32v2_e");

void ShowSetupMenu(Editor& e); // editor_win32.cpp

namespace {

// Geometry (panel pixels, see make_la_panel.py); [0] MT-32, [1] CM-32L
// VOICES box at 90 % about its centre (91, 94) (CM-32L: 6 px higher), issue #79; the GUI fonts keep their size
constexpr RECT ELA_VOICES[2] = {{31, 43, 151, 128}, {31, 37, 151, 122}}; // DrawVoices88 window
constexpr RECT ELA_UNITS[2]  = {{31, 134, 151, 150}, {31, 128, 151, 144}};
constexpr Voices88Layout ELA_VLAYOUT = {4, 38, 58, 20};
constexpr RECT ELA_SETUP  = {812, 226, 900, 256};
constexpr POINT ELA_KNOB[2] = {{850, 108}, {470, 116}};
constexpr int ELA_KNOB_R    = 30;
constexpr RECT ELA_GAIN[2]  = {{810, 150, 890, 166}, {430, 64, 510, 78}};        // value under (MT-32) / above (CM-32L) the knob
constexpr COLORREF ELA_GAIN_INK[2] = {RGB(0xff, 0x96, 0x1e), RGB(0x96, 0x5a, 0x0a)};
constexpr RECT ELA_LAMP_MIDI[2] = {{430, 119, 459, 137}, {684, 164, 696, 185}};  // lamp with its glow
constexpr RECT ELA_LAMP_POWER   = {754, 164, 766, 185};                          // CM-32L, always lit
// MT-32 LCD (same numbers as make_la_panel.py): 20 cells at LX0 + CELL_X0 + i * CELL_W, LY0 + DOT_Y0, each 9 x 15
// pixels (5 x 8 dots at a pitch of 2), the glass over LX0..LX1 x LY0..LY1
constexpr int ELA_LX0 = 190, ELA_LY0 = 100, ELA_LX1 = 418, ELA_LY1 = 140;
constexpr int ELA_CELL_X0 = 4, ELA_CELL_W = 11, ELA_DOT_Y0 = 12;
constexpr COLORREF ELA_LCD_INK = RGB(236, 254, 8);
// name in the lower strip: click area; the "v2" patch (make_la_panel.py --v2, cut to this rectangle)
constexpr RECT ELA_NAME = {36, 214, 330, 264};
// MT-32 switches: PART 1-5, RHYTHM (26 x 26), SOUND GROUP, VOLUME, SOUND, MASTER VOLUME (60 x 24)
struct LaSwitch { int cx, cy, hw, hh; uint32_t bit; };
constexpr LaSwitch ELA_SW[10] = {
        {490, 78, 13, 13, EMU88_MT32_BUTTON_PART1}, {534, 78, 13, 13, EMU88_MT32_BUTTON_PART2},
        {578, 78, 13, 13, EMU88_MT32_BUTTON_PART3}, {490, 138, 13, 13, EMU88_MT32_BUTTON_PART4},
        {534, 138, 13, 13, EMU88_MT32_BUTTON_PART5}, {578, 138, 13, 13, EMU88_MT32_BUTTON_RHYTHM},
        {665, 78, 30, 12, EMU88_MT32_BUTTON_SOUND_GROUP}, {757, 78, 30, 12, EMU88_MT32_BUTTON_VOLUME},
        {665, 138, 30, 12, EMU88_MT32_BUTTON_SOUND}, {757, 138, 30, 12, EMU88_MT32_BUTTON_MASTER_VOLUME}};
constexpr DWORD ELA_MIN_HOLD_MS = 120;   // a short click still reaches the switch scan
constexpr int ELA_SELECT_PX     = 1;     // right-drag: pixels per SELECT detent (256 px = the knob's travel)
constexpr int ELA_HIT_KNOB = 1, ELA_HIT_SETUP = 2, ELA_HIT_NAME = 3, ELA_HIT_SW = 100; // + switch index

HDC g_bg_la[2] = {}, g_lit_la[2] = {}, g_glass_la = nullptr, g_name_v2 = nullptr; // kept for the process
HDC g_dots_la = nullptr;                                      // lit LCD dots (premultiplied BGRA, LCD rectangle)
uint32_t* g_dots_bits = nullptr;

HDC DibLa(const unsigned char* b, const unsigned char* e, int w, int h, uint32_t** out_bits = nullptr)
{
    HDC screen = GetDC(nullptr);
    HDC dc     = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits  = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(dc, bmp);
    std::memcpy(bits, b, std::min(static_cast<size_t>(w) * h * 4, static_cast<size_t>(e - b)));
    if (out_bits) *out_bits = static_cast<uint32_t*>(bits);
    return dc;
}

// Panel shown: the device the units run as (before the first activation: the one chosen)
int PanelLa(const Editor& e)
{
    int d = e.plugin->LaActive();
    if (d < 0) d = e.plugin->la_device.load();
    return d == 2 ? 1 : 0;
}

// The MT-32 (new) runs: "v2" after the name
bool V2La(const Editor& e)
{
    int d = e.plugin->LaActive();
    if (d < 0) d = e.plugin->la_device.load();
    return d == 1;
}

// Click on the name: the next device with ROMs, CM-32L -> MT-32 (old) -> MT-32 (new) -> CM-32L
void NextLaDevice(NukedSc55& p)
{
    static constexpr int next[NukedSc55::kLaDevices] = {1, 2, 0};
    int d = std::clamp(p.la_device.load(), 0, NukedSc55::kLaDevices - 1);
    for (int i = 0; i < NukedSc55::kLaDevices; ++i) {
        d = next[d];
        if (p.LaAvailable(d)) break;
    }
    if (d == p.la_device.load()) return;
    p.la_device = d; // like SETUP > Device: the units are booted again as d, stored in the state
    p.NotifyStateChanged();
}

void LitLa(HDC dc, int k, const RECT& r)
{
    BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, g_lit_la[k], r.left, r.top, SRCCOPY);
}

int HitMt32(const Editor& e, int x, int y)
{
    const int k = PanelLa(e);
    if (InCircle(ELA_KNOB[k], ELA_KNOB_R, x, y)) return ELA_HIT_KNOB;
    if (InRect(ELA_SETUP, x, y)) return ELA_HIT_SETUP;
    if (InRect(ELA_NAME, x, y)) return ELA_HIT_NAME;
    if (k == 0)
        for (int i = 0; i < 10; ++i) {
            const auto& s = ELA_SW[i];
            if (std::abs(x - s.cx) <= s.hw + 2 && std::abs(y - s.cy) <= s.hh + 2) return ELA_HIT_SW + i;
        }
    return 0;
}

void DrawMt32(Editor& e, HDC dc)
{
    NukedSc55& p = *e.plugin;
    const int k  = PanelLa(e);
    if (!g_bg_la[0]) {
        g_bg_la[0]  = DibLa(nk_panelmt32_b, nk_panelmt32_e, Width, Height);
        g_bg_la[1]  = DibLa(nk_panelcm32l_b, nk_panelcm32l_e, Width, Height);
        g_lit_la[0] = DibLa(nk_litmt32_b, nk_litmt32_e, Width, Height);
        g_lit_la[1] = DibLa(nk_litcm32l_b, nk_litcm32l_e, Width, Height);
        g_glass_la  = DibLa(nk_glassmt32_b, nk_glassmt32_e, ELA_LX1 - ELA_LX0, ELA_LY1 - ELA_LY0);
        g_name_v2   = DibLa(nk_namemt32v2_b, nk_namemt32v2_e, ELA_NAME.right - ELA_NAME.left, ELA_NAME.bottom - ELA_NAME.top);
        g_dots_la   = DibLa(nk_glassmt32_b, nk_glassmt32_b, ELA_LX1 - ELA_LX0, ELA_LY1 - ELA_LY0, &g_dots_bits);
    }
    BitBlt(dc, 0, 0, Width, Height, g_bg_la[k], 0, 0, SRCCOPY);
    if (k == 0 && V2La(e))
        BitBlt(dc, ELA_NAME.left, ELA_NAME.top, ELA_NAME.right - ELA_NAME.left, ELA_NAME.bottom - ELA_NAME.top, g_name_v2, 0, 0, SRCCOPY);

    const uint32_t leds = p.PanelLeds();
    if (leds & 1u) LitLa(dc, k, ELA_LAMP_MIDI[k]);
    if (k == 1) LitLa(dc, 1, ELA_LAMP_POWER);

    if (k == 0) {
        // LCD: unit 0's dots (kept while the units are booted again; firmware-defined characters such as the
        // block of a playing part included), then the glass over it
        p.GetLaDots(e.la_dots, sizeof(e.la_dots));
        // lit dots at whole pixels: dot (2c, 2r) of the 9 x 15 cell and the gap pixels around it (bold, readable
        // at this size)
        constexpr int LW = ELA_LX1 - ELA_LX0, LH = ELA_LY1 - ELA_LY0;
        std::memset(g_dots_bits, 0, static_cast<size_t>(LW) * LH * 4);
        const uint32_t ink = 0xff000000u | (GetRValue(ELA_LCD_INK) << 16) | (GetGValue(ELA_LCD_INK) << 8) | GetBValue(ELA_LCD_INK);
        for (int i = 0; i < 20; ++i) {
            const int cx = ELA_CELL_X0 + i * ELA_CELL_W;
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 5; ++col) {
                    if (!e.la_dots[row * 120 + i * 6 + col]) continue;
                    for (int yy = std::max(0, 2 * row - 1); yy <= std::min(14, 2 * row + 1); ++yy)
                        for (int xx = std::max(0, 2 * col - 1); xx <= std::min(8, 2 * col + 1); ++xx)
                            g_dots_bits[(ELA_DOT_Y0 + yy) * LW + cx + xx] = ink;
                }
        }
        BLENDFUNCTION bd{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(dc, ELA_LX0, ELA_LY0, LW, LH, g_dots_la, 0, 0, LW, LH, bd);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(dc, ELA_LX0, ELA_LY0, ELA_LX1 - ELA_LX0, ELA_LY1 - ELA_LY0, g_glass_la, 0, 0, ELA_LX1 - ELA_LX0,
                   ELA_LY1 - ELA_LY0, bf);
        // switches held: the cap moves in a little
        const uint32_t held = e.held85 | e.latch85;
        for (const auto& s : ELA_SW)
            if (held & s.bit) {
                Fill(dc, s.cx - s.hw + 2, s.cy - s.hh + 2, 2 * s.hw - 3, 2 * s.hh - 3, RGB(0x0c, 0x0c, 0x0d));
                Fill(dc, s.cx - s.hw + 3, s.cy - s.hh + 3, 2 * s.hw - 5, 2 * s.hh - 5, RGB(0x30, 0x30, 0x34));
            }
    }
    {   // knob marker at the GAIN position: -135 deg (min) .. +135 deg (max), 0 dB at 12 o'clock
        const float gain = p.gain_db.load();
        const double t   = GainKnobFrame(gain) / 30.0;
        const double a   = (-135.0 + 270.0 * t) * 3.14159265358979 / 180.0;
        const double sx = std::sin(a), sy = -std::cos(a);
        const POINT c = ELA_KNOB[k];
        HPEN pen   = CreatePen(PS_SOLID, 2, k == 0 ? RGB(0xeb, 0xeb, 0xeb) : RGB(0xd7, 0xd7, 0xd7));
        HGDIOBJ op = SelectObject(dc, pen);
        const double r0 = k == 0 ? 10 : ELA_KNOB_R - 13, r1 = k == 0 ? ELA_KNOB_R - 2 : ELA_KNOB_R - 3; // marker near the rim
        MoveToEx(dc, c.x + static_cast<int>(std::lround(sx * r0)), c.y + static_cast<int>(std::lround(sy * r0)), nullptr);
        LineTo(dc, c.x + static_cast<int>(std::lround(sx * r1)), c.y + static_cast<int>(std::lround(sy * r1)));
        SelectObject(dc, op);
        DeleteObject(pen);
        char g[16];
        std::snprintf(g, sizeof(g), "%+.1f dB", std::fabs(gain) < 0.05f ? 0.0f : gain);
        Text(dc, k == 0 ? e.f_label : e.f_tiny, ELA_GAIN_INK[k], ELA_GAIN[k].left, ELA_GAIN[k].top, g, DT_CENTER,
             ELA_GAIN[k].right - ELA_GAIN[k].left, ELA_GAIN[k].bottom - ELA_GAIN[k].top);
    }
    DrawVoices88(e, dc, ELA_VOICES[k], ELA_UNITS[k], ELA_VLAYOUT);
    Box88(e, dc, ELA_SETUP, e.menu_open ? "SETUP  \x85" : "SETUP  >", C88_LED, true);
}

bool MouseMt32(Editor& e, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NukedSc55& p = *e.plugin;
    const auto send = [&] { p.ui_panel_buttons = e.held85 | e.latch85; };
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        const int h = HitMt32(e, x, y);
        if (h >= ELA_HIT_SW) {
            e.latch85   = 0; // a left-click switch lets go of the right-click ones (after this press)
            e.held85    = ELA_SW[h - ELA_HIT_SW].bit | p.ui_panel_buttons.load();
            e.release85 = false;
            e.press85   = GetTickCount();
            send();
            SetCapture(hwnd);
        } else if (h == ELA_HIT_KNOB) {
            if (msg == WM_LBUTTONDBLCLK) { // double-click: GAIN 0 dB
                p.gain_db = 0.0f;
                p.NotifyStateChanged();
                return true;
            }
            e.drag_vol = true;
            e.drag_x   = x;
            e.drag_y   = y;
            e.drag_v0  = p.gain_db.load();
            SetCapture(hwnd);
        } else if (h == ELA_HIT_SETUP) {
            ShowSetupMenu(e);
        } else if (h == ELA_HIT_NAME) {
            NextLaDevice(p);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }
    case WM_RBUTTONDOWN: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        const int h = HitMt32(e, x, y);
        if (h >= ELA_HIT_SW && !e.held85) { // right click: switch kept held (key combinations)
            e.latch85 ^= ELA_SW[h - ELA_HIT_SW].bit;
            send();
        } else if (h == ELA_HIT_KNOB && PanelLa(e) == 0 && !e.drag_vol) { // SELECT: the device's knob
            e.drag_sel = true;
            e.drag_x   = x;
            e.drag_y   = y;
            SetCapture(hwnd);
        }
        return true;
    }
    case WM_MOUSEMOVE:
        if (e.drag_sel) { // up / right = clockwise
            const int dx = (short)LOWORD(lp) - e.drag_x, dy = (short)HIWORD(lp) - e.drag_y;
            const int det = (dx - dy) / ELA_SELECT_PX;
            if (det != 0) {
                p.ui_encoder += det;
                e.drag_x += det * ELA_SELECT_PX; // remainder kept
            }
            return true;
        }
        return false;
    case WM_RBUTTONUP:
        if (e.drag_sel) {
            e.drag_sel = false;
            ReleaseCapture();
        }
        return true;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (msg == WM_CAPTURECHANGED) e.drag_sel = false;
        if (e.held85 && !e.release85) { // let go by the timer once held long enough
            e.release85 = true;
            if (GetTickCount() - e.press85 >= ELA_MIN_HOLD_MS) {
                e.held85 = 0;
                send();
            }
            if (msg == WM_LBUTTONUP && !e.drag_vol) ReleaseCapture();
        }
        return false; // GAIN drag: shared code (releases the capture)
    case WM_TIMER:
        if (e.release85 && e.held85 && GetTickCount() - e.press85 >= ELA_MIN_HOLD_MS) {
            e.held85 = 0;
            send();
        }
        return false;
    case WM_MOUSEWHEEL: {
        POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(hwnd, &pt);
        if (HitMt32(e, pt.x, pt.y) == ELA_HIT_KNOB) {
            const int dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
            if ((GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) && PanelLa(e) == 0) {
                p.ui_encoder += 2 * dir; // SELECT: about one value of 128 per notch
            } else { // GAIN, 0.1 dB per notch
                p.gain_db = GainWheelStep(p.gain_db.load(), dir);
                p.NotifyStateChanged();
            }
        }
        return true;
    }
    }
    return false;
}

} // namespace
