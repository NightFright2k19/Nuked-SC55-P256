// Nuked SC-55 Poly - authentic SC-55 / SC-88 character-LCD renderer for the plugin panels.
// Reproduces the glass layout of Nuked-SC55's LCD_Render (741x268 reference pixels: 3+16
// character instrument line, six 3-character fields, L/R lamp and the 16x16 dot matrix of the
// part level meters / SysEx bitmaps) scaled into any panel rectangle. Fed from the HD44780
// memory of the emulated units (DDRAM 80 bytes, CGRAM 64 bytes). Included by editor_win32.cpp.

#include "nuked-sc55/backend/lcd_back.h" // back_palette / back_data (741x268 glass with labels)

namespace lcdview {

constexpr int RefW = 741, RefH = 268;
const uint8_t kLcdLR[2][12][11] =
{
    {
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,0,0,0,0,0,0,0,0,0,},
        {1,1,1,1,1,1,1,1,1,1,1,},
        {1,1,1,1,1,1,1,1,1,1,1,},
    },
    {
        {1,1,1,1,1,1,1,1,1,0,0,},
        {1,1,1,1,1,1,1,1,1,1,0,},
        {1,1,0,0,0,0,0,0,1,1,0,},
        {1,1,0,0,0,0,0,0,1,1,0,},
        {1,1,0,0,0,0,0,0,1,1,0,},
        {1,1,1,1,1,1,1,1,1,1,0,},
        {1,1,1,1,1,1,1,1,1,0,0,},
        {1,1,0,0,0,0,0,1,1,0,0,},
        {1,1,0,0,0,0,0,0,1,1,0,},
        {1,1,0,0,0,0,0,0,1,1,0,},
        {1,1,0,0,0,0,0,0,0,1,1,},
        {1,1,0,0,0,0,0,0,0,1,1,},
    }
};
constexpr int kLrXY[2][2] = {{70, 264}, {232, 264}}; // (row, column) like Nuked
struct Field { int dd, count, row, col; };
constexpr Field kFields[] = {
    {0, 3, 11, 34},  {3, 16, 11, 153},                 // part / map, instrument line (text messages)
    {40, 3, 75, 34}, {43, 3, 75, 153},                 // level, pan
    {49, 3, 139, 34}, {46, 3, 139, 153},               // reverb, chorus
    {52, 3, 203, 34}, {55, 3, 203, 153},               // key shift, MIDI ch
};

inline const uint8_t* Glyph(uint8_t ch, const uint8_t* cg)
{
    return ch >= 16 ? &::lcd_font[ch - 16][0] : &cg[(ch & 7) * 8];
}

// 16x16 matrix (bit 15 = column 0) from DDRAM 20..23 / 60..63
inline void Matrix(const uint8_t* dd, const uint8_t* cg, uint16_t rows[16])
{
    for (int half = 0; half < 2; ++half)
        for (int r = 0; r < 8; ++r) {
            uint16_t bits = 0;
            for (int seg = 0; seg < 4; ++seg) {
                const uint8_t* f = Glyph(dd[(half ? 60 : 20) + seg], cg);
                const int width  = seg == 3 ? 1 : 5;
                for (int j = 0; j < width; ++j)
                    if (f[r] & (1 << (4 - j))) bits |= uint16_t(1u << (15 - (seg * 5 + j)));
            }
            rows[half * 8 + r] = bits;
        }
}

struct View {
    uint8_t dd[80]{};
    uint8_t cg[64]{};
    uint16_t matrix[16]{};
    bool on = false;
};

// Static part: the glass with its printed labels, scaled into r (once, into the cached background)
inline void DrawGlass(HDC dc, const RECT& r)
{
    static std::vector<uint32_t> px;
    if (px.empty()) {
        px.resize(RefW * RefH);
        for (int i = 0; i < RefW * RefH; ++i) {
            const uint32_t c = back_palette[back_data[i]]; // ABGR
            px[i] = (c & 0xff00ff00u) | ((c & 0xffu) << 16) | ((c >> 16) & 0xffu); // -> ARGB
        }
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = RefW;
    bi.bmiHeader.biHeight      = -RefH;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, RefW, RefH, px.data(), &bi,
                  DIB_RGB_COLORS, SRCCOPY);
}

// Dynamic part: the complete LCD (glass, characters, L/R lamp, dot matrix) is drawn at the
// reference resolution 741x268 with exact geometry and then reduced to the panel rectangle by
// area averaging (box filter). Every dot keeps the same size and spacing at any scale.
//
// Each editor keeps a cache: per frame only the characters, dots and the L/R lamp that changed
// since the last frame are redrawn, and only the panel pixels around them are reduced again.
// Full and partial reduction run through the same code with the same arithmetic in the same
// order, so a partial update yields exactly the pixels of a full one.
struct Cache {
    int w = 0, h = 0;
    bool valid = false;
    View last;
    std::vector<uint32_t> ref, dst, tmp;
    struct Span { int a = 0, b = 0; double norm = 0; double wt[64]; };
    std::vector<Span> sx, sy;
};

inline const std::vector<uint32_t>& Glass()
{
    static std::vector<uint32_t> glass;
    if (glass.empty()) {
        glass.resize(RefW * RefH);
        for (int i = 0; i < RefW * RefH; ++i) {
            const uint32_t c = back_palette[back_data[i]]; // ABGR -> ARGB
            glass[i] = (c & 0xff00ff00u) | ((c & 0xffu) << 16) | ((c >> 16) & 0xffu);
        }
    }
    return glass;
}

constexpr uint32_t DotOn  = 0xff000000u;  // lit dot
constexpr uint32_t DotOff = 0xffe2600au;  // unlit dot, softened towards the glass

inline void Box(std::vector<uint32_t>& ref, int row, int col, int h, int w, bool lit)
{
    const uint32_t c = lit ? DotOn : DotOff;
    for (int y = row; y < row + h && y < RefH; ++y)
        for (int x = col; x < col + w && x < RefW; ++x) ref[y * RefW + x] = c;
}
inline void DrawCell(std::vector<uint32_t>& ref, const Field& f, int k, const uint8_t* g)
{
    const int col0 = f.col + k * 35;
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 5; ++j) Box(ref, f.row + i * 6, col0 + j * 6, 5, 5, g[i] & (1 << (4 - j)));
}
inline void DrawLR(std::vector<uint32_t>& ref, bool lit)
{
    for (int letter = 0; letter < 2; ++letter)
        for (int i = 0; i < 12; ++i)
            for (int j = 0; j < 11; ++j)
                if (kLcdLR[letter][i][j]) Box(ref, kLrXY[letter][0] + i, kLrXY[letter][1] + j, 1, 1, lit);
}
inline int DotRow(int rr) { return 71 + (rr / 8) * 88 + (rr % 8) * 11; }
inline int DotCol(int c) { return 293 + (c / 5) * 130 + (c % 5) * 26; }
inline bool LrLit(const View& v) { return Glyph(v.dd[58], v.cg)[0] & 1; }

// Area weights of one target pixel (same formula for both axes)
inline void SpanOf(int src, int dstn, int i, Cache::Span& s)
{
    const double s0 = double(i) * src / dstn, s1 = double(i + 1) * src / dstn;
    s.a = int(s0);
    s.b = std::min(src, int(std::ceil(s1)));
    for (int k = s.a; k < s.b; ++k) s.wt[k - s.a] = std::min(s1, double(k + 1)) - std::max(s0, double(k));
    s.norm = 0;
    for (int k = s.a; k < s.b; ++k) s.norm += s.wt[k - s.a];
}

// Reduce the reference image to the target pixels [tx0, tx1) x [ty0, ty1)
inline void Reduce(Cache& c, int tx0, int ty0, int tx1, int ty1)
{
    tx0 = std::max(tx0, 0); ty0 = std::max(ty0, 0); tx1 = std::min(tx1, c.w); ty1 = std::min(ty1, c.h);
    if (tx0 >= tx1 || ty0 >= ty1) return;
    const int ya = c.sy[ty0].a, yb = c.sy[ty1 - 1].b, tw = tx1 - tx0;
    c.tmp.resize(size_t(yb - ya) * tw * 3);
    for (int x = tx0; x < tx1; ++x) {           // horizontal pass
        const auto& s = c.sx[x];
        for (int y = ya; y < yb; ++y) {
            double rr = 0, gg = 0, bb = 0;
            for (int k = s.a; k < s.b; ++k) {
                const uint32_t p = c.ref[y * RefW + k]; const double q = s.wt[k - s.a];
                rr += q * ((p >> 16) & 255); gg += q * ((p >> 8) & 255); bb += q * (p & 255);
            }
            const size_t o = (size_t(y - ya) * tw + (x - tx0)) * 3;
            c.tmp[o + 0] = uint32_t(rr / s.norm * 256);
            c.tmp[o + 1] = uint32_t(gg / s.norm * 256);
            c.tmp[o + 2] = uint32_t(bb / s.norm * 256);
        }
    }
    for (int y = ty0; y < ty1; ++y) {           // vertical pass
        const auto& s = c.sy[y];
        for (int x = tx0; x < tx1; ++x) {
            double rr = 0, gg = 0, bb = 0;
            for (int k = s.a; k < s.b; ++k) {
                const size_t o = (size_t(k - ya) * tw + (x - tx0)) * 3; const double q = s.wt[k - s.a];
                rr += q * c.tmp[o]; gg += q * c.tmp[o + 1]; bb += q * c.tmp[o + 2];
            }
            const auto c8 = [&](double v8) { return uint32_t(std::clamp(int(v8 / s.norm / 256 + 0.5), 0, 255)); };
            c.dst[size_t(y) * c.w + x] = 0xff000000u | (c8(rr) << 16) | (c8(gg) << 8) | c8(bb);
        }
    }
}

// Target pixels affected by a changed reference rectangle (with a 1-pixel margin)
inline void ReduceRefRect(Cache& c, int x0, int y0, int x1, int y1)
{
    Reduce(c, int(std::floor(double(x0) * c.w / RefW)) - 1, int(std::floor(double(y0) * c.h / RefH)) - 1,
           int(std::ceil(double(x1) * c.w / RefW)) + 1, int(std::ceil(double(y1) * c.h / RefH)) + 1);
}

inline void DrawDots(HDC dc, const RECT& r, const View& v, void*& slot)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    if (!slot) slot = new Cache();
    Cache& c = *static_cast<Cache*>(slot);
    if (!c.valid || c.w != w || c.h != h || c.last.on != v.on) {
        // full rebuild
        c.w = w; c.h = h;
        c.sx.resize(w); c.sy.resize(h);
        for (int x = 0; x < w; ++x) SpanOf(RefW, w, x, c.sx[x]);
        for (int y = 0; y < h; ++y) SpanOf(RefH, h, y, c.sy[y]);
        c.ref = Glass();
        if (v.on) {
            for (const auto& f : kFields)
                for (int k = 0; k < f.count; ++k) DrawCell(c.ref, f, k, Glyph(v.dd[f.dd + k], v.cg));
            DrawLR(c.ref, LrLit(v));
            for (int rr = 0; rr < 16; ++rr)
                for (int col = 0; col < 16; ++col)
                    Box(c.ref, DotRow(rr), DotCol(col), 9, 24, v.matrix[rr] & (1u << (15 - col)));
        }
        c.dst.assign(size_t(w) * h, 0);
        Reduce(c, 0, 0, w, h);
        c.valid = true;
    } else if (v.on) {
        // only what changed since the last frame
        for (const auto& f : kFields)
            for (int k = 0; k < f.count; ++k) {
                const uint8_t* g  = Glyph(v.dd[f.dd + k], v.cg);
                const uint8_t* og = Glyph(c.last.dd[f.dd + k], c.last.cg);
                if (std::memcmp(g, og, 7) == 0) continue;
                DrawCell(c.ref, f, k, g);
                const int col0 = f.col + k * 35;
                ReduceRefRect(c, col0, f.row, col0 + 29, f.row + 41);
            }
        if (LrLit(v) != LrLit(c.last)) {
            DrawLR(c.ref, LrLit(v));
            ReduceRefRect(c, kLrXY[0][1], kLrXY[0][0], kLrXY[1][1] + 11, kLrXY[1][0] + 12);
        }
        for (int rr = 0; rr < 16; ++rr) {
            const uint16_t diff = v.matrix[rr] ^ c.last.matrix[rr];
            if (!diff) continue;
            for (int col = 0; col < 16; ++col) {
                if (!(diff & (1u << (15 - col)))) continue;
                Box(c.ref, DotRow(rr), DotCol(col), 9, 24, v.matrix[rr] & (1u << (15 - col)));
                ReduceRefRect(c, DotCol(col), DotRow(rr), DotCol(col) + 24, DotRow(rr) + 9);
            }
        }
    }
    c.last = v;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, r.left, r.top, w, h, 0, 0, 0, h, c.dst.data(), &bi, DIB_RGB_COLORS);
}

inline void FreeCache(void*& slot)
{
    delete static_cast<Cache*>(slot);
    slot = nullptr;
}

// Column of the matrix (= part 0..15) under panel point (x, y) in r, or -1
inline int PartAt(const RECT& r, int x, int y)
{
    const double sx = double(r.right - r.left) / RefW, sy = double(r.bottom - r.top) / RefH;
    const double cx = (x - r.left) / sx, cy = (y - r.top) / sy;
    if (cy < 71 || cy > 71 + 88 + 8 * 11) return -1;
    for (int c = 0; c < 16; ++c) {
        const int col = 293 + (c / 5) * 130 + (c % 5) * 26;
        if (cx >= col - 1 && cx < col + 25) return c;
    }
    return -1;
}

} // namespace lcdview
