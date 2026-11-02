// The Win32/GDI subset of the plug-in editor on Linux / X11 (issue #112); see win32_x11.h.
// Software drawing into 32-bit BGRA bitmaps (the layout of the Windows DIB sections the editor uses), one X11
// child window per editor, embedded fonts through stb_truetype, popup menus in X11 override-redirect windows.
// Everything runs on the thread that calls editor::Idle (the host's main thread).

#if !defined(_WIN32) && defined(NUKED_SC55_X11_EDITOR)

#include "win32_x11.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <chrono>
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
#define NK_FONT_BLOB(sym, file)                                                                              \
	asm(".section .rodata\n.balign 16\n.globl " #sym "_b\n" #sym "_b:\n.incbin \"" NK_FONT_DIR "/" file  \
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

struct Dc {
	Bitmap* bmp = nullptr;
	Brush* brush = nullptr;
	Pen* pen = nullptr;
	Font* font = nullptr;
	COLORREF text = 0;
	int bk = OPAQUE;
	POINT cur{0, 0};
	Wnd* wnd = nullptr;
};

// ---------------------------------------------------------------------------------------------------------------
// Pixels

inline uint32_t Px(COLORREF c) // GDI drawing into a DIB section leaves the alpha byte 0
{
	return ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | GetBValue(c);
}

inline void Plot(Bitmap* b, int x, int y, uint32_t p)
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

// ---------------------------------------------------------------------------------------------------------------
// X connection, windows and menus

struct MenuItem {
	UINT flags = 0;
	UINT_PTR id = 0;
	std::u32string text;
	std::shared_ptr<struct MenuTree> sub;
};
struct MenuTree {
	std::vector<MenuItem> items;
};

struct Level {
	std::shared_ptr<MenuTree> tree;
	int tab_x = 0; // x of the second column (text after a tab)
	Window xid = 0;
	GC gc = nullptr;
	XImage* img = nullptr;
	std::unique_ptr<Bitmap> bmp;
	int x = 0, y = 0, w = 0, h = 0;
	int hover = -1;
	std::vector<int> top; // item y positions (+ total height)
};

struct Conn {
	Display* dpy = nullptr;
	int refs = 0;
	std::vector<Wnd*> wnds;
	// open popup menu (one at a time)
	std::vector<std::unique_ptr<Level>> levels;
	Wnd* menu_owner = nullptr;
	bool menu_armed = false;
	Font* menu_font = nullptr;
	Wnd* capture = nullptr;
};

Conn& C()
{
	static Conn c;
	return c;
}

struct Wnd {
	Window xid = 0, parent = 0;
	GC gc = nullptr;
	XImage* img = nullptr;
	std::unique_ptr<Bitmap> back;
	WNDPROC proc = nullptr;
	LONG_PTR user = 0;
	bool dirty = true, visible = true, track_leave = false;
	struct Timer { UINT_PTR id; UINT ms; uint64_t next; };
	std::vector<Timer> timers;
	Time last_click = 0;
	int click_x = -100, click_y = -100;
	std::function<void(int)> menu_handler;
};

uint64_t NowMs()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

XImage* MakeImage(Display* d, Bitmap* b)
{
	const int scr = DefaultScreen(d);
	XImage* img = XCreateImage(d, DefaultVisual(d, scr), DefaultDepth(d, scr), ZPixmap, 0,
	                           reinterpret_cast<char*>(b->px.data()), b->w, b->h, 32, b->w * 4);
	return img;
}

void FreeImage(XImage* img)
{
	if (!img) return;
	img->data = nullptr; // the pixels belong to the Bitmap
	XDestroyImage(img);
}

Window MakeXWindow(Display* d, Window parent, int x, int y, int w, int h, bool popup)
{
	const int scr = DefaultScreen(d);
	XSetWindowAttributes a{};
	a.colormap          = DefaultColormap(d, scr);
	a.border_pixel      = 0;
	a.background_pixel  = 0;
	a.override_redirect = popup ? True : False;
	a.save_under        = popup ? True : False;
	a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask |
	               LeaveWindowMask | StructureNotifyMask | KeyPressMask;
	const unsigned long mask = CWColormap | CWBorderPixel | CWBackPixel | CWEventMask |
	                           (popup ? (CWOverrideRedirect | CWSaveUnder) : 0);
	return XCreateWindow(d, parent, x, y, (unsigned)w, (unsigned)h, 0, DefaultDepth(d, scr), InputOutput,
	                     DefaultVisual(d, scr), mask, &a);
}

// --- menus -----------------------------------------------------------------------------------------------------

constexpr int kItemH = 22, kSepH = 7, kCheckW = 28, kArrowW = 24, kPad = 2;
constexpr COLORREF kMenuBg = RGB(0xf2, 0xf2, 0xf2), kMenuBorder = RGB(0xa0, 0xa0, 0xa0),
                   kMenuHover = RGB(0xd1, 0xe5, 0xf7), kMenuHoverLine = RGB(0x9a, 0xc6, 0xec),
                   kMenuText = RGB(0, 0, 0), kMenuGray = RGB(0x6d, 0x6d, 0x6d), kMenuSep = RGB(0xd0, 0xd0, 0xd0);

Font* MenuFont()
{
	Conn& c = C();
	if (!c.menu_font) c.menu_font = MakeFontObj(-12, FW_NORMAL, "Segoe UI");
	return c.menu_font;
}

void Layout(Level& l)
{
	Font* f = MenuFont();
	int left = 0, right = 0, y = kPad;
	l.top.clear();
	for (auto& it : l.tree->items) {
		l.top.push_back(y);
		if (it.flags & MF_SEPARATOR) {
			y += kSepH;
			continue;
		}
		const size_t tab = it.text.find(U'\t');
		left = std::max(left, TextWidth(f, it.text.substr(0, tab)));
		if (tab != std::u32string::npos) right = std::max(right, TextWidth(f, it.text.substr(tab + 1)));
		y += kItemH;
	}
	l.top.push_back(y);
	l.tab_x = kCheckW + left + (right ? 24 : 0);
	l.w     = std::max(120, l.tab_x + right + kArrowW + 8);
	l.h     = y + kPad;
}

void FillBox(Bitmap* b, int x0, int y0, int x1, int y1, COLORREF c)
{
	for (int y = y0; y < y1; ++y) FillSpan(b, x0, x1, y, Px(c));
}

void PaintLevel(Level& l)
{
	Bitmap* b = l.bmp.get();
	Font* f   = MenuFont();
	FillBox(b, 0, 0, l.w, l.h, kMenuBg);
	for (int x = 0; x < l.w; ++x) Plot(b, x, 0, Px(kMenuBorder)), Plot(b, x, l.h - 1, Px(kMenuBorder));
	for (int y = 0; y < l.h; ++y) Plot(b, 0, y, Px(kMenuBorder)), Plot(b, l.w - 1, y, Px(kMenuBorder));
	const RECT clip{1, 1, l.w - 1, l.h - 1};
	for (size_t i = 0; i < l.tree->items.size(); ++i) {
		const MenuItem& it = l.tree->items[i];
		const int y        = l.top[i];
		if (it.flags & MF_SEPARATOR) {
			FillSpan(b, kCheckW, l.w - 4, y + kSepH / 2, Px(kMenuSep));
			continue;
		}
		const bool enabled = !(it.flags & MF_GRAYED);
		if ((int)i == l.hover && enabled) {
			FillBox(b, 3, y, l.w - 3, y + kItemH, kMenuHover);
			for (int x = 3; x < l.w - 3; ++x) Plot(b, x, y, Px(kMenuHoverLine)), Plot(b, x, y + kItemH - 1, Px(kMenuHoverLine));
			for (int yy = y; yy < y + kItemH; ++yy) Plot(b, 3, yy, Px(kMenuHoverLine)), Plot(b, l.w - 4, yy, Px(kMenuHoverLine));
		}
		const COLORREF col = enabled ? kMenuText : kMenuGray;
		const int ty       = y + (kItemH - (f->ascent + f->descent)) / 2;
		const size_t tab   = it.text.find(U'\t');
		DrawRun(b, f, col, kCheckW, ty, it.text.substr(0, tab), clip);
		if (tab != std::u32string::npos) DrawRun(b, f, col, l.tab_x, ty, it.text.substr(tab + 1), clip);
		if (it.flags & MF_CHECKED) { // check mark: two strokes, 2 px wide
			const int cx = 10, cy = y + kItemH / 2;
			for (int k = 0; k <= 3; ++k) FillBox(b, cx + k, cy + k - 1, cx + k + 2, cy + k + 1, col);
			for (int k = 0; k <= 6; ++k) FillBox(b, cx + 3 + k, cy + 2 - k, cx + 5 + k, cy + 4 - k, col);
		}
		if (it.sub) { // submenu arrow
			const int ax = l.w - 16, ay = y + kItemH / 2;
			for (int dy = -4; dy <= 4; ++dy) FillSpan(b, ax, ax + 5 - std::abs(dy), ay + dy, Px(col));
		}
	}
}

void ShowLevel(Level& l)
{
	Display* d = C().dpy;
	if (l.img) XPutImage(d, l.xid, l.gc, l.img, 0, 0, 0, 0, (unsigned)l.w, (unsigned)l.h);
}

void OpenLevel(std::shared_ptr<MenuTree> tree, int x, int y, bool right_align, bool bottom_align, int left_edge)
{
	Conn& c    = C();
	Display* d = c.dpy;
	auto l     = std::make_unique<Level>();
	l->tree    = std::move(tree);
	Layout(*l);
	const int sw = DisplayWidth(d, DefaultScreen(d)), sh = DisplayHeight(d, DefaultScreen(d));
	int lx = right_align ? x - l->w : x, ly = bottom_align ? y - l->h : y;
	if (lx + l->w > sw) lx = left_edge >= 0 ? left_edge - l->w : sw - l->w; // submenu: to the left of its parent
	if (ly + l->h > sh) ly = sh - l->h;
	l->x   = std::max(lx, 0);
	l->y   = std::max(ly, 0);
	l->bmp = std::make_unique<Bitmap>(l->w, l->h);
	l->xid = MakeXWindow(d, RootWindow(d, DefaultScreen(d)), l->x, l->y, l->w, l->h, true);
	l->gc  = XCreateGC(d, l->xid, 0, nullptr);
	l->img = MakeImage(d, l->bmp.get());
	PaintLevel(*l);
	XMapRaised(d, l->xid);
	if (c.levels.empty()) {
		XGrabPointer(d, l->xid, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
		             GrabModeAsync, None, None, CurrentTime);
		XGrabKeyboard(d, l->xid, True, GrabModeAsync, GrabModeAsync, CurrentTime);
	}
	c.levels.push_back(std::move(l));
}

void CloseLevelsFrom(size_t n)
{
	Conn& c = C();
	while (c.levels.size() > n) {
		Level& l = *c.levels.back();
		FreeImage(l.img);
		XFreeGC(c.dpy, l.gc);
		XDestroyWindow(c.dpy, l.xid);
		c.levels.pop_back();
	}
}

void CloseMenu(int cmd, bool notify)
{
	Conn& c = C();
	if (c.levels.empty()) return;
	XUngrabPointer(c.dpy, CurrentTime);
	XUngrabKeyboard(c.dpy, CurrentTime);
	CloseLevelsFrom(0);
	XFlush(c.dpy);
	Wnd* owner      = c.menu_owner;
	c.menu_owner    = nullptr;
	if (notify && owner && owner->menu_handler) {
		auto h = std::move(owner->menu_handler);
		owner->menu_handler = nullptr;
		h(cmd);
	}
}

// level and item under the root position (x, y); -1 if none
int LevelAt(int x, int y)
{
	Conn& c = C();
	for (int i = (int)c.levels.size() - 1; i >= 0; --i) {
		const Level& l = *c.levels[i];
		if (x >= l.x && x < l.x + l.w && y >= l.y && y < l.y + l.h) return i;
	}
	return -1;
}

int ItemAt(const Level& l, int y) // y relative to the level
{
	for (size_t i = 0; i < l.tree->items.size(); ++i)
		if (y >= l.top[i] && y < l.top[i + 1] && !(l.tree->items[i].flags & MF_SEPARATOR)) return (int)i;
	return -1;
}

void MenuMotion(int rx, int ry)
{
	Conn& c = C();
	const int li = LevelAt(rx, ry);
	if (li < 0) return;
	Level& l = *c.levels[li];
	const int it = ItemAt(l, ry - l.y);
	if (it != l.hover) {
		l.hover = it;
		CloseLevelsFrom(li + 1);
		PaintLevel(l);
		ShowLevel(l);
		if (it >= 0 && l.tree->items[it].sub && !(l.tree->items[it].flags & MF_GRAYED))
			OpenLevel(l.tree->items[it].sub, l.x + l.w - 3, l.y + l.top[it] - kPad, false, false, l.x + 3);
	}
	if (it >= 0) c.menu_armed = true;
}

void MenuButton(bool press, int rx, int ry)
{
	Conn& c = C();
	const int li = LevelAt(rx, ry);
	if (li < 0) {
		if (press) CloseMenu(0, true); // click outside: closed without a choice
		return;
	}
	if (press) {
		c.menu_armed = true;
		return;
	}
	if (!c.menu_armed) return; // release of the click that opened the menu
	Level& l = *c.levels[li];
	const int it = ItemAt(l, ry - l.y);
	if (it < 0) return;
	const MenuItem& m = l.tree->items[it];
	if ((m.flags & MF_GRAYED) || m.sub) return;
	CloseMenu((int)m.id, true);
}

bool IsMenuWindow(Window w)
{
	for (auto& l : C().levels)
		if (l->xid == w) return true;
	return false;
}

// --- windows ---------------------------------------------------------------------------------------------------

std::map<std::string, WNDCLASSA>& Classes()
{
	static std::map<std::string, WNDCLASSA> m;
	return m;
}

Wnd* FindWnd(Window x)
{
	for (Wnd* w : C().wnds)
		if (w->xid == x) return w;
	return nullptr;
}

LPARAM MakeLp(int x, int y) { return (LPARAM)(uint32_t)((x & 0xffff) | ((y & 0xffff) << 16)); }

WPARAM Keys(unsigned state)
{
	WPARAM k = 0;
	if (state & Button1Mask) k |= MK_LBUTTON;
	if (state & Button3Mask) k |= MK_RBUTTON;
	if (state & ShiftMask) k |= MK_SHIFT;
	if (state & ControlMask) k |= MK_CONTROL;
	return k;
}

void Paint(Wnd* w)
{
	if (!w->proc) return;
	w->proc(w, WM_PAINT, 0, 0);
	w->dirty = false;
}

void Dispatch(XEvent& ev)
{
	Conn& c = C();
	if (!c.levels.empty()) { // an open menu has the pointer and the keyboard
		switch (ev.type) {
		case MotionNotify: MenuMotion(ev.xmotion.x_root, ev.xmotion.y_root); return;
		case ButtonPress: MenuButton(true, ev.xbutton.x_root, ev.xbutton.y_root); return;
		case ButtonRelease: MenuButton(false, ev.xbutton.x_root, ev.xbutton.y_root); return;
		case KeyPress:
			if (XLookupKeysym(&ev.xkey, 0) == XK_Escape) CloseMenu(0, true);
			return;
		case Expose:
			for (auto& l : c.levels)
				if (l->xid == ev.xexpose.window) ShowLevel(*l);
			if (IsMenuWindow(ev.xexpose.window)) return;
			break;
		default: break;
		}
	}
	Wnd* w = FindWnd(ev.xany.window);
	if (!w || !w->proc) return;
	switch (ev.type) {
	case Expose: w->dirty = true; break;
	case MotionNotify: w->proc(w, WM_MOUSEMOVE, Keys(ev.xmotion.state), MakeLp(ev.xmotion.x, ev.xmotion.y)); break;
	case LeaveNotify:
		if (ev.xcrossing.mode == NotifyNormal && w->track_leave && !c.capture) {
			w->track_leave = false;
			w->proc(w, WM_MOUSELEAVE, 0, 0);
		}
		break;
	case ButtonPress: {
		const auto& b = ev.xbutton;
		if (b.button == Button4 || b.button == Button5) { // wheel: screen coordinates as on Windows
			const int delta = b.button == Button4 ? 120 : -120;
			w->proc(w, WM_MOUSEWHEEL, (WPARAM)(((uint32_t)(delta & 0xffff) << 16) | Keys(b.state)),
			        MakeLp(b.x_root, b.y_root));
		} else if (b.button == Button1) {
			const bool dbl = b.time - w->last_click < 500 && std::abs(b.x - w->click_x) <= 4 &&
			                 std::abs(b.y - w->click_y) <= 4;
			w->last_click = dbl ? 0 : b.time;
			w->click_x = b.x;
			w->click_y = b.y;
			w->proc(w, dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, Keys(b.state) | MK_LBUTTON, MakeLp(b.x, b.y));
		} else if (b.button == Button3) {
			w->proc(w, WM_RBUTTONDOWN, Keys(b.state) | MK_RBUTTON, MakeLp(b.x, b.y));
		}
		break;
	}
	case ButtonRelease: {
		const auto& b = ev.xbutton;
		if (b.button == Button1) w->proc(w, WM_LBUTTONUP, Keys(b.state) & ~MK_LBUTTON, MakeLp(b.x, b.y));
		else if (b.button == Button3) w->proc(w, WM_RBUTTONUP, Keys(b.state) & ~MK_RBUTTON, MakeLp(b.x, b.y));
		break;
	}
	default: break;
	}
}

int Idle(HWND)
{
	Conn& c = C();
	if (!c.dpy) return -1;
	while (XPending(c.dpy)) {
		XEvent ev;
		XNextEvent(c.dpy, &ev);
		Dispatch(ev);
		if (!c.dpy) return -1;
	}
	const uint64_t now = NowMs();
	for (size_t i = 0; i < c.wnds.size(); ++i) {
		Wnd* w = c.wnds[i];
		for (size_t k = 0; k < w->timers.size(); ++k) {
			auto& t = w->timers[k];
			if (now >= t.next) {
				t.next = std::max(t.next + t.ms, now); // no catching up after a stall
				const UINT_PTR id = t.id;
				if (w->proc) w->proc(w, WM_TIMER, id, 0);
			}
		}
	}
	for (Wnd* w : c.wnds)
		if (w->dirty && w->visible) Paint(w);
	XFlush(c.dpy);
	return ConnectionNumber(c.dpy);
}

int ConnectionFd(HWND)
{
	return C().dpy ? ConnectionNumber(C().dpy) : -1;
}

} // namespace w32x

using namespace w32x;

// ---------------------------------------------------------------------------------------------------------------
// Win32 functions

BOOL GetClassInfoA(HINSTANCE, LPCSTR name, WNDCLASSA* wc)
{
	auto it = Classes().find(name ? name : "");
	if (it == Classes().end()) return FALSE;
	if (wc) *wc = it->second;
	return TRUE;
}

WORD RegisterClassA(const WNDCLASSA* wc)
{
	if (!wc || !wc->lpszClassName) return 0;
	Classes()[wc->lpszClassName] = *wc;
	return 1;
}

HWND CreateWindowExA(DWORD, LPCSTR cls, LPCSTR, DWORD style, int x, int y, int w, int h, HWND parent, HMENU,
                     HINSTANCE, void* param)
{
	auto it = Classes().find(cls ? cls : "");
	if (it == Classes().end()) return nullptr;
	Conn& c = C();
	if (!c.dpy) {
		c.dpy = XOpenDisplay(nullptr);
		if (!c.dpy) return nullptr;
	}
	auto* wnd   = new Wnd();
	wnd->proc   = it->second.lpfnWndProc;
	wnd->parent = (Window)(uintptr_t)parent; // the host's X11 window id
	wnd->back   = std::make_unique<Bitmap>(w, h);
	wnd->xid    = MakeXWindow(c.dpy, wnd->parent ? wnd->parent : RootWindow(c.dpy, DefaultScreen(c.dpy)), x, y,
	                          wnd->back->w, wnd->back->h, false);
	if (!wnd->xid) {
		delete wnd;
		return nullptr;
	}
	wnd->gc  = XCreateGC(c.dpy, wnd->xid, 0, nullptr);
	wnd->img = MakeImage(c.dpy, wnd->back.get());
	c.refs++;
	c.wnds.push_back(wnd);
	CREATESTRUCTA cs{param};
	wnd->proc(wnd, WM_CREATE, 0, (LPARAM)&cs);
	wnd->visible = (style & WS_VISIBLE) != 0;
	if (wnd->visible) XMapWindow(c.dpy, wnd->xid);
	XFlush(c.dpy);
	return wnd;
}

BOOL DestroyWindow(HWND w)
{
	if (!w) return FALSE;
	Conn& c = C();
	if (c.menu_owner == w) {
		w->menu_handler = nullptr;
		CloseMenu(0, false);
	}
	if (c.capture == w) c.capture = nullptr;
	if (w->proc) w->proc(w, WM_DESTROY, 0, 0);
	FreeImage(w->img);
	if (c.dpy) {
		XFreeGC(c.dpy, w->gc);
		XDestroyWindow(c.dpy, w->xid);
		XFlush(c.dpy);
	}
	c.wnds.erase(std::remove(c.wnds.begin(), c.wnds.end(), w), c.wnds.end());
	delete w;
	if (--c.refs == 0 && c.dpy) {
		delete c.menu_font;
		c.menu_font = nullptr;
		XCloseDisplay(c.dpy);
		c.dpy = nullptr;
	}
	return TRUE;
}

BOOL ShowWindow(HWND w, int cmd)
{
	if (!w || !C().dpy) return FALSE;
	w->visible = cmd != SW_HIDE;
	if (w->visible) XMapWindow(C().dpy, w->xid), w->dirty = true;
	else XUnmapWindow(C().dpy, w->xid);
	XFlush(C().dpy);
	return TRUE;
}

LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM) { return 0; }
LONG_PTR GetWindowLongPtrA(HWND w, int) { return w ? w->user : 0; }
LONG_PTR SetWindowLongPtrA(HWND w, int, LONG_PTR v)
{
	if (!w) return 0;
	const LONG_PTR old = w->user;
	w->user = v;
	return old;
}

UINT_PTR SetTimer(HWND w, UINT_PTR id, UINT ms, void*)
{
	if (!w) return 0;
	KillTimer(w, id);
	w->timers.push_back({id, std::max(ms, 10u), NowMs() + ms});
	return id;
}

BOOL KillTimer(HWND w, UINT_PTR id)
{
	if (!w) return FALSE;
	auto& t = w->timers;
	t.erase(std::remove_if(t.begin(), t.end(), [id](const Wnd::Timer& x) { return x.id == id; }), t.end());
	return TRUE;
}

BOOL InvalidateRect(HWND w, const RECT*, BOOL)
{
	if (w) w->dirty = true;
	return TRUE;
}

HDC BeginPaint(HWND w, PAINTSTRUCT* ps)
{
	HDC dc = GetDC(w);
	if (ps) {
		ps->hdc     = dc;
		ps->fErase  = FALSE;
		ps->rcPaint = RECT{0, 0, w->back->w, w->back->h};
	}
	return dc;
}

BOOL EndPaint(HWND w, const PAINTSTRUCT* ps)
{
	if (C().dpy && w->img) XPutImage(C().dpy, w->xid, w->gc, w->img, 0, 0, 0, 0, (unsigned)w->back->w, (unsigned)w->back->h);
	if (ps) ReleaseDC(w, ps->hdc);
	return TRUE;
}

HWND SetCapture(HWND w)
{
	HWND old    = C().capture;
	C().capture = w;
	return old;
}

BOOL ReleaseCapture()
{
	HWND w = C().capture;
	C().capture = nullptr;
	if (w && w->proc) w->proc(w, WM_CAPTURECHANGED, 0, 0);
	return TRUE;
}

BOOL TrackMouseEvent(TRACKMOUSEEVENT* t)
{
	if (t && t->hwndTrack && (t->dwFlags & TME_LEAVE)) t->hwndTrack->track_leave = true;
	return TRUE;
}

BOOL GetWindowRect(HWND w, RECT* r)
{
	if (!w || !r || !C().dpy) return FALSE;
	Display* d = C().dpy;
	int x = 0, y = 0;
	Window child;
	XTranslateCoordinates(d, w->xid, RootWindow(d, DefaultScreen(d)), 0, 0, &x, &y, &child);
	*r = RECT{x, y, x + w->back->w, y + w->back->h};
	return TRUE;
}

BOOL ScreenToClient(HWND w, POINT* p)
{
	RECT r;
	if (!p || !GetWindowRect(w, &r)) return FALSE;
	p->x -= r.left;
	p->y -= r.top;
	return TRUE;
}

DWORD GetTickCount() { return (DWORD)NowMs(); }

WORD GetUserDefaultUILanguage()
{
	for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"}) {
		const char* s = std::getenv(v);
		if (s && *s) return (s[0] == 'd' && s[1] == 'e') ? 0x0407 : 0x0409;
	}
	return 0x0409;
}

BOOL GetModuleHandleExA(DWORD, LPCSTR, HMODULE* m)
{
	static int self;
	if (m) *m = &self;
	return TRUE;
}

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
} // namespace

HDC GetDC(HWND w) { return NewDc(w ? w->back.get() : nullptr, w); }
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

namespace w32x {
struct Menu {
	std::shared_ptr<MenuTree> tree = std::make_shared<MenuTree>();
	std::vector<Menu*> subs; // owned (destroyed with the parent, as on Windows)
};
} // namespace w32x

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

int TrackPopupMenu(HMENU m, UINT flags, int x, int y, int, HWND owner, const RECT*)
{
	Conn& c = C();
	if (!m || !owner || !c.dpy) return 0;
	CloseMenu(0, false);
	c.menu_owner = owner;
	c.menu_armed = false;
	OpenLevel(m->tree, x, y, (flags & TPM_RIGHTALIGN) != 0, (flags & TPM_BOTTOMALIGN) != 0, -1);
	XFlush(c.dpy);
	return 0; // the choice arrives through x11menu::SetHandler
}

void x11menu::SetHandler(HWND owner, std::function<void(int)> handler)
{
	if (owner) owner->menu_handler = std::move(handler);
}

#endif // !_WIN32 && NUKED_SC55_X11_EDITOR
