// Nuked SC-8820 Poly - front panel (included by editor_win32.cpp in NUKED_SC55_DEVICE_8820 builds,
// after editor_sc88.inc).
//
// Background and lamp art: tools/make_sc8820_panel.py, drawn from scratch after photos of the
// device (918x280; --base = lamps off, --all-on = every lamp lit). The lamps of unit 0's firmware
// (NukedSc55::PanelLeds, ORed over ~40 ms because the firmware multiplexes them) select which
// regions are copied from the lit image. INST MAP and the VOLUME push (PREVIEW) drive unit 0's
// switches; the plugin adopts the map unit 0 shows and keeps the other units on it. The VOLUME
// knob is labelled GAIN and turns the plugin's output gain (left-drag, wheel; double-click = 0 dB), always
// silent. The device's PREVIEW (pushing the knob) is a right click on the knob here, because turning
// needs the left button (issue #48). VOICES / UNITS sit left of INST MAP like on the other panels; the
// GAIN value is printed under the knob.
//
// Nuked-SC8820 Pro (NUKED_SC55_8820_PRO, issue #49): the same panel drawn with make_sc8820_panel.py
// --pro: a 16-part level display in the window (the port of the selected part, click = select a
// part), and in the lower strip a part display (part, program, instrument or drum set, bank MSB /
// LSB, level, pan, reverb, chorus; read from unit 0's work RAM by NukedSc55::PartInfo) with edit
// buttons for PART, PROGRAM, BANK MSB and BANK LSB. The values also follow the mouse wheel, and
// LEVEL / PAN / REVERB / CHORUS a vertical drag. Edits are sent as MIDI to all units.

extern "C" const unsigned char nk_panel8820_b[] asm("nk_panel8820_b");
extern "C" const unsigned char nk_panel8820_e[] asm("nk_panel8820_e");
extern "C" const unsigned char nk_lit8820_b[] asm("nk_lit8820_b");
extern "C" const unsigned char nk_lit8820_e[] asm("nk_lit8820_e");

void ShowSetupMenu(Editor& e); // editor_win32.cpp

namespace {

// Geometry (panel pixels, see make_sc8820_panel.py)
constexpr POINT E20_INSTMAP = {215, 112};
constexpr int E20_INSTMAP_R = 13;
constexpr POINT E20_KNOB = {712, 118};
constexpr int E20_KNOB_R = 24;
constexpr RECT E20_VOICES = {33, 37, 149, 132}, E20_UNITS = {36, 140, 146, 160}; // DrawVoices88 window
constexpr RECT E20_GAIN = {682, 146, 742, 160};                                     // value under the knob
constexpr COLORREF E20_INK = RGB(0x30, 0x2e, 0x2c);                                 // front print
#ifdef NUKED_SC55_8820_PRO
constexpr RECT E20_SETUP = {812, 240, 900, 264};
constexpr RECT E20_POWER = {322, 42, 347, 66}, E20_USB = {376, 44, 405, 64};
#else
constexpr RECT E20_SETUP = {800, 218, 890, 242};
constexpr RECT E20_POWER = {309, 70, 338, 99}, E20_USB = {368, 72, 401, 96};
#endif
constexpr double E20_GX = 508.0, E20_GW = 11.4;           // first dot column, column pitch
constexpr int E20_ROW_A = 56, E20_ROW_B = 94;             // first dot row of PART A / PART B
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


#ifdef NUKED_SC55_8820_PRO
// ---- Pro: level display, part display, edit buttons (geometry: make_sc8820_panel.py --pro) ------
constexpr double E20P_EQ_X = 291.5, E20P_EQ_DX = 7.6, E20P_EQ_Y = 126.0, E20P_EQ_DY = 7.0;
constexpr RECT E20P_EQ = {287, 70, 410, 140};                       // click: select a part
// value fields (label printed in the background, value drawn here)
enum { F20_PART, F20_PROG, F20_NAME, F20_MSB, F20_LSB, F20_LEVEL, F20_PAN, F20_REV, F20_CHO, F20_N };
constexpr RECT E20P_FIELD[F20_N] = {
        {268, 206, 312, 238}, {312, 206, 356, 238}, {356, 206, 582, 238}, {268, 238, 312, 272}, {312, 238, 356, 272},
        {356, 238, 412, 272}, {412, 238, 468, 272}, {468, 238, 524, 272}, {524, 238, 582, 272}};
constexpr int E20P_VALUE_DY = 9;                                     // value under its label
// edit buttons: PART, PROGRAM, BANK MSB, BANK LSB; centre of each pill
constexpr POINT E20P_PILL[4] = {{640, 220}, {736, 220}, {640, 256}, {736, 256}};
constexpr int E20P_PILL_FIELD[4] = {F20_PART, F20_PROG, F20_MSB, F20_LSB};
constexpr DWORD E20P_REPEAT_DELAY = 400, E20P_REPEAT_MS = 90;      // held edit button
constexpr DWORD E20P_PENDING_MS = 1500;                              // value just sent shown that long
constexpr int E20_HIT_PRO = 100;                                     // + button (1..8)

// pending index of a field (PROG .. CHORUS), -1 for PART / NAME
int Pend20(int f)
{
    switch (f) {
    case F20_PROG: case F20_NAME: return 0;
    case F20_MSB: return 1;
    case F20_LSB: return 2;
    case F20_LEVEL: return 3;
    case F20_PAN: return 4;
    case F20_REV: return 5;
    case F20_CHO: return 6;
    }
    return -1;
}

int Value20(Editor& e, const NukedSc55::PartInfo8820& pi, int f) // current value, a pending edit first
{
    const int k = Pend20(f);
    if (k >= 0 && GetTickCount() < e.pend_until[e.part][k]) return e.pend_val[e.part][k];
    switch (f) {
    case F20_PROG: case F20_NAME: return pi.prog;
    case F20_MSB: return pi.msb;
    case F20_LSB: return pi.lsb;
    case F20_LEVEL: return pi.vol;
    case F20_PAN: return pi.pan;
    case F20_REV: return pi.rev;
    case F20_CHO: return pi.cho;
    }
    return 0;
}

// Sets a field of the selected part to v (PART: selects the part) and sends it to all units
void Set20(Editor& e, int f, int v)
{
    NukedSc55& p = *e.plugin;
    if (f == F20_PART) {
        e.part = ((v % 32) + 32) % 32;
        return;
    }
    const int k = Pend20(f);
    if (k < 0) return;
    const auto pi = p.PartInfo(e.part);
    const int ch = e.part % 16, port = e.part / 16;
    const uint8_t cc = static_cast<uint8_t>(0xb0 | ch);
    int msb = Value20(e, pi, F20_MSB), lsb = Value20(e, pi, F20_LSB), prog = Value20(e, pi, F20_PROG);
    switch (f) {
    case F20_PROG: case F20_NAME: v = ((v % 128) + 128) % 128; prog = v; break; // wraps like the SC-88 Pro
    case F20_MSB: v = std::clamp(v, 0, 127); msb = v; break;
    case F20_LSB: v = std::clamp(v, 0, 4); lsb = v; break;   // 0 = INST MAP, 1..4 = SC-55 .. SC-8820
    default: v = std::clamp(v, 0, 127); break;
    }
    if (v == Value20(e, pi, f)) return;
    e.pend_val[e.part][k]   = v;
    e.pend_until[e.part][k] = GetTickCount() + E20P_PENDING_MS;
    switch (f) {
    case F20_PROG: case F20_NAME: case F20_MSB: case F20_LSB: // the bank takes effect with the program change
        p.UiSendShort(cc, 0, static_cast<uint8_t>(msb), port);
        p.UiSendShort(cc, 32, static_cast<uint8_t>(lsb), port);
        p.UiSendShort(static_cast<uint8_t>(0xc0 | ch), static_cast<uint8_t>(prog), 0, port);
        break;
    case F20_LEVEL: p.UiSendShort(cc, 7, static_cast<uint8_t>(v), port); break;
    case F20_PAN: p.UiSendShort(cc, 10, static_cast<uint8_t>(v), port); break;
    case F20_REV: p.UiSendShort(cc, 91, static_cast<uint8_t>(v), port); break;
    case F20_CHO: p.UiSendShort(cc, 93, static_cast<uint8_t>(v), port); break;
    }
}

void Step20(Editor& e, int f, int dir)
{
    if (f == F20_PART) { Set20(e, f, e.part + dir); return; }
    Set20(e, f, Value20(e, e.plugin->PartInfo(e.part), f) + dir);
}

int Field20(int x, int y)
{
    for (int f = 0; f < F20_N; ++f)
        if (InRect(E20P_FIELD[f], x, y)) return f;
    return -1;
}

int Pill20(int x, int y) // edit button 1..8 (odd = left / down, even = right / up), 0 = none
{
    for (int k = 0; k < 4; ++k) {
        const POINT c = E20P_PILL[k];
        if (y < c.y - 8 || y > c.y + 8) continue;
        if (x >= c.x - 36 && x <= c.x - 2) return 1 + 2 * k;
        if (x >= c.x + 2 && x <= c.x + 36) return 2 + 2 * k;
    }
    return 0;
}

void Button20(Editor& e, int b) { Step20(e, E20P_PILL_FIELD[(b - 1) / 2], (b & 1) ? -1 : 1); }

void DrawPro20(Editor& e, HDC dc)
{
    NukedSc55& p   = *e.plugin;
    const int port = e.part / 16;
    // level display: 16 columns x 8 dots of the selected part's port, lit dots copied from the lamp image
    for (int c = 0; c < 16; ++c) {
        const float lv = e.disp32[port * 16 + c];
        const int n    = lv <= 0.0f ? 0 : std::clamp(static_cast<int>(std::ceil(lv * 8.0f / 127.0f)), 1, 8);
        for (int r = 0; r < n; ++r) {
            const double x = E20P_EQ_X + c * E20P_EQ_DX, y = E20P_EQ_Y - r * E20P_EQ_DY;
            Lit20(dc, {static_cast<int>(x - 3.8), static_cast<int>(y - 3.5), static_cast<int>(x + 3.8) + 1,
                       static_cast<int>(y + 3.5) + 1});
        }
    }
    Text(dc, e.f_tiny, C88_LED, static_cast<int>(E20P_EQ_X) - 14, static_cast<int>(E20P_EQ_Y) + 4, port ? "B" : "A");
    {   // marker under the selected part
        const int x = static_cast<int>(E20P_EQ_X + (e.part % 16) * E20P_EQ_DX);
        Fill(dc, x - 2, static_cast<int>(E20P_EQ_Y) + 16, 5, 2, C88_LED);
    }
    // part display
    const auto pi = p.PartInfo(e.part);
    char t[32];
    auto value = [&](int f, const char* s) {
        const RECT& r = E20P_FIELD[f];
        Text(dc, e.f_pro, C88_LED, r.left + 2, r.top + 4 + E20P_VALUE_DY, s, DT_LEFT, r.right - r.left, 20);
    };
    std::snprintf(t, sizeof(t), "%c%02d", port ? 'B' : 'A', e.part % 16 + 1);
    value(F20_PART, t);
    std::snprintf(t, sizeof(t), "%03d", Value20(e, pi, F20_PROG) + 1);
    value(F20_PROG, t);
    value(F20_NAME, pi.valid ? pi.name : "---");
    std::snprintf(t, sizeof(t), "%03d", Value20(e, pi, F20_MSB));
    value(F20_MSB, t);
    std::snprintf(t, sizeof(t), "%03d", Value20(e, pi, F20_LSB));
    value(F20_LSB, t);
    std::snprintf(t, sizeof(t), "%d", Value20(e, pi, F20_LEVEL));
    value(F20_LEVEL, t);
    const int pan = Value20(e, pi, F20_PAN) - 64;
    if (pan == 0) std::snprintf(t, sizeof(t), "0");
    else std::snprintf(t, sizeof(t), "%c%d", pan < 0 ? 'L' : 'R', pan < 0 ? -pan : pan);
    if (Value20(e, pi, F20_PAN) == 0) std::snprintf(t, sizeof(t), "RND"); // GS: 0 = random
    value(F20_PAN, t);
    std::snprintf(t, sizeof(t), "%d", Value20(e, pi, F20_REV));
    value(F20_REV, t);
    std::snprintf(t, sizeof(t), "%d", Value20(e, pi, F20_CHO));
    value(F20_CHO, t);
    if (e.pro_btn) { // held edit button: darker cap
        const POINT c = E20P_PILL[(e.pro_btn - 1) / 2];
        const int bx  = c.x + ((e.pro_btn & 1) ? -19 : 19);
        HBRUSH b = CreateSolidBrush(RGB(0x9a, 0x9a, 0x98));
        HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, bx - 16, c.y - 7, bx + 17, c.y + 8, 8, 8);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b);
        POINT tri[3];
        if (e.pro_btn & 1) { tri[0] = {bx + 3, c.y - 4}; tri[1] = {bx + 3, c.y + 4}; tri[2] = {bx - 4, c.y}; }
        else { tri[0] = {bx - 3, c.y - 4}; tri[1] = {bx - 3, c.y + 4}; tri[2] = {bx + 4, c.y}; }
        HBRUSH tb = CreateSolidBrush(RGB(0x28, 0x28, 0x28));
        ob = SelectObject(dc, tb); op = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, tri, 3);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(tb);
    }
}

// Pro mouse handling; true = handled
bool MousePro20(Editor& e, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (const int b = Pill20(x, y)) {
            e.pro_btn   = b;
            e.pro_btn_t = GetTickCount() + E20P_REPEAT_DELAY;
            Button20(e, b);
            SetCapture(hwnd);
            return true;
        }
        if (InRect(E20P_EQ, x, y)) { // select the part of that column (same port)
            const int c = std::clamp(static_cast<int>(std::floor((x - E20P_EQ_X) / E20P_EQ_DX + 0.5)), 0, 15);
            e.part      = (e.part / 16) * 16 + c;
            return true;
        }
        const int f = Field20(x, y);
        if (f >= F20_LEVEL) { // drag up / down
            e.pro_drag      = f + 1;
            e.pro_drag_y    = y;
            e.pro_drag_v0   = Value20(e, e.plugin->PartInfo(e.part), f);
            e.pro_drag_last = e.pro_drag_v0;
            SetCapture(hwnd);
            return true;
        }
        return f >= 0;
    }
    case WM_MOUSEMOVE:
        if (e.pro_drag) {
            const int y = (short)HIWORD(lp);
            const int v = std::clamp(e.pro_drag_v0 + (e.pro_drag_y - y) / 2, 0, 127);
            if (v != e.pro_drag_last) {
                e.pro_drag_last = v;
                Set20(e, e.pro_drag - 1, v);
            }
            return true;
        }
        return false;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (e.pro_btn || e.pro_drag) {
            e.pro_btn = e.pro_drag = 0;
            if (msg == WM_LBUTTONUP) ReleaseCapture();
            return true;
        }
        return false;
    case WM_TIMER:
        if (e.pro_btn && GetTickCount() >= e.pro_btn_t) { // auto-repeat
            e.pro_btn_t = GetTickCount() + E20P_REPEAT_MS;
            Button20(e, e.pro_btn);
        }
        return false;
    case WM_MOUSEWHEEL: {
        POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(hwnd, &pt);
        const int dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (const int b = Pill20(pt.x, pt.y)) { Step20(e, E20P_PILL_FIELD[(b - 1) / 2], dir); return true; }
        if (InRect(E20P_EQ, pt.x, pt.y)) { Step20(e, F20_PART, dir); return true; }
        if (const int f = Field20(pt.x, pt.y); f >= 0) { Step20(e, f, dir); return true; }
        return false;
    }
    }
    return false;
}
#endif

int Hit8820(int x, int y)
{
    if (InCircle(E20_INSTMAP, E20_INSTMAP_R, x, y)) return E20_HIT_INSTMAP;
    if (InCircle(E20_KNOB, E20_KNOB_R, x, y)) return E20_HIT_KNOB;
    if (InRect(E20_SETUP, x, y)) return E20_HIT_SETUP;
#ifdef NUKED_SC55_8820_PRO
    if (const int b = Pill20(x, y)) return E20_HIT_PRO + b;
#endif
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
        Text(dc, e.f_tiny, E20_INK, E20_GAIN.left, E20_GAIN.top, g, DT_CENTER, E20_GAIN.right - E20_GAIN.left,
             E20_GAIN.bottom - E20_GAIN.top);
    }
    DrawVoices88(e, dc, E20_VOICES, E20_UNITS); // count, bar and UNITS as on the other panels
#ifdef NUKED_SC55_8820_PRO
    DrawPro20(e, dc);
#endif
    Box88(e, dc, E20_SETUP, e.menu_open ? "SETUP  \x85" : "SETUP  >", C88_LED, true);
}

bool Mouse8820(Editor& e, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NukedSc55& p = *e.plugin;
#ifdef NUKED_SC55_8820_PRO
    if (MousePro20(e, hwnd, msg, wp, lp)) return true;
#endif
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
            e.drag_vol  = true; // turning only (silent); PREVIEW is the right click
            e.drag_x    = x;
            e.drag_y    = y;
            e.drag_v0   = p.gain_db.load();
            SetCapture(hwnd);
        } else if (h == E20_HIT_SETUP) {
            ShowSetupMenu(e);
        }
        return true;
    }
    case WM_RBUTTONDOWN: { // right click on the knob = push the knob (PREVIEW), held while the button is down
        const int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (Hit8820(x, y) == E20_HIT_KNOB && !e.drag_vol && !e.held85) {
            p.ui_preview_part = -1; // Setup menu's "Prevw Note" on the part the firmware plays preview on
            p.ui_preview      = 1;
            e.preview_down    = true;
            SetCapture(hwnd);
        }
        return true;
    }
    case WM_RBUTTONUP:
        if (e.preview_down) {
            e.preview_down = false;
            p.ui_preview   = 2; // note off
            ReleaseCapture();
        }
        return true;
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
        if (Hit8820(pt.x, pt.y) == E20_HIT_KNOB) { // 0.1 dB per notch
            const int dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
            p.gain_db = GainWheelStep(p.gain_db.load(), dir);
            p.NotifyStateChanged();
        }
        return true;
    }
    }
    return false;
}

} // namespace
