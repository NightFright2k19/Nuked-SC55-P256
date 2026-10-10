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
		Sc88Pro,
		Sc8850,
		Sc88
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
		uint8_t port = 0;    // MIDI input (SC-88 Pro: 0 = IN A, 1 = IN B; SC-8850: 0..3 = A..D)
	};
	struct Instance {
		std::unique_ptr<Emulator> emu;
		std::vector<float> buf_l, buf_r; // rendered, not yet mixed
		size_t read_pos = 0;
		std::vector<QueuedMidi> queue;   // events for the current block
		float dc_l = 0.0f, dc_r = 0.0f;  // idle output offset to cancel
#ifdef NUKED_SC55_ENGINE_88PRO
		emu88_context ctx = nullptr;     // SC-88 Pro / SC-8850 / SC-88 unit (88emu)
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
	void UiTrackShort(const uint8_t* d, int port = 0);
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
	               uint32_t render_frame, uint8_t port = 0);
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
	// Parts: SC-55 1..16; SC-88 Pro A01..A16 (MIDI IN A) and B01..B16 (IN B);
	// SC-8850 A01..D16 (IN A..D, the four USB cables)
#if defined(NUKED_SC55_DEVICE_8850)
	static constexpr int kNumPorts = 4;
#elif defined(NUKED_SC55_ENGINE_88PRO)
	static constexpr int kNumPorts = 2;
#else
	static constexpr int kNumPorts = 1;
#endif
	static constexpr int kNumParts = kNumPorts * 16;
	std::array<UiPart, kNumParts> ui_parts;
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
	std::atomic<uint64_t> mute_mask{0};    // MUTE per part (bit = part = port * 16 + MIDI channel), not stored
	std::atomic<int> ui_preview{0};        // 1 = PREVIEW pressed, 2 = released
	std::atomic<int> ui_preview_part{0};   // 0..31 = A01..B16
	static constexpr float kGainMinDb   = -12.0f, kGainMaxDb = 12.0f;
	// 88emu's output (DAC full scale = 1.0) sits about 5 dB under the SC-55 plugins (RMS and
	// peaks of the same songs); at GAIN 0 dB the SC-88 Pro is raised to their level. The SC-8850
	// sits another ~2.3 dB lower (RMS of e1m1/animus/grabbag vs. SC-88 Pro and SC-55); the SC-88
	// matches the SC-88 Pro with the same 5 dB (-20.8/-17.0/-18.6 dBFS vs. -20.8/-17.0/-18.5).
#if defined(NUKED_SC55_DEVICE_8850)
	static constexpr float kLevelMatchDb = 7.5f;
#else
	static constexpr float kLevelMatchDb = 5.0f;
#endif
	static float GainFactor(float gain_db);           // output factor incl. kLevelMatchDb
	static float GainFromLegacyVolume(int vol);       // old state "vol=0..1000" -> GAIN dB, same loudness
private:
	float applied_gain     = -1.0f; // < 0: not applied yet
	int preview_ch         = -1, preview_key = -1, preview_port = 0;
	uint64_t applied_mutes = 0;
	bool injecting         = false;
	// MIDI input selected by "F5 nn" port-select messages (nn = 1: IN A, 2: IN B, SC-8850
	// also 3: IN C, 4: IN D); applies to events of the first CLAP note port and to VST2
	uint8_t selected_port  = 0;
	void InjectShort(uint8_t s, uint8_t d1, uint8_t d2, uint16_t port = 0);
public:
#endif
#if defined(NUKED_SC55_DEVICE_8850) || defined(NUKED_SC55_DEVICE_88)
	// Front panel of the SC-8850 / SC-88: the GUI drives unit 0's own panel (firmware menus,
	// LCD, LEDs). Edits made there are passed on to the other units and the state log.
	std::atomic<uint32_t> ui_panel_buttons{0}; // switches held in the GUI (bit = 88emu button)
	uint32_t PanelLeds() const;                // unit 0's panel lamps (88emu bit order)
private:
	uint32_t applied_buttons = 0;
	void E88PanelInput();
	void E88Forward(uint8_t port, const uint8_t* msg, size_t size);
	void E88PanelMutes();
public:
#endif
#ifndef NUKED_SC55_ENGINE_88PRO
	// SC-55 / SC-55mk2 front panel (Issue #10): the GUI drives unit 0's own panel (firmware
	// LCD, ALL / MUTE lamps). Edits made there are read back from unit 0's parameter memory
	// and passed on to the other units and the state log.
	std::atomic<uint32_t> ui_panel_buttons{0}; // switches held in the GUI (bit = Nuked MCU button)
	std::atomic<uint32_t> ui_panel_leds{0};    // unit 0's lamps: bit 0 = ALL, bit 1 = MUTE
	uint32_t PanelLeds() const { return ui_panel_leds.load(std::memory_order_relaxed); }
	std::atomic<float> gain_db{0.0f};          // GAIN knob in dB, kGainMinDb..kGainMaxDb; 0 = default
	static constexpr float kGainMinDb = -12.0f, kGainMaxDb = 12.0f;
	static constexpr float kLevelMatchDb = 0.0f; // 0 dB = the level the SC-55 plugins always had
	static float GainFactor(float gain_db);
private:
	float applied_gain = -1.0f;    // < 0: not applied yet
	uint32_t applied_buttons = 0;
	uint64_t panel_active_until = 0; // render frame until which unit 0's edits are passed on
	std::array<uint8_t, 0x700> panel_seen{}; // unit 0's parameter memory (SRAM 0..6FF) as last seen
	bool panel_seen_valid = false;
	uint16_t applied_mute55 = 0xffff; // part MUTE flags applied to the other units (bit = GS block)
	bool all_mute55 = false;          // ALL + MUTE: whole module silent
	void E55PanelInput();
	void E55PanelPump();
	void E55ApplyMutes(Instance& inst, uint16_t rx);
public:
#endif
#ifdef NUKED_SC55_DEVICE_88
private:
	// SC-88: the firmware logs every panel edit as a GS parameter change (a ring in its work
	// RAM); the plugin reads the new entries after each block and passes them on as DT1.
	int edit_cursor  = -1;   // next unread byte of the log (-1: start at the current end)
	uint32_t gui_buttons = 0; // switches held in the GUI as last seen (ALL-mode handling)
	bool all_eq_on   = true; // ALL + EQ: EQ switch of all parts (applied as GS parameters)
	void E88EditPump();
	void E88SendAll(uint8_t port, const uint8_t* msg, size_t size);
public:
#endif
#ifdef NUKED_SC55_DEVICE_8850
	// SC-8850: edits are read back from unit 0 with GS data requests; MUTE and SOLO are taken
	// from unit 0 and applied to all units.
	std::atomic<int> ui_encoder{0};            // VALUE encoder detents not yet passed on
	bool GetLcdDots(uint8_t* dots, size_t size, bool& on); // unit 0, 160 x 64, one byte per dot
	static constexpr int kLcdW = 160, kLcdH = 64;
private:
	// GS dot display (45 10 01 00..10 05 7F, pages 1..10; page select 45 10 20 00): the SC-8850
	// firmware shows the text messages but ignores the 16 x 16 bitmaps, so the plugin keeps the
	// pages itself and lays the shown one over the level meters of the play screen.
	static constexpr double kDotShowSeconds = 3.2;
	std::array<std::array<uint16_t, 16>, 10> dot_pages{}; // per page 16 rows, bit 15 = left
	int dot_page        = 0;                 // page shown (0..9)
	uint64_t dot_until  = 0;                 // render frame at which the bitmap disappears
	std::array<std::atomic<uint16_t>, 16> ui_dots{}; // bitmap for the GUI
	std::atomic<bool> ui_dots_on{false};
	void DotSysEx(std::span<const uint8_t> msg);
	void DotShow(bool on);
	struct SyncReq {
		uint8_t port, a0, a1, a2, size;
		uint8_t tries = 0;
		uint64_t sent_frame = 0;
	};
	std::vector<SyncReq> sync_queue;       // requests still to send (the last one next)
	std::vector<SyncReq> sync_outstanding; // sent, answer pending
	std::vector<int16_t> sync_base;        // last known value per (port, address); -1 = unknown
	std::vector<uint8_t> sync_in;          // unit 0 MIDI output not yet parsed
	std::array<uint64_t, kNumParts + 1> host_change{}; // render frame of the host's last state change (part / [kNumParts] = SysEx)
	bool sync_dirty = false;               // panel input since the last pass started
	uint64_t panel_frame = 0;              // render frame of the last panel input
	void E88SyncStart(bool boot);
	void E88SyncPump(Instance& inst, bool forward, uint64_t now);
	void E88SyncAnswer(const SyncReq& req, const uint8_t* data, int n, bool forward);
	void E88SyncBoot(Instance& inst);
public:
#endif
	static constexpr bool HasToneMap()
	{
#if defined(NUKED_SC55_DEVICE_8850) || defined(NUKED_SC55_DEVICE_88)
		return false; // INST MAP on the panel, per part (synchronised like other panel edits)
#elif defined(NUKED_SC55_ENGINE_88PRO)
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
