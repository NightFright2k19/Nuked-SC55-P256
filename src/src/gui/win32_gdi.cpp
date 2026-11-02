// The Win32/GDI subset of the plug-in editor on Linux and macOS (issue #112): GDI objects, software drawing into
// 32-bit BGRA bitmaps (the layout of the Windows DIB sections the editor uses), text with the embedded fonts through
// stb_truetype, and the menu model. The windows and the popup menus are in win32_x11.cpp (Linux) and
// win32_cocoa.mm (macOS). See win32_shim.h.

#if !defined(_WIN32) && (defined(NUKED_SC55_X11_EDITOR) || defined(NUKED_SC55_COCOA_EDITOR))

#include "win32_shim_impl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

// Embedded fonts (src/third_party/fonts, unmodified; NK_FONT_DIR is set by the build)
#ifndef NK_FONT_DIR
#error "NK_FONT_DIR (folder of the embedded fonts) is not set"
#endif
#ifdef __APPLE__
#define NK_FONT_SECTION ".section __TEXT,__const\n"
#else
#define NK_FONT_SECTION ".section .rodata\n"
#endif
#define NK_FONT_BLOB(sym, file)                                                                              \
	asm(NK_FONT_SECTION ".balign 16\n.globl " #sym "_b\n" #sym "_b:\n.incbin \"" NK_FONT_DIR "/" file      \
	    "\"\n.globl " #sym "_e\n" #sym "_e:\n.byte 0\n.text\n");                                               \
	extern "C" const unsigned char sym##_b[] asm(#sym "_b");                                                  \
	extern "C" const unsigned char sym##_e[] asm(#sym "_e");
NK_FONT_BLOB(nk_font_sans, "LiberationSans-Regular.ttf")
NK_FONT_BLOB(nk_font_sans_bold, "LiberationSans-Bold.ttf")
NK_FONT_BLOB(nk_font_mono_bold, "DejaVuSansMono-Bold.ttf")

namespace w32x {

// ---------------------------------------------------------------------------------------------------------------
// GDI objects

struct Face {
	stbtt_fontinfo info{};
	int upem = 2048, win_ascent = 0, win_descent = 0;
	bool ok = false;
};

Face* FaceAt(int i)
{
	static Face faces[3];
	static bool init = false;
	if (!init) {
		init = true;
		const unsigned char* data[3] = {nk_font_sans_b, nk_font_sans_bold_b, nk_font_mono_bold_b};
		for (int k = 0; k < 3; ++k) {
			Face& f = faces[k];
			if (!stbtt_InitFont(&f.info, data[k], stbtt_GetFontOffsetForIndex(data[k], 0))) continue;
			f.ok   = true;
			f.upem = ttUSHORT(f.info.data + f.info.head + 18);
			// GDI places text by the OS/2 usWinAscent / usWinDescent of the font
			const stbtt_uint32 os2 = stbtt__find_table(f.info.data, f.info.fontstart, "OS/2");
			if (os2) {
				f.win_ascent  = ttUSHORT(f.info.data + os2 + 74);
				f.win_descent = ttUSHORT(f.info.data + os2 + 76);
			} else {
				int a, d, g;
				stbtt_GetFontVMetrics(&f.info, &a, &d, &g);
				f.win_ascent  = a;
				f.win_descent = -d;
			}
		}
	}
	return &faces[std::clamp(i, 0, 2)];
}

const Glyph& Font::Get(int cp)
	{
		auto it = cache.find(cp);
		if (it != cache.end()) return it->second;
		Glyph g;
		Face* f = FaceAt(face);
		if (f->ok) {
			int gi = stbtt_FindGlyphIndex(&f->info, cp);
			if (!gi && cp != ' ') gi = stbtt_FindGlyphIndex(&f->info, '?');
			int adv, lsb;
			stbtt_GetGlyphHMetrics(&f->info, gi, &adv, &lsb);
			g.advance = (int)std::lround(adv * scale); // GDI advances are whole pixels
			int x0, y0, x1, y1;
			stbtt_GetGlyphBitmapBox(&f->info, gi, scale, scale, &x0, &y0, &x1, &y1);
			g.w = x1 - x0;
			g.h = y1 - y0;
			g.xoff = x0;
			g.yoff = y0;
			if (g.w > 0 && g.h > 0) {
				g.cov.resize((size_t)g.w * g.h);
				stbtt_MakeGlyphBitmap(&f->info, g.cov.data(), g.w, g.h, g.w, scale, scale, gi);
			}
		}
		return cache.emplace(cp, std::move(g)).first->second;
	}

// ---------------------------------------------------------------------------------------------------------------
// Pixels


uint32_t Px(COLORREF c) // GDI drawing into a DIB section leaves the alpha byte 0
{
	return ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | GetBValue(c);
}

void Plot(Bitmap* b, int x, int y, uint32_t p)
{
	if (b && x >= 0 && y >= 0 && x < b->w && y < b->h) b->px[(size_t)y * b->w + x] = p;
}

void FillSpan(Bitmap* b, int x0, int x1, int y, uint32_t p) // [x0, x1)
{
	if (!b || y < 0 || y >= b->h) return;
	x0 = std::max(x0, 0);
	x1 = std::min(x1, b->w);
	if (x0 < x1) std::fill(b->px.begin() + (size_t)y * b->w + x0, b->px.begin() + (size_t)y * b->w + x1, p);
}

inline uint32_t Mix(uint32_t d, COLORREF c, int a) // text and shape anti-aliasing
{
	const int r = ((d >> 16) & 0xff) + ((GetRValue(c) - (int)((d >> 16) & 0xff)) * a + 127) / 255;
	const int g = ((d >> 8) & 0xff) + ((GetGValue(c) - (int)((d >> 8) & 0xff)) * a + 127) / 255;
	const int bl = (d & 0xff) + ((GetBValue(c) - (int)(d & 0xff)) * a + 127) / 255;
	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

int TextWidth(Font* f, const std::u32string& s)
{
	int w = 0;
	for (char32_t c : s) w += f->Get((int)c).advance;
	return w;
}

// draws s with its cell top at (x, y), clipped to clip
void DrawRun(Bitmap* b, Font* f, COLORREF col, int x, int y, const std::u32string& s, const RECT& clip)
{
	const int base = y + f->ascent;
	const int cl = std::max<int>(clip.left, 0), ct = std::max<int>(clip.top, 0);
	const int cr = std::min<int>(clip.right, b->w), cb = std::min<int>(clip.bottom, b->h);
	for (char32_t c : s) {
		const Glyph& g = f->Get((int)c);
		for (int gy = 0; gy < g.h; ++gy) {
			const int py = base + g.yoff + gy;
			if (py < ct || py >= cb) continue;
			for (int gx = 0; gx < g.w; ++gx) {
				const int pxx = x + g.xoff + gx;
				if (pxx < cl || pxx >= cr) continue;
				const int a = g.cov[(size_t)gy * g.w + gx];
				if (!a) continue;
				uint32_t& d = b->px[(size_t)py * b->w + pxx];
				d = Mix(d, col, a);
			}
		}
		x += g.advance;
	}
}

// the editor's narrow strings are Windows-1252 (e.g. "\x85" = ellipsis)
std::u32string FromAnsi(const char* s, int n)
{
	static const char32_t cp1252[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
	                                    0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C,
	                                    0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,
	                                    0x017E, 0x0178};
	std::u32string r;
	if (n < 0) n = (int)std::strlen(s);
	for (int i = 0; i < n; ++i) {
		const unsigned char c = (unsigned char)s[i];
		r.push_back(c >= 0x80 && c < 0xA0 ? cp1252[c - 0x80] : (char32_t)c);
	}
	return r;
}

std::u32string FromWide(const wchar_t* s)
{
	std::u32string r;
	for (; s && *s; ++s) r.push_back((char32_t)*s);
	return r;
}

Font* MakeFontObj(int px, int weight, const char* face)
{
	auto* f = new Font();
	const bool mono = face && (std::strstr(face, "Consolas") || std::strstr(face, "Courier") || std::strstr(face, "Mono"));
	f->face = mono ? 2 : (weight >= FW_SEMIBOLD ? 1 : 0);
	f->em   = (float)std::abs(px ? px : 12);
	Face* fc = FaceAt(f->face);
	// negative lfHeight = character height (em size) in pixels; positive = cell height
	const float cell = (float)(fc->win_ascent + fc->win_descent);
	f->scale   = px < 0 ? f->em / (float)fc->upem : f->em / std::max(cell, 1.0f);
	f->ascent  = (int)std::lround(fc->win_ascent * f->scale);
	f->descent = (int)std::lround(fc->win_descent * f->scale);
	return f;
}
} // namespace w32x

using namespace w32x;

// --- GDI -------------------------------------------------------------------------------------------------------

namespace {
Bitmap g_stock_bitmap(1, 1);
Brush* StockBrush(int i)
{
	static Brush white, null;
	static bool init = false;
	if (!init) {
		init = true;
		white.c = RGB(255, 255, 255);
		null.null = true;
		white.stock = null.stock = true;
	}
	return i == NULL_BRUSH ? &null : &white;
}
Pen* StockPen(int i)
{
	static Pen black, null;
	static bool init = false;
	if (!init) {
		init = true;
		null.null = true;
		black.stock = null.stock = true;
	}
	return i == NULL_PEN ? &null : &black;
}
w32x::Font* StockFont()
{
	static w32x::Font* f = [] {
		w32x::Font* x = MakeFontObj(-12, FW_NORMAL, "Arial");
		x->stock = true;
		return x;
	}();
	return f;
}
} // namespace

namespace w32x {
Dc* NewDc(Bitmap* b, Wnd* w)
{
	g_stock_bitmap.stock = true;
	auto* dc  = new Dc();
	dc->bmp   = b ? b : &g_stock_bitmap;
	dc->brush = StockBrush(0);
	dc->pen   = StockPen(0);
	dc->font  = StockFont();
	dc->wnd   = w;
	return dc;
}
} // namespace w32x

int ReleaseDC(HWND, HDC dc)
{
	delete dc;
	return 1;
}
HDC CreateCompatibleDC(HDC) { return NewDc(nullptr, nullptr); }
BOOL DeleteDC(HDC dc)
{
	delete dc;
	return TRUE;
}
HBITMAP CreateCompatibleBitmap(HDC, int w, int h) { return new Bitmap(w, h); }

HBITMAP CreateDIBSection(HDC, const BITMAPINFO* bi, UINT, void** bits, void*, DWORD)
{
	if (!bi || bi->bmiHeader.biBitCount != 32) return nullptr;
	auto* b = new Bitmap(bi->bmiHeader.biWidth, std::abs(bi->bmiHeader.biHeight)); // the editor uses top-down DIBs
	if (bits) *bits = b->px.data();
	return b;
}

HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj)
{
	if (!dc || !obj) return nullptr;
	HGDIOBJ old = nullptr;
	switch (obj->kind) {
	case GdiObj::BITMAP: old = dc->bmp; dc->bmp = static_cast<Bitmap*>(obj); break;
	case GdiObj::BRUSH: old = dc->brush; dc->brush = static_cast<Brush*>(obj); break;
	case GdiObj::PEN: old = dc->pen; dc->pen = static_cast<Pen*>(obj); break;
	case GdiObj::FONT: old = dc->font; dc->font = static_cast<w32x::Font*>(obj); break;
	}
	return old;
}

BOOL DeleteObject(HGDIOBJ obj)
{
	if (obj && !obj->stock) delete obj;
	return TRUE;
}

HGDIOBJ GetStockObject(int i)
{
	if (i == NULL_PEN) return StockPen(NULL_PEN);
	return StockBrush(i);
}

HBRUSH CreateSolidBrush(COLORREF c)
{
	auto* b = new Brush();
	b->c    = c;
	return b;
}

HPEN CreatePen(int, int width, COLORREF c)
{
	auto* p  = new Pen();
	p->c     = c;
	p->width = std::max(width, 1);
	return p;
}

HFONT CreateFontA(int height, int, int, int, int weight, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                  LPCSTR face)
{
	return MakeFontObj(height, weight, face);
}

BOOL GdiFlush() { return TRUE; }

BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD)
{
	if (!dst || !src) return FALSE;
	Bitmap *d = dst->bmp, *s = src->bmp;
	for (int r = 0; r < h; ++r) {
		const int dy = y + r, syy = sy + r;
		if (dy < 0 || dy >= d->h || syy < 0 || syy >= s->h) continue;
		int x0 = 0, x1 = w;
		x0 = std::max({x0, -x, -sx});
		x1 = std::min({x1, d->w - x, s->w - sx});
		if (x0 >= x1) continue;
		if (d == s && dy == syy) std::memmove(&d->px[(size_t)dy * d->w + x + x0], &s->px[(size_t)syy * s->w + sx + x0], (size_t)(x1 - x0) * 4);
		else std::memcpy(&d->px[(size_t)dy * d->w + x + x0], &s->px[(size_t)syy * s->w + sx + x0], (size_t)(x1 - x0) * 4);
	}
	return TRUE;
}

BOOL AlphaBlend(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, BLENDFUNCTION bf)
{
	if (!dst || !src || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return FALSE;
	Bitmap *d = dst->bmp, *s = src->bmp;
	const int ca = bf.SourceConstantAlpha;
	const bool per_pixel = bf.AlphaFormat & AC_SRC_ALPHA;
	for (int r = 0; r < h; ++r) {
		const int dy = y + r, syy = sy + (sw == w && sh == h ? r : r * sh / h);
		if (dy < 0 || dy >= d->h || syy < 0 || syy >= s->h) continue;
		for (int k = 0; k < w; ++k) {
			const int dx = x + k, sxx = sx + (sw == w ? k : k * sw / w);
			if (dx < 0 || dx >= d->w || sxx < 0 || sxx >= s->w) continue;
			const uint32_t sp = s->px[(size_t)syy * s->w + sxx];
			uint32_t& dp = d->px[(size_t)dy * d->w + dx];
			// premultiplied source (AC_SRC_ALPHA): dst = src * ca + dst * (1 - srcA * ca)
			const int sa = per_pixel ? (int)(sp >> 24) * ca / 255 : ca;
			uint32_t out = 0;
			for (int sh2 = 0; sh2 < 32; sh2 += 8) {
				const int sc = (int)((sp >> sh2) & 0xff), dc = (int)((dp >> sh2) & 0xff);
				const int v  = per_pixel ? (sc * ca + 127) / 255 + (dc * (255 - sa) + 127) / 255
				                         : (sc * ca + dc * (255 - ca) + 127) / 255;
				out |= (uint32_t)std::min(v, 255) << sh2;
			}
			dp = out;
		}
	}
	return TRUE;
}

int SetStretchBltMode(HDC, int mode) { return mode; }
BOOL SetBrushOrgEx(HDC, int, int, POINT*) { return TRUE; }

int StretchDIBits(HDC dc, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void* bits,
                  const BITMAPINFO* bi, UINT, DWORD)
{
	// HALFTONE: area average when shrinking (the only use: LCD glass scaled into the cached background)
	if (!dc || !bits || !bi || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return 0;
	const uint32_t* s = static_cast<const uint32_t*>(bits);
	const int stride  = bi->bmiHeader.biWidth;
	Bitmap* d         = dc->bmp;
	for (int r = 0; r < h; ++r) {
		const int dy = y + r;
		if (dy < 0 || dy >= d->h) continue;
		const double y0 = sy + (double)r * sh / h, y1 = sy + (double)(r + 1) * sh / h;
		for (int k = 0; k < w; ++k) {
			const int dx = x + k;
			if (dx < 0 || dx >= d->w) continue;
			const double x0 = sx + (double)k * sw / w, x1 = sx + (double)(k + 1) * sw / w;
			double acc[4] = {}, area = 0;
			for (int yy = (int)y0; yy < (int)std::ceil(y1); ++yy) {
				const double wy = std::min<double>(yy + 1, y1) - std::max<double>(yy, y0);
				if (wy <= 0) continue;
				for (int xx = (int)x0; xx < (int)std::ceil(x1); ++xx) {
					const double wx = std::min<double>(xx + 1, x1) - std::max<double>(xx, x0);
					if (wx <= 0) continue;
					const uint32_t p = s[(size_t)yy * stride + xx];
					for (int c = 0; c < 4; ++c) acc[c] += wx * wy * ((p >> (8 * c)) & 0xff);
					area += wx * wy;
				}
			}
			uint32_t out = 0;
			for (int c = 0; c < 3; ++c) out |= (uint32_t)std::lround(acc[c] / area) << (8 * c);
			d->px[(size_t)dy * d->w + dx] = out;
		}
	}
	return h;
}

int SetDIBitsToDevice(HDC dc, int x, int y, DWORD w, DWORD h, int sx, int sy, UINT, UINT lines, const void* bits,
                      const BITMAPINFO* bi, UINT)
{
	if (!dc || !bits || !bi) return 0;
	const uint32_t* s = static_cast<const uint32_t*>(bits);
	const int stride  = bi->bmiHeader.biWidth;
	Bitmap* d         = dc->bmp;
	const int rows    = std::min<int>((int)h, (int)lines);
	for (int r = 0; r < rows; ++r) {
		const int dy = y + r;
		if (dy < 0 || dy >= d->h) continue;
		for (int k = 0; k < (int)w; ++k) {
			const int dx = x + k;
			if (dx < 0 || dx >= d->w) continue;
			d->px[(size_t)dy * d->w + dx] = s[(size_t)(sy + r) * stride + sx + k];
		}
	}
	return rows;
}

int FillRect(HDC dc, const RECT* r, HBRUSH b)
{
	if (!dc || !r || !b || b->null) return 0;
	for (int y = r->top; y < r->bottom; ++y) FillSpan(dc->bmp, r->left, r->right, y, Px(b->c));
	return 1;
}

namespace {
void Stamp(Bitmap* b, int x, int y, int width, uint32_t p) // round pen tip
{
	if (width <= 1) {
		Plot(b, x, y, p);
		return;
	}
	const double rad = width / 2.0;
	const int r = (int)std::ceil(rad);
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx)
			if ((dx + 0.5 - (width % 2 ? 0.5 : 0)) * (dx + 0.5 - (width % 2 ? 0.5 : 0)) +
			            (dy + 0.5 - (width % 2 ? 0.5 : 0)) * (dy + 0.5 - (width % 2 ? 0.5 : 0)) <= rad * rad)
				Plot(b, x + dx, y + dy, p);
}

void Line(Bitmap* b, int x0, int y0, int x1, int y1, int width, uint32_t p)
{
	// Bresenham; GDI leaves out the end point of thin lines
	const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	while (true) {
		if (x0 == x1 && y0 == y1) {
			if (width > 1) Stamp(b, x0, y0, width, p);
			break;
		}
		Stamp(b, x0, y0, width, p);
		const int e2 = 2 * err;
		if (e2 >= dy) err += dy, x0 += sx;
		if (e2 <= dx) err += dx, y0 += sy;
	}
}

void FillPolygon(Bitmap* b, const POINT* pts, int n, uint32_t p) // even-odd rule (ALTERNATE), pixel centres
{
	int ymin = pts[0].y, ymax = pts[0].y;
	for (int i = 1; i < n; ++i) ymin = std::min<int>(ymin, pts[i].y), ymax = std::max<int>(ymax, pts[i].y);
	std::vector<double> xs;
	for (int y = ymin; y < ymax; ++y) {
		const double cy = y + 0.5;
		xs.clear();
		for (int i = 0; i < n; ++i) {
			const POINT a = pts[i], c = pts[(i + 1) % n];
			if ((a.y <= cy && c.y > cy) || (c.y <= cy && a.y > cy))
				xs.push_back(a.x + (cy - a.y) * (double)(c.x - a.x) / (double)(c.y - a.y));
		}
		std::sort(xs.begin(), xs.end());
		for (size_t i = 0; i + 1 < xs.size(); i += 2)
			FillSpan(b, (int)std::ceil(xs[i] - 0.5), (int)std::ceil(xs[i + 1] - 0.5), y, p);
	}
}

// filled ellipse in the box [l, r) x [t, b) (GDI leaves out the right and bottom edge)
void FillEllipseBox(Bitmap* bm, double l, double t, double r, double b, int y_from, int y_to, uint32_t p,
                    double x_inset_l = 0, double x_inset_r = 0)
{
	const double cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
	for (int y = y_from; y < y_to; ++y) {
		const double dy = (y + 0.5 - cy) / ry;
		if (dy * dy > 1) continue;
		const double half = rx * std::sqrt(1 - dy * dy);
		FillSpan(bm, (int)std::ceil(cx - half - 0.5 - x_inset_l), (int)std::ceil(cx + half - 0.5 + x_inset_r), y, p);
	}
}
} // namespace

BOOL MoveToEx(HDC dc, int x, int y, POINT* old)
{
	if (!dc) return FALSE;
	if (old) *old = dc->cur;
	dc->cur = POINT{x, y};
	return TRUE;
}

BOOL LineTo(HDC dc, int x, int y)
{
	if (!dc) return FALSE;
	if (!dc->pen->null) Line(dc->bmp, dc->cur.x, dc->cur.y, x, y, dc->pen->width, Px(dc->pen->c));
	dc->cur = POINT{x, y};
	return TRUE;
}

BOOL Polygon(HDC dc, const POINT* pts, int n)
{
	if (!dc || !pts || n < 3) return FALSE;
	if (!dc->brush->null) FillPolygon(dc->bmp, pts, n, Px(dc->brush->c));
	if (!dc->pen->null)
		for (int i = 0; i < n; ++i)
			Line(dc->bmp, pts[i].x, pts[i].y, pts[(i + 1) % n].x, pts[(i + 1) % n].y, dc->pen->width, Px(dc->pen->c));
	return TRUE;
}

BOOL Ellipse(HDC dc, int l, int t, int r, int b)
{
	if (!dc) return FALSE;
	// with a null pen GDI fills one pixel less to the right and bottom
	const int rr = dc->pen->null ? r - 1 : r, bb = dc->pen->null ? b - 1 : b;
	if (!dc->brush->null) FillEllipseBox(dc->bmp, l, t, rr, bb, t, bb, Px(dc->brush->c));
	return TRUE;
}

BOOL RoundRect(HDC dc, int l, int t, int r, int b, int w, int h)
{
	if (!dc || dc->brush->null) return FALSE;
	const int rr = dc->pen->null ? r - 1 : r, bb = dc->pen->null ? b - 1 : b;
	const uint32_t p = Px(dc->brush->c);
	w = std::min(w, rr - l);
	h = std::min(h, bb - t);
	const int ch = h / 2;
	for (int y = t + ch; y < bb - ch; ++y) FillSpan(dc->bmp, l, rr, y, p);
	// corner rows: the ellipse of the corners stretched by the straight part in the middle
	const double straight = (rr - l) - w;
	FillEllipseBox(dc->bmp, l, t, l + w, t + h, t, t + ch, p, 0, straight);
	FillEllipseBox(dc->bmp, l, bb - h, l + w, bb, bb - ch, bb, p, 0, straight);
	return TRUE;
}

COLORREF SetTextColor(HDC dc, COLORREF c)
{
	if (!dc) return 0;
	const COLORREF old = dc->text;
	dc->text = c;
	return old;
}

int SetBkMode(HDC dc, int mode)
{
	if (!dc) return 0;
	const int old = dc->bk;
	dc->bk = mode;
	return old;
}

int DrawTextA(HDC dc, LPCSTR s, int n, RECT* r, UINT format)
{
	if (!dc || !s || !r) return 0;
	w32x::Font* f = dc->font;
	std::u32string t = FromAnsi(s, n);
	const int rw = r->right - r->left;
	if ((format & DT_END_ELLIPSIS) && TextWidth(f, t) > rw) {
		const std::u32string dots = U"...";
		while (!t.empty() && TextWidth(f, t) + TextWidth(f, dots) > rw) t.pop_back();
		t += dots;
	}
	const int tw = TextWidth(f, t), th = f->ascent + f->descent;
	int x = r->left, y = r->top;
	if (format & DT_CENTER) x = r->left + (rw - tw) / 2;
	else if (format & DT_RIGHT) x = r->right - tw;
	if (format & DT_SINGLELINE) {
		if (format & DT_VCENTER) y = r->top + (r->bottom - r->top - th) / 2;
		else if (format & DT_BOTTOM) y = r->bottom - th;
	}
	if (dc->bk == OPAQUE) {
		const RECT box{x, y, x + tw, y + th};
		HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
		FillRect(dc, &box, b);
		DeleteObject(b);
	}
	DrawRun(dc->bmp, f, dc->text, x, y, t, *r); // clipped to the rectangle (no DT_NOCLIP)
	return th;
}

// --- menus -----------------------------------------------------------------------------------------------------

HMENU CreatePopupMenu() { return new Menu(); }

BOOL AppendMenuW(HMENU m, UINT flags, UINT_PTR id, LPCWSTR text)
{
	if (!m) return FALSE;
	MenuItem it;
	it.flags = flags;
	if (flags & MF_POPUP) {
		Menu* sub = reinterpret_cast<Menu*>(id);
		it.sub    = sub->tree;
		m->subs.push_back(sub);
	} else {
		it.id = id;
	}
	if (!(flags & MF_SEPARATOR)) it.text = FromWide(text);
	m->tree->items.push_back(std::move(it));
	return TRUE;
}

BOOL DestroyMenu(HMENU m)
{
	if (!m) return FALSE;
	for (Menu* s : m->subs) DestroyMenu(s);
	delete m; // an open popup keeps its own reference to the items
	return TRUE;
}

#endif // !_WIN32 && (NUKED_SC55_X11_EDITOR || NUKED_SC55_COCOA_EDITOR)
