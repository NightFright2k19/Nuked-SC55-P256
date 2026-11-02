#pragma once
// Internal to the Win32 shim (win32_gdi.cpp and the window parts win32_x11.cpp / win32_cocoa.mm; issue #112):
// device contexts, pixel and text helpers and the menu model shared by the platforms. See win32_shim.h.

#include "win32_shim.h"

#include <memory>
#include <string>
#include <vector>

namespace w32x {

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

// pixels and text (win32_gdi.cpp)
uint32_t Px(COLORREF c); // GDI drawing into a DIB section leaves the alpha byte 0
void Plot(Bitmap* b, int x, int y, uint32_t p);
void FillSpan(Bitmap* b, int x0, int x1, int y, uint32_t p); // [x0, x1)
int TextWidth(Font* f, const std::u32string& s);
void DrawRun(Bitmap* b, Font* f, COLORREF col, int x, int y, const std::u32string& s, const RECT& clip);
std::u32string FromAnsi(const char* s, int n);
std::u32string FromWide(const wchar_t* s);
Font* MakeFontObj(int px, int weight, const char* face);
Dc* NewDc(Bitmap* b, Wnd* w); // a DC drawing into b (the window's back buffer, or the stock bitmap)

// popup menus as the editor builds them (AppendMenuW); shown by the platform's TrackPopupMenu
struct MenuItem {
	UINT flags = 0;
	UINT_PTR id = 0;
	std::u32string text;
	std::shared_ptr<struct MenuTree> sub;
};
struct MenuTree {
	std::vector<MenuItem> items;
};
struct Menu {
	std::shared_ptr<MenuTree> tree = std::make_shared<MenuTree>();
	std::vector<Menu*> subs; // owned (destroyed with the parent, as on Windows)
};

} // namespace w32x
