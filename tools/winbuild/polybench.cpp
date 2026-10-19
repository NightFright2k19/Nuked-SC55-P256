// polybench: misst, wie gut die Einheiten eines Poly-Plugins (Nuked-SC55 P256, Emu-SC88/8820/8850/MT32) auf mehrere
// CPU-Kerne verteilt werden (Worker-Pool im Plugin). Das Plugin bleibt unveraendert: polybench laedt eine fertige
// .clap/.dll, setzt ueber die vorhandenen Umgebungsvariablen NUKED_SC55_POLY_VOICES / NUKED_SC55_POLY_DYNAMIC=0
// die Zahl der Einheiten (alle wach) und misst je Block
//   - die Wandzeit des process()-Aufrufs im Audio-Thread (W) und
//   - die CPU-Zeit aller Threads des Prozesses (C, ohne die Last-Threads von --load).
// Daraus: Speedup = C / W (wie viele Kerne im Mittel gleichzeitig rechnen), Effizienz = Speedup / Threads, und der
// Verlust gegenueber dem Ideal (N Einheiten x Zeit einer Einheit / Threads). Ein Wert deutlich unter 100 % bei vier
// oder mehr Einheiten zeigt Warten (Aufwecken der Worker, Warten auf die langsamste Einheit); bei Hyper-Threading
// ist auch ohne Warten weniger als der Faktor der Thread-Zahl zu erwarten (siehe README).
// Bau: x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/include polybench.cpp -o polybench.exe -static
// usage: polybench <plugin.clap|.dll> [--n 1,2,3,4,6,8] [--seconds 15] [--block 512] [--rate 44100] [--reps 3]
//        [--per-instance V] [--events datei.txt] [--load K] [--prio 0|1] [--csv datei.csv]
//   --events: Eventliste im Format der anderen Testhosts (Zeit n b0 b1 b2 ...); ohne: synthetische Last (ca. 70 % der
//             Stimmen aller Einheiten belegt)
//   --load K: K Busy-Threads (normale Prioritaet) als Last wie in einer vollen DAW; --prio 1 (Standard): der
//             Audio-Thread laeuft mit hoher Prioritaet wie in einem Host, die Worker des Plugins mit normaler
#include <windows.h>
#include "clap/clap.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

struct Ev { union { clap_event_midi_t m; clap_event_midi_sysex_t s; }; std::vector<uint8_t> buf; };
static std::vector<Ev> evs;
static uint32_t ev_size(const clap_input_events_t*) { return (uint32_t)evs.size(); }
static const clap_event_header_t* ev_get(const clap_input_events_t*, uint32_t i) { return &evs[i].m.header; }

struct Note { double t; uint8_t d[3]; };

static double ft(const FILETIME& f) { return (((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime) * 1e-7; }
static double proc_cpu()
{
	FILETIME c, e, k, u;
	GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
	return ft(k) + ft(u);
}
static double thread_cpu(HANDLE h)
{
	FILETIME c, e, k, u;
	GetThreadTimes(h, &c, &e, &k, &u);
	return ft(k) + ft(u);
}
static double quant(std::vector<double> v, double q)
{
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, (size_t)(q * (v.size() - 1) + 0.5))];
}
static int physical_cores()
{
	DWORD len = 0;
	GetLogicalProcessorInformation(nullptr, &len);
	std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> v(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
	if (!GetLogicalProcessorInformation(v.data(), &len)) return 0;
	int n = 0;
	for (auto& i : v) if (i.Relationship == RelationProcessorCore) ++n;
	return n;
}

struct Result {
	int n = 0, threads = 0;
	double mean = 0, med = 0, p99 = 0, p999 = 0, mx = 0;
	long over = 0, blocks = 0;
	double wall = 0, cpu = 0;        // Summe der Blockzeiten (s), CPU-Zeit der Plugin-Threads (s)
};

static std::atomic<bool> g_stop{false};
static DWORD WINAPI busy(LPVOID) { volatile double x = 1.0; while (!g_stop.load(std::memory_order_relaxed)) x = x * 1.0000001 + 1e-9; return 0; }

int main(int argc, char** argv)
{
	if (argc < 2) { puts("usage: polybench <plugin.clap|.dll> [--n 1,2,3,4,6,8] [--seconds 15] [--block 512] [--rate 44100] [--reps 3] [--per-instance V] [--events datei.txt] [--load K] [--prio 0|1] [--csv datei.csv]"); return 1; }
	std::string plugin = argv[1], events, csv = "polybench_results.csv";
	std::vector<int> ns{1, 2, 3, 4, 6, 8};
	double secs = 15, warm = 2;
	uint32_t B = 512, SR = 44100;
	int per = 0, reps = 3, load = 0;
	bool prio = true;
	for (int i = 2; i < argc; ++i) {
		std::string a = argv[i];
		auto nx = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
		if (a == "--n") { ns.clear(); for (char* t = strtok((char*)nx(), ","); t; t = strtok(nullptr, ",")) ns.push_back(atoi(t)); }
		else if (a == "--seconds") secs = atof(nx());
		else if (a == "--block") B = (uint32_t)atoi(nx());
		else if (a == "--rate") SR = (uint32_t)atoi(nx());
		else if (a == "--reps") reps = std::max(1, atoi(nx()));
		else if (a == "--per-instance") per = atoi(nx());
		else if (a == "--events") events = nx();
		else if (a == "--load") load = atoi(nx());
		else if (a == "--prio") prio = atoi(nx()) != 0;
		else if (a == "--csv") csv = nx();
		else { printf("unbekannte Option %s\n", a.c_str()); return 1; }
	}
	HMODULE h = LoadLibraryA(plugin.c_str());
	if (!h) { printf("kann %s nicht laden\n", plugin.c_str()); return 1; }
	auto entry = (const clap_plugin_entry_t*)GetProcAddress(h, "clap_entry");
	if (!entry || !entry->init(plugin.c_str())) { puts("clap_entry fehlt oder init fehlgeschlagen"); return 1; }
	auto f = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	const auto* desc = f->get_plugin_descriptor(f, 0);
	const std::string id = desc->id;
	if (per <= 0) {                                   // Stimmen je Einheit (wie im Plugin)
		per = 24;
		if (id.find("mk2") != std::string::npos) per = 28;
		else if (id.find("sc8850") != std::string::npos) per = 128;
		else if (id.find("sc88pro") != std::string::npos || id.find("sc8820") != std::string::npos) per = 64;
		else if (id.find("mt32") != std::string::npos) per = 32;
	}
	const unsigned hw = std::thread::hardware_concurrency();
	const int phys = physical_cores();
	const double budget_ms = 1000.0 * B / SR;
	printf("Maschine: %u logische CPUs, %d physische Kerne%s\n", hw, phys, (phys && (int)hw > phys) ? " (Hyper-Threading)" : "");
	printf("Plugin: %s | %s | %d Stimmen je Einheit\n", id.c_str(), desc->name, per);
	printf("Block %u Frames bei %u Hz (Budget %.2f ms), %.0f s Audio je Lauf + %.0f s Anlauf, %d Wiederholungen, Audio-Thread-Prioritaet %s, %d Last-Threads\n\n",
	       B, SR, budget_ms, secs, warm, reps, prio ? "hoch" : "normal", load);

	std::vector<Note> fixed;
	if (!events.empty()) {
		FILE* fp = fopen(events.c_str(), "r");
		if (!fp) { printf("kann %s nicht lesen\n", events.c_str()); return 1; }
		char line[8192];
		while (fgets(line, sizeof line, fp)) {
			char* q = line;
			const double t = strtod(q, &q);
			const int n = strtol(q, &q, 10);
			std::vector<uint8_t> d;
			for (int i = 0; i < n; i++) d.push_back((uint8_t)strtol(q, &q, 10));
			if (d.size() == 3 && d[0] >= 0x80 && d[0] < 0xf0) fixed.push_back({t, {d[0], d[1], d[2]}});
		}
		fclose(fp);
	}

	std::vector<HANDLE> lt;
	if (prio) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	std::vector<Result> table;
	for (int n : ns) {
		SetEnvironmentVariableA("NUKED_SC55_POLY_VOICES", std::to_string(per * n).c_str());
		SetEnvironmentVariableA("NUKED_SC55_POLY_DYNAMIC", "0");          // alle Einheiten wach
		std::vector<Result> runs;
		for (int rep = 0; rep < reps; ++rep) {
			// Last: synthetische Noten (3 s lang, 15 Melodiekanaele, ca. 70 % der Stimmen) oder die Eventliste
			std::vector<Note> notes;
			if (!fixed.empty()) {
				const double span = fixed.back().t + 0.01;
				for (double off = 0; off < secs + warm; off += span)         // Liste wiederholen
					for (auto nt : fixed) { nt.t += off; notes.push_back(nt); }
			} else {
				const double dur = 3.0, dt = dur / (0.7 * n * per);
				static const int chs[15] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15};
				int k = 0;
				for (double t = 0.05; t < secs + warm; t += dt, ++k) {
					const uint8_t ch = (uint8_t)chs[k % 15], key = (uint8_t)(36 + (k * 7) % 60), vel = (uint8_t)(70 + (k * 13) % 50);
					notes.push_back({t, {(uint8_t)(0x90 | ch), key, vel}});
					notes.push_back({t + dur, {(uint8_t)(0x80 | ch), key, 0}});
				}
			}
			std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.t < b.t; });

			clap_host_t host{CLAP_VERSION_INIT, nullptr, "polybench", "t", "", "1",
			                 [](const clap_host_t*, const char*) -> const void* { return nullptr; }, [](const clap_host_t*) {},
			                 [](const clap_host_t*) {}, [](const clap_host_t*) {}};
			auto p = f->create_plugin(f, &host, desc->id);
			if (!p || !p->init(p) || !p->activate(p, SR, 1, B)) { puts("Plugin konnte nicht gestartet werden"); return 1; }
			p->start_processing(p);
			std::vector<float> L(B), R(B);
			float* ch[2] = {L.data(), R.data()};
			clap_audio_buffer_t out{};
			out.data32 = ch; out.channel_count = 2;
			clap_input_events_t in{nullptr, ev_size, ev_get};
			clap_process_t pr{};
			pr.frames_count = B; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &in;

			g_stop = false; lt.clear();
			for (int i = 0; i < load; ++i) lt.push_back(CreateThread(nullptr, 0, busy, nullptr, 0, nullptr));
			LARGE_INTEGER qf, q0, q1;
			QueryPerformanceFrequency(&qf);
			std::vector<double> ms;
			ms.reserve((size_t)(secs * SR / B) + 16);
			double cpu0 = 0, lt0 = 0;
			size_t ei = 0;
			long over = 0;
			const long warm_end = (long)(warm * SR), end = (long)((warm + secs) * SR);
			for (long pos = 0; pos < end; pos += B) {
				if (pos >= warm_end && ms.empty()) {                             // Messung beginnt
					cpu0 = proc_cpu();
					lt0 = 0; for (HANDLE hh : lt) lt0 += thread_cpu(hh);
				}
				const double t1 = (pos + B) / (double)SR;
				evs.clear(); evs.reserve(4096);
				while (ei < notes.size() && notes[ei].t < t1) {
					Ev e{};
					const uint32_t off = (uint32_t)std::max(0.0, (notes[ei].t - pos / (double)SR) * SR);
					e.m.header = {sizeof(clap_event_midi_t), std::min(off, B - 1), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
					for (int i = 0; i < 3; i++) e.m.data[i] = notes[ei].d[i];
					evs.push_back(std::move(e));
					++ei;
				}
				QueryPerformanceCounter(&q0);
				p->process(p, &pr);
				QueryPerformanceCounter(&q1);
				if (pos >= warm_end) {
					const double d = 1000.0 * (double)(q1.QuadPart - q0.QuadPart) / (double)qf.QuadPart;
					ms.push_back(d);
					if (d > budget_ms) ++over;
				}
			}
			Result r;
			r.n = n; r.blocks = (long)ms.size(); r.over = over;
			double lt1 = 0; for (HANDLE hh : lt) lt1 += thread_cpu(hh);
			r.cpu = proc_cpu() - cpu0 - (lt1 - lt0);
			g_stop = true;
			for (HANDLE hh : lt) { WaitForSingleObject(hh, 2000); CloseHandle(hh); }
			for (double d : ms) r.wall += d / 1000.0;
			r.mean = 1000.0 * r.wall / std::max<size_t>(1, ms.size());
			r.med = quant(ms, 0.5); r.p99 = quant(ms, 0.99); r.p999 = quant(ms, 0.999); r.mx = quant(ms, 1.0);
			const int workers = std::clamp(std::min(n, (int)hw - 1) - 1, 0, n - 1);   // wie im Plugin
			r.threads = workers + 1;
			runs.push_back(r);
			p->stop_processing(p); p->deactivate(p); p->destroy(p);
		}
		std::sort(runs.begin(), runs.end(), [](const Result& a, const Result& b) { return a.mean < b.mean; });
		table.push_back(runs[runs.size() / 2]);                                   // Lauf mit mittlerem Blockmittel
		const Result& r = table.back();
		printf("N=%d: Blockmittel je Wiederholung (ms):", n);
		for (auto& x : runs) printf(" %.2f", x.mean);
		printf("  -> mittlerer Lauf ausgewertet\n");
		fflush(stdout);
	}
	entry->deinit();

	// Zeit einer Einheit (seriell) aus dem N=1-Lauf (ohne Pool): CPU-Zeit je Block
	double t1 = 0;
	for (auto& r : table) if (r.n == 1 && r.blocks) t1 = 1000.0 * r.cpu / r.blocks;
	puts("\n  N Thr | Block ms: Mittel Median   p99 p99.9   max | >Budget | CPU/Block ms | Speedup Effizienz | je Einheit ms | Ideal ms Verlust");
	FILE* cf = fopen(csv.c_str(), "w");
	if (cf) fputs("N;Threads;Mittel_ms;Median_ms;p99_ms;p999_ms;max_ms;ueber_Budget;Blocks;CPU_je_Block_ms;Speedup;Effizienz;je_Einheit_ms;Ideal_ms;Verlust\n", cf);
	for (auto& r : table) {
		const double cpu_blk = r.blocks ? 1000.0 * r.cpu / r.blocks : 0;
		const double speed = r.wall > 0 ? r.cpu / r.wall : 0;
		const double eff = r.threads ? speed / r.threads : 0;
		const double per_inst = cpu_blk / r.n;
		const double ideal = t1 > 0 ? r.n * t1 / r.threads : 0;
		const double loss = ideal > 0 ? r.mean / ideal - 1.0 : 0;
		printf("%3d %3d | %9.2f %6.2f %6.2f %5.2f %6.2f | %3ld/%-4ld | %12.2f | %7.2f %7.0f %% | %13.2f | %8.2f %+6.0f %%\n", r.n, r.threads, r.mean, r.med, r.p99,
		       r.p999, r.mx, r.over, r.blocks, cpu_blk, speed, 100 * eff, per_inst, ideal, 100 * loss);
		if (cf) fprintf(cf, "%d;%d;%.3f;%.3f;%.3f;%.3f;%.3f;%ld;%ld;%.3f;%.3f;%.3f;%.3f;%.3f;%.3f\n", r.n, r.threads, r.mean, r.med, r.p99, r.p999, r.mx, r.over, r.blocks,
		                cpu_blk, speed, eff, per_inst, ideal, loss);
	}
	if (cf) { fclose(cf); printf("\nCSV: %s\n", csv.c_str()); }
	puts("Ideal = N x (Zeit einer Einheit aus dem N=1-Lauf) / Threads; Verlust = Blockmittel / Ideal - 1.");
	return 0;
}
