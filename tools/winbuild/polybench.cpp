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
// Bau: x86_64-w64-mingw32-g++-posix -O2 -std=c++20 -I$S/include polybench.cpp -o polybench.exe -static -lpsapi
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
#include <memory>
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

// ======================================================================================================================
// Echtzeit-Dauermodus:  polybench --soak <plugin.clap> [<weitere.clap> ...] [Optionen]
// Spielt ein oder mehrere Plugins gleichzeitig in ECHTZEIT (Blocktakt wie ein Host, je Instanz ein Audio-Thread mit hoher
// Prioritaet / MMCSS) ueber Stunden und protokolliert je Minute: Blockzeiten (Mittel, p99, p99.9, Maximum, Blöcke ueber Budget,
// groesste Verspaetung des Starts), Ausgangspegel (Stille faellt auf), und fuer den Prozess privaten Speicher, Arbeitssatz,
// Handles, Threads, GDI-/USER-Objekte sowie Gesamt-CPU, Plugin-CPU und "fremde Last" (andere Programme). Minuten mit hoher
// fremder Last werden markiert und getrennt ausgewertet. Am Ende (oder bei Strg+C) steht eine Zusammenfassung mit Steigungen
// (nach der Anlaufzeit) fuer Speicher, Handles, Threads und GDI-Objekte.
//   --minutes M | --hours H   Dauer (Standard 60 Minuten)
//   --rate 48000 --block 512  Abtastrate, Blockgroesse
//   --events datei.txt        Eventliste (Zeit n b0 b1 ...; auch SysEx, 2-Byte-Programmwechsel); ohne: eingebautes Stueck
//   --density K               jede Note zusaetzlich K-1 mal mit hoeheren Toenen (mehr Stimmen / Einheiten)
//   --no-reset                keinen GS-Reset am Beginn jeder Runde senden (Standard: genau einer je Runde)
//   --react-min N             alle N Audiominuten Neu-Aktivierung (Abtastrate wechselt 48000 <-> 44100)
//   --save-min N              alle N Audiominuten State speichern und wieder laden
//   --warm MIN                Anlaufzeit, die bei den Steigungen entfaellt (Standard 10 Minuten)
//   --noisy PCT               Minute gilt als "gestoert", wenn fremde Last > PCT Prozent der CPU-Kapazitaet (Standard 25)
//   --prio 0|1                Audio-Threads mit hoher Prioritaet / MMCSS "Pro Audio" (Standard 1, wie ein Host)
//   --out PREFIX              Ausgabedateien PREFIX_instanzen.csv, PREFIX_system.csv, PREFIX_zusammenfassung.txt
// ======================================================================================================================
#include <mutex>
#include <psapi.h>
#include <tlhelp32.h>
namespace soak {
struct Line { double t; std::vector<uint8_t> d; };
struct Ev { alignas(8) uint8_t raw[64]{}; std::vector<uint8_t> sx; };
struct EvList { std::vector<Ev> v; };
static uint32_t ev_size(const clap_input_events_t* l) { return (uint32_t)static_cast<EvList*>(l->ctx)->v.size(); }
static const clap_event_header_t* ev_get(const clap_input_events_t* l, uint32_t i) { return reinterpret_cast<const clap_event_header_t*>(static_cast<EvList*>(l->ctx)->v[i].raw); }
static void add_midi(EvList& L, uint32_t off, uint32_t block, const uint8_t* d, size_t n)
{
	Ev e; auto* m = reinterpret_cast<clap_event_midi_t*>(e.raw);
	m->header.size = sizeof(*m); m->header.time = std::min(off, block - 1); m->header.space_id = CLAP_CORE_EVENT_SPACE_ID; m->header.type = CLAP_EVENT_MIDI; m->header.flags = 0;
	m->port_index = 0; for (size_t i = 0; i < 3; ++i) m->data[i] = i < n ? d[i] : 0;
	L.v.push_back(std::move(e));
}
static void add_sysex(EvList& L, uint32_t off, uint32_t block, const std::vector<uint8_t>& d)
{
	Ev e; e.sx = d; auto* m = reinterpret_cast<clap_event_midi_sysex_t*>(e.raw);
	m->header.size = sizeof(*m); m->header.time = std::min(off, block - 1); m->header.space_id = CLAP_CORE_EVENT_SPACE_ID; m->header.type = CLAP_EVENT_MIDI_SYSEX; m->header.flags = 0;
	m->port_index = 0; m->buffer = e.sx.data(); m->size = uint32_t(e.sx.size());
	L.v.push_back(std::move(e));
}

struct Cfg {
	std::vector<std::string> plugins; double minutes = 60; uint32_t SR = 48000, B = 512; std::string events, out = "polysoak";
	int density = 1; bool reset = true; double react_min = 0, save_min = 0, noisy = 25; int warm = 10, prio = 1;
};
static std::atomic<bool> g_stop{false};
static BOOL WINAPI on_ctrl(DWORD) { g_stop = true; return TRUE; }
static int64_t qpc() { LARGE_INTEGER x; QueryPerformanceCounter(&x); return x.QuadPart; }
static double qpf() { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return double(x.QuadPart); }

struct MinStats { double mean = 0, p99 = 0, p999 = 0, mx = 0, late_max = 0, react_ms = 0, react_blk = 0; long blocks = 0, over = 0, resync = 0, reacts = 0, saves = 0; float peak = 0; };
struct Inst {
	std::string path, id, name; HMODULE h = nullptr; const clap_plugin_t* p = nullptr; clap_host_t host{};
	std::mutex m; std::vector<float> ms; double late_max = 0, react_ms = 0, react_blk = 0; int skip = 0; long over = 0, resync = 0, reacts = 0, saves = 0, blocks = 0; float peak = 0;
	std::thread th; uint32_t rate = 48000;
};
static bool load(Inst& I, const Cfg& C)
{
	I.h = LoadLibraryA(I.path.c_str());
	auto entry = I.h ? (const clap_plugin_entry_t*)GetProcAddress(I.h, "clap_entry") : nullptr;
	if (!entry || !entry->init(I.path.c_str())) return false;
	auto f = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	const auto* d = f->get_plugin_descriptor(f, 0); I.id = d->id; I.name = d->name;
	I.host = clap_host_t{CLAP_VERSION_INIT, nullptr, "polybench", "t", "", "1", [](const clap_host_t*, const char*) -> const void* { return nullptr; }, [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
	I.p = f->create_plugin(f, &I.host, d->id);
	I.rate = C.SR;
	if (!I.p || !I.p->init(I.p) || !I.p->activate(I.p, I.rate, 1, C.B)) return false;
	I.p->start_processing(I.p);
	return true;
}
static std::vector<Line> builtin()
{
	std::vector<Line> v; static const int prog[9] = {0, 48, 33, 25, 61, 73, 52, 89, 19};
	for (int c = 0; c < 9; ++c) v.push_back({0.0, {uint8_t(0xC0 | c), uint8_t(prog[c])}});
	for (int k = 0; k < 240; ++k) {
		const double t = 0.5 + k * 0.25;
		for (int c = 0; c < 6; ++c) if ((k + c) % 3 != 2) {
			const uint8_t key = uint8_t(36 + (c * 7 + k * 5) % 48);
			v.push_back({t, {uint8_t(0x90 | c), key, uint8_t(70 + (k * 13 + c * 9) % 50)}});
			v.push_back({t + 0.4 + (c % 3) * 0.2, {uint8_t(0x80 | c), key, 0}});
		}
	}
	std::stable_sort(v.begin(), v.end(), [](const Line& a, const Line& b) { return a.t < b.t; });
	return v;
}
static void wait_until(HANDLE tm, int64_t deadline, double freq)
{
	const double rest_ms = double(deadline - qpc()) * 1000.0 / freq;
	if (rest_ms <= 0.0) return;
	if (tm) {
		LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(rest_ms * 10000.0);            // relativ, 100-ns-Schritte
		if (SetWaitableTimer(tm, &due, 0, nullptr, nullptr, FALSE)) { WaitForSingleObject(tm, INFINITE); return; }
	}
	Sleep((DWORD)rest_ms);
}
static void audio_thread(Inst& I, const Cfg& C, const std::vector<Line>& lines, double loop_len)
{
	const double freq = qpf();
	HANDLE tm = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
	if (!tm) tm = CreateWaitableTimerW(nullptr, TRUE, nullptr);
	HANDLE av = nullptr;
	if (C.prio) {
		using AvSet = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
		HMODULE a = LoadLibraryA("avrt.dll");
		auto fn = a ? reinterpret_cast<AvSet>(reinterpret_cast<void*>(GetProcAddress(a, "AvSetMmThreadCharacteristicsW"))) : nullptr;
		DWORD task = 0; if (fn) av = fn(L"Pro Audio", &task);
		if (!av) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	}
	std::vector<float> L(C.B), R(C.B); float* ch[2] = {L.data(), R.data()};
	clap_audio_buffer_t out{}; out.data32 = ch; out.channel_count = 2;
	auto st = (const clap_plugin_state_t*)I.p->get_extension(I.p, CLAP_EXT_STATE);
	static const std::vector<uint8_t> gs = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7};
	double pos = 0, loop_base = 0, next_react = C.react_min * 60.0, next_save = C.save_min * 60.0;
	size_t li = 0; bool reset_sent = false;
	int64_t dl = qpc();
	while (!g_stop.load()) {
		const double period_s = double(C.B) / I.rate; const int64_t period = int64_t(period_s * freq);
		wait_until(tm, dl, freq);
		const int64_t t_start = qpc(); const double late = double(t_start - dl) * 1000.0 / freq;
		EvList E; const double t0 = pos, t1 = pos + period_s;
		if (li == 0 && C.reset && !reset_sent) { add_sysex(E, 0, C.B, gs); reset_sent = true; }
		static const int steps[10] = {7, 12, 19, 24, 4, 16, 28, 9, 14, 21};
		while (li < lines.size() && loop_base + lines[li].t < t1 && E.v.size() < 3000) {
			const Line& l = lines[li];
			const uint32_t off = uint32_t(std::max(0.0, (loop_base + l.t - t0) * I.rate));
			if (l.d[0] == 0xf0) add_sysex(E, off, C.B, l.d);
			else {
				add_midi(E, off, C.B, l.d.data(), l.d.size());
				const uint8_t status = l.d[0] & 0xf0;
				if (C.density > 1 && (status == 0x90 || status == 0x80) && l.d.size() >= 2 && (l.d[0] & 0x0f) != 9)
					for (int k = 1; k < C.density; ++k) {
						const int key = int(l.d[1]) + steps[(k - 1) % 10]; if (key > 127) continue;
						uint8_t d3[3] = {l.d[0], uint8_t(key), l.d.size() > 2 ? l.d[2] : uint8_t(0)}; add_midi(E, off, C.B, d3, 3);
					}
			}
			if (++li >= lines.size()) { li = 0; loop_base += loop_len; reset_sent = false; break; }
		}
		clap_input_events_t iev{&E, ev_size, ev_get};
		clap_output_events_t oev{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
		clap_process_t pr{}; pr.steady_time = -1; pr.frames_count = C.B; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &iev; pr.out_events = &oev;
		I.p->process(I.p, &pr);
		const int64_t t_end = qpc(); const double dur = double(t_end - t_start) * 1000.0 / freq;
		float pk = 0; for (uint32_t i = 0; i < C.B; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
		{   // die ersten Bloecke nach einer Neu-Aktivierung (Einheiten booten) zaehlen getrennt, nicht als laufender Betrieb
		  std::lock_guard<std::mutex> g(I.m); I.peak = std::max(I.peak, pk);
		  if (I.skip > 0) { --I.skip; I.react_blk = std::max(I.react_blk, dur); }
		  else { I.ms.push_back(float(dur)); ++I.blocks; I.late_max = std::max(I.late_max, late); if (dur > period_s * 1000.0) ++I.over; } }
		pos += period_s; dl += period;
		if (t_end > dl + int64_t(0.25 * freq)) { dl = t_end; std::lock_guard<std::mutex> g(I.m); ++I.resync; }   // Takt verloren (Aussetzer): neu aufsetzen
		if (C.save_min > 0 && st && pos >= next_save) {
			std::string s; clap_ostream_t os{&s, [](const clap_ostream_t* o, const void* b, uint64_t n) -> int64_t { static_cast<std::string*>(o->ctx)->append((const char*)b, (size_t)n); return (int64_t)n; }};
			st->save(I.p, &os);
			struct In { std::string s; size_t p = 0; } in{s, 0}; clap_istream_t is{&in, [](const clap_istream_t* i, void* b, uint64_t n) -> int64_t { auto* c = static_cast<In*>(i->ctx); const uint64_t k = std::min<uint64_t>(n, c->s.size() - c->p); memcpy(b, c->s.data() + c->p, (size_t)k); c->p += (size_t)k; return (int64_t)k; }};
			st->load(I.p, &is); next_save += C.save_min * 60.0; { std::lock_guard<std::mutex> g(I.m); ++I.saves; } dl = qpc();
		}
		if (C.react_min > 0 && pos >= next_react) {
			const int64_t a = qpc();
			I.p->stop_processing(I.p); I.p->deactivate(I.p); I.rate = (I.rate == 48000) ? 44100 : 48000; I.p->activate(I.p, I.rate, 1, C.B); I.p->start_processing(I.p);
			const double ms = double(qpc() - a) * 1000.0 / freq; next_react += C.react_min * 60.0;
			{ std::lock_guard<std::mutex> g(I.m); ++I.reacts; I.react_ms = std::max(I.react_ms, ms); I.skip = 3; } dl = qpc();
		}
	}
	if (tm) CloseHandle(tm);
}
static double sorted_q(std::vector<float>& v, double q) { if (v.empty()) return 0; size_t i = std::min(v.size() - 1, (size_t)(q * (v.size() - 1) + 0.5)); return v[i]; }
static MinStats rollup(Inst& I)
{
	std::vector<float> v; MinStats s;
	{ std::lock_guard<std::mutex> g(I.m); v.swap(I.ms); s.late_max = I.late_max; s.over = I.over; s.resync = I.resync; s.reacts = I.reacts; s.saves = I.saves; s.react_ms = I.react_ms; s.react_blk = I.react_blk; s.peak = I.peak; s.blocks = (long)v.size();
	  I.late_max = 0; I.over = 0; I.resync = 0; I.reacts = 0; I.saves = 0; I.react_ms = 0; I.react_blk = 0; I.peak = 0; }
	if (!v.empty()) { double sum = 0; for (float x : v) sum += x; s.mean = sum / v.size(); std::sort(v.begin(), v.end()); s.p99 = sorted_q(v, 0.99); s.p999 = sorted_q(v, 0.999); s.mx = v.back(); }
	return s;
}
struct Sys { double priv = 0, ws = 0; long handles = 0, threads = 0, gdi = 0, user = 0; double sys_pct = 0, proc_pct = 0, foreign = 0; };
static double ft100(const FILETIME& f) { return double(((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime); }
struct CpuPrev { double idle = 0, kern = 0, user = 0, pk = 0, pu = 0; int64_t wall = 0; };
static Sys sample(CpuPrev& pv, int ncpu)
{
	Sys s; PROCESS_MEMORY_COUNTERS_EX pm{}; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm);
	s.priv = pm.PrivateUsage / 1048576.0; s.ws = pm.WorkingSetSize / 1048576.0;
	DWORD hc = 0; GetProcessHandleCount(GetCurrentProcess(), &hc); s.handles = hc;
	s.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS); s.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
	if (HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); sn != INVALID_HANDLE_VALUE) {
		THREADENTRY32 te{}; te.dwSize = sizeof te; const DWORD pid = GetCurrentProcessId();
		if (Thread32First(sn, &te)) do { if (te.th32OwnerProcessID == pid) ++s.threads; } while (Thread32Next(sn, &te));
		CloseHandle(sn);
	}
	FILETIME i, k, u, c, e, pk, pu; GetSystemTimes(&i, &k, &u); GetProcessTimes(GetCurrentProcess(), &c, &e, &pk, &pu);
	const double idle = ft100(i), kern = ft100(k), user = ft100(u), pkk = ft100(pk), puu = ft100(pu);
	const double total = (kern + user) - (pv.kern + pv.user);                 // kern enthaelt idle
	if (pv.wall && total > 0) {
		s.sys_pct = 100.0 * (1.0 - (idle - pv.idle) / total);
		s.proc_pct = 100.0 * ((pkk - pv.pk) + (puu - pv.pu)) / total;
		s.foreign = std::max(0.0, s.sys_pct - s.proc_pct);
	}
	pv.idle = idle; pv.kern = kern; pv.user = user; pv.pk = pkk; pv.pu = puu; pv.wall = 1; (void)ncpu;
	return s;
}
static void slope(const std::vector<double>& x, const std::vector<double>& y, double& per_hour, double& r2)
{
	per_hour = 0; r2 = 0; const size_t n = x.size(); if (n < 3) return;
	double mx = 0, my = 0; for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; } mx /= n; my /= n;
	double sxx = 0, sxy = 0, syy = 0; for (size_t i = 0; i < n; ++i) { sxx += (x[i] - mx) * (x[i] - mx); sxy += (x[i] - mx) * (y[i] - my); syy += (y[i] - my) * (y[i] - my); }
	if (sxx > 0) { per_hour = 60.0 * sxy / sxx; r2 = syy > 0 ? (sxy * sxy) / (sxx * syy) : 0; }
}

static int main_soak(int argc, char** argv)
{
	Cfg C;
	for (int i = 2; i < argc; ++i) {
		std::string a = argv[i]; auto nx = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
		if (a == "--minutes") C.minutes = atof(nx()); else if (a == "--hours") C.minutes = 60.0 * atof(nx());
		else if (a == "--rate") C.SR = (uint32_t)atoi(nx()); else if (a == "--block") C.B = (uint32_t)atoi(nx());
		else if (a == "--events") C.events = nx(); else if (a == "--density") C.density = std::max(1, atoi(nx()));
		else if (a == "--no-reset") C.reset = false; else if (a == "--react-min") C.react_min = atof(nx()); else if (a == "--save-min") C.save_min = atof(nx());
		else if (a == "--warm") C.warm = atoi(nx()); else if (a == "--noisy") C.noisy = atof(nx()); else if (a == "--prio") C.prio = atoi(nx()); else if (a == "--out") C.out = nx();
		else if (a.rfind("--", 0) == 0) { printf("unbekannte Option %s\n", a.c_str()); return 1; }
		else C.plugins.push_back(a);
	}
	if (C.plugins.empty()) { puts("usage: polybench --soak <plugin.clap> [<weitere.clap> ...] [--minutes M|--hours H] [--events datei] [--density K] [--react-min N] [--save-min N] [--out PREFIX] ..."); return 1; }
	std::vector<Line> lines; double loop_len = 0;
	if (!C.events.empty()) {
		FILE* fp = fopen(C.events.c_str(), "r"); if (!fp) { printf("kann %s nicht lesen\n", C.events.c_str()); return 1; }
		char b[8192]; while (fgets(b, sizeof b, fp)) { char* q = b; Line l; l.t = strtod(q, &q); const int n = (int)strtol(q, &q, 10); for (int i = 0; i < n; ++i) l.d.push_back((uint8_t)strtol(q, &q, 10)); if (!l.d.empty()) lines.push_back(std::move(l)); }
		fclose(fp);
	} else lines = builtin();
	if (lines.empty()) { puts("Eventliste leer"); return 1; }
	loop_len = lines.back().t + 1.0;
	SetConsoleCtrlHandler(on_ctrl, TRUE);
	std::vector<std::unique_ptr<Inst>> insts;
	for (auto& p : C.plugins) { auto I = std::make_unique<Inst>(); I->path = p; if (!load(*I, C)) { printf("Plugin %s startet nicht\n", p.c_str()); return 1; } insts.push_back(std::move(I)); }
	const int ncpu = (int)std::thread::hardware_concurrency();
	FILE* fi = fopen((C.out + "_instanzen.csv").c_str(), "w"); FILE* fs = fopen((C.out + "_system.csv").c_str(), "w");
	if (!fi || !fs) { puts("kann Ausgabedateien nicht anlegen"); return 1; }
	fputs("uhrzeit;minute;instanz;plugin;mittel_ms;p99_ms;p999_ms;max_ms;bloecke;ueber_budget;spaet_max_ms;takt_verloren;spitze;stille;neu_aktivierungen;neu_akt_ms;neu_akt_erster_block_ms;state_zyklen;gestoert\n", fi);
	fputs("uhrzeit;minute;privat_mb;arbeitssatz_mb;handles;threads;gdi;user;cpu_gesamt_pct;cpu_plugin_pct;fremd_pct;gestoert\n", fs);
	printf("Echtzeit-Dauermodus: %zu Instanz(en), %.0f Minuten, %u Hz, Block %u (Budget %.2f ms), Dichte %d, Reset je Runde: %s, Neu-Aktivierung: %s, State: %s\n", insts.size(), C.minutes, C.SR, C.B, 1000.0 * C.B / C.SR, C.density, C.reset ? "ja" : "nein",
	       C.react_min > 0 ? (std::to_string((int)C.react_min) + " min").c_str() : "aus", C.save_min > 0 ? (std::to_string((int)C.save_min) + " min").c_str() : "aus");
	for (size_t k = 0; k < insts.size(); ++k) printf("  [%zu] %s (%s)\n", k, insts[k]->name.c_str(), insts[k]->path.c_str());
	puts("Strg+C beendet den Lauf mit Zusammenfassung. Hinweis: Netzteil, Energiemodus \"Beste Leistung\", kein Ruhezustand, keine Windows-Updates.\n");
	CpuPrev prev; sample(prev, ncpu);
	for (auto& I : insts) I->th = std::thread(audio_thread, std::ref(*I), std::cref(C), std::cref(lines), loop_len);
	struct Row { double minute; std::vector<MinStats> st; Sys sys; bool noisy; };
	std::vector<Row> rows; const int64_t t0 = qpc(); const double freq = qpf();
	for (int minute = 1; !g_stop.load() && minute <= (int)C.minutes; ++minute) {
		while (!g_stop.load() && double(qpc() - t0) / freq < minute * 60.0) Sleep(200);
		if (g_stop.load() && double(qpc() - t0) / freq < minute * 60.0 - 5.0) break;         // Abbruch mitten in der Minute: Rest verwerfen
		Row r; r.minute = minute; r.sys = sample(prev, ncpu); r.noisy = r.sys.foreign > C.noisy;
		SYSTEMTIME tm; GetLocalTime(&tm); char clock[16]; snprintf(clock, sizeof clock, "%02d:%02d:%02d", tm.wHour, tm.wMinute, tm.wSecond);
		for (size_t k = 0; k < insts.size(); ++k) {
			MinStats s = rollup(*insts[k]); r.st.push_back(s);
			fprintf(fi, "%s;%d;%zu;%s;%.3f;%.3f;%.3f;%.3f;%ld;%ld;%.2f;%ld;%.3f;%d;%ld;%.0f;%.0f;%ld;%d\n", clock, minute, k, insts[k]->name.c_str(), s.mean, s.p99, s.p999, s.mx, s.blocks, s.over, s.late_max, s.resync, double(s.peak), s.peak < 1e-4f ? 1 : 0, s.reacts, s.react_ms, s.react_blk, s.saves, r.noisy ? 1 : 0);
		}
		fprintf(fs, "%s;%d;%.1f;%.1f;%ld;%ld;%ld;%ld;%.1f;%.1f;%.1f;%d\n", clock, minute, r.sys.priv, r.sys.ws, r.sys.handles, r.sys.threads, r.sys.gdi, r.sys.user, r.sys.sys_pct, r.sys.proc_pct, r.sys.foreign, r.noisy ? 1 : 0);
		fflush(fi); fflush(fs);
		long over = 0; double worst = 0; for (auto& s : r.st) { over += s.over; worst = std::max(worst, s.mx); }
		printf("%s  Min %4d | privat %7.1f MB, Handles %5ld, Threads %3ld | CPU gesamt %5.1f %%, Plugin %5.1f %%, fremd %5.1f %%%s | ueber Budget %ld, max %.2f ms\n", clock, minute, r.sys.priv, r.sys.handles, r.sys.threads, r.sys.sys_pct, r.sys.proc_pct, r.sys.foreign, r.noisy ? " (GESTOERT)" : "", over, worst);
		fflush(stdout); rows.push_back(std::move(r));
	}
	g_stop = true; for (auto& I : insts) if (I->th.joinable()) I->th.join();
	// Zusammenfassung
	std::string rep; char b[512];
	auto P = [&](const char* fmt, auto... args) { snprintf(b, sizeof b, fmt, args...); rep += b; };
	P("Zusammenfassung Echtzeit-Dauermodus: %zu Minuten ausgewertet, %zu Instanz(en), Block %u bei %u Hz (Budget %.2f ms)\n", rows.size(), insts.size(), C.B, C.SR, 1000.0 * C.B / C.SR);
	size_t noisy_n = 0; for (auto& r : rows) noisy_n += r.noisy; P("Gestoerte Minuten (fremde Last > %.0f %%): %zu von %zu\n\n", C.noisy, noisy_n, rows.size());
	for (size_t k = 0; k < insts.size(); ++k) {
		long blocks = 0, over = 0, over_q = 0, resync = 0, silent = 0, reacts = 0; double worst = 0, worst_q = 0, late = 0, react_worst = 0, react_blk_worst = 0;
		for (auto& r : rows) { const MinStats& s = r.st[k]; blocks += s.blocks; over += s.over; resync += s.resync; worst = std::max(worst, s.mx); late = std::max(late, s.late_max); reacts += s.reacts; react_worst = std::max(react_worst, s.react_ms); react_blk_worst = std::max(react_blk_worst, s.react_blk); if (s.peak < 1e-4f) ++silent; if (!r.noisy) { over_q += s.over; worst_q = std::max(worst_q, s.mx); } }
		P("[%zu] %s: %ld Bloecke, ueber Budget %ld (in ruhigen Minuten %ld), groesste Blockzeit %.2f ms (ruhig %.2f ms), groesste Verspaetung %.2f ms, Takt verloren %ld mal, stille Minuten %ld; Neu-Aktivierungen %ld (laengste %.0f ms, erster Block danach %.0f ms, nicht in der Statistik)\n", k, insts[k]->name.c_str(), blocks, over, over_q, worst, worst_q, late, resync, silent, reacts, react_worst, react_blk_worst);
	}
	P("\nSteigungen nach der Anlaufzeit (%d Minuten), je Stunde (alle Minuten / nur ruhige Minuten; R2 = Geradheit des Trends):\n", C.warm);
	struct Col { const char* name; double Sys::* d; long Sys::* l; };
	const Col cols[] = {{"Privater Speicher [MB]", &Sys::priv, nullptr}, {"Arbeitssatz [MB]", &Sys::ws, nullptr}, {"Handles", nullptr, &Sys::handles}, {"Threads", nullptr, &Sys::threads}, {"GDI-Objekte", nullptr, &Sys::gdi}, {"USER-Objekte", nullptr, &Sys::user}};
	for (const Col& c : cols) {
		std::vector<double> xa, ya, xq, yq;
		for (auto& r : rows) if (r.minute > C.warm) { const double v = c.d ? r.sys.*c.d : double(r.sys.*c.l); xa.push_back(r.minute); ya.push_back(v); if (!r.noisy) { xq.push_back(r.minute); yq.push_back(v); } }
		double sa, ra, sq, rq; slope(xa, ya, sa, ra); slope(xq, yq, sq, rq);
		P("  %-24s alle: %+9.2f /h (R2 %.2f, %zu Min) | ruhig: %+9.2f /h (R2 %.2f, %zu Min)\n", c.name, sa, ra, xa.size(), sq, rq, xq.size());
	}
	P("\nLesart: Ein dauerhaft steigender Wert mit hohem R2 ueber viele Minuten deutet auf Wachstum hin, einzelne Spruenge (z. B. beim ersten Aufwecken von Einheiten, nach einer Neu-Aktivierung) nicht.\n");
	P("Aussetzer: 'ueber Budget' = Block dauerte laenger als sein Zeitbudget; 'Takt verloren' = Rueckstand von mehr als 0,25 s (hoerbarer Aussetzer).\n");
	fputs(rep.c_str(), stdout); if (FILE* fz = fopen((C.out + "_zusammenfassung.txt").c_str(), "w")) { fputs(rep.c_str(), fz); fclose(fz); }
	fclose(fi); fclose(fs);
	for (auto& I : insts) { I->p->stop_processing(I->p); I->p->deactivate(I->p); I->p->destroy(I->p); }
	return 0;
}
} // namespace soak

int main(int argc, char** argv)
{
	if (argc >= 2 && std::string(argv[1]) == "--soak") return soak::main_soak(argc, argv);
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
