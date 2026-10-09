#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <filesystem>
#include <memory>
#include <vector>

#include "clap/clap.h"
#include "nuked-sc55/backend/emu.h"
#include "speex/speex_resampler.h"
#include "poly_router.h"
#ifdef NUKED_SC55_ENGINE_88PRO
#include "c_interface.h" // 88emu (Gearmulator, GPLv3)
#endif
#include "state_log.h"

// Simple persistent worker pool: runs fn(0..count-1) in parallel, the
// calling (audio) thread participates. No allocations in Run().
class WorkerPool {
public:
	void Start(int num_threads);
	void Stop();
	void Run(int count, const std::function<void(int)>& fn);
	~WorkerPool() { Stop(); }

private:
	void WorkerLoop();
	void Drain();
	std::vector<std::thread> threads;
	std::mutex m;
	std::condition_variable cv_start, cv_done;
	const std::function<void(int)>* job = nullptr;
	int job_count = 0;
	std::atomic<int> next{0};
	int busy = 0;
	uint64_t generation = 0;
	bool quit = false;
};

class NukedSc55 {
public:
	enum class Model {
		Sc55_v1_00,
		Sc55_v1_10,
		Sc55_v1_20,
		Sc55_v1_21,
		Sc55_v2_00,
		Sc55mk2_v1_01,
#ifdef NUKED_SC55_ENGINE_88PRO
		Sc88Pro
#endif
	};

	// Init/shutdown
	NukedSc55(const clap_plugin_t plugin_class, const clap_host_t* host,
	          const Model model);

	const clap_plugin_t* GetPluginClass();

	bool Init(const clap_plugin* plugin_instance);
	void Shutdown();

	bool Activate(const double sample_rate, const uint32_t min_frame_count,
	              const uint32_t max_frame_count);

	// Processing
	clap_process_status Process(const clap_process_t* process);

	void Flush(const clap_input_events_t* in, const clap_output_events_t* out);

	void PublishFrame(const float left, const float right);

	// State handling
	bool LoadState(const clap_istream_t* stream);
	bool SaveState(const clap_ostream_t* stream);

private:
	std::filesystem::path path = {};

	Model model = {};

	clap_plugin_t plugin_class         = {};
	const clap_host_t* host            = nullptr;
	const clap_plugin* plugin_instance = nullptr;

	// One emulated SC-55 per instance; combined polyphony = N x 24/28.
	struct QueuedMidi {
		uint32_t frame;      // render-frame offset in the current block
		uint32_t offset;     // into midi_bytes
		uint32_t size;
	};
	struct Instance {
		std::unique_ptr<Emulator> emu;
		std::vector<float> buf_l, buf_r; // rendered, not yet mixed
		size_t read_pos = 0;
		std::vector<QueuedMidi> queue;   // events for the current block
		float dc_l = 0.0f, dc_r = 0.0f;  // idle output offset to cancel
#ifdef NUKED_SC55_ENGINE_88PRO
		emu88_context ctx = nullptr;     // SC-88 Pro unit (88emu)
		std::shared_ptr<std::atomic<bool>> booted = std::make_shared<std::atomic<bool>>(false);
		int applied_map   = -1;          // tone map set on this unit's panel
		// pending panel-button sequence (non-blocking, advanced while rendering)
		int seq_len = 0, seq_pos = 0;
		uint32_t seq_mask[4] = {};
		uint32_t seq_frames[4] = {};
		uint32_t seq_left = 0;
		int seq_map = -1;
#endif
		bool awake = true;               // dynamic instance management
		uint64_t sleep_seq = 0;          // state-log position the instance is synced to
		// background sync in progress (heap-allocated: keeps Instance movable)
		std::shared_ptr<std::atomic<bool>> warming =
		        std::make_shared<std::atomic<bool>>(false);
		uint64_t silent_frames = 0;      // consecutive silent output
		float last_peak = 0.0f;          // diag: last block peak (after DC)
		double last_wake_ms = 0.0;       // diag: duration of last Wake()
		int last_wake_bytes = 0;         // diag: replayed MIDI bytes
	};
	std::vector<Instance> instances;
	bool emu_ok = false;

	PolyRouter router;
	WorkerPool pool;
	std::vector<uint8_t> midi_bytes; // storage for queued MIDI/SysEx

	StateLog state_log;
	std::vector<int> awake_list;
	bool dynamic_instances = true;
	int min_active = 1;
	uint32_t sleep_after_frames = 0;
	int wakes = 0, sleeps = 0;

	void ManageInstances(int incoming_note_ons);

	// Background sync of sleeping instances ("warmer"): keeps them close to
	// the current part/effect state so that waking only has to replay a tiny
	// remainder in the audio thread (avoids a dropout on the first wake).
	struct WarmJob {
		int instance = -1;
		uint64_t target_seq = 0;
		std::vector<StateLog::Entry> entries;
	};
	std::thread warmer_thread;
	std::mutex warmer_mutex;
	std::condition_variable warmer_cv;
	WarmJob warmer_job;
	bool warmer_has_job = false, warmer_quit = false;
	uint64_t warmer_next_frame = 0, render_frame_count = 0;
	void StartWarmer();
	void StopWarmer();
	void WarmerLoop();
	void ScheduleWarming();
	void ReplayInto(Instance& inst, const std::vector<StateLog::Entry>& entries);
#ifdef NUKED_SC55_ENGINE_88PRO
	bool E88BootUnit(int i);
	void JoinBootThreads();
	std::vector<std::thread> boot_threads; // background boot of units 1..N-1
	void E88RenderFrames(Instance& inst, size_t frames);
	void E88StartMapSequence(Instance& inst, int map);
	void E88ApplyMapSync(Instance& inst, int map);
	void E88Drain(Instance& inst, size_t bytes, double extra_seconds);
#endif
	void UiTrackShort(const uint8_t* d);
	void UiResetParts();
	void HandleUiCommands();
	uint32_t panel_button_bits = 0;  // emulated front-panel button held on unit 0
	int panel_button_blocks    = 0;  // blocks until it is released

	void Wake(int i);
	void Sleep(int i);

	int target_voices = 0;
	int partials_per_instance = 24;

	double render_sample_rate_hz = 0.0;
	double output_sample_rate_hz = 0.0;

	std::array<std::vector<float>, 2> render_buf = {};

	SpeexResamplerState* resampler = nullptr;
	bool do_resample               = false;
	double resample_ratio          = 0.0f;

	// Methods
	std::vector<std::filesystem::path> GetRomEnvDirs();
	std::vector<std::filesystem::path> GetRomBasePaths();

	void ProcessEvent(const clap_event_header_t* event, uint32_t render_frame);
	void QueueMidi(PolyRouter::Mask mask, const uint8_t* data, size_t size,
	               uint32_t render_frame);
	void MeasureLoad();
	void RenderInstance(Instance& inst, uint32_t num_frames);

public:
	// For the sample callback
	static void ReceiveSample(void* userdata, const AudioFrame<int32_t>& in);
	int NumInstances() const { return static_cast<int>(instances.size()); }
	int NumAwake() const
	{
		int n = 0;
		for (const auto& i : instances) n += i.awake;
		return n;
	}
	int ActivePartials(int instance) const;

	// Diagnostics (tests only)
	struct InstanceDiag {
		bool awake; int load; uint64_t silent_frames; bool owns_notes;
		float last_peak; double last_wake_ms; int last_wake_bytes;
	};
	InstanceDiag Diag(int i) const;
#ifdef NUKED_SC55_ENGINE_88PRO
	int UnitToneMap(int i) const; // diagnostics: tone map per unit (panel LEDs)
#endif
	const PolyRouter& Router() const { return router; }

	// ---- UI / settings interface (all members thread-safe) ----------------
	struct UiPart {
		std::atomic<uint8_t> hit{0};   // max note level since last GUI read
		std::atomic<uint8_t> prog{0}, bank{0}, vol{100}, pan{64};
		std::atomic<uint8_t> rev{40}, cho{0}, expr{127};
		std::atomic<uint8_t> key_shift{0x40}; // GS 40 1x 16, 0x40 = 0 semitones
		std::atomic<bool> rhythm{false};
	};
	std::array<UiPart, 16> ui_parts;
	std::atomic<int> ui_voices{0};      // sounding partials (all instances)
	std::atomic<int> ui_awake{1};       // awake emulator instances
	std::atomic<int> max_voices{256};   // user limit (setup menu)
	std::atomic<int> ui_command{0};     // 1 = all sound off, 2 = GS reset, 3/4 = PART </> button
	std::atomic<uint64_t> ui_awake_mask{1}; // awake units (bit i = unit i)
	std::atomic<bool> lcd_ready{false};   // units exist and may be read by the GUI

	// Character-LCD memory of one unit (HD44780 DDRAM/CGRAM), for the panel. GUI thread.
	struct LcdSnapshot {
		uint8_t dd[80];
		uint8_t cg[64];
		bool on;
	};
	bool GetLcd(int unit, LcdSnapshot& out);

	int MaxSelectableVoices() const { return NumInstances() * partials_per_instance; }
	int PartialsPerInstance() const { return partials_per_instance; }
	void SetMaxVoices(int v);
	// Called by the GUI after a user change: tells the host that the plugin
	// state changed (CLAP state.mark_dirty, or the VST2 bridge's callback).
	void NotifyStateChanged();
	std::function<void()> state_changed_callback;
	const char* ModelName() const;
	bool IsMk2() const { return model == Model::Sc55mk2_v1_01; }

	void* editor = nullptr; // opaque GUI object (Windows only)

	// Tone map of the SC-88 Pro (front-panel SC-55 MAP / SC-88 MAP), stored in
	// the plugin state: 0 = SC-55, 1 = SC-88 (default), 2 = SC-88 Pro.
	float boot_dc_l = 0.0f, boot_dc_r = 0.0f; // idle offset measured on unit 0 after boot
	bool clone_units = true; // start units as copies of unit 0 (NUKED_SC55_NO_CLONE=1: boot each)
	std::atomic<int> tone_map{1};
	int last_tone_map = 1;
#ifdef NUKED_SC55_ENGINE_88PRO
	// SC-88 Pro front panel (GUI thread writes, audio thread reads)
	std::atomic<float> gain_db{0.0f};      // GAIN knob in dB, kGainMinDb..kGainMaxDb; 0 = default
	std::atomic<int> preview_note{60};     // system parameter "Prevw Note" 0..127 (C-1..G9), C4 = 60
	std::atomic<uint32_t> mute_mask{0};    // MUTE per part (bit = part = MIDI channel), not stored
	std::atomic<int> ui_preview{0};        // 1 = PREVIEW pressed, 2 = released
	std::atomic<int> ui_preview_part{0};
	static constexpr float kGainMinDb   = -12.0f, kGainMaxDb = 12.0f;
	// 88emu's output (DAC full scale = 1.0) sits about 5 dB under the SC-55 plugins (RMS and
	// peaks of the same songs); at GAIN 0 dB the SC-88 Pro is raised to their level.
	static constexpr float kLevelMatchDb = 5.0f;
	static float GainFactor(float gain_db);           // output factor incl. kLevelMatchDb
	static float GainFromLegacyVolume(int vol);       // old state "vol=0..1000" -> GAIN dB, same loudness
private:
	float applied_gain     = -1.0f; // < 0: not applied yet
	int preview_ch         = -1, preview_key = -1;
	uint32_t applied_mutes = 0;
	bool injecting         = false;
	void InjectShort(uint8_t s, uint8_t d1, uint8_t d2);
public:
#endif
	static constexpr bool HasToneMap()
	{
#ifdef NUKED_SC55_ENGINE_88PRO
		return true;
#else
		return false;
#endif
	}

private:

	void RenderAudio(const uint32_t num_frames);

	void ResampleAndPublishFrames(const uint32_t num_out_frames,
	                              float* out_left, float* out_right);
};
