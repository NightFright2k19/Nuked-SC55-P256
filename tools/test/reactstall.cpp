// Measures the audio-thread stall that follows a re-activation of a poly plug-in (CLAP interface, Linux test variant,
// build with TESTSRC=reactstall.cpp bash build_mtstress.sh <model> none).
// After every activate() the test plays silence in real time for <delay> seconds (like a host that starts playback a
// little later), then sends a dense chord (240 notes at once, enough to need every unit) and plays on in real time. It reports
// the time activate() took, the longest block and how many blocks exceeded the time budget.
// usage: reactstall <plugin-id> <delays in seconds, e.g. 0,0.5,1,2,4> [repetitions per delay]
#include "clap/clap.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" const clap_plugin_entry_t clap_entry;

// REACTSTALL_NPROCS=<n> pretends to have n CPUs (std::thread::hardware_concurrency asks get_nprocs), so that the worker pool and the
// parallel boot are used even on a one-core machine.
extern "C" int get_nprocs(void)
{
	const char* e = getenv("REACTSTALL_NPROCS");
	return e ? atoi(e) : (int)sysconf(_SC_NPROCESSORS_ONLN);
}

namespace {
struct Ev { alignas(8) uint8_t raw[64]{}; };
std::vector<Ev> evs;
uint32_t ev_size(const clap_input_events_t*) { return uint32_t(evs.size()); }
const clap_event_header_t* ev_get(const clap_input_events_t*, uint32_t i) { return reinterpret_cast<const clap_event_header_t*>(evs[i].raw); }
void add(uint32_t t, uint8_t a, uint8_t b, uint8_t c)
{
	Ev e; auto* m = reinterpret_cast<clap_event_midi_t*>(e.raw);
	m->header.size = sizeof(*m); m->header.time = t; m->header.space_id = CLAP_CORE_EVENT_SPACE_ID; m->header.type = CLAP_EVENT_MIDI; m->header.flags = 0;
	m->port_index = 0; m->data[0] = a; m->data[1] = b; m->data[2] = c;
	evs.push_back(e);
}
double now_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

int main(int argc, char** argv)
{
	if (argc < 3) { std::puts("usage: reactstall <plugin-id> <delays e.g. 0,0.5,1,2,4> [repetitions]"); return 2; }
	std::vector<double> delays; { std::string s = argv[2]; size_t p = 0; while (p <= s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); delays.push_back(atof(s.substr(p, q - p).c_str())); p = q + 1; } }
	const int reps = argc > 3 ? std::max(1, std::atoi(argv[3])) : 2;
	const uint32_t B = 512, SR = 48000;
	const double period_ms = 1000.0 * B / SR;
	clap_entry.init("/tmp/x/plugin.clap");
	auto f = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
	clap_host_t host{CLAP_VERSION_INIT, nullptr, "reactstall", "t", "", "1", [](const clap_host_t*, const char*) -> const void* { return nullptr; }, [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
	auto p = f->create_plugin(f, &host, argv[1]);
	if (!p || !p->init(p)) { std::puts("plug-in does not start"); return 1; }
	std::vector<float> L(B), R(B); float* ch[2] = {L.data(), R.data()};
	clap_audio_buffer_t out{}; out.data32 = ch; out.channel_count = 2;
	auto block = [&](double& dur) {
		clap_input_events_t iev{nullptr, ev_size, ev_get};
		clap_output_events_t oev{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
		clap_process_t pr{}; pr.steady_time = -1; pr.frames_count = B; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &iev; pr.out_events = &oev;
		const double t0 = now_ms(); p->process(p, &pr); dur = now_ms() - t0;
	};
	std::printf("%s, block %u at %u Hz (budget %.2f ms)\n", argv[1], B, SR, period_ms);
	std::printf("%8s | %12s | %14s | %12s | %s\n", "delay s", "activate ms", "longest block", "blocks > bud", "stall sum ms (blocks over budget)");
	for (double delay : delays) {
		std::vector<double> mx, act, over, sum;
		for (int r = 0; r < reps; ++r) {
			const double a0 = now_ms();
			if (!p->activate(p, SR, 1, B)) { std::puts("activate failed"); return 1; }
			p->start_processing(p);
			act.push_back(now_ms() - a0);
			auto next = std::chrono::steady_clock::now();
			const long wait_blocks = long(delay * 1000.0 / period_ms + 0.5);
			for (long b = 0; b < wait_blocks; ++b) {            // silence, real-time pace (host not yet playing)
				evs.clear(); double d; block(d);
				next += std::chrono::microseconds(long(period_ms * 1000)); std::this_thread::sleep_until(next);
			}
			evs.clear();                                         // dense chord at once: needs all units
			for (int n = 0; n < 240; ++n) { const uint8_t c = uint8_t(n % 15 < 9 ? n % 15 : n % 15 + 1); add(0, uint8_t(0x90 | c), uint8_t(30 + (n * 7) % 70), 90); }
			double worst = 0, ssum = 0; long nover = 0;
			for (int b = 0; b < 160; ++b) {                       // about 1.7 s of playing on
				double d; block(d); evs.clear();
				worst = std::max(worst, d); if (d > period_ms) { ++nover; ssum += d; }
				next = std::max(next + std::chrono::microseconds(long(period_ms * 1000)), std::chrono::steady_clock::now());
				std::this_thread::sleep_until(next);
			}
			mx.push_back(worst); over.push_back(double(nover)); sum.push_back(ssum);
			p->stop_processing(p); p->deactivate(p);
		}
		auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return s / v.size(); };
		auto mxv = [](const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); };
		std::printf("%8.1f | %12.0f | %11.0f ms | %12.1f | %.0f\n", delay, mean(act), mxv(mx), mean(over), mean(sum));
		std::fflush(stdout);
	}
	p->destroy(p);
	return 0;
}
