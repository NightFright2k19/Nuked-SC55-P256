// Nuked SC-8850 Poly - front panel (included by editor_win32.cpp in
// NUKED_SC55_DEVICE_8850 builds, after editor_sc88.inc).
//
// Background: the 88emu SC-8850 panel art (The Usual Suspects, GPLv3), prepared
// at build time by tools/make_sc8850_panel.py (918x280, branding replaced,
// playlist -> VOICES, VOLUME -> GAIN). The controls are the player's sprites
// (tools/make_sc8850_sprites.py) at the positions of its skin. All switches and
// the VALUE encoder drive unit 0's own panel: the firmware runs its menus, the
// graphic LCD and the LEDs, and the plugin passes panel edits on to the other
// units (NukedSc55::E88SyncPump).

extern "C" const unsigned char nk_panel8850_b[] asm("nk_panel8850_b");
extern "C" const unsigned char nk_panel8850_e[] asm("nk_panel8850_e");
extern "C" const unsigned char nk_sprites8850_b[] asm("nk_sprites8850_b");
extern "C" const unsigned char nk_sprites8850_e[] asm("nk_sprites8850_e");

void ShowSetupMenu(Editor& e); // editor_win32.cpp

namespace {

// Geometry: player skin (dp) x 1.5
constexpr double E85_LCD_X = 247.5, E85_LCD_Y = 42.0, E85_LCD_W = 262.5, E85_LCD_H = 105.0; // 175x70 dp
constexpr int E85_LCD_PX = 247, E85_LCD_PY = 42, E85_LCD_PW = 264, E85_LCD_PH = 105;           // pixels touched
constexpr RECT E85_VOICES = {29, 38, 142, 145};
constexpr RECT E85_UNITS  = {48, 152, 126, 172};
constexpr RECT E85_SETUP  = P88_SETUP_RIGHT;
constexpr POINT E85_GAIN = {189, 57}, E85_PREVIEW = {189, 133};
constexpr int E85_GAIN_R = 21, E85_PREVIEW_R = 18, E85_GAIN_TEXT_Y = 81;
constexpr double E85_VALUE_CX = 832.5, E85_VALUE_CY = 82.5, E85_VALUE_R = 57.0;
constexpr int E85_VALUE_X = 775, E85_VALUE_Y = 25;   // sprite origin (116x116)
constexpr double E85_DETENT_PX = 10.5;               // drag per encoder detent (player: 224 dp / 32)
constexpr int E85_CLICK_PX = 4;                       // a VALUE click that moved less pushes the encoder
constexpr int E85_PUSH_FRAMES = 3;                    // push pulse, editor timer frames (~80 ms)
constexpr DWORD E85_MIN_HOLD_MS = 80;                 // a short click still reaches the panel scan
constexpr int E85_BIT_VALUE = 19, E85_BIT_PREVIEW = 20;

// The 19 panel switches: 17x17 dp box (left/top), 88emu Sc8850Button bit, LED bit (-1 = none)
struct E85Btn { short l, t; uint8_t bit; int8_t led; };
constexpr E85Btn E85_BTNS[] = {
    {400, 20, 0, 0},   {439, 20, 1, -1},  {478, 20, 2, -1},              // EDIT, PART <, PART >
    {400, 46, 3, 1},   {439, 46, 4, -1},  {478, 46, 5, -1},              // DRUM, VARIATION, INSTRUMENT
    {400, 72, 6, 2},   {439, 72, 7, -1},  {478, 72, 8, -1},              // EFFECTS, EXIT, ENTER
    {400, 102, 9, 3},  {439, 102, 10, 4}, {478, 102, 11, 5},             // SHIFT, SOLO, MUTE
    {517, 102, 12, -1}, {556, 102, 13, -1},                              // DEC, INC
    {178, 149, 14, -1}, {222, 149, 15, -1}, {266, 149, 16, -1}, {310, 149, 17, -1}, // F1..F4
    {354, 149, 18, -1},                                                  // INST MAP
};
constexpr int E85_NBTN = sizeof(E85_BTNS) / sizeof(E85_BTNS[0]);
// Hit ids: 1..19 switches, then:
constexpr int E85_HIT_PREVIEW = 20, E85_HIT_VALUE = 21, E85_HIT_GAIN = 22, E85_HIT_SETUP = 23;

// Sprite sheet (make_sc8850_sprites.py order), stacked in one DIB 116 px wide
constexpr int E85_SW = 116;
constexpr int E85_BTN_SZ = 38, E85_PRV_SZ = 44, E85_KNOB_SZ = 42, E85_VAL_SZ = 116;
constexpr int E85_Y_PRV  = 24 * E85_BTN_SZ;
constexpr int E85_Y_KNOB = E85_Y_PRV + 2 * E85_PRV_SZ;
constexpr int E85_Y_VAL  = E85_Y_KNOB + 31 * E85_KNOB_SZ;
constexpr int E85_SH     = E85_Y_VAL + 4 * E85_VAL_SZ;
HDC g_sp85_dc = nullptr;

void BuildSprites8850()
{
    if (g_sp85_dc) return;
    HDC screen = GetDC(nullptr);
    g_sp85_dc  = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = E85_SW;
    bi.bmiHeader.biHeight      = -E85_SH;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits   = nullptr;
    HBITMAP bmp  = CreateDIBSection(g_sp85_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(g_sp85_dc, bmp);
    auto* dst = static_cast<uint8_t*>(bits);
    std::memset(dst, 0, size_t(E85_SW) * E85_SH * 4);
    const uint8_t* src = nk_sprites8850_b;
    const uint8_t* end = nk_sprites8850_e;
    const auto copy = [&](int y0, int size) {
        for (int y = 0; y < size && src + size * 4 <= end; ++y, src += size * 4)
            std::memcpy(dst + (size_t(y0 + y) * E85_SW) * 4, src, size_t(size) * 4);
    };
    for (int i = 0; i < 24; ++i) copy(i * E85_BTN_SZ, E85_BTN_SZ);
    for (int i = 0; i < 2; ++i) copy(E85_Y_PRV + i * E85_PRV_SZ, E85_PRV_SZ);
    for (int f = 0; f < 31; ++f) copy(E85_Y_KNOB + f * E85_KNOB_SZ, E85_KNOB_SZ);
    for (int f = 0; f < 4; ++f) copy(E85_Y_VAL + f * E85_VAL_SZ, E85_VAL_SZ);
}

void Blend85(HDC dc, int x, int y, int sy, int sz, BYTE alpha = 255)
{
    BuildSprites8850();
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, sz, sz, g_sp85_dc, 0, sy, sz, sz, bf);
}

// face: 0 = plain switch, 1 = LED off, 2 = LED on
void Switch85(HDC dc, const E85Btn& b, int face, bool pressed)
{
    const int x2 = b.l * 3, y2 = b.t * 3;                        // 2 x pixel position
    const int phase = (x2 & 1) | ((y2 & 1) << 1);               // half-pixel phases
    const int sy = ((phase * 3 + face) * 2 + (pressed ? 1 : 0)) * E85_BTN_SZ;
    Blend85(dc, x2 / 2 - 4, y2 / 2 - 4, sy, E85_BTN_SZ);
}

POINT Centre85(const E85Btn& b)
{
    return {static_cast<LONG>((b.l + 8.5) * 1.5), static_cast<LONG>((b.t + 8.5) * 1.5)};
}

int Hit8850(int x, int y)
{
    for (int i = 0; i < E85_NBTN; ++i) {
        const POINT c = Centre85(E85_BTNS[i]);
        if ((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= 14 * 14) return i + 1;
    }
    if ((x - E85_PREVIEW.x) * (x - E85_PREVIEW.x) + (y - E85_PREVIEW.y) * (y - E85_PREVIEW.y) <=
        E85_PREVIEW_R * E85_PREVIEW_R) return E85_HIT_PREVIEW;
    const double vx = x - E85_VALUE_CX, vy = y - E85_VALUE_CY;
    if (vx * vx + vy * vy <= E85_VALUE_R * E85_VALUE_R) return E85_HIT_VALUE;
    if ((x - E85_GAIN.x) * (x - E85_GAIN.x) + (y - E85_GAIN.y) * (y - E85_GAIN.y) <= E85_GAIN_R * E85_GAIN_R)
        return E85_HIT_GAIN;
    if (x >= E85_SETUP.left && x < E85_SETUP.right && y >= E85_SETUP.top && y < E85_SETUP.bottom)
        return E85_HIT_SETUP;
    return 0;
}

void BuildBackground8850(Editor& e)
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
    const size_t have = static_cast<size_t>(nk_panel8850_e - nk_panel8850_b);
    std::memcpy(bits, nk_panel8850_b, std::min(need, have));
}

// Graphic LCD: the player draws lit dots black straight onto the orange glass,
// edge to edge (4 px pitch, no gap). Here every panel pixel takes the share of
// its area that lit dots cover (160 x 64 dots on 262.5 x 105 px).
struct Lcd85 {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    uint32_t* px = nullptr;
    uint8_t dots[NukedSc55::kLcdW * NukedSc55::kLcdH] = {};
    uint8_t shown[NukedSc55::kLcdW * NukedSc55::kLcdH] = {};
    bool valid = false, on = false;
    // per pixel column/row: first dot and weights of up to 2 dots
    int x0[E85_LCD_PW], y0[E85_LCD_PH];
    float wx[E85_LCD_PW][2], wy[E85_LCD_PH][2];
};

void Weights85(int n, int first_px, double org, double len, int dots, int* d0, float (*w)[2])
{
    const double pitch = len / dots;
    for (int i = 0; i < n; ++i) {
        const double a = first_px + i - org, b = a + 1.0; // pixel span in LCD coordinates
        const int k = static_cast<int>(std::floor(a / pitch));
        d0[i] = k;
        for (int j = 0; j < 2; ++j) {
            const double lo = std::max(a, (k + j) * pitch), hi = std::min(b, (k + j + 1) * pitch);
            w[i][j] = (k + j >= 0 && k + j < dots && hi > lo) ? static_cast<float>(hi - lo) : 0.0f;
        }
    }
}

Lcd85* MakeLcd85()
{
    auto* l = new Lcd85();
    HDC screen = GetDC(nullptr);
    l->dc      = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = E85_LCD_PW;
    bi.bmiHeader.biHeight      = -E85_LCD_PH;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    l->bmp     = CreateDIBSection(l->dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    l->old     = SelectObject(l->dc, l->bmp);
    l->px      = static_cast<uint32_t*>(bits);
    Weights85(E85_LCD_PW, E85_LCD_PX, E85_LCD_X, E85_LCD_W, NukedSc55::kLcdW, l->x0, l->wx);
    Weights85(E85_LCD_PH, E85_LCD_PY, E85_LCD_Y, E85_LCD_H, NukedSc55::kLcdH, l->y0, l->wy);
    return l;
}

void FreeLcd85(void* p)
{
    auto* l = static_cast<Lcd85*>(p);
    if (!l) return;
    SelectObject(l->dc, l->old);
    DeleteObject(l->bmp);
    DeleteDC(l->dc);
    delete l;
}

void DrawLcd85(Editor& e, HDC dc)
{
    if (!e.lcd85) e.lcd85 = MakeLcd85();
    auto& l = *static_cast<Lcd85*>(e.lcd85);
    bool on = false;
    if (!e.plugin->GetLcdDots(l.dots, sizeof(l.dots), on) || !on) return; // glass only
    if (!l.valid || std::memcmp(l.dots, l.shown, sizeof(l.dots)) != 0) {
        std::memcpy(l.shown, l.dots, sizeof(l.dots));
        l.valid = true;
        const auto* bg = reinterpret_cast<const uint32_t*>(nk_panel8850_b);
        constexpr int W = NukedSc55::kLcdW, H = NukedSc55::kLcdH;
        for (int y = 0; y < E85_LCD_PH; ++y) {
            const int dy = l.y0[y];
            for (int x = 0; x < E85_LCD_PW; ++x) {
                const int dx = l.x0[x];
                float cov = 0.0f;
                for (int j = 0; j < 2; ++j) {
                    if (l.wy[y][j] <= 0.0f) continue;
                    const uint8_t* row = l.shown + (dy + j) * W;
                    for (int i = 0; i < 2; ++i)
                        if (l.wx[x][i] > 0.0f && row[dx + i]) cov += l.wx[x][i] * l.wy[y][j];
                }
                (void)H;
                const uint32_t c = bg[(E85_LCD_PY + y) * Width + E85_LCD_PX + x];
                const float k    = 1.0f - std::min(cov, 1.0f);
                const auto ch = [&](int sh) { return static_cast<uint32_t>(((c >> sh) & 0xff) * k + 0.5f) << sh; };
                l.px[y * E85_LCD_PW + x] = ch(0) | ch(8) | ch(16) | 0xff000000u;
            }
        }
        GdiFlush();
    }
    BitBlt(dc, E85_LCD_PX, E85_LCD_PY, E85_LCD_PW, E85_LCD_PH, l.dc, 0, 0, SRCCOPY);
}

// Dark recessed slot (like the UNITS slot of the art) with centred orange text
void Slot85(Editor& e, HDC dc, const RECT& r, const char* s)
{
    Fill(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, RGB(0x1c, 0x1d, 0x20));
    Fill(dc, r.left, r.top, r.right - r.left, 1, RGB(0x0a, 0x0a, 0x0c));
    Fill(dc, r.left, r.bottom - 1, r.right - r.left, 1, RGB(0x8c, 0x8e, 0x92));
    Box88(e, dc, r, s, C88_LED, true);
}

uint32_t PanelButtons85(const Editor& e)
{
    return e.held85 | (e.push85 > 0 ? (1u << E85_BIT_VALUE) : 0u);
}

void Draw8850(Editor& e, HDC dc)
{
    NukedSc55& p = *e.plugin;
    if (!e.bg_dc) BuildBackground8850(e);
    BitBlt(dc, 0, 0, Width, Height, e.bg_dc, 0, 0, SRCCOPY);
    DrawLcd85(e, dc);

    // Switches: faces as the firmware lights them, darker while held
    const uint32_t leds = p.PanelLeds();
    for (const auto& b : E85_BTNS) {
        const int face = b.led < 0 ? 0 : ((leds >> b.led) & 1u) ? 2 : 1;
        Switch85(dc, b, face, (e.held85 >> b.bit) & 1u);
    }
    Blend85(dc, 169, 111, E85_Y_PRV + ((e.held85 >> E85_BIT_PREVIEW) & 1u) * E85_PRV_SZ, E85_PRV_SZ);
    Blend85(dc, E85_VALUE_X, E85_VALUE_Y, E85_Y_VAL + (e.value85 & 3) * E85_VAL_SZ, E85_VAL_SZ,
            e.push85 > 0 ? 200 : 255);
    const float gain = p.gain_db.load();
    Blend85(dc, E85_GAIN.x - 21, E85_GAIN.y - 21, E85_Y_KNOB + GainKnobFrame(gain) * E85_KNOB_SZ, E85_KNOB_SZ);
    {   // GAIN readout under the knob, printed like the panel labels
        char g[16];
        std::snprintf(g, sizeof(g), "%+.1f dB", std::fabs(gain) < 0.05f ? 0.0f : gain);
        Text(dc, e.f_tiny, RGB(0x1c, 0x1c, 0x1c), E85_GAIN.x - 30, E85_GAIN_TEXT_Y, g, DT_CENTER, 60, 12);
    }

    DrawVoices88(e, dc, E85_VOICES, E85_UNITS);
    Slot85(e, dc, E85_SETUP, e.menu_open ? "SETUP  \x85" : "SETUP  >");
}

// Mouse input of the SC-8850 panel; true = handled. Switches are held while the
// mouse button is down (the firmware sees press and release), the VALUE encoder
// turns by dragging (right/up = clockwise) or the wheel, and a click on it that
// does not move pushes it (a short pulse after the release, as in the player:
// push + turn is the firmware's coarse mode).
bool Mouse8850(Editor& e, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NukedSc55& p = *e.plugin;
    const auto send = [&] { p.ui_panel_buttons = PanelButtons85(e); };
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        const int h = Hit8850(x, y);
        if ((h >= 1 && h <= E85_NBTN) || h == E85_HIT_PREVIEW) {
            e.held85 |= 1u << (h == E85_HIT_PREVIEW ? E85_BIT_PREVIEW : E85_BTNS[h - 1].bit);
            e.release85 = false;
            e.press85   = GetTickCount();
            send();
            SetCapture(hwnd);
        } else if (h == E85_HIT_VALUE) {
            e.drag85  = true;
            e.moved85 = false;
            e.drag_x  = x;
            e.drag_y  = y;
            e.steps85 = 0;
            SetCapture(hwnd);
        } else if (h == E85_HIT_GAIN) { // drag; double-click = 0 dB
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
        } else if (h == E85_HIT_SETUP) {
            ShowSetupMenu(e);
        }
        return true;
    }
    case WM_MOUSEMOVE:
        if (e.drag85) {
            const int dx = (short)LOWORD(lp) - e.drag_x, dy = (short)HIWORD(lp) - e.drag_y;
            if (std::abs(dx) > E85_CLICK_PX || std::abs(dy) > E85_CLICK_PX) e.moved85 = true;
            const int steps = static_cast<int>(std::trunc((dx - dy) / E85_DETENT_PX));
            if (steps != e.steps85) {
                p.ui_encoder.fetch_add(steps - e.steps85);
                e.value85 += steps - e.steps85;
                e.steps85 = steps;
            }
            return true;
        }
        return false; // GAIN drag: shared code
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (e.held85 && !e.release85) { // released by the timer once held long enough
            e.release85 = true;
            if (GetTickCount() - e.press85 >= E85_MIN_HOLD_MS) {
                e.held85 = 0;
                send();
            }
            if (msg == WM_LBUTTONUP) ReleaseCapture();
        }
        if (e.drag85) {
            e.drag85 = false;
            if (!e.moved85 && msg == WM_LBUTTONUP) {
                e.push85 = E85_PUSH_FRAMES;
                send();
            }
            if (msg == WM_LBUTTONUP) ReleaseCapture();
        }
        return false; // GAIN drag: shared code
    case WM_TIMER:
        if (e.push85 > 0 && --e.push85 == 0) send();
        if (e.release85 && e.held85 && GetTickCount() - e.press85 >= E85_MIN_HOLD_MS) {
            e.held85 = 0;
            send();
        }
        return false;
    case WM_MOUSEWHEEL: {
        POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(hwnd, &pt);
        const int dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        const int h   = Hit8850(pt.x, pt.y);
        if (h == E85_HIT_VALUE) {
            p.ui_encoder.fetch_add(dir);
            e.value85 += dir;
        } else if (h == E85_HIT_GAIN) { // one knob position (of 31, 0.8 dB) per notch
            p.gain_db = GainOfKnobFrame(GainKnobFrame(p.gain_db.load()) + dir);
            p.NotifyStateChanged();
        }
        return true;
    }
    }
    return false;
}

} // namespace
