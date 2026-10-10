// Shared helpers of the 88emu-style front panels (included by editor_win32.cpp in all builds):
// geometry of the common elements (LCD, VOICES window, GAIN knob), text boxes, VOICES meter.
// The panels themselves: editor_sc55.inc (SC-55 / SC-55mk2), editor_sc88orig.inc (SC-88 and
// SC-88 Pro), editor_sc8850.inc (SC-8850).

namespace {

// Geometry in panel pixels (918x280)
constexpr RECT P88_LCD       = {234, 33, 547, 146}; // 313x113 = original glass 741x268 scaled
constexpr RECT P88_VOICES    = {29, 38, 142, 145};
constexpr RECT P88_UNITS     = {44, 151, 128, 171};
constexpr POINT P88_GAIN     = {189, 57};
constexpr int P88_GAIN_R     = 21;
constexpr int P88_GAIN_TEXT_Y = 81; // GAIN readout (dB) under the knob
// SETUP box: the same place on all panels with a free bottom row (SC-55, SC-55mk2, SC-8850)
// and between the logo and the EFX lamp on the SC-88 / SC-88 Pro
constexpr RECT P88_SETUP_RIGHT = {804, 246, 892, 264};
constexpr RECT P88_SETUP_LEFT  = {284, 246, 372, 264};

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

constexpr COLORREF C88_LED      = RGB(0xff, 0x9a, 0x2e);
constexpr COLORREF C88_LED_DIM  = RGB(0x3a, 0x24, 0x10);

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

const char* NoteName88(int n, char* buf, size_t len) // Roland: C-1 = 0, C4 = 60, G9 = 127
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    std::snprintf(buf, len, "%s%d", names[n % 12], n / 12 - 1);
    return buf;
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

// Part shown in the firmware's part field (DDRAM 0..2, "A01".."B16"), else -1
int LcdPart88(const uint8_t* dd)
{
    if ((dd[0] != 'A' && dd[0] != 'B') || dd[1] < '0' || dd[1] > '1' || dd[2] < '0' || dd[2] > '9') return -1;
    const int n = (dd[1] - '0') * 10 + (dd[2] - '0');
    if (n < 1 || n > 16) return -1;
    return (dd[0] == 'B' ? 16 : 0) + n - 1;
}

} // namespace
