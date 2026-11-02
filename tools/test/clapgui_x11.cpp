// Linux CLAP GUI test host (issue #112): opens the plug-in's X11 editor in a window of its own, drives it like a
// DAW (timer-support every 33 ms, posix-fd-support for the X connection) and plays notes, so the editor can be
// looked at, clicked (xdotool) and photographed (import -window root) under Xvfb.
// Build: g++ -O2 -std=c++17 -I<plug-in sources>/include tools/test/clapgui_x11.cpp -lX11 -ldl -o clapgui_x11
// Usage: clapgui_x11 <plugin.clap> [seconds] [state-in] [state-out]   (window title "P256 GUI test")
#include "clap/clap.h"
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <poll.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static const clap_plugin_t* g_plugin = nullptr;
static std::vector<clap_id> g_timers;
static int g_fd = -1;
static int g_state_changes = 0;

static bool reg_timer(const clap_host_t*, uint32_t, clap_id* id) { *id = (clap_id)g_timers.size(); g_timers.push_back(*id); return true; }
static bool unreg_timer(const clap_host_t*, clap_id) { g_timers.clear(); return true; }
static bool reg_fd(const clap_host_t*, int fd, clap_posix_fd_flags_t) { g_fd = fd; return true; }
static bool mod_fd(const clap_host_t*, int, clap_posix_fd_flags_t) { return true; }
static bool unreg_fd(const clap_host_t*, int) { g_fd = -1; return true; }
static const clap_host_timer_support_t host_timer = {reg_timer, unreg_timer};
static const clap_host_posix_fd_support_t host_fd = {reg_fd, mod_fd, unreg_fd};
static void mark_dirty(const clap_host_t*) { g_state_changes++; }
static const clap_host_state_t host_state = {mark_dirty};

struct Ev { clap_event_midi_t m; };
static std::vector<Ev> evs;
static uint32_t ev_size(const clap_input_events_t*) { return (uint32_t)evs.size(); }
static const clap_event_header_t* ev_get(const clap_input_events_t*, uint32_t i) { return &evs[i].m.header; }
static void midi(uint8_t a, uint8_t b, uint8_t c) {
	Ev e{}; e.m.header = {sizeof(clap_event_midi_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
	e.m.data[0] = a; e.m.data[1] = b; e.m.data[2] = c; evs.push_back(e);
}
static std::vector<uint8_t> blob; static size_t rd = 0;
static int64_t os_write(const clap_ostream_t*, const void* d, uint64_t n) { blob.insert(blob.end(), (const uint8_t*)d, (const uint8_t*)d + n); return (int64_t)n; }
static int64_t is_read(const clap_istream_t*, void* d, uint64_t n) { n = std::min<uint64_t>(n, blob.size() - rd); memcpy(d, blob.data() + rd, n); rd += n; return (int64_t)n; }

int main(int argc, char** argv) {
	if (argc < 2) { puts("usage: clapgui_x11 <plugin.clap> [seconds] [state-in] [state-out]"); return 1; }
	const double secs = argc > 2 ? atof(argv[2]) : 20;
	void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
	auto entry = (const clap_plugin_entry_t*)dlsym(h, "clap_entry");
	entry->init(argv[1]);
	auto f = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	clap_host_t host{CLAP_VERSION_INIT, nullptr, "clapgui_x11", "P256", "", "1",
	        [](const clap_host_t*, const char* id) -> const void* {
		        if (!strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &host_timer;
		        if (!strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &host_fd;
		        if (!strcmp(id, CLAP_EXT_STATE)) return &host_state;
		        return nullptr; },
	        [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
	g_plugin = f->create_plugin(f, &host, f->get_plugin_descriptor(f, 0)->id);
	if (!g_plugin || !g_plugin->init(g_plugin)) { puts("init failed"); return 1; }
	auto st = (const clap_plugin_state_t*)g_plugin->get_extension(g_plugin, CLAP_EXT_STATE);
	if (argc > 3 && strcmp(argv[3], "-")) {
		FILE* in = fopen(argv[3], "rb");
		if (in) { int c; while ((c = fgetc(in)) != EOF) blob.push_back((uint8_t)c); fclose(in); rd = 0;
			clap_istream_t is{nullptr, is_read}; printf("state load %d\n", st->load(g_plugin, &is)); }
	}
	auto gui   = (const clap_plugin_gui_t*)g_plugin->get_extension(g_plugin, CLAP_EXT_GUI);
	auto ptim  = (const clap_plugin_timer_support_t*)g_plugin->get_extension(g_plugin, CLAP_EXT_TIMER_SUPPORT);
	auto pfd   = (const clap_plugin_posix_fd_support_t*)g_plugin->get_extension(g_plugin, CLAP_EXT_POSIX_FD_SUPPORT);
	if (!gui || !gui->is_api_supported(g_plugin, CLAP_WINDOW_API_X11, false)) { puts("no X11 GUI"); return 1; }
	Display* d = XOpenDisplay(nullptr);
	if (!d) { puts("no display"); return 1; }
	uint32_t w = 0, hh = 0;
	gui->create(g_plugin, CLAP_WINDOW_API_X11, false);
	gui->get_size(g_plugin, &w, &hh);
	Window top = XCreateSimpleWindow(d, DefaultRootWindow(d), 20, 20, w, hh, 0, 0, 0x202020);
	XStoreName(d, top, "P256 GUI test");
	XMapWindow(d, top);
	XSync(d, False);
	clap_window_t win{CLAP_WINDOW_API_X11, {}}; win.x11 = top;
	printf("set_parent %d show %d size %ux%u timers %zu fd %d\n", gui->set_parent(g_plugin, &win), gui->show(g_plugin), w, hh, g_timers.size(), g_fd);
	fflush(stdout);
	g_plugin->activate(g_plugin, 48000, 1, 256);
	g_plugin->start_processing(g_plugin);
	std::vector<float> L(256), R(256); float* ch[2] = {L.data(), R.data()};
	clap_audio_buffer_t out{}; out.data32 = ch; out.channel_count = 2;
	clap_input_events_t inq{nullptr, ev_size, ev_get};
	clap_process_t pr{}; pr.frames_count = 256; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &inq;
	auto t0 = std::chrono::steady_clock::now(); double next_timer = 0, next_block = 0; long block = 0;
	const uint8_t prog[8] = {0, 48, 24, 32, 56, 73, 16, 81};
	while (true) {
		const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		if (t > secs) break;
		if (t >= next_block) { // audio, in the main thread for simplicity (5.3 ms blocks)
			evs.clear();
			if (block == 0) for (int c = 0; c < 8; ++c) midi(0xC0 | c, prog[c], 0);
			if (block % 90 == 10) for (int c = 0; c < 8; ++c) midi(0x90 | c, (uint8_t)(48 + c * 3 + (block / 90) % 5), (uint8_t)(70 + c * 6));
			if (block % 90 == 70) for (int c = 0; c < 8; ++c) midi(0x80 | c, (uint8_t)(48 + c * 3 + (block / 90) % 5), 0);
			g_plugin->process(g_plugin, &pr); block++; next_block += 256.0 / 48000.0;
		}
		if (t >= next_timer) { if (ptim && !g_timers.empty()) ptim->on_timer(g_plugin, g_timers[0]); next_timer += 0.033; }
		if (g_fd >= 0) { pollfd p{g_fd, POLLIN, 0}; if (poll(&p, 1, 1) > 0 && pfd) pfd->on_fd(g_plugin, g_fd, CLAP_POSIX_FD_READ); }
		else std::this_thread::sleep_for(std::chrono::milliseconds(1));
		while (XPending(d)) { XEvent e; XNextEvent(d, &e); }
	}
	if (argc > 4) {
		blob.clear(); clap_ostream_t os{nullptr, os_write};
		st->save(g_plugin, &os); FILE* o = fopen(argv[4], "wb"); fwrite(blob.data(), 1, blob.size(), o); fclose(o);
		printf("state saved (%zu bytes)\n", blob.size());
	}
	printf("host state changes %d\n", g_state_changes);
	gui->destroy(g_plugin);
	printf("after destroy: timers %zu fd %d\n", g_timers.size(), g_fd);
	g_plugin->stop_processing(g_plugin); g_plugin->deactivate(g_plugin); g_plugin->destroy(g_plugin);
	entry->deinit(); XCloseDisplay(d);
	return 0;
}
