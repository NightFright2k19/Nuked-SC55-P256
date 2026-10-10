// Nuked SC-55 Poly – VST 2.4 entry point.
//
// Wraps the CLAP plugin of this same binary (single-model build, see
// NUKED_SC55_ONLY_MODEL) behind the VST2 binary interface. MIDI and SysEx
// events are passed through sample-accurately.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "clap/clap.h"
#include "vst2_abi.h"
#include "nuked_sc55.h"
#ifdef _WIN32
#include "gui/editor.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VST_EXPORT extern "C" __declspec(dllexport)
#else
#include <dlfcn.h>
#define VST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Product family / vendor shown by VST2 hosts
#if defined(NUKED_SC55_DEVICE_8850)
#define NUKED_SC55_FAMILY "Nuked SC-8850 P256"
#elif defined(NUKED_SC55_ENGINE_88PRO)
#define NUKED_SC55_FAMILY "Nuked SC-88 P256"
#else
#define NUKED_SC55_FAMILY "Nuked SC-55 P256"
#endif
#ifndef NUKED_SC55_VST2_NAME
#define NUKED_SC55_VST2_NAME NUKED_SC55_FAMILY
#endif
#ifndef NUKED_SC55_VST2_VENDOR
#define NUKED_SC55_VST2_VENDOR NUKED_SC55_FAMILY
#endif
#ifndef NUKED_SC55_VST2_ID
#define NUKED_SC55_VST2_ID 0x53355031 // 'S5P1'
#endif

extern "C" const clap_plugin_entry_t clap_entry;

namespace {

using namespace vst2;

std::string ModulePath()
{
#ifdef _WIN32
	HMODULE hm = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCSTR>(&ModulePath),
	                   &hm);
	char buf[MAX_PATH * 2] = {};
	GetModuleFileNameA(hm, buf, sizeof(buf));
	return buf;
#else
	Dl_info info{};
	dladdr(reinterpret_cast<void*>(&ModulePath), &info);
	return info.dli_fname ? info.dli_fname : "";
#endif
}

void CopyString(void* dst, const char* src, size_t max_len)
{
	auto d = static_cast<char*>(dst);
	std::strncpy(d, src, max_len - 1);
	d[max_len - 1] = 0;
}

struct Bridge {
	AEffect fx{};
	HostCallback host = nullptr;

	const clap_plugin_t* plugin = nullptr;
	clap_host_t clap_host{};

	double sample_rate   = 44100.0;
	int32_t block_size   = 512;
	double active_rate   = 0.0;
	uint32_t active_block = 0;
	bool active          = false;

	// Pending events of the current block
	struct Pending {
		int32_t delta;
		bool is_sysex;
		clap_event_midi_t midi;
		size_t sysex_index;
	};
	std::vector<Pending> pending;
	std::vector<std::vector<uint8_t>> sysex_data;
	size_t sysex_used = 0;

	// Per-chunk CLAP event list
	std::vector<clap_event_midi_sysex_t> chunk_sysex;
	std::vector<const clap_event_header_t*> chunk_events;

	char state_chunk[64] = {};
	ERect edit_rect{};

	NukedSc55* Engine() const
	{
		return plugin ? static_cast<NukedSc55*>(plugin->plugin_data) : nullptr;
	}

	static uint32_t InSize(const clap_input_events_t* list)
	{
		return static_cast<uint32_t>(
		        static_cast<Bridge*>(list->ctx)->chunk_events.size());
	}
	static const clap_event_header_t* InGet(const clap_input_events_t* list,
	                                        uint32_t i)
	{
		return static_cast<Bridge*>(list->ctx)->chunk_events[i];
	}
	static bool OutPush(const clap_output_events_t*, const clap_event_header_t*)
	{
		return true;
	}

	bool Create()
	{
		const auto path = ModulePath();
		if (!clap_entry.init(path.c_str())) {
			return false;
		}
		auto factory = static_cast<const clap_plugin_factory_t*>(
		        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
		if (!factory || factory->get_plugin_count(factory) < 1) {
			return false;
		}
		const auto desc = factory->get_plugin_descriptor(factory, 0);

		clap_host = clap_host_t{
		        .clap_version     = CLAP_VERSION_INIT,
		        .host_data        = this,
		        .name             = "VST2 bridge",
		        .vendor           = "",
		        .url              = "",
		        .version          = "1",
		        .get_extension    = [](const clap_host_t*, const char*)
		                -> const void* { return nullptr; },
		        .request_restart  = [](const clap_host_t*) {},
		        .request_process  = [](const clap_host_t*) {},
		        .request_callback = [](const clap_host_t*) {},
		};

		plugin = factory->create_plugin(factory, &clap_host, desc->id);
		if (!plugin || !plugin->init(plugin)) {
			plugin = nullptr;
			return false;
		}
		if (auto ns = Engine()) {
			// Let the host know the chunk changed (marks the project as modified)
			ns->state_changed_callback = [this] {
				if (host) host(&fx, 42 /* audioMasterUpdateDisplay */, 0, 0, nullptr, 0.0f);
			};
		}
		pending.reserve(4096);
		chunk_events.reserve(4096);
		chunk_sysex.reserve(256);
		return true;
	}

	void Destroy()
	{
#ifdef _WIN32
		if (auto ns = Engine(); ns && ns->editor) {
			editor::Destroy(ns->editor);
			ns->editor = nullptr;
		}
#endif
		if (plugin) {
			if (active) {
				plugin->stop_processing(plugin);
				plugin->deactivate(plugin);
			}
			plugin->destroy(plugin);
			plugin = nullptr;
		}
		clap_entry.deinit();
	}

	void EnsureActive()
	{
		if (!plugin) {
			return;
		}
		// Booting all emulated units is expensive (and resets the GS state),
		// so only re-activate when the sample rate actually changes. Larger
		// host blocks are split into chunks instead.
		if (active && active_rate == sample_rate) {
			return;
		}
		if (active) {
			plugin->stop_processing(plugin);
			plugin->deactivate(plugin);
			active = false;
		}
		const auto max_block = static_cast<uint32_t>(std::max(block_size, 64));
		if (plugin->activate(plugin, sample_rate, 1, max_block)) {
			plugin->start_processing(plugin);
			active       = true;
			active_rate  = sample_rate;
			active_block = max_block;
		}
	}

	void AddEvents(const Events* ev)
	{
		for (int32_t i = 0; i < ev->numEvents; ++i) {
			const Event* e = ev->events[i];
			if (!e) {
				continue;
			}
			if (e->type == kMidiType) {
				const auto m = reinterpret_cast<const MidiEvent*>(e);
				Pending p{};
				p.delta    = m->deltaFrames;
				p.is_sysex = false;
				p.midi.header.size     = sizeof(clap_event_midi_t);
				p.midi.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
				p.midi.header.type     = CLAP_EVENT_MIDI;
				p.midi.port_index      = 0;
				p.midi.data[0]         = m->midiData[0];
				p.midi.data[1]         = m->midiData[1];
				p.midi.data[2]         = m->midiData[2];
				pending.push_back(p);

			} else if (e->type == kSysExType) {
				const auto s = reinterpret_cast<const SysExEvent*>(e);
				if (!s->sysexDump || s->dumpBytes <= 0) {
					continue;
				}
				if (sysex_used == sysex_data.size()) {
					sysex_data.emplace_back();
				}
				auto& buf = sysex_data[sysex_used];
				buf.assign(s->sysexDump, s->sysexDump + s->dumpBytes);
				Pending p{};
				p.delta       = s->deltaFrames;
				p.is_sysex    = true;
				p.sysex_index = sysex_used++;
				pending.push_back(p);
			}
		}
	}

	void Process(float** outputs, int32_t num_frames)
	{
		float* out_l = outputs[0];
		float* out_r = outputs[1];

		EnsureActive();
		if (!active || num_frames <= 0) {
			std::fill(out_l, out_l + std::max(num_frames, 0), 0.0f);
			std::fill(out_r, out_r + std::max(num_frames, 0), 0.0f);
			pending.clear();
			sysex_used = 0;
			return;
		}

		std::stable_sort(pending.begin(), pending.end(),
		                 [](const Pending& a, const Pending& b) {
			                 return a.delta < b.delta;
		                 });

		size_t ev_pos = 0;
		for (int32_t start = 0; start < num_frames;) {
			const auto len = static_cast<uint32_t>(
			        std::min<int32_t>(num_frames - start,
			                          static_cast<int32_t>(active_block)));

			chunk_events.clear();
			chunk_sysex.clear();
			while (ev_pos < pending.size() &&
			       pending[ev_pos].delta < start + static_cast<int32_t>(len)) {
				auto& p = pending[ev_pos++];
				const auto t = static_cast<uint32_t>(
				        std::clamp<int32_t>(p.delta - start, 0,
				                            static_cast<int32_t>(len) - 1));
				if (p.is_sysex) {
					const auto& buf = sysex_data[p.sysex_index];
					clap_event_midi_sysex_t sx{};
					sx.header.size     = sizeof(clap_event_midi_sysex_t);
					sx.header.time     = t;
					sx.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
					sx.header.type     = CLAP_EVENT_MIDI_SYSEX;
					sx.port_index      = 0;
					sx.buffer          = buf.data();
					sx.size            = static_cast<uint32_t>(buf.size());
					chunk_sysex.push_back(sx);
					chunk_events.push_back(nullptr); // fixed up below
				} else {
					p.midi.header.time = t;
					chunk_events.push_back(&p.midi.header);
				}
			}
			// Point the SysEx placeholders at the (now stable) vector
			for (size_t i = 0, k = 0; i < chunk_events.size(); ++i) {
				if (!chunk_events[i]) {
					chunk_events[i] = &chunk_sysex[k++].header;
				}
			}

			float* ch[2] = {out_l + start, out_r + start};
			clap_audio_buffer_t out{};
			out.data32        = ch;
			out.channel_count = 2;

			clap_input_events_t in_ev{this, &InSize, &InGet};
			clap_output_events_t out_ev{this, &OutPush};

			clap_process_t proc{};
			proc.steady_time         = -1;
			proc.frames_count        = len;
			proc.audio_outputs       = &out;
			proc.audio_outputs_count = 1;
			proc.in_events           = &in_ev;
			proc.out_events          = &out_ev;

			plugin->process(plugin, &proc);
			start += static_cast<int32_t>(len);
		}

		pending.clear();
		sysex_used = 0;
	}
};

Bridge* Get(AEffect* fx)
{
	return static_cast<Bridge*>(fx->object);
}

intptr_t Dispatcher(AEffect* fx, int32_t opcode, int32_t index, intptr_t value,
                    void* ptr, float opt)
{
	auto b = Get(fx);
	switch (opcode) {
	case effOpen: return 0;

	case effClose:
		b->Destroy();
		delete b;
		return 0;

	case effSetSampleRate: b->sample_rate = opt; return 0;
	case effSetBlockSize: b->block_size = static_cast<int32_t>(value); return 0;

	case effMainsChanged:
		if (value) {
			b->EnsureActive();
		}
		return 0;

	case effProcessEvents:
		if (ptr) {
			b->AddEvents(static_cast<const Events*>(ptr));
		}
		return 1;

	case effGetProgram: return 0;
	case effGetProgramName:
	case effGetProgramNameIndexed:
		if (ptr) CopyString(ptr, "Default", 24);
		return 1;

	case effGetPlugCategory: return kPlugCategSynth;

#ifdef _WIN32
	case effEditGetRect:
		if (ptr) {
			b->edit_rect = {0, 0, int16_t(editor::Height), int16_t(editor::Width)};
			*static_cast<ERect**>(ptr) = &b->edit_rect;
		}
		return 1;
	case effEditOpen:
		if (auto ns = b->Engine()) {
			if (!ns->editor) ns->editor = editor::Create(ns, NUKED_SC55_VST2_NAME);
			return editor::Open(ns->editor, ptr) ? 1 : 0;
		}
		return 0;
	case effEditClose:
		if (auto ns = b->Engine(); ns && ns->editor) editor::Close(ns->editor);
		return 0;
	case effEditIdle: return 0;
#endif

	case effGetChunk:
		if (auto ns = b->Engine(); ns && ptr) {
#if defined(NUKED_SC55_DEVICE_8850) || defined(NUKED_SC55_DEVICE_88)
			const int n = std::snprintf(b->state_chunk, sizeof(b->state_chunk), "NSC55P1 max_voices=%d gain=%d",
			                            ns->max_voices.load(),
			                            static_cast<int>(std::lround(ns->gain_db.load() * 10.0f)));
#elif defined(NUKED_SC55_ENGINE_88PRO)
			const int n = std::snprintf(b->state_chunk, sizeof(b->state_chunk),
			                            "NSC55P1 max_voices=%d map=%d gain=%d pnote=%d", ns->max_voices.load(),
			                            ns->tone_map.load(),
			                            static_cast<int>(std::lround(ns->gain_db.load() * 10.0f)),
			                            ns->preview_note.load());
#else
			const int n = std::snprintf(b->state_chunk, sizeof(b->state_chunk), "NSC55P1 max_voices=%d gain=%d",
			                            ns->max_voices.load(),
			                            static_cast<int>(std::lround(ns->gain_db.load() * 10.0f)));
#endif
			*static_cast<void**>(ptr) = b->state_chunk;
			return n;
		}
		return 0;
	case effSetChunk:
		if (auto ns = b->Engine(); ns && ptr && value > 0) {
			char buf[64] = {};
			std::memcpy(buf, ptr, std::min<size_t>(sizeof(buf) - 1, size_t(value)));
			int v = 0;
			if (std::sscanf(buf, "NSC55P1 max_voices=%d", &v) == 1) ns->SetMaxVoices(v);
#ifdef NUKED_SC55_ENGINE_88PRO
			if (const char* m = std::strstr(buf, "map="); NukedSc55::HasToneMap() && m && m[4] >= '0' && m[4] <= '2')
				ns->tone_map = m[4] - '0';
			if (const char* m = std::strstr(buf, "gain="); m)
				ns->gain_db = std::clamp(std::atoi(m + 5) / 10.0f, NukedSc55::kGainMinDb, NukedSc55::kGainMaxDb);
			else if (const char* v = std::strstr(buf, "vol="); v)
				ns->gain_db = NukedSc55::GainFromLegacyVolume(std::atoi(v + 4));
			if (const char* m = std::strstr(buf, "pnote="); m)
				ns->preview_note = std::clamp(std::atoi(m + 6), 0, 127);
#else
			if (const char* m = std::strstr(buf, "gain="); m) // older states: no GAIN = 0 dB
				ns->gain_db = std::clamp(std::atoi(m + 5) / 10.0f, NukedSc55::kGainMinDb, NukedSc55::kGainMaxDb);
#endif
		}
		return 1;

	case effGetEffectName:
		if (ptr) CopyString(ptr, NUKED_SC55_VST2_NAME, 32);
		return 1;
	case effGetProductString:
		if (ptr) CopyString(ptr, NUKED_SC55_VST2_NAME, 64);
		return 1;
	case effGetVendorString:
		if (ptr) CopyString(ptr, NUKED_SC55_VST2_VENDOR, 64);
		return 1;
	case effGetVendorVersion: return 1000;
	case effGetVstVersion: return kVstVersion;

	case effCanDo: {
		const auto s = static_cast<const char*>(ptr);
		if (!s) return 0;
		if (!std::strcmp(s, "receiveVstEvents") ||
		    !std::strcmp(s, "receiveVstMidiEvent") ||
		    !std::strcmp(s, "receiveVstSysExEvent")) {
			return 1;
		}
		return 0;
	}
	case effGetTailSize: return 0;
	case effGetNumMidiInputChannels: return 16;

	case effStartProcess:
	case effStopProcess: return 0;

	default: return 0;
	}
}

void ProcessReplacing(AEffect* fx, float** /*in*/, float** out, int32_t n)
{
	Get(fx)->Process(out, n);
}

void ProcessAccumulate(AEffect* fx, float** /*in*/, float** out, int32_t n)
{
	// Deprecated accumulating process: render to temp, then add.
	std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
	float* tmp[2] = {l.data(), r.data()};
	Get(fx)->Process(tmp, n);
	for (int32_t i = 0; i < n; ++i) {
		out[0][i] += l[i];
		out[1][i] += r[i];
	}
}

void SetParameter(AEffect*, int32_t, float) {}
float GetParameter(AEffect*, int32_t) { return 0.0f; }

} // namespace

VST_EXPORT AEffect* VSTPluginMain(HostCallback host)
{
	if (!host) {
		return nullptr;
	}

	auto b  = new Bridge();
	b->host = host;
	if (!b->Create()) {
		delete b;
		return nullptr;
	}

	auto& fx                  = b->fx;
	fx.magic                  = kMagic;
	fx.dispatcher             = &Dispatcher;
	fx.process_deprecated     = &ProcessAccumulate;
	fx.setParameter           = &SetParameter;
	fx.getParameter           = &GetParameter;
	fx.numPrograms            = 1;
	fx.numParams              = 0;
	fx.numInputs              = 0;
	fx.numOutputs             = 2;
	fx.flags                  = FlagCanReplacing | FlagIsSynth | FlagProgramChunks
#ifdef _WIN32
	                          | FlagHasEditor
#endif
	        ;
	fx.ioRatio                = 1.0f;
	fx.object                 = b;
	fx.uniqueID               = NUKED_SC55_VST2_ID;
	fx.version                = 1000;
	fx.processReplacing       = &ProcessReplacing;
	fx.processDoubleReplacing = nullptr;

	// Old hosts need this to send MIDI at all
	host(&fx, audioMasterWantMidi, 0, 1, nullptr, 0.0f);
	return &fx;
}
