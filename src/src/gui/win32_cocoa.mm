// The windows and popup menus of the Win32 shim on macOS / Cocoa (issue #112); see win32_shim.h. One NSView per
// editor, added to the host's view; the editor draws into the window's 32-bit back buffer (win32_gdi.cpp), which
// drawRect: puts on the screen. Mouse events become WM_* messages, timers are NSTimers on the main run loop and
// popup menus are native NSMenus. Everything runs on the main thread.
//
// The view class is registered at run time under a name unique to this plug-in binary: several P256 plug-ins (and
// several versions of one) can be loaded into the same host process, and Objective-C class names are global.
// Built with ARC (-fobjc-arc).

#if defined(__APPLE__) && defined(NUKED_SC55_COCOA_EDITOR)

#include "win32_shim_impl.h"

#import <Cocoa/Cocoa.h>
#include <objc/message.h>
#include <objc/runtime.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace w32x {

struct Wnd {
	NSView* view = nil;
	std::unique_ptr<Bitmap> back;
	WNDPROC proc = nullptr;
	UINT cls_style = 0;
	LONG_PTR user = 0;
	bool track_leave = false, in_paint = false;
	std::map<UINT_PTR, NSTimer*> timers;
	std::function<void(int)> menu_handler;
	int menu_choice = 0;
	double wheel = 0; // scrolled distance not yet sent (precise trackpad deltas)
};

namespace {

Wnd* g_capture = nullptr;

std::map<std::string, WNDCLASSA>& Classes()
{
	static std::map<std::string, WNDCLASSA> m;
	return m;
}

uint64_t NowMs()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

LPARAM MakeLp(int x, int y) { return (LPARAM)(uint32_t)((x & 0xffff) | ((y & 0xffff) << 16)); }

char kWndKey; // address = key of the view's associated Wnd pointer

Wnd* WndOf(id self)
{
	NSValue* v = objc_getAssociatedObject(self, &kWndKey);
	return v ? (Wnd*)v.pointerValue : nullptr;
}

void SetWndOf(id self, Wnd* w)
{
	objc_setAssociatedObject(self, &kWndKey, w ? [NSValue valueWithPointer:w] : nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

WPARAM Keys(NSEvent* e)
{
	WPARAM k = 0;
	const NSUInteger b = [NSEvent pressedMouseButtons];
	if (b & 1) k |= MK_LBUTTON;
	if (b & 2) k |= MK_RBUTTON;
	const NSEventModifierFlags m = e ? e.modifierFlags : [NSEvent modifierFlags];
	if (m & NSEventModifierFlagShift) k |= MK_SHIFT;
	if (m & NSEventModifierFlagControl) k |= MK_CONTROL;
	return k;
}

POINT LocalPoint(NSView* v, NSEvent* e) // view coordinates, origin top left (the view is flipped)
{
	const NSPoint p = [v convertPoint:e.locationInWindow fromView:nil];
	return POINT{(LONG)std::floor(p.x), (LONG)std::floor(p.y)};
}

// screen coordinates with the origin at the top left of the main screen (Win32 style)
double ScreenTop() { return NSMaxY([NSScreen screens].firstObject.frame); }

POINT ScreenPoint(NSPoint p) { return POINT{(LONG)std::floor(p.x), (LONG)std::floor(ScreenTop() - p.y)}; }

// --- view methods ------------------------------------------------------------------------------------------------

BOOL ViewIsFlipped(id, SEL) { return YES; }
BOOL ViewAcceptsFirstMouse(id, SEL, NSEvent*) { return YES; }
BOOL ViewIsOpaque(id, SEL) { return YES; }

void ViewDrawRect(id self, SEL, NSRect)
{
	Wnd* w = WndOf(self);
	if (!w || !w->back) return;
	if (w->proc && !w->in_paint) {
		w->in_paint = true;
		w->proc(w, WM_PAINT, 0, 0); // draws into the back buffer (BeginPaint / EndPaint)
		w->in_paint = false;
	}
	Bitmap* b = w->back.get();
	CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	// bytes B, G, R, x in memory = 32-bit little endian xRGB
	CGContextRef bc = CGBitmapContextCreate(b->px.data(), (size_t)b->w, (size_t)b->h, 8, (size_t)b->w * 4, cs,
	                                        kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst);
	CGImageRef img = bc ? CGBitmapContextCreateImage(bc) : nullptr;
	if (img) {
		CGContextSaveGState(ctx);
		CGContextSetInterpolationQuality(ctx, kCGInterpolationNone); // whole pixels on Retina screens
		CGContextTranslateCTM(ctx, 0, b->h);                         // the view is flipped, the image is not
		CGContextScaleCTM(ctx, 1, -1);
		CGContextDrawImage(ctx, CGRectMake(0, 0, b->w, b->h), img);
		CGContextRestoreGState(ctx);
		CGImageRelease(img);
	}
	if (bc) CGContextRelease(bc);
	CGColorSpaceRelease(cs);
}

void Send(id self, NSEvent* e, UINT msg, WPARAM extra_keys, WPARAM drop_keys)
{
	Wnd* w = WndOf(self);
	if (!w || !w->proc) return;
	const POINT p = LocalPoint(w->view, e);
	w->proc(w, msg, (Keys(e) | extra_keys) & ~drop_keys, MakeLp(p.x, p.y));
}

void ViewMouseDown(id self, SEL, NSEvent* e)
{
	Wnd* w = WndOf(self);
	const bool dbl = w && (w->cls_style & CS_DBLCLKS) && e.clickCount == 2;
	Send(self, e, dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN, MK_LBUTTON, 0);
}
void ViewMouseUp(id self, SEL, NSEvent* e) { Send(self, e, WM_LBUTTONUP, 0, MK_LBUTTON); }
void ViewRightMouseDown(id self, SEL, NSEvent* e) { Send(self, e, WM_RBUTTONDOWN, MK_RBUTTON, 0); }
void ViewRightMouseUp(id self, SEL, NSEvent* e) { Send(self, e, WM_RBUTTONUP, 0, MK_RBUTTON); }
void ViewMouseMoved(id self, SEL, NSEvent* e) { Send(self, e, WM_MOUSEMOVE, 0, 0); }

void ViewMouseExited(id self, SEL, NSEvent*)
{
	Wnd* w = WndOf(self);
	if (w && w->proc && w->track_leave && !g_capture) {
		w->track_leave = false;
		w->proc(w, WM_MOUSELEAVE, 0, 0);
	}
}

void ViewScrollWheel(id self, SEL, NSEvent* e)
{
	Wnd* w = WndOf(self);
	if (!w || !w->proc) return;
	// one notch = 120 as on Windows; trackpads deliver pixel distances (about 10 per notch)
	w->wheel += e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 10.0 : (e.scrollingDeltaY > 0 ? 1 : e.scrollingDeltaY < 0 ? -1 : 0);
	const int notches = (int)w->wheel;
	if (!notches) return;
	w->wheel -= notches;
	const POINT p = ScreenPoint([NSEvent mouseLocation]); // screen coordinates as on Windows
	const int delta = notches * 120;
	w->proc(w, WM_MOUSEWHEEL, (WPARAM)(((uint32_t)(delta & 0xffff) << 16) | Keys(e)), MakeLp(p.x, p.y));
}

void ViewUpdateTrackingAreas(id self, SEL _cmd)
{
	NSView* v = self;
	for (NSTrackingArea* a in [v.trackingAreas copy]) [v removeTrackingArea:a];
	[v addTrackingArea:[[NSTrackingArea alloc]
	                           initWithRect:NSZeroRect
	                                options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
	                                        NSTrackingActiveAlways | NSTrackingInVisibleRect
	                                  owner:v
	                               userInfo:nil]];
	struct objc_super sup = {self, [NSView class]};
	((void (*)(struct objc_super*, SEL))objc_msgSendSuper)(&sup, _cmd);
}

void ViewPick(id self, SEL, NSMenuItem* item) // action of the popup menu items
{
	if (Wnd* w = WndOf(self)) w->menu_choice = (int)item.tag;
}

Class ViewClass()
{
	static Class cls = [] {
		char name[64];
		std::snprintf(name, sizeof(name), "NukedP256View_%llx", (unsigned long long)(uintptr_t)&ViewIsFlipped);
		Class c = objc_allocateClassPair([NSView class], name, 0);
		class_addMethod(c, @selector(isFlipped), (IMP)ViewIsFlipped, "c@:");
		class_addMethod(c, @selector(isOpaque), (IMP)ViewIsOpaque, "c@:");
		class_addMethod(c, @selector(acceptsFirstMouse:), (IMP)ViewAcceptsFirstMouse, "c@:@");
		class_addMethod(c, @selector(drawRect:), (IMP)ViewDrawRect, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(c, @selector(mouseDown:), (IMP)ViewMouseDown, "v@:@");
		class_addMethod(c, @selector(mouseUp:), (IMP)ViewMouseUp, "v@:@");
		class_addMethod(c, @selector(mouseDragged:), (IMP)ViewMouseMoved, "v@:@");
		class_addMethod(c, @selector(mouseMoved:), (IMP)ViewMouseMoved, "v@:@");
		class_addMethod(c, @selector(rightMouseDown:), (IMP)ViewRightMouseDown, "v@:@");
		class_addMethod(c, @selector(rightMouseUp:), (IMP)ViewRightMouseUp, "v@:@");
		class_addMethod(c, @selector(rightMouseDragged:), (IMP)ViewMouseMoved, "v@:@");
		class_addMethod(c, @selector(mouseExited:), (IMP)ViewMouseExited, "v@:@");
		class_addMethod(c, @selector(scrollWheel:), (IMP)ViewScrollWheel, "v@:@");
		class_addMethod(c, @selector(updateTrackingAreas), (IMP)ViewUpdateTrackingAreas, "v@:");
		class_addMethod(c, @selector(nkPick:), (IMP)ViewPick, "v@:@");
		objc_registerClassPair(c);
		return c;
	}();
	return cls;
}

// --- menus ---------------------------------------------------------------------------------------------------------

NSString* Str(const std::u32string& s)
{
	std::u16string u;
	for (char32_t c : s) {
		if (c < 0x10000) u.push_back((char16_t)c);
		else u.push_back((char16_t)(0xD800 + ((c - 0x10000) >> 10))), u.push_back((char16_t)(0xDC00 + ((c - 0x10000) & 0x3FF)));
	}
	return [NSString stringWithCharacters:(const unichar*)u.data() length:u.size()];
}

NSMenu* BuildMenu(const MenuTree& tree, id target)
{
	NSMenu* m = [[NSMenu alloc] initWithTitle:@""];
	m.autoenablesItems = NO;
	NSFont* font = [NSFont menuFontOfSize:0];
	// "text\tnote": the notes form a second column (a tab stop after the widest text)
	CGFloat left = 0;
	for (const auto& it : tree.items) {
		if (it.flags & MF_SEPARATOR) continue;
		const size_t tab = it.text.find(U'\t');
		left = std::max(left, [Str(it.text.substr(0, tab)) sizeWithAttributes:@{NSFontAttributeName : font}].width);
	}
	NSMutableParagraphStyle* para = [[NSMutableParagraphStyle alloc] init];
	para.tabStops = @[ [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentLeft location:left + 24 options:@{}] ];
	for (const auto& it : tree.items) {
		if (it.flags & MF_SEPARATOR) {
			[m addItem:[NSMenuItem separatorItem]];
			continue;
		}
		NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:Str(it.text) action:nil keyEquivalent:@""];
		if (it.text.find(U'\t') != std::u32string::npos)
			mi.attributedTitle = [[NSAttributedString alloc]
			        initWithString:Str(it.text)
			            attributes:@{NSFontAttributeName : font, NSParagraphStyleAttributeName : para}];
		mi.enabled = !(it.flags & MF_GRAYED);
		mi.state   = (it.flags & MF_CHECKED) ? NSControlStateValueOn : NSControlStateValueOff;
		if (it.sub) {
			mi.submenu = BuildMenu(*it.sub, target);
		} else {
			mi.tag    = (NSInteger)it.id;
			mi.target = target;
			mi.action = @selector(nkPick:);
		}
		[m addItem:mi];
	}
	return m;
}

} // namespace
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
	if (it == Classes().end() || !parent) return nullptr;
	NSView* host = (__bridge NSView*)(void*)parent; // the host's NSView
	auto* wnd      = new Wnd();
	wnd->proc      = it->second.lpfnWndProc;
	wnd->cls_style = it->second.style;
	wnd->back      = std::make_unique<Bitmap>(w, h);
	NSView* v = [[ViewClass() alloc] initWithFrame:NSMakeRect(x, y, wnd->back->w, wnd->back->h)];
	SetWndOf(v, wnd);
	wnd->view = v;
	CREATESTRUCTA cs{param};
	wnd->proc(wnd, WM_CREATE, 0, (LPARAM)&cs);
	v.hidden = (style & WS_VISIBLE) == 0;
	[host addSubview:v];
	[v setNeedsDisplay:YES];
	return wnd;
}

BOOL DestroyWindow(HWND w)
{
	if (!w) return FALSE;
	if (g_capture == w) g_capture = nullptr;
	for (auto& t : w->timers) [t.second invalidate];
	w->timers.clear();
	if (w->proc) w->proc(w, WM_DESTROY, 0, 0);
	if (w->view) {
		SetWndOf(w->view, nullptr);
		[w->view removeFromSuperview];
		w->view = nil;
	}
	delete w;
	return TRUE;
}

BOOL ShowWindow(HWND w, int cmd)
{
	if (!w || !w->view) return FALSE;
	w->view.hidden = cmd == SW_HIDE;
	if (cmd != SW_HIDE) [w->view setNeedsDisplay:YES];
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
	NSTimer* t = [NSTimer timerWithTimeInterval:std::max(ms, 10u) / 1000.0
	                                    repeats:YES
	                                      block:^(NSTimer*) {
		                                      if (w->proc) w->proc(w, WM_TIMER, id, 0);
	                                      }];
	// common modes: the timer also fires while a menu is open or the mouse is dragged
	[[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
	w->timers[id] = t;
	return id;
}

BOOL KillTimer(HWND w, UINT_PTR id)
{
	if (!w) return FALSE;
	auto it = w->timers.find(id);
	if (it != w->timers.end()) {
		[it->second invalidate];
		w->timers.erase(it);
	}
	return TRUE;
}

BOOL InvalidateRect(HWND w, const RECT*, BOOL)
{
	if (w && w->view && !w->in_paint) [w->view setNeedsDisplay:YES];
	return TRUE;
}

HDC GetDC(HWND w) { return NewDc(w ? w->back.get() : nullptr, w); }

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

BOOL EndPaint(HWND w, const PAINTSTRUCT* ps) // drawRect: puts the back buffer on the screen
{
	if (ps) ReleaseDC(w, ps->hdc);
	return TRUE;
}

HWND SetCapture(HWND w)
{
	HWND old  = g_capture;
	g_capture = w; // AppKit sends the drag events to the view that got the mouse down
	return old;
}

BOOL ReleaseCapture()
{
	HWND w    = g_capture;
	g_capture = nullptr;
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
	if (!w || !r || !w->view || !w->view.window) return FALSE;
	const NSRect inWin = [w->view convertRect:w->view.bounds toView:nil];
	const NSRect scr   = [w->view.window convertRectToScreen:inWin];
	const POINT tl     = ScreenPoint(NSMakePoint(NSMinX(scr), NSMaxY(scr)));
	*r = RECT{tl.x, tl.y, tl.x + w->back->w, tl.y + w->back->h};
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
	NSString* lang = [NSLocale preferredLanguages].firstObject;
	return (lang && [lang hasPrefix:@"de"]) ? 0x0407 : 0x0409;
}

BOOL GetModuleHandleExA(DWORD, LPCSTR, HMODULE* m)
{
	static int self;
	if (m) *m = &self;
	return TRUE;
}

// --- menus -----------------------------------------------------------------------------------------------------

int TrackPopupMenu(HMENU m, UINT flags, int x, int y, int, HWND owner, const RECT*)
{
	if (!m || !owner || !owner->view || !owner->view.window) return 0;
	NSMenu* menu = BuildMenu(*m->tree, owner->view);
	// (x, y) is in screen coordinates; the menu opens with its top left corner at the given view point
	POINT p{x, y};
	ScreenToClient(owner, &p);
	const NSSize size = menu.size;
	CGFloat px = p.x, py = p.y;
	if (flags & TPM_RIGHTALIGN) px -= size.width;
	if (flags & TPM_BOTTOMALIGN) py -= size.height;
	owner->menu_choice = 0;
	auto handler = std::move(owner->menu_handler);
	owner->menu_handler = nullptr;
	[menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(px, py) inView:owner->view]; // returns when closed
	if (handler) handler(owner->menu_choice);
	return 0; // the choice went to the handler (popupmenu::SetHandler), as on Linux
}

void popupmenu::SetHandler(HWND owner, std::function<void(int)> handler)
{
	if (owner) owner->menu_handler = std::move(handler);
}

#endif // __APPLE__ && NUKED_SC55_COCOA_EDITOR
