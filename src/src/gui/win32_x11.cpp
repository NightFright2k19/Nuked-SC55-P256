// The windows and popup menus of the Win32 shim on Linux / X11 (issue #112); see win32_shim.h. One X11 child window
// per editor, popup menus drawn into X11 override-redirect windows; the drawing itself is in win32_gdi.cpp.
// Everything runs on the thread that calls editor::Idle (the host's main thread).

#if !defined(_WIN32) && defined(NUKED_SC55_X11_EDITOR)

#include "win32_shim_impl.h"

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

namespace w32x {

// ---------------------------------------------------------------------------------------------------------------
// X connection, windows and menus

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

HDC GetDC(HWND w) { return NewDc(w ? w->back.get() : nullptr, w); }

// --- menus -----------------------------------------------------------------------------------------------------

int TrackPopupMenu(HMENU m, UINT flags, int x, int y, int, HWND owner, const RECT*)
{
	Conn& c = C();
	if (!m || !owner || !c.dpy) return 0;
	CloseMenu(0, false);
	c.menu_owner = owner;
	c.menu_armed = false;
	OpenLevel(m->tree, x, y, (flags & TPM_RIGHTALIGN) != 0, (flags & TPM_BOTTOMALIGN) != 0, -1);
	XFlush(c.dpy);
	return 0; // the choice arrives through popupmenu::SetHandler
}

void popupmenu::SetHandler(HWND owner, std::function<void(int)> handler)
{
	if (owner) owner->menu_handler = std::move(handler);
}

#endif // !_WIN32 && NUKED_SC55_X11_EDITOR