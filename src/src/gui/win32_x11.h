#pragma once
// The Win32/GDI subset of the plug-in editor (editor_win32.cpp and the editor_*.inc files) for Linux / X11
// (issue #112). The editor code stays the same on both systems: windows, device contexts, bitmaps, brushes,
// pens and fonts are implemented here on top of 32-bit BGRA memory bitmaps (software drawing, same pixel
// format as the Windows DIB sections), the window is an X11 child window of the host's window, text uses
// embedded fonts (Liberation Sans for Arial / Segoe UI, DejaVu Sans Mono for Consolas; stb_truetype), and
// popup menus are drawn by this file in X11 popup windows.
//
// Differences to Win32 that the editor relies on being handled:
//  * The host drives the window: editor::Idle() (called from the CLAP timer / file descriptor callbacks on
//    the host's main thread) dispatches the X events, WM_TIMER and WM_PAINT to the window procedure.
//  * TrackPopupMenu with TPM_RETURNCMD returns 0 at once and shows the menu without blocking the host;
//    the chosen command arrives later through the handler set with x11menu::SetHandler.
// Only what the editor uses is implemented; everything is local to the plug-in (no symbol is exported).

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <functional>
#include <map>
#include <vector>

using BOOL      = int;
using BYTE      = uint8_t;
using WORD      = uint16_t;
using DWORD     = uint32_t;
using UINT      = unsigned int;
using LONG      = int32_t;
using LONG_PTR  = intptr_t;
using UINT_PTR  = uintptr_t;
using WPARAM    = uintptr_t;
using LPARAM    = intptr_t;
using LRESULT   = intptr_t;
using COLORREF  = uint32_t; // 0x00BBGGRR
using LPCSTR    = const char*;
using LPCWSTR   = const wchar_t*;

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
#define CALLBACK
#define WINAPI

namespace w32x {
// GDI objects (handles point to them; the editor only passes them around)
struct GdiObj {
	enum Kind { BITMAP, BRUSH, PEN, FONT } kind;
	bool stock = false;
	explicit GdiObj(Kind k) : kind(k) {}
	virtual ~GdiObj() = default;
};

struct Bitmap : GdiObj {
	int w = 0, h = 0;
	std::vector<uint32_t> px; // 0xAARRGGBB = bytes B, G, R, A (Windows DIB)
	Bitmap(int ww, int hh) : GdiObj(BITMAP), w(std::max(ww, 1)), h(std::max(hh, 1)), px((size_t)w * h, 0) {}
};

struct Brush : GdiObj {
	COLORREF c = 0;
	bool null  = false;
	Brush() : GdiObj(BRUSH) {}
};

struct Pen : GdiObj {
	COLORREF c = 0;
	int width  = 1;
	bool null  = false;
	Pen() : GdiObj(PEN) {}
};

struct Glyph {
	std::vector<uint8_t> cov;
	int w = 0, h = 0, xoff = 0, yoff = 0, advance = 0;
};

struct Font : GdiObj {
	int face = 0;
	float em = 12, scale = 1;
	int ascent = 0, descent = 0;
	std::map<int, Glyph> cache;
	Font() : GdiObj(FONT) {}
	const Glyph& Get(int cp);
};
struct Dc;
struct Wnd;
struct Menu;
} // namespace w32x

using HGDIOBJ   = w32x::GdiObj*;
using HBITMAP   = w32x::Bitmap*;
using HBRUSH    = w32x::Brush*;
using HPEN      = w32x::Pen*;
using HFONT     = w32x::Font*;
using HDC       = w32x::Dc*;
using HWND      = w32x::Wnd*;
using HMENU     = w32x::Menu*;
using HINSTANCE = void*;
using HMODULE   = void*;
using HCURSOR   = void*;

struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };

struct BITMAPINFOHEADER {
	DWORD biSize;
	LONG biWidth, biHeight;
	WORD biPlanes, biBitCount;
	DWORD biCompression, biSizeImage;
	LONG biXPelsPerMeter, biYPelsPerMeter;
	DWORD biClrUsed, biClrImportant;
};
struct RGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; };
struct BLENDFUNCTION { BYTE BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; };
struct PAINTSTRUCT { HDC hdc; BOOL fErase; RECT rcPaint; };
struct CREATESTRUCTA { void* lpCreateParams; };
struct TRACKMOUSEEVENT { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; };

using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
struct WNDCLASSA {
	UINT style;
	WNDPROC lpfnWndProc;
	int cbClsExtra, cbWndExtra;
	HINSTANCE hInstance;
	void* hIcon;
	HCURSOR hCursor;
	HBRUSH hbrBackground;
	LPCSTR lpszMenuName, lpszClassName;
};

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)((c) & 0xff))
#define GetGValue(c) ((BYTE)(((c) >> 8) & 0xff))
#define GetBValue(c) ((BYTE)(((c) >> 16) & 0xff))
#define LOWORD(l) ((WORD)(((uintptr_t)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((uintptr_t)(l)) >> 16) & 0xffff))
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#define GET_KEYSTATE_WPARAM(w) (LOWORD(w))
#define PRIMARYLANGID(l) ((WORD)(l) & 0x3ff)
#define LANG_GERMAN 0x07

// messages
enum : UINT {
	WM_CREATE = 0x0001, WM_DESTROY = 0x0002, WM_PAINT = 0x000F, WM_ERASEBKGND = 0x0014, WM_TIMER = 0x0113,
	WM_MOUSEMOVE = 0x0200, WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202, WM_LBUTTONDBLCLK = 0x0203,
	WM_RBUTTONDOWN = 0x0204, WM_RBUTTONUP = 0x0205, WM_MOUSEWHEEL = 0x020A, WM_CAPTURECHANGED = 0x0215,
	WM_MOUSELEAVE = 0x02A3,
};
enum : UINT { MK_LBUTTON = 0x1, MK_RBUTTON = 0x2, MK_SHIFT = 0x4, MK_CONTROL = 0x8 };
enum : UINT { TME_LEAVE = 0x2 };
enum : UINT { CS_DBLCLKS = 0x8 };
enum : DWORD { WS_CHILD = 0x40000000, WS_VISIBLE = 0x10000000, WS_CLIPSIBLINGS = 0x04000000 };
enum : int { SW_HIDE = 0, SW_SHOW = 5 };
enum : int { GWLP_USERDATA = -21 };
#define IDC_ARROW ((const char*)32512)
#define LoadCursor(inst, name) ((HCURSOR) nullptr)

// GDI
enum : DWORD { SRCCOPY = 0x00CC0020 };
enum : DWORD { BI_RGB = 0 };
enum : UINT { DIB_RGB_COLORS = 0 };
enum : BYTE { AC_SRC_OVER = 0, AC_SRC_ALPHA = 1 };
enum : int { TRANSPARENT = 1, OPAQUE = 2 };
enum : int { HALFTONE = 4, COLORONCOLOR = 3 };
enum : int { PS_SOLID = 0 };
enum : int { NULL_BRUSH = 5, NULL_PEN = 8 };
enum : int { FW_NORMAL = 400, FW_SEMIBOLD = 600, FW_BOLD = 700, FW_HEAVY = 900 };
enum : UINT {
	DT_TOP = 0x0, DT_LEFT = 0x0, DT_CENTER = 0x1, DT_RIGHT = 0x2, DT_VCENTER = 0x4, DT_BOTTOM = 0x8,
	DT_SINGLELINE = 0x20, DT_NOPREFIX = 0x800, DT_END_ELLIPSIS = 0x8000,
};
enum : DWORD { DEFAULT_CHARSET = 1, OUT_DEFAULT_PRECIS = 0, CLIP_DEFAULT_PRECIS = 0, CLEARTYPE_QUALITY = 5,
	       DEFAULT_PITCH = 0 };

// menus
enum : UINT { MF_STRING = 0x0, MF_GRAYED = 0x1, MF_CHECKED = 0x8, MF_POPUP = 0x10, MF_SEPARATOR = 0x800 };
enum : UINT { TPM_LEFTALIGN = 0x0, TPM_RIGHTALIGN = 0x8, TPM_TOPALIGN = 0x0, TPM_BOTTOMALIGN = 0x20,
	      TPM_RETURNCMD = 0x100 };

// windows
BOOL GetClassInfoA(HINSTANCE, LPCSTR name, WNDCLASSA* wc);
WORD RegisterClassA(const WNDCLASSA* wc);
HWND CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h, HWND parent,
                     HMENU menu, HINSTANCE inst, void* param); // parent: the X11 window id of the host
BOOL DestroyWindow(HWND w);
BOOL ShowWindow(HWND w, int cmd);
LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM);
LONG_PTR GetWindowLongPtrA(HWND w, int index);
LONG_PTR SetWindowLongPtrA(HWND w, int index, LONG_PTR v);
UINT_PTR SetTimer(HWND w, UINT_PTR id, UINT ms, void* fn);
BOOL KillTimer(HWND w, UINT_PTR id);
BOOL InvalidateRect(HWND w, const RECT* r, BOOL erase);
HDC BeginPaint(HWND w, PAINTSTRUCT* ps);
BOOL EndPaint(HWND w, const PAINTSTRUCT* ps);
HWND SetCapture(HWND w);
BOOL ReleaseCapture();
BOOL TrackMouseEvent(TRACKMOUSEEVENT* t);
BOOL GetWindowRect(HWND w, RECT* r);          // screen (X11 root window) coordinates
BOOL ScreenToClient(HWND w, POINT* p);
DWORD GetTickCount();
WORD GetUserDefaultUILanguage();              // from LC_ALL / LC_MESSAGES / LANG
BOOL GetModuleHandleExA(DWORD, LPCSTR, HMODULE* m);
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x2

// GDI
HDC GetDC(HWND w);                            // nullptr: a screen DC (for compatible DCs / bitmaps)
int ReleaseDC(HWND w, HDC dc);
HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h);
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO* bi, UINT usage, void** bits, void* section, DWORD offset);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ obj);
BOOL DeleteObject(HGDIOBJ obj);
HGDIOBJ GetStockObject(int i);
HBRUSH CreateSolidBrush(COLORREF c);
HPEN CreatePen(int style, int width, COLORREF c);
HFONT CreateFontA(int height, int width, int esc, int orient, int weight, DWORD italic, DWORD underline,
                  DWORD strike, DWORD charset, DWORD out, DWORD clip, DWORD quality, DWORD pitch, LPCSTR face);
BOOL GdiFlush();
BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop);
BOOL AlphaBlend(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, BLENDFUNCTION bf);
int SetStretchBltMode(HDC dc, int mode);
BOOL SetBrushOrgEx(HDC dc, int x, int y, POINT* old);
int StretchDIBits(HDC dc, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void* bits,
                  const BITMAPINFO* bi, UINT usage, DWORD rop);
int SetDIBitsToDevice(HDC dc, int x, int y, DWORD w, DWORD h, int sx, int sy, UINT start, UINT lines,
                      const void* bits, const BITMAPINFO* bi, UINT usage);
int FillRect(HDC dc, const RECT* r, HBRUSH b);
BOOL MoveToEx(HDC dc, int x, int y, POINT* old);
BOOL LineTo(HDC dc, int x, int y);
BOOL Polygon(HDC dc, const POINT* pts, int n);
BOOL Ellipse(HDC dc, int l, int t, int r, int b);
BOOL RoundRect(HDC dc, int l, int t, int r, int b, int w, int h);
COLORREF SetTextColor(HDC dc, COLORREF c);
int SetBkMode(HDC dc, int mode);
int DrawTextA(HDC dc, LPCSTR s, int n, RECT* r, UINT format);

// menus
HMENU CreatePopupMenu();
BOOL AppendMenuW(HMENU m, UINT flags, UINT_PTR id, LPCWSTR text);
BOOL DestroyMenu(HMENU m);
int TrackPopupMenu(HMENU m, UINT flags, int x, int y, int reserved, HWND owner, const RECT* r);

namespace x11menu {
// Called once with the chosen command (0: closed without a choice) after a TrackPopupMenu of this window.
void SetHandler(HWND owner, std::function<void(int)> handler);
} // namespace x11menu

namespace w32x {
// Dispatches the pending X events, timers and paints of all windows and menus of this connection
// (CLAP timer / fd callbacks). Returns the X connection's file descriptor (-1 if none).
int Idle(HWND w);
int ConnectionFd(HWND w);
} // namespace w32x
