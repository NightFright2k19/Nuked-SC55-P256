// Linux CLAP check for the native plug-ins (issue #112): loads a .clap with dlopen, lists the plug-ins of the
// factory, and for each one plays 256 notes for a few seconds, reports the RMS of the last second, checks that no GUI
// extension is offered (no Linux editor yet) and that the state can be saved and loaded.
// Build: g++ -O2 -std=c++17 -I<plug-in sources>/include tools/test/clapcheck.cpp -ldl -o clapcheck
// Usage: clapcheck <plugin.clap> [seconds]   (ROMs are inside the plug-in; exit code 0 = all plug-ins sound)
#include "clap/clap.h"
#include <dlfcn.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct Ev { clap_event_midi_t m; };
static std::vector<Ev> evs;
static uint32_t ev_size(const clap_input_events_t*) { return (uint32_t)evs.size(); }
static const clap_event_header_t* ev_get(const clap_input_events_t*, uint32_t i) { return &evs[i].m.header; }
static void midi(uint32_t t, uint8_t a, uint8_t b, uint8_t c) {
	Ev e{};
	e.m.header = {sizeof(clap_event_midi_t), t, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
	e.m.data[0] = a; e.m.data[1] = b; e.m.data[2] = c;
	evs.push_back(e);
}
static std::vector<uint8_t> blob;
static int64_t os_write(const clap_ostream_t*, const void* d, uint64_t n) {
	blob.insert(blob.end(), (const uint8_t*)d, (const uint8_t*)d + n); return (int64_t)n;
}
static size_t rd = 0;
static int64_t is_read(const clap_istream_t*, void* d, uint64_t n) {
	n = std::min<uint64_t>(n, blob.size() - rd); memcpy(d, blob.data() + rd, n); rd += n; return (int64_t)n;
}

int main(int argc, char** argv) {
	if (argc < 2) { puts("usage: clapcheck <plugin.clap> [seconds]"); return 1; }
	const double secs = argc > 2 ? atof(argv[2]) : 5.0;
	void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!h) { printf("dlopen failed: %s\n", dlerror()); return 1; }
	auto entry = (const clap_plugin_entry_t*)dlsym(h, "clap_entry");
	if (!entry || !entry->init(argv[1])) { puts("clap_entry missing or init failed"); return 1; }
	auto f = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	clap_host_t host{CLAP_VERSION_INIT, nullptr, "clapcheck", "P256", "", "1",
	        [](const clap_host_t*, const char*) -> const void* { return nullptr; },
	        [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
	int bad = 0;
	const uint32_t count = f->get_plugin_count(f);
	printf("%u plug-in(s)\n", count);
	for (uint32_t n = 0; n < count; n++) {
		const auto* d = f->get_plugin_descriptor(f, n);
		auto p = f->create_plugin(f, &host, d->id);
		if (!p || !p->init(p)) { printf("  %s: init failed\n", d->id); bad++; continue; }
		const uint32_t B = 256;
		if (!p->activate(p, 48000, 1, B)) { printf("  %s: activate failed\n", d->id); bad++; continue; }
		p->start_processing(p);
		std::vector<float> L(B), R(B);
		float* ch[2] = {L.data(), R.data()};
		clap_audio_buffer_t out{}; out.data32 = ch; out.channel_count = 2;
		clap_input_events_t in{nullptr, ev_size, ev_get};
		clap_process_t pr{}; pr.frames_count = B; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &in;
		const long nb = (long)(secs * 48000 / B);
		double ss = 0, tp = 0; long cnt = 0;
		for (long b = 0; b < nb; b++) {
			evs.clear();
			for (int k = 0; k < 256; k++) {
				long t = 10 + (long)(k * 0.01 * 48000 / B);   // after the boot time of the units
				if (t == b) midi(k % B, 0x90 | (k & 1), (uint8_t)(36 + (k * 7) % 60), 100);
			}
			auto t0 = std::chrono::steady_clock::now();
			p->process(p, &pr);
			tp += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			if (b * B >= (secs - 1) * 48000) { for (uint32_t i = 0; i < B; i++) ss += L[i] * L[i] + R[i] * R[i]; cnt += 2 * B; }
		}
		const double rms = sqrt(ss / std::max(cnt, 1L));
		const bool gui = p->get_extension(p, CLAP_EXT_GUI) != nullptr;
		auto st = (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
		bool state_ok = false;
		if (st) {
			blob.clear(); rd = 0;
			clap_ostream_t os{nullptr, os_write}; clap_istream_t is{nullptr, is_read};
			state_ok = st->save(p, &os) && !blob.empty() && st->load(p, &is);
		}
		printf("  %s (%s): RMS %.4f, compute %.1f s for %.0f s audio, gui %s, state %s\n", d->id, d->name, rms, tp, secs,
		       gui ? "OFFERED" : "none", state_ok ? "ok" : "FAILED");
		if (!(rms > 1e-4) || !state_ok) bad++;
		p->stop_processing(p); p->deactivate(p); p->destroy(p);
	}
	entry->deinit();
	dlclose(h);
	return bad ? 2 : 0;
}
