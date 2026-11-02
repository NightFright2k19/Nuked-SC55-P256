// macOS CLAP GUI test host (issue #112): loads a plug-in binary, opens its Cocoa editor in a window of its own and
// saves screenshots, so the editor can be looked at on the GitHub macOS runners (no ROMs there: the LCD stays dark).
//   <prefix>_panel.png  the editor view, drawn by the view itself (needs no screen recording permission)
//   <prefix>_menu.png   the whole screen after a click on SETUP (screencapture; the menu is a window of its own)
// Build: clang++ -std=c++17 -fobjc-arc -I<plug-in sources>/include tools/test/clapgui_mac.mm -framework Cocoa -o clapgui_mac
// Usage: clapgui_mac <plugin binary> <prefix> <setup x> <setup y>   (SETUP position in editor coordinates)
#import <Cocoa/Cocoa.h>
#include "clap/clap.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <string>

static const clap_plugin_t* g_plugin;
static const clap_plugin_gui_t* g_gui;

static void SavePng(NSView* v, const std::string& path)
{
	NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
	[v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
	NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
	[png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
	printf("saved %s (%ld x %ld)\n", path.c_str(), (long)rep.pixelsWide, (long)rep.pixelsHigh);
}

int main(int argc, char** argv)
{
	if (argc < 5) { puts("usage: clapgui_mac <plugin binary> <prefix> <setup x> <setup y>"); return 1; }
	const std::string prefix = argv[2];
	const double sx = atof(argv[3]), sy = atof(argv[4]);
	@autoreleasepool {
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
		void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
		if (!h) { printf("dlopen failed: %s\n", dlerror()); return 1; }
		auto entry = (const clap_plugin_entry_t*)dlsym(h, "clap_entry");
		if (!entry || !entry->init(argv[1])) { puts("clap_entry missing or init failed"); return 1; }
		auto f = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
		static clap_host_t host{CLAP_VERSION_INIT, nullptr, "clapgui_mac", "P256", "", "1",
		        [](const clap_host_t*, const char*) -> const void* { return nullptr; },
		        [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
		const auto* d = f->get_plugin_descriptor(f, 0);
		g_plugin = f->create_plugin(f, &host, d->id);
		if (!g_plugin) { puts("create_plugin failed"); return 1; }
		printf("%s: init %s\n", d->id, g_plugin->init(g_plugin) ? "ok" : "FAILED (no ROMs)");
		g_gui = (const clap_plugin_gui_t*)g_plugin->get_extension(g_plugin, CLAP_EXT_GUI);
		if (!g_gui || !g_gui->is_api_supported(g_plugin, CLAP_WINDOW_API_COCOA, false) ||
		    !g_gui->create(g_plugin, CLAP_WINDOW_API_COCOA, false)) {
			puts("no Cocoa editor");
			return 1;
		}
		uint32_t w = 0, hh = 0;
		g_gui->get_size(g_plugin, &w, &hh);
		const NSRect screen = [NSScreen mainScreen].frame;
		NSWindow* win = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, NSMaxY(screen) - 120 - hh, w, hh)
		                                            styleMask:NSWindowStyleMaskTitled
		                                              backing:NSBackingStoreBuffered
		                                                defer:NO];
		win.title = @"P256 GUI test";
		clap_window_t cw{CLAP_WINDOW_API_COCOA, {}};
		cw.cocoa = (__bridge void*)win.contentView;
		if (!g_gui->set_parent(g_plugin, &cw)) { puts("set_parent failed"); return 1; }
		g_gui->show(g_plugin);
		[win makeKeyAndOrderFront:nil];
		[NSApp activateIgnoringOtherApps:YES];
		printf("editor %u x %u, view %s\n", w, hh, win.contentView.subviews.count ? "added" : "MISSING");

		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
			NSView* ed = win.contentView.subviews.firstObject;
			if (!ed) exit(2);
			SavePng(ed, prefix + "_panel.png");
			// the menu runs its own event loop; this timer fires inside it (common modes)
			NSTimer* t = [NSTimer timerWithTimeInterval:1.5 repeats:NO block:^(NSTimer*) {
				system(("screencapture -x " + prefix + "_menu.png").c_str());
				printf("menu shot %s_menu.png\n", prefix.c_str());
				exit(0);
			}];
			[[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
			// click on SETUP (view coordinates, origin top left -> window coordinates)
			const NSPoint p = [ed convertPoint:NSMakePoint(sx, sy) toView:nil];
			for (NSEventType type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
				NSEvent* e = [NSEvent mouseEventWithType:type location:p modifierFlags:0 timestamp:0
				                            windowNumber:win.windowNumber context:nil eventNumber:0 clickCount:1
				                                pressure:1];
				if (type == NSEventTypeLeftMouseDown) [ed mouseDown:e];
				else [ed mouseUp:e];
			}
			puts("menu closed without a shot");
			exit(3);
		});
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(20 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
			puts("timeout");
			exit(4);
		});
		[NSApp run];
	}
	return 0;
}
