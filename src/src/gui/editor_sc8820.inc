// Nuked SC-8820 Poly - front panel (included by editor_win32.cpp in NUKED_SC55_DEVICE_8820 builds,
// after editor_sc88.inc).
//
// Background and lamp art: tools/make_sc8820_panel.py, drawn from scratch after photos of the
// device (918x280; --base = lamps off, --all-on = every lamp lit). The lamps of unit 0's firmware
// (NukedSc55::PanelLeds, ORed over ~40 ms because the firmware multiplexes them) select which
// regions are copied from the lit image. INST MAP and the VOLUME push (PREVIEW) drive unit 0's
// switches; the plugin adopts the map unit 0 shows and keeps the other units on it. The VOLUME
// knob turns the plugin GAIN (drag, wheel; double-click = 0 dB); a click without turning is the
// PREVIEW push, as on the device.

extern "C" const unsigned char nk_panel8820_b[] asm("nk_panel8820_b");
extern "C" const unsigned char nk_panel8820_e[] asm("nk_panel8820_e");
extern "C" const unsigned char nk_lit8820_b[] asm("nk_lit8820_b");
extern "C" const unsigned char nk_lit8820_e[] asm("nk_lit8820_e");

void ShowSetupMenu(Editor& e); // editor_win32.cpp

namespace {

// Geometry (panel pixels, see make_sc8820_panel.py)
constexpr POINT E20_INSTMAP = {228, 112};
constexpr int E20_INSTMAP_R = 13;
constexpr POINT E20_KNOB = {712, 118};
constexpr int E20_KNOB_R = 24;
constexpr RECT E20_VOICES = {84, 214, 196, 246}, E20_UNITS = {212, 218, 300, 242}, E20_GAIN = {356, 218, 434, 242};
constexpr RECT E20_SETUP = {800, 218, 890, 242};
constexpr RECT E20_POWER = {309, 70, 338, 99}, E20_USB = {368, 72, 401, 96};
constexpr double E20_GX = 508.0, E20_GW = 11.4;           // first dot column, column pitch
constexpr int E20_ROW_A = 56, E20_ROW_B = 94;             // first dot row of PART A / PART B
constexpr int E20_CLICK_PX = 3;                           // a knob click that moved less = PREVIEW push
constexpr DWORD E20_MIN_HOLD_MS = 120;                    // a short click still reaches the switch scan
constexpr uint32_t E20_BIT_INSTMAP = 1u, E20_BIT_PREVIEW = 2u;
constexpr int E20_HIT_INSTMAP = 1, E20_HIT_KNOB = 2, E20_HIT_SETUP = 3;

HDC g_lit20_dc = nullptr;

HDC Dib20(const unsigned char* b, const unsigned char* e, HBITMAP* bmp_out, HGDIOBJ* old_out)
{
    HDC screen = GetDC(nullptr);
    HDC dc     = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = Width;
    bi.bmiHeader.biHeight      = -Height;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits  = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    const size_t need = static_cast<size_t>(Width) * Height * 4;
    std::memcpy(bits, b, std::min(need, static_cast<size_t>(e - b)));
    if (bmp_out) *bmp_out = bmp;
    if (old_out) *old_out = old;
    return dc;
}

void BuildBackground8820(Editor& e)
{
    e.bg_dc = Dib20(nk_panel8820_b, nk_panel8820_e, &e.bg_bmp, &e.bg_old);
    if (!g_lit20_dc) g_lit20_dc = Dib20(nk_lit8820_b, nk_lit8820_e, nullptr, nullptr); // kept for the process
}

void Lit20(HDC dc, const RECT& r) { BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, g_lit20_dc, r.left, r.top, SRCCOPY); }

RECT Block20(int row, int block) // 3 columns x 3 rows of dots, with their glow
{
    const int x0 = static_cast<int>(E20_GX + block * 3 * E20_GW) - 7;
    const int x1 = static_cast<int>(E20_GX + (block * 3 + 2) * E20_GW) + 8;
    return {x0, row - 7, x1, row + 20};
}

int Hit8820(int x, int y)
{
    if (InCircle(E20_INSTMAP, E20_INSTMAP_R, x, y)) return E20_HIT_INSTMAP;
    if (InCircle(E20_KNOB, E20_KNOB_R, x, y)) return E20_HIT_KNOB;
    if (InRect(E20_SETUP, x, y)) return E20_HIT_SETUP;
    return 0;
}

void Draw8820(Editor& e, HDC dc)
{
    NukedSc55& p = *e.plugin;
    if (!e.bg_dc) BuildBackground8820(e);
    BitBlt(dc, 0, 0, Width, Height, e.bg_dc, 0, 0, SRCCOPY);

    // lamps: bits 0-3 PART A, 4-7 PART B (level, or the map after INST MAP), POWER; USB shown as
    // connected (the emulation has no USB host, the firmware would blink it)
    const uint32_t leds = p.PanelLeds();
    Lit20(dc, E20_POWER);
    Lit20(dc, E20_USB);
    for (int k = 0; k < 4; ++k) {
        if ((leds >> k) & 1u) Lit20(dc, Block20(E20_ROW_A, k));
        if ((leds >> (4 + k)) & 1u) Lit20(dc, Block20(E20_ROW_B, k));
    }
    if (e.held85 & E20_BIT_INSTMAP) { // pressed: the cap moves in a little
        HBRUSH b = CreateSolidBrush(RGB(0xa8, 0xa8, 0xa6));
        HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, E20_INSTMAP.x - 8, E20_INSTMAP.y - 8, E20_INSTMAP.x + 9, E20_INSTMAP.y + 9);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b);
    }
    {   // VOLUME knob marker at the GAIN position: -135 deg (min) .. +135 deg (max), 0 dB at the top
        const float gain = p.gain_db.load();
        const double t   = GainKnobFrame(gain) / 30.0;                     // 0..1
        const double a   = (-135.0 + 270.0 * t) * 3.14159265358979 / 180.0;
        const double sx = std::sin(a), sy = -std::cos(a);
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(0xe8, 0xd6, 0x78));
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, E20_KNOB.x + static_cast<int>(sx * 13), E20_KNOB.y + static_cast<int>(sy * 13), nullptr);
        LineTo(dc, E20_KNOB.x + static_cast<int>(sx * 20), E20_KNOB.y + static_cast<int>(sy * 20));
        SelectObject(dc, op);
        DeleteObject(pen);
        char g[16];
        std::snprintf(g, sizeof(g), "%+.1f dB", std::fabs(gain) < 0.05f ? 0.0f : gain);
        Box88(e, dc, E20_GAIN, g, C88_LED, true);
    }
    {   // VOICES / UNITS
        NukedSc55& pl = p;
        char v[32];
        const int cap = pl.PartialsPerInstance(), units = (pl.max_voices.load() + cap - 1) / cap;
        std::snprintf(v, sizeof(v), "%03d/%d", pl.ui_voices.load(), units * cap);
        Text(dc, e.f_value, C88_LED, E20_VOICES.left, E20_VOICES.top + 4, v, DT_CENTER,
             E20_VOICES.right - E20_VOICES.left, E20_VOICES.bottom - E20_VOICES.top - 8);
        const int awake = pl.ui_awake.load();
        if (awake > units) std::snprintf(v, sizeof(v), "UNITS %d->%d", awake, units);
        else std::snprintf(v, sizeof(v), "UNITS %d/%d", awake, units);
        Box88(e, dc, E20_UNITS, v, C88_LED, true);
    }
    Box88(e, dc, E20_SETUP, e.menu_open ? "SETUP  \x85" : "SETUP  >", C88_LED, true);
}

bool Mouse8820(Editor& e, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NukedSc55& p = *e.plugin;
    const auto send = [&] { p.ui_panel_buttons = e.held85; };
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        const int h = Hit8820(x, y);
        if (h == E20_HIT_INSTMAP) {
            e.held85    = E20_BIT_INSTMAP;
            e.release85 = false;
            e.press85   = GetTickCount();
            send();
            SetCapture(hwnd);
        } else if (h == E20_HIT_KNOB) {
            if (msg == WM_LBUTTONDBLCLK) { // double-click: GAIN 0 dB
                p.gain_db = 0.0f;
                p.NotifyStateChanged();
                return true;
            }
            e.drag_vol  = true; // turning; a click without turning pushes the knob (PREVIEW)
            e.drag_x    = x;
            e.drag_y    = y;
            e.drag_v0   = p.gain_db.load();
            e.held85    = E20_BIT_PREVIEW;
            e.release85 = false;
            e.press85   = GetTickCount();
            send();
            SetCapture(hwnd);
        } else if (h == E20_HIT_SETUP) {
            ShowSetupMenu(e);
        }
        return true;
    }
    case WM_MOUSEMOVE:
        if (e.drag_vol && (e.held85 & E20_BIT_PREVIEW)) {
            const int dx = (short)LOWORD(lp) - e.drag_x, dy = (short)HIWORD(lp) - e.drag_y;
            if (std::abs(dx) > E20_CLICK_PX || std::abs(dy) > E20_CLICK_PX) { // turning, not pushing
                e.held85 = 0;
                send();
            }
        }
        return false; // GAIN drag: shared code
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (e.held85 && !e.release85) { // let go by the timer once held long enough
            e.release85 = true;
            if (GetTickCount() - e.press85 >= E20_MIN_HOLD_MS) {
                e.held85 = 0;
                send();
            }
            if (msg == WM_LBUTTONUP && !e.drag_vol) ReleaseCapture();
        }
        return false; // GAIN drag: shared code (releases the capture)
    case WM_TIMER:
        if (e.release85 && e.held85 && GetTickCount() - e.press85 >= E20_MIN_HOLD_MS) {
            e.held85 = 0;
            send();
        }
        return false;
    case WM_MOUSEWHEEL: {
        POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(hwnd, &pt);
        if (Hit8820(pt.x, pt.y) == E20_HIT_KNOB) { // one knob position (of 31) per notch
            const int dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
            p.gain_db = GainOfKnobFrame(GainKnobFrame(p.gain_db.load()) + dir);
            p.NotifyStateChanged();
        }
        return true;
    }
    }
    return false;
}

} // namespace
