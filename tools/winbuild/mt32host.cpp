// MT-32 test (issue #69): plays an event list through the CLAP plug-in Emu-MT32, RMS per second,
// optionally device switching via state during playback (time of the block with the restart).
// usage: mt32host <clap> <events.txt> <seconds> [state] [t_switch state2]
//   channels of the event list are mapped onto the MT-32 parts (1..9 -> 2..10, 10 stays rhythm, 11..16 are dropped),
//   NK_NOSHIFT=1: unchanged (e.g. for SC-55 comparisons).
#include <windows.h>
#include "clap/clap.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
struct E { double t; std::vector<uint8_t> d; };
struct Ev { union { clap_event_midi_t m; clap_event_midi_sysex_t s; }; std::vector<uint8_t> buf; };
static std::vector<Ev> evs;
static uint32_t ev_size(const clap_input_events_t*) { return (uint32_t)evs.size(); }
static const clap_event_header_t* ev_get(const clap_input_events_t*, uint32_t i) { return &evs[i].m.header; }
struct Src { std::string s; size_t pos = 0; };
static int64_t rd(const clap_istream_t* st, void* buf, uint64_t n)
{
	auto* src = (Src*)st->ctx;
	const size_t k = std::min<size_t>(n, src->s.size() - src->pos);
	memcpy(buf, src->s.data() + src->pos, k);
	src->pos += k;
	return (int64_t)k;
}
static void load_state(const clap_plugin_t* p, const char* s)
{
	auto st = (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
	Src src{s};
	clap_istream_t is{&src, rd};
	printf("State %s: %s\n", st->load(p, &is) ? "loaded" : "ERROR", s);
}
int main(int argc, char** argv)
{
	std::vector<E> ev;
	{
		FILE* f = fopen(argv[2], "r");
		char line[8192];
		while (f && fgets(line, sizeof line, f)) {
			char* q = line;
			E e;
			e.t   = strtod(q, &q);
			int n = strtol(q, &q, 10);
			for (int i = 0; i < n; i++) e.d.push_back((uint8_t)strtol(q, &q, 10));
			if (e.d.empty()) continue;
			if (!getenv("NK_NOSHIFT") && e.d[0] >= 0x80 && e.d[0] < 0xf0) {
				const int ch = e.d[0] & 15;
				if (ch >= 10) continue;
				if (ch < 9) e.d[0] = (uint8_t)((e.d[0] & 0xf0) | (ch + 1));
			}
			ev.push_back(e);
		}
		if (f) fclose(f);
	}
	const double secs = atof(argv[3]);
	HMODULE h = LoadLibraryA(argv[1]);
	auto entry = (const clap_plugin_entry_t*)GetProcAddress(h, "clap_entry");
	entry->init(argv[1]);
	auto f = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	printf("Plugin: %s | %s\n", f->get_plugin_descriptor(f, 0)->id, f->get_plugin_descriptor(f, 0)->name);
	clap_host_t host{CLAP_VERSION_INIT, nullptr, "t", "t", "", "1",
	                 [](const clap_host_t*, const char*) -> const void* { return nullptr; }, [](const clap_host_t*) {},
	                 [](const clap_host_t*) {}, [](const clap_host_t*) {}};
	auto p = f->create_plugin(f, &host, f->get_plugin_descriptor(f, 0)->id);
	if (!p || !p->init(p)) { puts("init fail"); return 1; }
	if (argc > 4) load_state(p, argv[4]);
	const uint32_t B = 512;
	auto t0 = std::chrono::steady_clock::now();
	if (!p->activate(p, 44100, 1, B)) { puts("activate fail"); return 1; }
	printf("Activate: %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	p->start_processing(p);
	std::vector<float> L(B), R(B);
	float* ch[2] = {L.data(), R.data()};
	clap_audio_buffer_t out{};
	out.data32 = ch; out.channel_count = 2;
	clap_input_events_t in{nullptr, ev_size, ev_get};
	clap_process_t pr{};
	pr.frames_count = B; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &in;
	const double tsw = argc > 6 ? atof(argv[5]) : -1;
	size_t ei = 0;
	double ss = 0, ssall = 0, tp = 0, worst = 0;
	long n = 0, nall = 0;
	bool switched = false;
	for (long pos = 0; pos < (long)(secs * 44100); pos += B) {
		const double t1 = (pos + B) / 44100.0;
		if (tsw >= 0 && !switched && t1 >= tsw) { load_state(p, argv[6]); switched = true; }
		evs.clear();
		evs.reserve(4096);
		while (ei < ev.size() && ev[ei].t < t1) {
			const auto& d = ev[ei].d;
			const uint32_t off = (uint32_t)std::max(0.0, (ev[ei].t - pos / 44100.0) * 44100);
			Ev e{};
			if (d[0] == 0xf0) {
				e.buf = d;
				e.s.header = {sizeof(clap_event_midi_sysex_t), std::min(off, B - 1), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI_SYSEX, 0};
				e.s.size = (uint32_t)d.size();
			} else {
				e.m.header = {sizeof(clap_event_midi_t), std::min(off, B - 1), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
				for (size_t i = 0; i < d.size() && i < 3; i++) e.m.data[i] = d[i];
			}
			evs.push_back(std::move(e));
			ei++;
		}
		for (auto& e : evs)
			if (e.m.header.type == CLAP_EVENT_MIDI_SYSEX) e.s.buffer = e.buf.data();
		auto a = std::chrono::steady_clock::now();
		p->process(p, &pr);
		const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
		tp += dt;
		worst = std::max(worst, dt);
		for (uint32_t i = 0; i < B; i++) { ss += L[i] * L[i] + R[i] * R[i]; }
		n += B;
		if (n >= 44100) {
			printf("t=%5.1f s  RMS %.4f\n", t1, std::sqrt(ss / (2 * n)));
			ssall += ss; nall += n; ss = 0; n = 0;
		}
	}
	ssall += ss; nall += n;
	printf("RMS total %.4f (%.1f dBFS) | compute time %.1f s for %.0f s of audio, longest block %.2f s\n",
	       std::sqrt(ssall / (2 * nall)), 20 * std::log10(std::sqrt(ssall / (2 * nall)) + 1e-12), tp, secs, worst);
	auto st = (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
	struct Dst { std::string s; } dst;
	clap_ostream_t os{&dst, [](const clap_ostream_t* o, const void* b, uint64_t k) -> int64_t {
		((Dst*)o->ctx)->s.append((const char*)b, k); return (int64_t)k; }};
	st->save(p, &os);
	printf("State: %s\n", dst.s.c_str());
	p->stop_processing(p);
	p->deactivate(p);
	p->destroy(p);
	entry->deinit();
	return 0;
}
