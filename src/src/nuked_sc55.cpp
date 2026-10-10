#include <chrono>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#endif

#include "nuked-sc55/common/rom_loader.h"
#include "nuked_sc55.h"
#ifdef _WIN32
#include "gui/editor.h"
#endif
#ifdef NUKED_SC55_EMBED_ROMS
#include "embedded_roms.h"
#endif

#ifndef NUKED_SC55_POLY_TARGET_VOICES
#define NUKED_SC55_POLY_TARGET_VOICES 128
#endif

static std::string get_env_var(const char* var_name);

// #define DEBUG

//----------------------------------------------------------------------------
// Simple debug logging
#ifdef DEBUG

#include <cstdarg>

static FILE* logfile = nullptr;

static void log_init()
{
	if (logfile) {
		return;
	}

#ifdef _WIN32
	if (fopen_s(&logfile, "D:\\nuked-sc55-clap.log", "wb") != 0) {
		logfile = nullptr;
	}
#else
	logfile = fopen("/Users/jnovak/nuked-sc55-clap.log", "wb");
#endif
}

static void _log(const char* fmt, ...)
{
	if (!logfile) {
		return;
	}

	va_list args;
	va_start(args, fmt);

	vfprintf(logfile, fmt, args);
	fprintf(logfile, "\n");
	fflush(logfile);

	va_end(args);
}

static void log_shutdown()
{
	if (logfile) {
		fclose(logfile);
		logfile = nullptr;
	}
}

#define log(...) _log(__VA_ARGS__)
#else

static void log_init() {}
static void log_shutdown() {}

#define log(...)
#endif

//----------------------------------------------------------------------------

// Get the environment variable value from the provided name,
// if the variable exists. Returns an empty string if the
// variable does not exist, or is empty
static std::string get_env_var(const char* var_name)
{
	std::string env_var = {};
#ifdef _WIN32
	auto size = GetEnvironmentVariableA(var_name, nullptr, 0);
	if (size > 0) {
		// Note, 'size' includes the null terminator
		env_var.resize(size - 1);
		GetEnvironmentVariableA(var_name, env_var.data(), size);
	}
#else
	const char* env_var_c_str = getenv(var_name);
	if (env_var_c_str) {
		env_var = env_var_c_str;
	}
#endif
	return env_var;
}

#ifdef _WIN32
constexpr auto PathSeparator = std::string_view(";");
#else
constexpr auto PathSeparator = std::string_view(":");
#endif

extern std::string plugin_path;

NukedSc55::NukedSc55(const clap_plugin_t _plugin_class,
                     const clap_host_t* _host, const Model _model)
{
	log_init();

	path = plugin_path;
	log("Plugin path: %s", path.string().c_str());

	plugin_class = _plugin_class;

	plugin_class.plugin_data = this;

	host  = _host;
	model = _model;
}

const clap_plugin_t* NukedSc55::GetPluginClass()
{
	return &plugin_class;
}

// Get a list of potential ROM directories from an environment variable
// if it is set to a non-empty value. The paths must be absolute directory
// paths. Entries must be separated by the OS PATH separator
std::vector<std::filesystem::path> NukedSc55::GetRomEnvDirs()
{
	constexpr char env_rom_dir_name[]        = "SOUNDCANVAS_ROM_PATH";
	std::vector<std::filesystem::path> paths = {};
	const auto env_dir_list = get_env_var(env_rom_dir_name);
	if (env_dir_list.empty()) {
		return paths;
	}
	// NOTE: this used to use std::views::split(env_dir_list,
	// PathSeparator), but that relies on GCC's ranges implementation of
	// split_view, which differs meaningfully between GCC 11 and GCC 13: on
	// GCC 11 the inner range's begin()/end() are different types (iterator
	// vs. default_sentinel_t), which std::string's iterator-pair
	// constructor can't deduce against, while GCC 13 supports it. To avoid
	// depending on that version-sensitive behaviour at all, this now does
	// plain find()/substr() tokenizing instead.
	constexpr char separator = PathSeparator[0];

	size_t pos = 0;
	while (pos <= env_dir_list.size()) {
		const size_t next = env_dir_list.find(separator, pos);
		const size_t len = (next == std::string::npos) ? std::string::npos
		                                               : next - pos;
		const std::string token = env_dir_list.substr(pos, len);

		if (!token.empty()) {
			auto dir = std::filesystem::path(token);
			if (dir.is_relative()) {
				log("Error: path is relative: %s",
				    dir.string().c_str());
			} else {
				std::error_code ec;
				if (std::filesystem::is_directory(dir, ec)) {
					paths.push_back(dir);
				} else if (ec) {
					log("Error getting directory status: %s",
					    ec.message().c_str());
				}
			}
		}

		if (next == std::string::npos) {
			break;
		}
		pos = next + 1;
	}

	return paths;
}

std::vector<std::filesystem::path> NukedSc55::GetRomBasePaths()
{
	auto paths = GetRomEnvDirs();

	const char* default_rom_dir = "ROMs";

	// Try the Resources folder inside the application bundle first on macOS
#ifdef __APPLE__
	paths.push_back(path / "Resources" / default_rom_dir);
#endif

	const char* resources_dir = "Nuked-SC55-Resources";
	paths.push_back(path.parent_path() / resources_dir / default_rom_dir);

	return paths;
}

bool NukedSc55::Init(const clap_plugin* _plugin_instance)
{
	log("Init");

	plugin_instance = _plugin_instance;

	// Polyphony target: compile-time default, overridable at runtime via
	// the NUKED_SC55_POLY_VOICES environment variable (24..1536).
	target_voices = NUKED_SC55_POLY_TARGET_VOICES;
	if (const auto env = get_env_var("NUKED_SC55_POLY_VOICES"); !env.empty()) {
		target_voices = std::atoi(env.c_str());
	}
#if defined(NUKED_SC55_DEVICE_8850)
	partials_per_instance = 128; // SC-8850: 128 voices per unit (two XP tone generators)
#elif defined(NUKED_SC55_ENGINE_88PRO)
	partials_per_instance = 64; // SC-88 Pro: 64 voices per unit
#else
	partials_per_instance = (model == Model::Sc55mk2_v1_01) ? 28 : 24;
#endif
	target_voices = std::clamp(target_voices, partials_per_instance,
	                           partials_per_instance * PolyRouter::MaxInstances);
	const int num_instances = (target_voices + partials_per_instance - 1) /
	                          partials_per_instance;
	log("Poly: %d voices requested -> %d instances (%d partials)",
	    target_voices, num_instances, num_instances * partials_per_instance);

	if (const auto env = get_env_var("NUKED_SC55_POLY_DYNAMIC"); env == "0") {
		dynamic_instances = false;
	}
	if (const auto env = get_env_var("NUKED_SC55_POLY_MIN_ACTIVE"); !env.empty()) {
		min_active = std::atoi(env.c_str());
	}
	min_active = std::clamp(min_active, 1, num_instances);

	instances.clear();
	instances.resize(num_instances);
	awake_list.reserve(num_instances);

#ifdef NUKED_SC55_ENGINE_88PRO
	// SC-88 Pro / SC-8850 through 88emu: units are created and booted in Activate().
	router.Init(num_instances);
	router.SetCapacity(partials_per_instance);
	{
#ifdef NUKED_SC55_EMBED_ROMS
		// Single-file build: the normalized ROM set is linked into the binary
		// (locations 0..3 = control, wave A, wave B, wave C; SC-8850: internal, program,
		// data, wave; SC-88: control and the four wave chips as dumped); no file access.
		const uint8_t* rp[5] = {};
		size_t rn[5]         = {};
		for (int k = 0; k < g_embedded_rom_count; ++k) {
			const auto& r = g_embedded_roms[k];
			if (r.location < 5) {
				rp[r.location] = r.begin;
				rn[r.location] = static_cast<size_t>(r.end - r.begin);
			}
		}
#if defined(NUKED_SC55_DEVICE_8850)
		if (emu88_set_sc8850_rom_images(rp[0], rn[0], rp[1], rn[1], rp[2], rn[2], rp[3], rn[3]) !=
		    EMU88_RC_OK) {
#elif defined(NUKED_SC55_DEVICE_88)
		if (emu88_set_sc88_rom_images(rp[0], rn[0], rp[1], rn[1], rp[2], rn[2], rp[3], rn[3], rp[4], rn[4]) !=
		    EMU88_RC_OK) {
#else
		if (emu88_set_sc88pro_rom_images(rp[0], rn[0], rp[1], rn[1], rp[2], rn[2], rp[3], rn[3]) !=
		    EMU88_RC_OK) {
#endif
			log("88emu: embedded ROM set invalid");
			instances.clear();
			return false;
		}
#else
		for (const auto& base : GetRomBasePaths()) {
			emu88_add_rom_path((base / "SC-88Pro").string().c_str());
			emu88_add_rom_path((base / "SC-8850").string().c_str());
			emu88_add_rom_path((base / "SC-88").string().c_str());
			emu88_add_rom_path(base.string().c_str());
		}
#endif
	}
	emu_ok     = true;
	max_voices = MaxSelectableVoices();
	{
		const int hw = static_cast<int>(std::thread::hardware_concurrency());
		const int threads = std::clamp(std::min(num_instances, hw - 1) - 1, 0, num_instances - 1);
		pool.Start(threads);
	}
	return true;
#endif

	// The emulator only keeps its LCD memory (text, bitmap and level-meter
	// glyphs) up to date while a backend is attached; the panel renders that
	// memory itself, so attach one that draws nothing.
	struct PanelLcdBackend final : LCD_Backend {
		bool Start(const lcd_t&) override { return true; }
		void Stop() override {}
		void Render() override {}
	};
	static PanelLcdBackend panel_lcd_backend;
	clone_units = get_env_var("NUKED_SC55_NO_CLONE") != "1";
	const EMU_Options opts = {.lcd_backend    = &panel_lcd_backend,
	                          .nvram_filename = std::filesystem::path{}};
	for (auto& inst : instances) {
		inst.emu = std::make_unique<Emulator>();
		if (!inst.emu->Init(opts)) {
			log("emu->Init failed");
			instances.clear();
			return false;
		}
	}
	router.Init(num_instances);
	router.SetCapacity(partials_per_instance);

#ifdef NUKED_SC55_EMBED_ROMS
	// Single-file build: the ROM images are linked into the binary
	// (see embedded_roms.h); no external ROM directory is needed.
	{
		RomsetInfo info = {};
		for (int k = 0; k < g_embedded_rom_count; ++k) {
			const auto& r = g_embedded_roms[k];
			info.rom_data[r.location].assign(r.begin, r.end);
		}
		// Same post-processing as the file loader (unscrambles waveroms)
		if (!LoadRomset(info, nullptr)) {
			log("Embedded ROM post-processing failed");
			instances.clear();
			return false;
		}
		const auto embedded_romset = static_cast<Romset>(g_embedded_romset);
		for (auto& inst : instances) {
			// With cloned units only unit 0 needs the ROMs; the others receive
			// them together with its state in Activate().
			if (clone_units && &inst != &instances[0]) continue;
			RomLocationSet loaded = {};
			if (!inst.emu->LoadRoms(embedded_romset, info, &loaded)) {
				log("`emu->LoadRoms()` failed (embedded ROMs)");
				instances.clear();
				return false;
			}
		}
		log("Loaded embedded ROMs: %s", g_embedded_romset_name);
		// All units run the same ROM set: one shared copy of the wave ROMs.
		for (size_t k = 1; k < instances.size(); ++k) {
			instances[k].emu->ShareWaveRomsFrom(*instances[0].emu);
		}
		emu_ok = true;
		max_voices = MaxSelectableVoices();

		const int hw = static_cast<int>(std::thread::hardware_concurrency());
		const int threads = std::clamp(std::min(num_instances, hw - 1) - 1,
		                               0, num_instances - 1);
		pool.Start(threads);
		return true;
	}
#endif

	auto rom_paths = GetRomBasePaths();
	for (const auto& base_path : rom_paths) {
		auto rom_path      = base_path;
		const char* romset = "";

		switch (model) {
		case Model::Sc55_v1_00:
			romset = "mk1-v1.00";
			rom_path /= "SC-55-v1.00";
			break;
		case Model::Sc55_v1_10:
			romset = "mk1-v1.10";
			rom_path /= "SC-55-v1.10";
			break;
		case Model::Sc55_v1_20:
			romset = "mk1-v1.20";
			rom_path /= "SC-55-v1.20";
			break;
		case Model::Sc55_v1_21:
			romset = "mk1-v1.21";
			rom_path /= "SC-55-v1.21";
			break;
		case Model::Sc55_v2_00:
			romset = "mk1-v2.00";
			rom_path /= "SC-55-v2.00";
			break;
		case Model::Sc55mk2_v1_01:
			romset = "mk2-v1.01";
			rom_path /= "SC-55mk2-v1.01";
			break;
		default: assert(false);
		}

		log("Trying ROM dir: %s", rom_path.string().c_str());

		// SC-55mk2: prefer a Capital-Tone-Fallback patched rom2 (from
		// shingo45endo/sc55mk2-ctf-patcher) if one is present; the plain
		// mk2 firmware stays silent on missing variation tones. A specific
		// romset can be forced with NUKED_SC55_MK2_ROMSET (e.g. "mk2-v1.01"
		// to disable CTF).
		std::vector<std::string> candidates;
		if (model == Model::Sc55mk2_v1_01) {
			if (const auto forced = get_env_var("NUKED_SC55_MK2_ROMSET");
			    !forced.empty()) {
				candidates.push_back(forced);
			} else {
				candidates = {"mk2-ctf-sc55-drum-sc55-v1.21",
				              "mk2-ctf-sc55-drum-sc55-v2.00",
				              "mk2-ctf-strict-sc55-drum-sc55-v1.21",
				              "mk2-ctf-strict-sc55-drum-sc55-v2.00",
				              "mk2-ctf-mk2-drum-sc55-v1.21",
				              "mk2-ctf-mk2-drum-sc55-v2.00",
				              romset};
			}
		} else {
			candidates.push_back(romset);
		}

		std::unique_ptr<common::LoadRomsetResult> result;
		for (const auto& candidate : candidates) {
			auto attempt = std::make_unique<common::LoadRomsetResult>();
			common::RomOverrides rom_overrides;
			const auto err = common::LoadRomset(rom_path,
			                                    candidate,
			                                    common::RomLoader::Hashing,
			                                    rom_overrides,
			                                    *attempt);
			if (err == common::LoadRomsetError{}) {
				log("Loaded romset: %s", candidate.c_str());
				result = std::move(attempt);
				break;
			}
		}
		if (!result) {
			log("`common::LoadRomset()` failed. Trying next directory");
			continue;
		}
		auto& load_result = *result;
		for (auto& inst : instances) {
			if (clone_units && &inst != &instances[0]) continue; // see above
			RomLocationSet loaded = {};
			if (!inst.emu->LoadRoms(load_result.romset,
			                        load_result.romset_info,
			                        &loaded)) {
				log("`emu->LoadRoms()` failed");
				instances.clear();
				return false;
			}
		}
		// All units run the same ROM set: one shared copy of the wave ROMs.
		for (size_t k = 1; k < instances.size(); ++k) {
			instances[k].emu->ShareWaveRomsFrom(*instances[0].emu);
		}
		emu_ok = true;
		max_voices = MaxSelectableVoices();

		// Worker threads: one per instance at most, leave one core free.
		const int hw = static_cast<int>(std::thread::hardware_concurrency());
		const int threads = std::clamp(std::min(num_instances, hw - 1) - 1,
		                               0, num_instances - 1);
		pool.Start(threads);
		return true;
	}
	log("Init failed, tried all ROM directories");
	instances.clear();
	return false;
}

void NukedSc55::Shutdown()
{
#ifdef _WIN32
	if (editor) {
		editor::Destroy(editor);
		editor = nullptr;
	}
#endif
	lcd_ready = false;
	StopWarmer();
#ifdef NUKED_SC55_ENGINE_88PRO
	JoinBootThreads();
#endif
	pool.Stop();
#ifdef NUKED_SC55_ENGINE_88PRO
	for (auto& inst : instances) {
		if (inst.ctx) {
			emu88_free_context(inst.ctx);
			inst.ctx = nullptr;
		}
	}
#endif
	log("Shutdown");

	if (resampler) {
		speex_resampler_destroy(resampler);
		resampler = nullptr;
	}
	log_shutdown();
}

void NukedSc55::ReceiveSample(void* userdata, const AudioFrame<int32_t>& in)
{
	assert(userdata);
	auto inst = reinterpret_cast<Instance*>(userdata);

	AudioFrame<float> out = {};
	Normalize(in, out);

	inst->buf_l.push_back(out.left);
	inst->buf_r.push_back(out.right);
}

bool NukedSc55::Activate(const double requested_sample_rate,
                         [[maybe_unused]] const uint32_t min_frame_count,
                         const uint32_t max_frame_count)
{
	log("Activate: requested_sample_rate: %g, min_frame_count: %d, max_frame_count: %d",
	    requested_sample_rate,
	    min_frame_count,
	    max_frame_count);

	if (!emu_ok) {
		return false;
	}
	StopWarmer(); // no background access while (re)booting
	lcd_ready = false;

	// Speed up the devices' bootup delay
	const size_t num_steps = (model == Model::Sc55mk2_v1_01) ? 9'500'000
	                                                         : 700'000;

#ifdef NUKED_SC55_ENGINE_88PRO
	(void)num_steps;
	// Boot all SC-88 Pro units identically, set the selected tone map on their
	// panels (survives GS resets like on the hardware) and measure the DC offset.
	// Unit 0 boots now; with embedded ROMs the other units (asleep anyway) boot in the
	// background, in parallel. Waking or warming a unit waits until it has booted.
	JoinBootThreads();
	for (auto& inst : instances) inst.booted->store(false, std::memory_order_relaxed);
#ifdef NUKED_SC55_EMBED_ROMS
	const bool background_boot = get_env_var("NUKED_SC55_NO_BGBOOT") != "1";
#else
	const bool background_boot = false; // ROM scan from files is not thread-safe
#endif
	if (background_boot) {
		if (!E88BootUnit(0)) return false;
		instances[0].booted->store(true, std::memory_order_release);
		for (int i = 1; i < NumInstances(); ++i) {
			boot_threads.emplace_back([this, i] {
				E88BootUnit(i);
				instances[i].booted->store(true, std::memory_order_release);
			});
		}
	} else {
		std::atomic<bool> boot_failed{false};
#ifdef NUKED_SC55_EMBED_ROMS
		pool.Run(NumInstances(), [&](int i) { if (!E88BootUnit(i)) boot_failed = true; });
#else
		for (int i = 0; i < NumInstances(); ++i) if (!E88BootUnit(i)) boot_failed = true;
#endif
		if (boot_failed) return false;
		for (auto& inst : instances) inst.booted->store(true, std::memory_order_release);
	}
#else
	// Boot all instances identically (in parallel). Identical boot makes
	// all instances' LFOs, chorus and reverb run in lock-step, so the summed
	// effects behave like the effects of a single unit.
	const auto boot_unit = [&](int i) {
		auto& inst = instances[i];
		inst.emu->SetSampleCallback(MCU_DefaultSampleCallback, nullptr);
		inst.emu->Reset();
		inst.emu->GetPCM().enable_oversampling = false;
		inst.emu->PostSystemReset(EMU_SystemReset::GS_RESET);
		for (size_t s = 0; s < num_steps; s++) {
			MCU_Step(inst.emu->GetMCU());
		}
		inst.buf_l.clear();
		inst.buf_r.clear();
		inst.read_pos = 0;
		inst.queue.clear();
		inst.queue.reserve(1024);
		inst.buf_l.reserve(1 << 16);
		inst.buf_r.reserve(1 << 16);
		inst.emu->SetSampleCallback(NukedSc55::ReceiveSample, &inst);

		// Every emulated unit outputs a constant DC offset (1/32 FS) when
		// idle. Summing N units would multiply it, so measure it once after
		// boot and cancel it on all instances except the first one -- the
		// mix then equals a single unit (incl. its offset) at low polyphony.
		while (inst.buf_l.size() < 2048) {
			MCU_Step(inst.emu->GetMCU());
		}
		double sl = 0.0, sr = 0.0;
		for (size_t f = 1024; f < 2048; ++f) {
			sl += inst.buf_l[f];
			sr += inst.buf_r[f];
		}
		inst.dc_l = (i == 0) ? 0.0f : static_cast<float>(sl / 1024.0);
		inst.dc_r = (i == 0) ? 0.0f : static_cast<float>(sr / 1024.0);
		boot_dc_l = static_cast<float>(sl / 1024.0);
		boot_dc_r = static_cast<float>(sr / 1024.0);
		inst.buf_l.clear();
		inst.buf_r.clear();
	};
	if (!clone_units) {
		pool.Run(NumInstances(), boot_unit); // every unit boots by itself (reference path)
	} else {
		// Boot unit 0 once and give all other units an exact copy of its state: they would
		// reach exactly this state by booting themselves (identical ROMs, identical boot).
		boot_unit(0);
		for (int i = 1; i < NumInstances(); ++i) {
			auto& inst = instances[i];
			inst.emu->CopyStateFrom(*instances[0].emu);
			inst.emu->SetSampleCallback(NukedSc55::ReceiveSample, &inst);
			inst.buf_l.clear();
			inst.buf_r.clear();
			inst.read_pos = 0;
			inst.queue.clear();
			inst.queue.reserve(1024);
			inst.buf_l.reserve(1 << 16);
			inst.buf_r.reserve(1 << 16);
			inst.dc_l = boot_dc_l; // same state -> same idle offset
			inst.dc_r = boot_dc_r;
		}
	}
#endif
	router.Init(NumInstances());
	router.SetCapacity(partials_per_instance);
	router.SetPacking(dynamic_instances);
	router.SetWakeHandler([this]() {
		for (int i = 0; i < router.Allowed(); ++i) {
			if (!instances[i].awake) {
				Wake(i);
				return true;
			}
		}
		return false;
	});
	midi_bytes.clear();
	midi_bytes.reserve(1 << 16);

	// All instances were booted identically. With dynamic management only
	// `min_active` stay awake; the rest sleep (cost no CPU) until needed.
	state_log.Clear();
	wakes = sleeps = 0;
	for (int i = 0; i < NumInstances(); ++i) {
		instances[i].awake         = true;
		instances[i].silent_frames = 0;
		instances[i].warming->store(false);
		if (dynamic_instances && i >= min_active) {
			Sleep(i);
		}
	}
	sleeps = 0;
	render_frame_count = 0;
	warmer_next_frame  = 0;
#ifdef NUKED_SC55_DEVICE_8850
	dot_pages = {};
	dot_page  = 0;
	DotShow(false);
#endif
	if (dynamic_instances) StartWarmer();
	lcd_ready = true;

#ifdef NUKED_SC55_ENGINE_88PRO
	render_sample_rate_hz = emu88_get_device_samplerate(instances[0].ctx);
	last_tone_map         = tone_map.load();
#else
	auto& emu = instances[0].emu;

	render_sample_rate_hz = PCM_GetOutputFrequency(emu->GetPCM());
#endif
	// An instance may sleep after 2 s of silence (covers reverb tails)
	sleep_after_frames = static_cast<uint32_t>(render_sample_rate_hz * 1.0);

	log("render_sample_rate_hz: %g", render_sample_rate_hz);

	if (requested_sample_rate != render_sample_rate_hz) {
		do_resample = true;

		output_sample_rate_hz = requested_sample_rate;

		// Initialise Speex resampler
		resample_ratio = render_sample_rate_hz / output_sample_rate_hz;

		const spx_uint32_t in_rate_hz = static_cast<int>(render_sample_rate_hz);
		const spx_uint32_t out_rate_hz = static_cast<int>(output_sample_rate_hz);

		constexpr auto NumChannels = 2; // always stereo
		constexpr auto ResampleQuality = SPEEX_RESAMPLER_QUALITY_DESKTOP;

		resampler = speex_resampler_init(NumChannels,
		                                 in_rate_hz,
		                                 out_rate_hz,
		                                 ResampleQuality,
		                                 nullptr);

		speex_resampler_set_rate(resampler, in_rate_hz, out_rate_hz);
		speex_resampler_skip_zeros(resampler);

		const auto max_render_buf_size = static_cast<size_t>(
		        static_cast<double>(max_frame_count) * resample_ratio * 1.10f);

		render_buf[0].reserve(max_render_buf_size);
		render_buf[1].reserve(max_render_buf_size);

	} else {
		do_resample = false;

		output_sample_rate_hz = render_sample_rate_hz;
		resample_ratio        = 1.0;

		render_buf[0].reserve(max_frame_count);
		render_buf[1].reserve(max_frame_count);
	}

	log("do_resample: %s", do_resample ? "true" : "false");
	log("output_sample_rate_hz: %g", output_sample_rate_hz);
	log("resample_ratio: %g", resample_ratio);

	return true;
}

clap_process_status NukedSc55::Process(const clap_process_t* process)
{
	if (!emu_ok || instances.empty()) {
		return CLAP_PROCESS_ERROR;
	}

	assert(process->audio_outputs_count == 1);
	assert(process->audio_inputs_count == 0);

	const uint32_t num_frames = process->frames_count;
	const uint32_t num_events = process->in_events->size(process->in_events);
	log("--- num_frames: %d, num_events: %d", num_frames, num_events);

	// Route the whole block first. Event timestamps are converted to
	// render-rate frame offsets; each instance receives its own timed queue
	// and renders the block independently (in parallel).
	const auto num_render_frames = static_cast<uint32_t>(
	        static_cast<double>(num_frames) * resample_ratio);

	MeasureLoad();

	// Setup-menu limit: number of instances that may get new notes
	{
		const int cap     = partials_per_instance;
		const int allowed = (max_voices.load(std::memory_order_relaxed) + cap - 1) / cap;
		router.SetAllowed(allowed);
	}
	HandleUiCommands();
#ifdef NUKED_SC55_DEVICE_8850
	// Front panel of unit 0: GUI input, edits passed on to the other units, MUTE / SOLO
	E88PanelInput();
	E88SyncPump(instances[0], true, render_frame_count);
	E88PanelMutes();
#endif
#ifdef NUKED_SC55_DEVICE_88
	// Front panel of unit 0: edits of the last block passed on, GUI input, ALL + MUTE
	E88EditPump();
	E88PanelInput();
	E88PanelMutes();
#endif
#ifdef NUKED_SC55_ENGINE_88PRO
	{   // PREVIEW: note of the selected part while the button is held
		const int pv = ui_preview.exchange(0);
		if ((pv == 1 || pv == 2) && preview_ch >= 0) {
			InjectShort(static_cast<uint8_t>(0x80 | preview_ch), static_cast<uint8_t>(preview_key), 0,
			            static_cast<uint16_t>(preview_port));
			preview_ch = preview_key = -1;
		}
		if (pv == 1) {
			const int part = std::clamp(ui_preview_part.load(), 0, kNumParts - 1);
			preview_ch   = part % 16;
			preview_port = part / 16;
			preview_key  = std::clamp(preview_note.load(), 0, 127);
			InjectShort(static_cast<uint8_t>(0x90 | preview_ch), static_cast<uint8_t>(preview_key), 100,
			            static_cast<uint16_t>(preview_port));
		}
		// MUTE: newly muted parts fall silent at once (All Sound Off); new notes are dropped
		const uint64_t mutes = mute_mask.load();
		for (int part = 0; part < kNumParts; ++part)
			if ((mutes & ~applied_mutes) & (uint64_t{1} << part))
				InjectShort(static_cast<uint8_t>(0xB0 | (part % 16)), 120, 0, static_cast<uint16_t>(part / 16));
		applied_mutes = mutes;
	}
	{
		const int map = tone_map.load();
		for (auto& inst : instances) {
			if (inst.awake && inst.ctx && inst.seq_len == 0 && inst.applied_map != map)
				E88StartMapSequence(inst, map);
		}
	}
#endif

	if (dynamic_instances) {
		int note_ons = 0;
		for (uint32_t i = 0; i < num_events; ++i) {
			const auto ev = process->in_events->get(process->in_events, i);
			if (ev->space_id == CLAP_CORE_EVENT_SPACE_ID &&
			    ev->type == CLAP_EVENT_MIDI) {
				const auto m = reinterpret_cast<const clap_event_midi_t*>(ev);
				if ((m->data[0] & 0xf0) == 0x90 && m->data[2] != 0) {
					++note_ons;
				}
			}
		}
		ManageInstances(note_ons);
	}
	for (uint32_t i = 0; i < num_events; ++i) {
		const auto event = process->in_events->get(process->in_events, i);
		auto frame = static_cast<uint32_t>(
		        static_cast<double>(event->time) * resample_ratio);
		if (num_render_frames > 0) {
			frame = std::min(frame, num_render_frames - 1);
		}
		ProcessEvent(event, frame);
	}

	RenderAudio(num_render_frames);
	midi_bytes.clear(); // all queued events have been consumed
	render_frame_count += num_render_frames;
	if (dynamic_instances) ScheduleWarming();
#ifdef NUKED_SC55_DEVICE_8850
	if (dot_until != 0 && render_frame_count >= dot_until) DotShow(false);
#endif

	// Publish display data
	{
		int voices = 0;
		for (int i = 0; i < NumInstances(); ++i) {
			if (instances[i].awake) voices += ActivePartials(i);
		}
		ui_voices.store(voices, std::memory_order_relaxed);
		uint64_t mask = 0;
		for (int i = 0; i < NumInstances(); ++i)
			if (instances[i].awake) mask |= uint64_t{1} << i;
		ui_awake_mask.store(mask, std::memory_order_relaxed);
		ui_awake.store(NumAwake(), std::memory_order_relaxed);
		for (int ch = 0; ch < kNumParts; ++ch) {
			ui_parts[ch].rhythm.store(router.IsRhythm(ch), std::memory_order_relaxed);
		}
	}

	auto out_left  = process->audio_outputs[0].data32[0];
	auto out_right = process->audio_outputs[0].data32[1];

	if (do_resample) {
		ResampleAndPublishFrames(num_frames, out_left, out_right);

	} else {
		assert(out_left && out_right);

		assert(render_buf.size() == 2);
		assert(render_buf[0].size() >= num_frames);
		assert(render_buf[1].size() >= num_frames);

		for (size_t i = 0; i < num_frames; ++i) {
			out_left[i]  = render_buf[0][i];
			out_right[i] = render_buf[1][i];
		}

		render_buf[0].clear();
		render_buf[1].clear();
	}

#ifdef NUKED_SC55_ENGINE_88PRO
	{   // GAIN knob: analog-style output level after the emulation, ramped over the block
		const float target = GainFactor(gain_db.load(std::memory_order_relaxed));
		if (applied_gain < 0.0f) applied_gain = target; // first block: no ramp
		const float step = (target - applied_gain) / static_cast<float>(std::max<uint32_t>(1, num_frames));
		for (uint32_t i = 0; i < num_frames; ++i) {
			const float g = (step == 0.0f) ? target : applied_gain + step * static_cast<float>(i + 1);
			out_left[i] *= g;
			out_right[i] *= g;
		}
		applied_gain = target;
	}
#endif

	return CLAP_PROCESS_CONTINUE;
}

bool NukedSc55::LoadState([[maybe_unused]] const clap_istream_t* stream)
{
	if (!emu_ok) {
		return false;
	}

	// Settings only: "NSC55P1 max_voices=<n>"
	char buf[256] = {};
	size_t got = 0;
	while (got < sizeof(buf) - 1) {
		const int64_t r = stream->read(stream, buf + got, sizeof(buf) - 1 - got);
		if (r <= 0) break;
		got += static_cast<size_t>(r);
	}
	int v = 0;
	if (std::sscanf(buf, "NSC55P1 max_voices=%d", &v) == 1) {
		SetMaxVoices(v);
#ifdef NUKED_SC55_ENGINE_88PRO
		if (const char* m = std::strstr(buf, "map="); HasToneMap() && m && m[4] >= '0' && m[4] <= '2') {
			tone_map = m[4] - '0';
		}
		if (const char* m = std::strstr(buf, "gain="); m) {
			gain_db = std::clamp(std::atoi(m + 5) / 10.0f, kGainMinDb, kGainMaxDb);
		} else if (const char* v = std::strstr(buf, "vol="); v) {
			gain_db = GainFromLegacyVolume(std::atoi(v + 4));
		}
		if (const char* m = std::strstr(buf, "pnote="); m) preview_note = std::clamp(std::atoi(m + 6), 0, 127);
#endif
		return true;
	}
	return false;
}

bool NukedSc55::SaveState([[maybe_unused]] const clap_ostream_t* stream)
{
	if (!emu_ok) {
		return 0;
	}

	char buf[64];
#if defined(NUKED_SC55_DEVICE_8850) || defined(NUKED_SC55_DEVICE_88)
	// the panel's own settings (tone maps, preview note, ...) live in the firmware
	const int n = std::snprintf(buf, sizeof(buf), "NSC55P1 max_voices=%d gain=%d", max_voices.load(),
	                            static_cast<int>(std::lround(gain_db.load() * 10.0f)));
#elif defined(NUKED_SC55_ENGINE_88PRO)
	const int n = std::snprintf(buf, sizeof(buf), "NSC55P1 max_voices=%d map=%d gain=%d pnote=%d",
	                            max_voices.load(), tone_map.load(),
	                            static_cast<int>(std::lround(gain_db.load() * 10.0f)), preview_note.load());
#else
	const int n = std::snprintf(buf, sizeof(buf), "NSC55P1 max_voices=%d", max_voices.load());
#endif
	int64_t done = 0;
	while (done < n) {
		const int64_t w = stream->write(stream, buf + done, static_cast<uint64_t>(n - done));
		if (w <= 0) return false;
		done += w;
	}
	return true;
}

void NukedSc55::Flush(const clap_input_events_t* in,
                      [[maybe_unused]] const clap_output_events_t* out)
{
	if (!emu_ok) {
		return;
	}

	log("Flush");

	const uint32_t num_events = in->size(in);

	// Process events sent to our plugin from the host.
	for (uint32_t event_index = 0; event_index < num_events; ++event_index) {
		ProcessEvent(in->get(in, event_index), 0);
	}
}

void NukedSc55::PublishFrame(const float left, const float right)
{
	render_buf[0].emplace_back(left);
	render_buf[1].emplace_back(right);
}

constexpr uint8_t NoteOff         = 0x80;
constexpr uint8_t NoteOn          = 0x90;
constexpr uint8_t PolyKeyPressure = 0xa0;
constexpr uint8_t ControlChange   = 0xb0;
constexpr uint8_t ProgramChange   = 0xc0;
constexpr uint8_t ChannelPressure = 0xd0;
constexpr uint8_t PitchBend       = 0xe0;

[[maybe_unused]] static const char* status_to_string(const uint8_t status)
{
	switch (status) {
	case NoteOff: return "NoteOff"; break;
	case NoteOn: return "NoteOn"; break;
	case PolyKeyPressure: return "PolyKeyPressure"; break;
	case ControlChange: return "ControlChange"; break;
	case ProgramChange: return "ProgramChange"; break;
	case ChannelPressure: return "ChannelPressure"; break;
	case PitchBend: return "PitchBend"; break;
	default: return "unknown";
	}
}

[[maybe_unused]] static void log_midi_message(const clap_event_midi_t* event)
{
	const uint8_t status = static_cast<uint8_t>(event->data[0] & 0xf0U);

	// 3-byte messages
	switch (status) {
	case NoteOff:
	case NoteOn:
	case PolyKeyPressure:
	case ControlChange:
	case PitchBend:
		log("MIDI event: %02x %02x %02x | Ch %d, %s",
		    event->data[0],
		    event->data[1],
		    event->data[2],
		    0, // channel,
		    status_to_string(status));
		break;

	default:
		log("MIDI event: %02x %02x    | Ch %d, %s",
		    event->data[0],
		    event->data[1],
		    0, // channel,
		    status_to_string(status));
	}
}

void NukedSc55::QueueMidi(const PolyRouter::Mask mask, const uint8_t* data,
                          const size_t size, const uint32_t render_frame, const uint8_t port)
{
	const auto offset = static_cast<uint32_t>(midi_bytes.size());
	midi_bytes.insert(midi_bytes.end(), data, data + size);

	for (int i = 0; i < NumInstances(); ++i) {
		if ((mask & (PolyRouter::Mask{1} << i)) && instances[i].awake) {
			instances[i].queue.push_back(
			        {render_frame, offset, static_cast<uint32_t>(size), port});
		}
	}
}

void NukedSc55::ProcessEvent(const clap_event_header_t* event,
                             const uint32_t render_frame)
{
	if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {

		switch (event->type) {
		case CLAP_EVENT_MIDI: {
			const auto midi_event =
			        reinterpret_cast<const clap_event_midi_t*>(event);

			const auto status = midi_event->data[0] & 0xf0;
			size_t len        = 2;
			switch (status) {
			case NoteOff:
			case NoteOn:
			case PolyKeyPressure:
			case ControlChange:
			case PitchBend: len = 3; break;
			}
			if (midi_event->data[0] >= 0xf0) {
				len = 1; // realtime/system: forward status only
			}

			uint8_t port = 0;
#ifdef NUKED_SC55_ENGINE_88PRO
			if (midi_event->data[0] == 0xf5) { // port select: F5 01 = IN A, F5 02 = IN B (.. F5 04 = IN D)
				if (midi_event->data[1] >= 1 && midi_event->data[1] <= kNumPorts)
					selected_port = static_cast<uint8_t>(midi_event->data[1] - 1);
				break; // not passed on to the device
			}
			// further CLAP note ports = IN B (C, D); the first one: the port chosen by F5
			// (panel actions name their port directly)
			if (injecting) port = static_cast<uint8_t>(midi_event->port_index % kNumPorts);
			else if (midi_event->port_index >= 1 && midi_event->port_index < kNumPorts)
				port = static_cast<uint8_t>(midi_event->port_index);
			else port = selected_port;
			if (!injecting && status == NoteOn && midi_event->data[2] != 0 &&
			    (applied_mutes & (uint64_t{1} << (port * 16 + (midi_event->data[0] & 0x0f))))) {
				break; // MUTE: part silenced
			}
#endif
#ifdef NUKED_SC55_DEVICE_8850
			if (!injecting && (status == ControlChange || status == ProgramChange))
				host_change[port * 16 + (midi_event->data[0] & 0x0f)] = render_frame_count + render_frame + 1;
#endif
			state_log.AddShort(midi_event->data, port);
			UiTrackShort(midi_event->data, port);
			const auto mask = router.RouteShort(midi_event->data, port);
			QueueMidi(mask, midi_event->data, len, render_frame, port);
#ifdef DEBUG
			log_midi_message(midi_event);
#endif
		} break;

		case CLAP_EVENT_MIDI_SYSEX: {
			const auto sysex_event =
			        reinterpret_cast<const clap_event_midi_sysex*>(event);

			const std::span msg{sysex_event->buffer, sysex_event->size};
			uint8_t port = 0;
#ifdef NUKED_SC55_ENGINE_88PRO
			if (sysex_event->port_index >= 1 && sysex_event->port_index < kNumPorts)
				port = static_cast<uint8_t>(sysex_event->port_index);
			else port = selected_port;
#endif
#ifdef NUKED_SC55_DEVICE_8850
			host_change[kNumParts] = render_frame_count + render_frame + 1;
#endif
			state_log.AddSysEx(msg, port);
			{
				const bool gm_on = msg.size() >= 6 && msg[1] == 0x7e && msg[3] == 0x09;
				const bool gs_reset = msg.size() >= 10 && msg[1] == 0x41 && msg[4] == 0x12 &&
				                      msg[5] == 0x40 && msg[6] == 0x00 && msg[7] == 0x7f;
				if (gm_on || gs_reset) UiResetParts();
#ifdef NUKED_SC55_DEVICE_8850
				if (gm_on || gs_reset) DotShow(false);
				DotSysEx(msg);
#endif
				// GS part parameter "Pitch Key Shift" (40 1x 16), for the panel display;
				// SC-88 Pro: 50 1x 16 or 40 1x 16 received on IN B = B part (40 1x on IN C/D: C/D)
				if (msg.size() >= 10 && msg[1] == 0x41 && msg[3] == 0x42 && msg[4] == 0x12 &&
				    (msg[5] == 0x40 || (kNumPorts > 1 && msg[5] == 0x50)) &&
				    (msg[6] & 0xf0) == 0x10 && msg[7] == 0x16) {
					const int x    = msg[6] & 0x0f;
					const int part = ((msg[5] == 0x50) ? 16 : port * 16) +
					                 ((x == 0) ? 9 : (x <= 9 ? x - 1 : x));
					ui_parts[part].key_shift = msg[8];
				}
			}
			const auto mask = router.RouteSysEx(msg, port);
			QueueMidi(mask, msg.data(), msg.size(), render_frame, port);

			log("SysEx message, length: %d", sysex_event->size);
		} break;
		}
	}
}

#ifdef NUKED_SC55_ENGINE_88PRO
int NukedSc55::UnitToneMap(const int i) const
{
	// tracked tone map of the unit (the panel LEDs do not show it reliably)
	return instances[i].ctx ? instances[i].applied_map : -1;
}
#endif

bool NukedSc55::GetLcd(const int unit, LcdSnapshot& out)
{
	if (!lcd_ready.load() || unit < 0 || unit >= NumInstances()) return false;
#ifdef NUKED_SC55_ENGINE_88PRO
	int on = 0;
	if (!instances[unit].ctx ||
	    !emu88_get_display_memory(instances[unit].ctx, 0, out.dd, out.cg, &on))
		return false;
	out.on = on != 0;
	return true;
#else
	lcd_t& lcd = instances[unit].emu->GetLCD();
	std::lock_guard lk(lcd.mutex); // the MCU writes the LCD under this mutex
	std::memcpy(out.dd, lcd.LCD_Data, sizeof(out.dd));
	std::memcpy(out.cg, lcd.LCD_CG, sizeof(out.cg));
	out.on = lcd.enable.load();
	return true;
#endif
}

NukedSc55::InstanceDiag NukedSc55::Diag(const int i) const
{
	const auto& in = instances[i];
	return {in.awake, router.MeasuredLoad(i), in.silent_frames, router.OwnsNotes(i),
	        in.last_peak, in.last_wake_ms, in.last_wake_bytes};
}

int NukedSc55::ActivePartials(const int i) const
{
#ifdef NUKED_SC55_ENGINE_88PRO
	// Voices whose effective amplitude (amp envelope x TVA gain) is non-zero
	const auto ctx = instances[i].ctx;
	return ctx ? std::max(0, emu88_get_active_voice_count(ctx)) : 0;
#endif
	// A partial counts as sounding while its TVA envelope level
	// (PCM RAM2 word 10) is non-zero. The firmware keeps all voice slots
	// enabled permanently, so the voice-enable mask can't be used.
	const auto& pcm = instances[i].emu->GetPCM();
	const uint32_t enabled = pcm.voice_mask & pcm.voice_mask_pending;
	int n = 0;
	for (int s = 0; s < 28; ++s) {
		if (((enabled >> s) & 1) && pcm.ram2[s][10] != 0) {
			++n;
		}
	}
	return n;
}

void NukedSc55::MeasureLoad()
{
	for (int i = 0; i < NumInstances(); ++i) {
		router.SetMeasuredLoad(i, instances[i].awake ? ActivePartials(i) : 0);
	}
}

void NukedSc55::RenderInstance(Instance& inst, const uint32_t num_frames)
{
	if (!inst.awake) {
		return;
	}
	const auto available = [&] {
		return static_cast<uint32_t>(inst.buf_l.size() - inst.read_pos);
	};
#ifdef NUKED_SC55_ENGINE_88PRO
	for (const auto& ev : inst.queue) {
		if (available() < ev.frame) E88RenderFrames(inst, ev.frame - available());
		emu88_parse_stream_on_port(inst.ctx, ev.port, midi_bytes.data() + ev.offset, ev.size);
	}
	inst.queue.clear();
	if (available() < num_frames) E88RenderFrames(inst, num_frames - available());
	return;
#endif
	auto& mcu = inst.emu->GetMCU();

	for (const auto& ev : inst.queue) {
		while (available() < ev.frame) {
			MCU_Step(mcu);
		}
		inst.emu->PostMIDI(std::span{midi_bytes.data() + ev.offset, ev.size});
	}
	inst.queue.clear();

	while (available() < num_frames) {
		MCU_Step(mcu);
	}
}

void NukedSc55::RenderAudio(const uint32_t num_frames)
{
	log("RenderAudio: num_frames: %d", num_frames);

	// Only awake instances; a single one is rendered inline (no thread
	// hand-off), which keeps low-polyphony CPU use at the original level.
	awake_list.clear();
	for (int i = 0; i < NumInstances(); ++i) {
		if (instances[i].awake) awake_list.push_back(i);
	}
	const int n_awake = static_cast<int>(awake_list.size());
	if (n_awake == 1) {
		RenderInstance(instances[awake_list[0]], num_frames);
	} else {
		pool.Run(n_awake, [&](int k) {
			RenderInstance(instances[awake_list[k]], num_frames);
		});
	}

	// Silence detection (for putting idle instances to sleep)
	for (auto& inst : instances) {
		if (!inst.awake) continue;
		float peak = 0.0f;
		for (uint32_t f = 0; f < num_frames; ++f) {
			peak = std::max(peak, std::abs(inst.buf_l[inst.read_pos + f] - inst.dc_l));
			peak = std::max(peak, std::abs(inst.buf_r[inst.read_pos + f] - inst.dc_r));
		}
		inst.last_peak     = peak;
		// "Silent": below two output LSBs of the SC-55 (~-72 dBFS); the mk1
		// output keeps toggling by one LSB even without any sounding voice.
		inst.silent_frames = (peak < 2.5e-4f) ? inst.silent_frames + num_frames : 0;
	}

	// Mix: sum all awake instances (each a complete SC-55 incl. effects)
	for (uint32_t f = 0; f < num_frames; ++f) {
		float l = 0.0f, r = 0.0f;
		for (auto& inst : instances) {
			if (!inst.awake) continue;
			l += inst.buf_l[inst.read_pos + f] - inst.dc_l;
			r += inst.buf_r[inst.read_pos + f] - inst.dc_r;
		}
		PublishFrame(l, r);
	}

	for (auto& inst : instances) {
		if (!inst.awake) continue;
		inst.read_pos += num_frames;
		// Keep only the few overshoot frames of the last MCU step
		if (inst.read_pos > 4096) {
			inst.buf_l.erase(inst.buf_l.begin(),
			                 inst.buf_l.begin() + inst.read_pos);
			inst.buf_r.erase(inst.buf_r.begin(),
			                 inst.buf_r.begin() + inst.read_pos);
			inst.read_pos = 0;
		}
	}
}

//----------------------------------------------------------------------------
// UI data, commands and settings
//----------------------------------------------------------------------------
void NukedSc55::UiResetParts()
{
	for (auto& p : ui_parts) {
		p.prog = 0; p.bank = 0; p.vol = 100; p.pan = 64;
		p.rev = 40; p.cho = 0; p.expr = 127; p.key_shift = 0x40;
	}
}

void NukedSc55::UiTrackShort(const uint8_t* d, const int port)
{
	const int ch = d[0] & 0x0f;
	auto& p      = ui_parts[(port % kNumPorts) * 16 + ch];
	switch (d[0] & 0xf0) {
	case 0x90:
		if (d[2] != 0) {
			// SC-55 style level bar: velocity scaled by volume and expression
			const int lvl = d[2] * p.vol.load() * p.expr.load() / (127 * 127);
			uint8_t cur   = p.hit.load(std::memory_order_relaxed);
			while (lvl > cur && !p.hit.compare_exchange_weak(cur, uint8_t(lvl))) {}
		}
		break;
	case 0xb0:
		switch (d[1]) {
		case 0: p.bank = d[2]; break;
		case 7: p.vol = d[2]; break;
		case 10: p.pan = d[2]; break;
		case 11: p.expr = d[2]; break;
		case 91: p.rev = d[2]; break;
		case 93: p.cho = d[2]; break;
		case 121: p.expr = 127; break;
		}
		break;
	case 0xc0: p.prog = d[1] & 0x7f; break;
	}
}

void NukedSc55::HandleUiCommands()
{
	const int cmd = ui_command.exchange(0);
	// Release a panel button pressed in an earlier block (SC-55 units)
	if (panel_button_blocks > 0 && --panel_button_blocks == 0 && !instances.empty()) {
#ifndef NUKED_SC55_ENGINE_88PRO
		instances[0].emu->GetMCU().button_pressed = 0;
#endif
		panel_button_bits = 0;
	}
	if ((cmd == 3 || cmd == 4) && kNumPorts <= 2) { // front-panel PART < / PART > on unit 0 (SC-8850: own panel)
		const uint32_t bit = (cmd == 3) ? (1u << 22) : (1u << 14);
#ifdef NUKED_SC55_ENGINE_88PRO
		auto& inst = instances[0];
		if (inst.ctx && inst.seq_len == 0) {
			inst.seq_mask[0] = bit;
			inst.seq_len     = 2; // press + release, keeps the tone map
			inst.seq_pos     = 0;
			inst.seq_map     = inst.applied_map;
			inst.seq_left    = static_cast<uint32_t>(emu88_get_device_samplerate(inst.ctx) * 0.15);
			emu88_set_panel_buttons(inst.ctx, bit);
		}
#else
		instances[0].emu->GetMCU().button_pressed = bit;
		panel_button_bits   = bit;
		panel_button_blocks = 12; // ~0.1 s at typical block sizes
#endif
		return;
	}
	if (cmd == 1) { // all sound off + all notes off + reset controllers
		for (uint8_t port = 0; port < kNumPorts; ++port) {
			for (uint8_t ch = 0; ch < 16; ++ch) {
				for (uint8_t cc : {120, 123}) {
					const uint8_t m[3] = {uint8_t(0xb0 | ch), cc, 0};
					state_log.AddShort(m, port);
					QueueMidi(router.RouteShort(m, port), m, 3, 0, port);
				}
			}
		}
	} else if (cmd == 2) { // GS reset
		static const uint8_t gs[] = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40,
		                             0x00, 0x7f, 0x00, 0x41, 0xf7};
		const std::span msg{gs, sizeof(gs)};
		state_log.AddSysEx(msg);
		QueueMidi(router.RouteSysEx(msg), gs, sizeof(gs), 0);
		UiResetParts();
	}
}

const char* NukedSc55::ModelName() const
{
	switch (model) {
	case Model::Sc55_v1_00: return "SC-55 v1.00";
	case Model::Sc55_v1_10: return "SC-55 v1.10";
	case Model::Sc55_v1_20: return "SC-55 v1.20";
	case Model::Sc55_v1_21: return "SC-55 v1.21";
	case Model::Sc55_v2_00: return "SC-55 v2.00";
#ifdef NUKED_SC55_ENGINE_88PRO
	case Model::Sc88Pro: return "SC-88 Pro";
	case Model::Sc8850: return "SC-8850";
	case Model::Sc88: return "SC-88";
#endif
	default: return "SC-55mk2 v1.01";
	}
}

// The limit lives only in the plugin state (CLAP state / VST2 chunk), which
// the host stores with its project; the plugin itself writes no files.
void NukedSc55::SetMaxVoices(const int v)
{
	max_voices = std::clamp(v, 1, std::max(1, MaxSelectableVoices()));
}

void NukedSc55::NotifyStateChanged()
{
	if (state_changed_callback) {
		state_changed_callback(); // VST2 bridge
		return;
	}
	if (host && host->get_extension) {
		const auto st = static_cast<const clap_host_state_t*>(
		        host->get_extension(host, CLAP_EXT_STATE));
		if (st && st->mark_dirty) st->mark_dirty(host);
	}
}

#ifdef NUKED_SC55_ENGINE_88PRO
// Audio taper of the VOLUME knob: fully left = off, then -60 dB .. 0 dB (right).
float NukedSc55::GainFactor(const float gain_db)
{
	return std::pow(10.0f, (std::clamp(gain_db, kGainMinDb, kGainMaxDb) + kLevelMatchDb) / 20.0f);
}

float NukedSc55::GainFromLegacyVolume(const int vol)
{
	// The old VOLUME knob: 0 = off, else -60 dB .. 0 dB without level matching
	if (vol <= 0) return kGainMinDb;
	const float old_db = (std::min(vol, 1000) / 1000.0f - 1.0f) * 60.0f;
	return std::clamp(old_db - kLevelMatchDb, kGainMinDb, kGainMaxDb);
}

// Boot one SC-88 Pro unit: create the device, set the selected tone map on its panel,
// measure its idle DC offset. Called for unit 0 in Activate(), for the others possibly
// from background threads (they only touch their own Instance).
bool NukedSc55::E88BootUnit(const int i)
{
		auto& inst = instances[i];
		if (inst.ctx) {
			emu88_free_context(inst.ctx);
			inst.ctx = nullptr;
		}
		inst.ctx = emu88_create_context();
#if defined(NUKED_SC55_DEVICE_8850)
		emu88_select_device(inst.ctx, EMU88_DEVICE_SC8850);
#elif defined(NUKED_SC55_DEVICE_88)
		emu88_select_device(inst.ctx, EMU88_DEVICE_SC88);
#else
		emu88_select_device(inst.ctx, EMU88_DEVICE_SC88PRO);
#endif
		emu88_set_stereo_output_samplerate(inst.ctx, 0); // device rate (32 kHz)
		if (emu88_open_synth(inst.ctx) != EMU88_RC_OK) {
			log("%s: open_synth failed (ROMs?)", ModelName());
			emu88_free_context(inst.ctx);
			inst.ctx = nullptr;
			return false;
		}
		inst.buf_l.clear();
		inst.buf_r.clear();
		inst.read_pos = 0;
		inst.queue.clear();
		inst.queue.reserve(1024);
		inst.buf_l.reserve(1 << 16);
		inst.buf_r.reserve(1 << 16);
		inst.seq_len     = 0;
		inst.applied_map = 2; // boots with the SC-88 Pro map
		E88ApplyMapSync(inst, tone_map.load());
#ifdef NUKED_SC55_DEVICE_8850
		if (i == 0) E88SyncBoot(inst); // what unit 0's panel can change, as booted
#endif
#ifdef NUKED_SC55_DEVICE_88
		if (i == 0) { // panel edits are read from the end of the log as booted
			edit_cursor     = -1;
			applied_buttons = gui_buttons = 0;
			all_eq_on       = true;
		}
#endif
		E88RenderFrames(inst, 2048);
		double sl = 0.0, sr = 0.0;
		for (size_t f = 1024; f < 2048; ++f) {
			sl += inst.buf_l[f];
			sr += inst.buf_r[f];
		}
		inst.dc_l = (i == 0) ? 0.0f : static_cast<float>(sl / 1024.0);
		inst.dc_r = (i == 0) ? 0.0f : static_cast<float>(sr / 1024.0);
		inst.buf_l.clear();
		inst.buf_r.clear();
	return inst.ctx != nullptr;
}

void NukedSc55::JoinBootThreads()
{
	for (auto& t : boot_threads)
		if (t.joinable()) t.join();
	boot_threads.clear();
}

// A short MIDI message from the panel (PREVIEW, MUTE), routed like host input.
void NukedSc55::InjectShort(const uint8_t s, const uint8_t d1, const uint8_t d2, const uint16_t port)
{
	clap_event_midi_t ev{};
	ev.port_index      = port; // 1 = IN B (same as the second CLAP note port)
	ev.header.size     = sizeof(ev);
	ev.header.time     = 0;
	ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
	ev.header.type     = CLAP_EVENT_MIDI;
	ev.data[0] = s; ev.data[1] = d1; ev.data[2] = d2;
	injecting = true;
	ProcessEvent(&ev.header, 0);
	injecting = false;
}

//----------------------------------------------------------------------------
// SC-88 Pro unit helpers (88emu)
//----------------------------------------------------------------------------
namespace {
constexpr uint32_t Btn88Map  = 1u << 1; // Sc88ProButton::Sc88Map
constexpr uint32_t Btn55Map  = 1u << 2; // Sc88ProButton::Sc55Map
constexpr uint32_t BtnAll    = 1u << 6; // Sc88ProButton::InstAll
}

// Tone-map switching like on the hardware front panel. Both map buttons
// toggle: SC-55 MAP switches SC-55 <-> SC-88 Pro (from SC-88: to SC-55),
// SC-88 MAP switches SC-88 <-> SC-88 Pro (from SC-55: to SC-88). The panel
// LEDs do not reliably show the map, so the state of every unit is tracked
// (boot = SC-88 Pro); nothing else presses these buttons and GS resets keep
// the map. ALL (latching, LED 0) makes the press apply to all parts.
static uint32_t E88MapKey(const int from, const int to)
{
	if (to == 0) return Btn55Map;
	if (to == 1) return Btn88Map;
	return from == 0 ? Btn55Map : Btn88Map; // back to SC-88 Pro
}

// Render frames into the unit's buffer; advances a pending panel sequence
// (each button: press 0.15 s, release 0.35 s).
void NukedSc55::E88RenderFrames(Instance& inst, size_t frames)
{
	float tmp[2 * 256];
	while (frames > 0) {
		uint32_t n = static_cast<uint32_t>(std::min<size_t>(frames, 256));
		if (inst.seq_len > 0) n = std::max<uint32_t>(1, std::min(n, inst.seq_left));
		emu88_render_float(inst.ctx, tmp, n);
		for (uint32_t k = 0; k < n; ++k) {
			inst.buf_l.push_back(tmp[2 * k]);
			inst.buf_r.push_back(tmp[2 * k + 1]);
		}
		frames -= n;
		if (inst.seq_len > 0 && (inst.seq_left = inst.seq_left > n ? inst.seq_left - n : 0) == 0) {
			const double rate = emu88_get_device_samplerate(inst.ctx);
			++inst.seq_pos;
			if (inst.seq_pos >= inst.seq_len) {
				inst.applied_map = inst.seq_map;
				inst.seq_len     = 0;
			} else if (inst.seq_pos % 2 == 1) { // release
				emu88_set_panel_buttons(inst.ctx, 0);
				inst.seq_left = static_cast<uint32_t>(rate * 0.35);
			} else { // next press
				emu88_set_panel_buttons(inst.ctx, inst.seq_mask[inst.seq_pos / 2]);
				inst.seq_left = static_cast<uint32_t>(rate * 0.15);
			}
		}
	}
}

// Start a (non-blocking) panel sequence towards `map`.
void NukedSc55::E88StartMapSequence(Instance& inst, const int map)
{
	if (!HasToneMap()) { // SC-8850: tone maps are part settings of its own panel
		inst.applied_map = map;
		return;
	}
	if (inst.seq_len > 0 || map == inst.applied_map) return;
	const bool all_on = (emu88_get_panel_leds(inst.ctx) & 1u) != 0;
	int k = 0;
	if (!all_on) inst.seq_mask[k++] = BtnAll;
	inst.seq_mask[k++] = E88MapKey(inst.applied_map, map);
	inst.seq_mask[k++] = BtnAll; // leave ALL mode again
	inst.seq_map  = map;
	inst.seq_len  = 2 * k; // press + release per button
	inst.seq_pos  = 0;
	inst.seq_left = static_cast<uint32_t>(emu88_get_device_samplerate(inst.ctx) * 0.15);
	emu88_set_panel_buttons(inst.ctx, inst.seq_mask[0]);
}

// Blocking variant (boot / background warmer only): renders until applied.
void NukedSc55::E88ApplyMapSync(Instance& inst, const int map)
{
	for (int round = 0; round < 2 && inst.applied_map != map; ++round) {
		E88StartMapSequence(inst, map);
		while (inst.seq_len > 0) {
			E88RenderFrames(inst, inst.seq_left);
			inst.buf_l.clear();
			inst.buf_r.clear();
		}
	}
	while (inst.seq_len > 0) {
		E88RenderFrames(inst, inst.seq_left);
		inst.buf_l.clear();
		inst.buf_r.clear();
	}
}

// Let replayed MIDI reach the firmware: real MIDI transfer time (10 bits per
// byte at 31250 baud) plus settle time; the rendered audio is discarded.
void NukedSc55::E88Drain(Instance& inst, const size_t bytes, const double extra_seconds)
{
	const double secs = static_cast<double>(bytes) * 10.0 / 31250.0 + extra_seconds;
	E88RenderFrames(inst, static_cast<size_t>(render_sample_rate_hz * secs) + 1);
	inst.buf_l.clear();
	inst.buf_r.clear();
}
#endif

#if defined(NUKED_SC55_DEVICE_8850) || defined(NUKED_SC55_DEVICE_88)
//----------------------------------------------------------------------------
// Front panel of unit 0 (SC-8850, SC-88)
//----------------------------------------------------------------------------
uint32_t NukedSc55::PanelLeds() const
{
	if (!lcd_ready.load() || instances.empty() || !instances[0].ctx) return 0;
	return emu88_get_panel_leds(instances[0].ctx);
}

// A DT1 for all units but unit 0 (which made the change) and for the state log.
void NukedSc55::E88Forward(const uint8_t port, const uint8_t* msg, const size_t size)
{
	const std::span<const uint8_t> m{msg, size};
	state_log.AddSysEx(m, port);
	const auto mask = router.RouteSysEx(m, port) & ~PolyRouter::Mask{1};
	QueueMidi(mask, msg, size, 0, port);
	log("%s panel: %zu bytes to the other units", ModelName(), size);
}

namespace {
#ifdef NUKED_SC55_DEVICE_88
// SC-88 (control ROM 1.01, the only one the builder accepts): switch bits (88emu sc88types.h)
constexpr uint32_t kBtn88Eq      = 1u << 1;
constexpr uint32_t kBtn88InstMap = 1u << 2;
constexpr uint32_t kBtn88MidiCh  = (1u << 8) | (1u << 9);
// Work RAM: the panel-edit log, a ring of 256 bytes, and its write pointer (big endian).
// Entry: 4 bytes [b0 b1 b2 b3]; GS parameter when b1 = 0x6n: address 40/50 (b0 bit 4 = port B),
// n(b0 & 0x0f), b2, value b3; with b0 bit 6 the entry carries b3 data bytes after it (padded to
// 4). Panel states without a GS parameter (ALL + MUTE, ALL + EQ, ...) use other b1 values.
constexpr uint32_t kRam88EditLog   = 0x03c2;
constexpr uint32_t kRam88EditWrite = 0x03c0;
constexpr int kRam88EditSize       = 0x100;
constexpr uint32_t kRam88PanelFlags = 0xc06a; // bit 0: ALL + MUTE (everything muted)
#endif
} // namespace

#ifdef NUKED_SC55_DEVICE_8850
namespace {
// SC-8850 switches (88emu Sc8850Button bits) that never edit a parameter: PART < >, EXIT,
// SHIFT, SOLO, MUTE (taken from the work RAM, E88PanelMutes), PREVIEW
constexpr uint32_t kBtn8850NoEdit = (1u << 1) | (1u << 2) | (1u << 7) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 20);
constexpr uint32_t kBtn8850Enter  = 1u << 8;
constexpr uint32_t kLed8850Drum   = 1u << 1;
constexpr uint32_t kRam8850CurPart = 0x01060349; // part shown on the panel, 0..63 = A01..D16
} // namespace
#endif

// Audio thread, start of a block: GUI switches (and the SC-8850's VALUE detents) to unit 0
void NukedSc55::E88PanelInput()
{
	auto& inst = instances[0];
	if (!inst.ctx) return;
	uint32_t buttons = ui_panel_buttons.load(std::memory_order_relaxed);
#ifdef NUKED_SC55_DEVICE_88
	// In ALL mode, EQ, INST MAP and MIDI CH change panel states of the whole unit that have no
	// GS parameter (EQ off for everything, all tone maps, device ID) and could not be passed on.
	// EQ is applied as the EQ switch of all 32 parts instead; the others stay on unit 0's panel.
	const uint32_t pressed = buttons & ~gui_buttons;
	gui_buttons            = buttons;
	if (emu88_get_panel_leds(inst.ctx) & 1u) {
		if (pressed & kBtn88Eq) {
			all_eq_on = !all_eq_on;
			for (int part = 0; part < kNumParts; ++part) {
				const int x = part % 16, block = x == 9 ? 0 : (x < 9 ? x + 1 : x);
				uint8_t msg[11] = {0xf0, 0x41, 0x10, 0x42, 0x12, static_cast<uint8_t>(part < 16 ? 0x40 : 0x50),
				                   static_cast<uint8_t>(0x40 | block), 0x20, all_eq_on ? uint8_t(1) : uint8_t(0), 0, 0xf7};
				int sum = 0;
				for (int k = 5; k < 9; ++k) sum += msg[k];
				msg[9] = static_cast<uint8_t>((128 - (sum & 0x7f)) & 0x7f);
				E88SendAll(0, msg, sizeof(msg));
			}
		}
		buttons &= ~(kBtn88Eq | kBtn88InstMap | kBtn88MidiCh);
	}
	if (buttons == applied_buttons) return;
	emu88_set_panel_buttons(inst.ctx, buttons);
	applied_buttons = buttons;
#else
	const int detents = ui_encoder.exchange(0);
	if (buttons == applied_buttons && detents == 0) return;
	const uint32_t pressed = buttons & ~applied_buttons;
	if (buttons != applied_buttons) {
		emu88_set_panel_buttons(inst.ctx, buttons);
		applied_buttons = buttons;
	}
	if (detents != 0) emu88_turn_panel_encoder(inst.ctx, std::clamp(detents, -64, 63));
	panel_frame = render_frame_count;
	// Only input that can edit parameters starts a read-back pass, and the pass reads only
	// what that input can have changed: the data requests go through unit 0's MIDI input
	// and delay the host's notes there while they are answered (Issue #13).
	if ((pressed & ~kBtn8850NoEdit) != 0 || detents != 0) {
		uint8_t cur = 0;
		emu88_peek_work_ram(inst.ctx, kRam8850CurPart, &cur, 1);
		if (cur < kNumParts) sync_parts |= uint64_t{1} << cur;
		sync_drums |= (emu88_get_panel_leds(inst.ctx) & kLed8850Drum) != 0;
		sync_full |= (pressed & kBtn8850Enter) != 0;
		sync_dirty = true;
	}
#endif
}
#endif

#ifdef NUKED_SC55_DEVICE_88
//----------------------------------------------------------------------------
// SC-88 front panel: synchronisation of panel edits
//----------------------------------------------------------------------------
// A DT1 for all units (unit 0 included) and the state log.
void NukedSc55::E88SendAll(const uint8_t port, const uint8_t* msg, const size_t size)
{
	const std::span<const uint8_t> m{msg, size};
	state_log.AddSysEx(m, port);
	QueueMidi(router.RouteSysEx(m, port), msg, size, 0, port);
}

// New entries of unit 0's panel-edit log -> DT1 to the other units and the state log
void NukedSc55::E88EditPump()
{
	auto& inst = instances[0];
	if (!inst.ctx) return;
	uint8_t wp[2] = {};
	emu88_peek_work_ram(inst.ctx, kRam88EditWrite, wp, 2);
	const int w = wp[0] << 8 | wp[1];
	if (w < static_cast<int>(kRam88EditLog) || w >= static_cast<int>(kRam88EditLog) + kRam88EditSize) return;
	if (edit_cursor < 0) edit_cursor = w;
	if (w == edit_cursor) return;
	uint8_t ring[kRam88EditSize];
	emu88_peek_work_ram(inst.ctx, kRam88EditLog, ring, sizeof(ring));
	int pos   = edit_cursor - static_cast<int>(kRam88EditLog);
	int avail = (w - edit_cursor) & (kRam88EditSize - 1);
	const auto at = [&](int k) { return ring[(pos + k) & (kRam88EditSize - 1)]; };
	while (avail >= 4) {
		const uint8_t b0 = at(0), b1 = at(1), b2 = at(2), b3 = at(3);
		const bool multi = (b0 & 0x40) != 0;
		const int size   = multi ? 4 + ((b3 + 3) & ~3) : 4;
		if (size > avail) break; // not complete yet
		if ((b1 & 0xf0) == 0x60 && (!multi || (b3 >= 1 && b3 <= 16))) {
			uint8_t msg[32] = {0xf0, 0x41, 0x10, 0x42, 0x12, static_cast<uint8_t>(0x40 | (b0 & 0x10)),
			                   static_cast<uint8_t>(((b1 & 0x0f) << 4) | (b0 & 0x0f)), b2};
			size_t k = 8;
			bool ok  = true;
			for (int i = 0; i < (multi ? b3 : 1); ++i) {
				const uint8_t v = multi ? at(4 + i) : b3;
				ok &= v < 0x80; // 0x80: no change (a limit was reached)
				msg[k++] = v;
			}
			int sum = 0;
			for (size_t i = 5; i < k; ++i) sum += msg[i];
			msg[k++] = static_cast<uint8_t>((128 - (sum & 0x7f)) & 0x7f);
			msg[k++] = 0xf7;
			if (ok) E88Forward(0, msg, k);
		}
		pos = (pos + size) & (kRam88EditSize - 1);
		avail -= size;
	}
	edit_cursor = static_cast<int>(kRam88EditLog) + pos;
}

// ALL + MUTE of unit 0's panel -> mute mask of all units
void NukedSc55::E88PanelMutes()
{
	auto& inst = instances[0];
	if (!inst.ctx) return;
	uint8_t flags = 0;
	emu88_peek_work_ram(inst.ctx, kRam88PanelFlags, &flags, 1);
	mute_mask.store((flags & 1u) ? ~uint64_t{0} >> (64 - kNumParts) : 0, std::memory_order_relaxed);
}
#endif

#ifdef NUKED_SC55_DEVICE_8850
//----------------------------------------------------------------------------
// SC-8850 front panel (unit 0) and its synchronisation with the other units
//----------------------------------------------------------------------------
// The GUI operates unit 0's own panel, so every firmware menu works. Panel edits stay
// on unit 0; they are read back with GS data requests (RQ1) once the panel has been idle
// for a moment and passed on to the other units and the state log (sleeping units) as
// DT1 messages, only what changed since the last read. MUTE and SOLO are panel states
// without GS parameters: they are read from unit 0's work RAM and applied to all units.
namespace {
// Firmware work RAM (program "SC-GS 1.00", the only one the builder accepts): one record
// of 0x450 bytes per part, in firmware order (A10, A01..A09, A11..A16, B10, B01, ...).
constexpr uint32_t kRam8850PartRec  = 0x0103BA12; // bit 1 of the first byte clear = MUTE
constexpr uint32_t kRam8850PartSize = 0x450;
constexpr uint32_t kRam8850SoloGate = 0x79;       // 0 while another part is soloed (else 0x7F)
int Fw8850PartIndex(const int part)
{
	const int c = part % 16;
	return part / 16 * 16 + (c == 9 ? 0 : (c < 9 ? c + 1 : c));
}
int GsBlockOfChannel(const int c) { return c == 9 ? 0 : (c < 9 ? c + 1 : c); } // 40 1x: x of part c+1
size_t SyncIndex(const int port, const int a0, const int a1, const int a2)
{
	return ((static_cast<size_t>(port) * 2 + static_cast<size_t>(a0 - 0x40)) * 128 + a1) * 128 + a2;
}
constexpr uint32_t kSyncMaxOutstanding = 4;   // more requests at once overflow the firmware
} // namespace

bool NukedSc55::GetLcdDots(uint8_t* dots, const size_t size, bool& on)
{
	if (!lcd_ready.load() || instances.empty() || !instances[0].ctx) return false;
	int w = 0, h = 0, o = 0;
	if (!emu88_get_display_pixels(instances[0].ctx, 0, dots, size, &w, &h, &o)) return false;
	on = o != 0;
	if (w != kLcdW || h != kLcdH) return false;
	if (on && ui_dots_on.load() && size >= static_cast<size_t>(kLcdW * kLcdH)) {
		// Level meters of the play screen: 16 bars, 5 dots wide every 6 dots from x 49, the
		// bottom row (y 47) always lit. Other screens (menus) keep their own picture.
		constexpr int X0 = 49, Y0 = 15, YB = 47;
		bool meters = true;
		for (int c = 0; c < 16 && meters; ++c) {
			for (int k = 0; k < 6; ++k) {
				const bool lit = dots[YB * kLcdW + X0 + c * 6 + k] != 0;
				if (lit != (k < 5) && !(c == 15 && k == 5)) meters = false;
			}
		}
		if (meters) {
			// 16 x 16 bitmap on the meter area: a column = one bar (5 dots), a row = 2 dots
			for (int y = Y0; y <= YB; ++y)
				std::memset(dots + y * kLcdW + X0, 0, 16 * 6 - 1);
			for (int r = 0; r < 16; ++r) {
				const uint16_t bits = ui_dots[r].load(std::memory_order_relaxed);
				for (int c = 0; c < 16; ++c) {
					if (!(bits & (0x8000u >> c))) continue;
					for (int dy = 0; dy < 2; ++dy)
						std::memset(dots + (Y0 + r * 2 + dy) * kLcdW + X0 + c * 6, 1, 5);
				}
			}
		}
	}
	return true;
}

// Audio thread: GS dot display messages (Roland, model 45, DT1). Address 10 0n xx: page
// 2(n-1) + (xx >= 40) with the byte at xx & 3F; byte i = row i % 16, columns 5(i / 16) ..
// +4 from bit 4 down. 10 20 00 v: show page v (1..10), 0 = back to the meters.
void NukedSc55::DotSysEx(const std::span<const uint8_t> msg)
{
	if (msg.size() < 11 || msg[0] != 0xf0 || msg[1] != 0x41 || msg[3] != 0x45 || msg[4] != 0x12 ||
	    msg[5] != 0x10 || msg.back() != 0xf7)
		return;
	const size_t n = msg.size() - 10; // data bytes (without address, checksum, F7)
	const uint8_t a1 = msg[6], a2 = msg[7];
	if (a1 >= 0x01 && a1 <= 0x05) {
		const int page = (a1 - 1) * 2 + (a2 >> 6);
		auto& rows    = dot_pages[page];
		for (size_t k = 0; k < n; ++k) {
			const size_t i = (a2 & 0x3f) + k;
			if (i >= 64) break;
			const int row = static_cast<int>(i % 16), first = static_cast<int>(i / 16) * 5;
			for (int b = 0; b < 5 && first + b < 16; ++b) {
				const uint16_t bit = 0x8000u >> (first + b);
				if (msg[8 + k] & (0x10 >> b)) rows[row] |= bit;
				else rows[row] &= static_cast<uint16_t>(~bit);
			}
		}
		if (page == dot_page) DotShow(true);
	} else if (a1 == 0x20 && a2 == 0x00 && n >= 1) {
		const int v = msg[8];
		if (v == 0) {
			DotShow(false);
		} else if (v <= 10) {
			dot_page = v - 1;
			DotShow(true);
		}
	}
}

void NukedSc55::DotShow(const bool on)
{
	if (on) {
		for (int r = 0; r < 16; ++r) ui_dots[r].store(dot_pages[dot_page][r], std::memory_order_relaxed);
		dot_until = render_frame_count + 1 +
		            static_cast<uint64_t>(kDotShowSeconds * (render_sample_rate_hz > 0 ? render_sample_rate_hz : 32000.0));
	} else {
		dot_until = 0;
	}
	ui_dots_on.store(on);
}


// Queue the data requests of one pass: system and effects first, then the part shown on
// the panel, then the other parts and both drum maps. At boot everything; after panel input
// only the parts shown while editing (all after ENTER) and the drum maps if DRUM was open.
void NukedSc55::E88SyncStart(const bool boot)
{
	sync_queue.clear();
	const auto add = [&](int port, int a0, int a1, int a2, int n) {
		sync_queue.push_back({static_cast<uint8_t>(port), static_cast<uint8_t>(a0), static_cast<uint8_t>(a1),
		                      static_cast<uint8_t>(a2), static_cast<uint8_t>(n)});
	};
	const auto add_part = [&](int part) {
		const int port = part / 16, x = GsBlockOfChannel(part % 16);
		add(port, 0x40, 0x10 | x, 0x00, 0x26); // tone .. delay send
		add(port, 0x40, 0x10 | x, 0x2a, 1);
		add(port, 0x40, 0x10 | x, 0x2c, 1);
		add(port, 0x40, 0x10 | x, 0x30, 9);    // tone modify
		add(port, 0x40, 0x10 | x, 0x40, 12);   // scale tuning
		add(port, 0x40, 0x10 | x, 0x60, 1);
		for (int k = 0; k < 6; ++k) add(port, 0x40, 0x20 | x, k * 0x10, 11); // controllers
		add(port, 0x40, 0x40 | x, 0x00, 2);    // tone maps
		add(port, 0x40, 0x40 | x, 0x20, 3);    // EQ, output, EFX assign
	};
	add(0, 0x40, 0x00, 0x00, 7);    // master tune, volume, key shift, pan
	add(0, 0x40, 0x01, 0x00, 0x10); // patch name
	add(0, 0x40, 0x01, 0x30, 0x11); // reverb, chorus
	add(0, 0x40, 0x01, 0x50, 0x0b); // delay
	add(0, 0x40, 0x02, 0x00, 4);    // EQ
	add(0, 0x40, 0x03, 0x00, 0x20); // EFX
	int cur = 0;
	if (!boot && instances[0].ctx) {
		uint8_t v = 0;
		emu88_peek_work_ram(instances[0].ctx, kRam8850CurPart, &v, 1);
		cur = (v < kNumParts) ? v : 0;
	}
	const bool all = boot || sync_full;
	add_part(cur);
	for (int part = 0; part < kNumParts; ++part)
		if (part != cur && (all || (sync_parts & (uint64_t{1} << part)))) add_part(part);
	if (all || sync_drums)
		for (int map = 0; map < 2; ++map)
			for (int prm = 1; prm <= 9; ++prm) add(0, 0x41, map * 0x10 + prm, 0x00, 0x80); // drum setup
	sync_parts = 0;
	sync_drums = sync_full = false;
	std::reverse(sync_queue.begin(), sync_queue.end()); // sent from the back
}

// Compare an answer with what the other units are known to have and pass on the changes.
void NukedSc55::E88SyncAnswer(const SyncReq& req, const uint8_t* data, const int n, const bool forward)
{
	const bool part_block = req.a0 == 0x40 && req.a1 >= 0x10;
	int part              = -1;
	if (part_block) {
		const int x = req.a1 & 0x0f;
		part        = req.port * 16 + (x == 0 ? 9 : (x <= 9 ? x - 1 : x));
	}
	if (forward && req.tries < 3) {
		// The host may have changed the same parameters while the answer was on its way: then
		// the answer can be older than what all units have now, so ask again a little later.
		const uint64_t window = 3200; // 0.1 s
		const auto recent     = [&](uint64_t f) { return f != 0 && f + window > render_frame_count; };
		if (recent(host_change[kNumParts]) || (part >= 0 && recent(host_change[part]))) {
			SyncReq again = req;
			++again.tries;
			sync_queue.insert(sync_queue.begin(), again);
			return;
		}
	}
	std::array<bool, 128> send{};
	bool any = false;
	for (int i = 0; i < n && req.a2 + i < 128; ++i) {
		const size_t idx = SyncIndex(req.port, req.a0, req.a1, req.a2 + i);
		if (sync_base[idx] != data[i]) send[i] = any = true;
		sync_base[idx] = data[i];
	}
	if (!any || !forward) return;
	// Multi-byte parameters are only written as a whole (master tune, tone number, fine tune,
	// EFX type): a changed byte takes the other bytes of its parameter along.
	const auto param_of = [&](int a, int& from, int& to) {
		from = to = a;
		if (req.a0 != 0x40) return;
		if (req.a1 == 0x00 && a <= 0x03) from = 0x00, to = 0x03;
		else if (req.a1 == 0x03 && a <= 0x02) from = 0x00, to = 0x02;
		else if ((req.a1 & 0xf0) == 0x10 && a <= 0x01) from = 0x00, to = 0x01;
		else if ((req.a1 & 0xf0) == 0x10 && (a == 0x17 || a == 0x18)) from = 0x17, to = 0x18;
	};
	for (int i = 0; i < n && req.a2 + i < 128; ++i) {
		if (!send[i]) continue;
		int from, to;
		param_of(req.a2 + i, from, to);
		for (int a = std::max(from, static_cast<int>(req.a2)); a <= to && a - req.a2 < n; ++a) send[a - req.a2] = true;
	}
	// An effect type or macro sets the defaults of the following parameters: resend those too
	const auto keep_group = [&](int from, int to) {
		const int f = from - req.a2;
		if (f < 0 || f >= n || !send[f]) return;
		for (int a = from; a <= to && a - req.a2 < n; ++a) send[a - req.a2] = true;
	};
	if (req.a0 == 0x40 && req.a1 == 0x01) {
		keep_group(0x30, 0x37); // reverb macro
		keep_group(0x38, 0x40); // chorus macro
		keep_group(0x50, 0x5a); // delay macro
	} else if (req.a0 == 0x40 && req.a1 == 0x03) {
		keep_group(0x00, 0x7f); // EFX type
		keep_group(0x01, 0x7f);
	}
	uint8_t msg[128 + 10];
	for (int i = 0; i < n && req.a2 + i < 128;) {
		if (!send[i]) { ++i; continue; }
		int j = i;
		while (j < n && req.a2 + j < 128 && send[j]) ++j;
		const uint8_t addr[3] = {req.a0, req.a1, static_cast<uint8_t>(req.a2 + i)};
		size_t k = 0;
		for (uint8_t b : {0xf0, 0x41, 0x10, 0x42, 0x12}) msg[k++] = b;
		int sum = 0;
		for (uint8_t b : addr) { msg[k++] = b; sum += b; }
		for (int m = i; m < j; ++m) { msg[k++] = data[m]; sum += data[m]; }
		msg[k++] = static_cast<uint8_t>((128 - (sum & 0x7f)) & 0x7f);
		msg[k++] = 0xf7;
		E88Forward(req.port, msg, k);
		i = j;
	}
}


// Collect answers, keep requests flowing, start a pass after panel input.
void NukedSc55::E88SyncPump(Instance& inst, const bool forward, const uint64_t now)
{
	if (!inst.ctx || sync_base.empty()) return;
	uint8_t buf[2048];
	for (size_t got; (got = emu88_read_midi_out(inst.ctx, buf, sizeof(buf))) > 0;)
		sync_in.insert(sync_in.end(), buf, buf + got);
	size_t pos = 0;
	for (;;) {
		const auto b = std::find(sync_in.begin() + static_cast<std::ptrdiff_t>(pos), sync_in.end(), 0xf0);
		const auto e = std::find(b, sync_in.end(), 0xf7);
		if (e == sync_in.end()) {
			pos = static_cast<size_t>(b - sync_in.begin());
			break;
		}
		const uint8_t* m = &*b;
		const auto len   = static_cast<size_t>(e - b) + 1;
		if (len >= 10 && m[1] == 0x41 && m[3] == 0x42 && m[4] == 0x12) {
			for (auto it = sync_outstanding.begin(); it != sync_outstanding.end(); ++it) {
				if (it->a0 == m[5] && it->a1 == m[6] && it->a2 == m[7]) {
					const SyncReq req = *it;
					sync_outstanding.erase(it);
					E88SyncAnswer(req, m + 8, static_cast<int>(len - 10), forward);
					break;
				}
			}
		}
		pos = static_cast<size_t>(e - sync_in.begin()) + 1;
	}
	sync_in.erase(sync_in.begin(), sync_in.begin() + static_cast<std::ptrdiff_t>(std::min(pos, sync_in.size())));
	if (sync_in.size() > 8192) sync_in.clear();
	// unanswered (address not readable on this firmware): give up after 0.25 s
	std::erase_if(sync_outstanding, [&](const SyncReq& r) { return now > r.sent_frame + 8000; });
	// a new pass 0.4 s after the last panel input
	if (forward && sync_dirty && sync_queue.empty() && sync_outstanding.empty() && now > panel_frame + 12800) {
		sync_dirty = false;
		E88SyncStart(false);
	}
	while (!sync_queue.empty() && sync_outstanding.size() < kSyncMaxOutstanding) {
		SyncReq r = sync_queue.back();
		sync_queue.pop_back();
		uint8_t m[13] = {0xf0, 0x41, 0x10, 0x42, 0x11, r.a0, r.a1, r.a2, 0x00,
		                 static_cast<uint8_t>(r.size >> 7), static_cast<uint8_t>(r.size & 0x7f), 0, 0xf7};
		int sum = 0;
		for (int k = 5; k < 11; ++k) sum += m[k];
		m[11] = static_cast<uint8_t>((128 - (sum & 0x7f)) & 0x7f);
		emu88_parse_stream_on_port(inst.ctx, r.port, m, sizeof(m));
		r.sent_frame = now;
		sync_outstanding.push_back(r);
	}
}

// Boot of unit 0: read everything once (nothing to pass on yet).
void NukedSc55::E88SyncBoot(Instance& inst)
{
	sync_base.assign(static_cast<size_t>(kNumPorts) * 2 * 128 * 128, -1);
	sync_in.clear();
	sync_outstanding.clear();
	host_change.fill(0);
	sync_dirty      = false;
	sync_parts      = 0;
	sync_drums = sync_full = false;
	applied_buttons = 0;
	emu88_capture_midi_out(inst.ctx, 1);
	E88SyncStart(true);
	uint64_t now = 0;
	for (int k = 0; k < 6 * 32000 / 64 && (!sync_queue.empty() || !sync_outstanding.empty()); ++k) {
		E88RenderFrames(inst, 64);
		inst.buf_l.clear();
		inst.buf_r.clear();
		now += 64;
		E88SyncPump(inst, false, now);
	}
	int known = 0;
	for (const auto v : sync_base) known += v >= 0;
	log("SC-8850: %d panel parameters read in %.2f s (device time)", known, now / 32000.0);
}

// MUTE / SOLO of unit 0's panel -> mute mask of all units
void NukedSc55::E88PanelMutes()
{
	auto& inst = instances[0];
	if (!inst.ctx) return;
	uint64_t mask = 0;
	for (int part = 0; part < kNumParts; ++part) {
		const uint32_t rec = kRam8850PartRec + static_cast<uint32_t>(Fw8850PartIndex(part)) * kRam8850PartSize;
		uint8_t flags = 0xff, gate = 0x7f;
		emu88_peek_work_ram(inst.ctx, rec, &flags, 1);
		emu88_peek_work_ram(inst.ctx, rec + kRam8850SoloGate, &gate, 1);
		if (!(flags & 2) || gate == 0) mask |= uint64_t{1} << part;
	}
	mute_mask.store(mask, std::memory_order_relaxed);
}
#endif

//----------------------------------------------------------------------------
// Background warmer
//----------------------------------------------------------------------------
void NukedSc55::StartWarmer()
{
	StopWarmer();
	warmer_quit     = false;
	warmer_has_job  = false;
	warmer_thread   = std::thread([this] { WarmerLoop(); });
}

void NukedSc55::StopWarmer()
{
	if (!warmer_thread.joinable()) return;
	{
		std::lock_guard lk(warmer_mutex);
		warmer_quit = true;
	}
	warmer_cv.notify_all();
	warmer_thread.join();
}

void NukedSc55::ReplayInto(Instance& inst, const std::vector<StateLog::Entry>& entries)
{
#ifdef NUKED_SC55_ENGINE_88PRO
	E88ApplyMapSync(inst, tone_map.load());
	size_t bytes = 0;
	for (const auto& e : entries) {
		emu88_parse_stream_on_port(inst.ctx, e.port, e.bytes.data(), static_cast<uint32_t>(e.bytes.size()));
		bytes += e.bytes.size();
		if (e.is_reset) {
			E88Drain(inst, bytes, 0.06);
			bytes = 0;
		}
	}
	E88Drain(inst, bytes, 0.005);
	return;
#endif
	auto& mcu = inst.emu->GetMCU();
	const auto step_until_drained = [&](double extra_seconds) {
		while (mcu.uart_read_ptr != mcu.uart_write_ptr) MCU_Step(mcu);
		const auto target = inst.buf_l.size() +
		                    static_cast<size_t>(render_sample_rate_hz * extra_seconds);
		while (inst.buf_l.size() < target) MCU_Step(mcu);
		inst.buf_l.clear();
		inst.buf_r.clear();
	};
	for (const auto& e : entries) {
		inst.emu->PostMIDI(std::span{e.bytes.data(), e.bytes.size()});
		if (e.is_reset) step_until_drained(0.06);
	}
	step_until_drained(0.005);
}

void NukedSc55::WarmerLoop()
{
	for (;;) {
		WarmJob job;
		{
			std::unique_lock lk(warmer_mutex);
			warmer_cv.wait(lk, [&] { return warmer_quit || warmer_has_job; });
			if (warmer_quit) return;
			job = std::move(warmer_job);
			warmer_has_job = false;
		}
		auto& inst = instances[job.instance];
		ReplayInto(inst, job.entries);
		inst.sleep_seq = job.target_seq;
		inst.warming->store(false, std::memory_order_release);
	}
}

// Audio thread, after rendering: hand the lowest sleeping (allowed) instance
// that is behind the state log to the warmer. Never blocks (try_lock).
void NukedSc55::ScheduleWarming()
{
	static const bool disabled = !get_env_var("NUKED_SC55_NO_WARMER").empty(); // tests
	if (disabled) return;
	if (render_frame_count < warmer_next_frame) return;
	warmer_next_frame = render_frame_count +
	                    static_cast<uint64_t>(render_sample_rate_hz * 0.25);

	// Only the next two sleeping instances (waking goes lowest first)
	int cand = -1, considered = 0;
	for (int i = 0; i < NumInstances() && i < router.Allowed() && considered < 2; ++i) {
		const auto& inst = instances[i];
		if (inst.awake) continue;
		++considered;
		if (inst.warming->load(std::memory_order_acquire)) return; // one at a time
#ifdef NUKED_SC55_ENGINE_88PRO
		// Still booting in the background: only if it needs warming now, wait for it, so the
		// warming sequence (and therefore the output) is exactly that of a unit booted in
		// Activate(). Before the first MIDI arrives nothing needs warming and nothing waits.
		if (!inst.booted->load(std::memory_order_acquire)) {
			if (inst.sleep_seq >= state_log.Seq()) continue;
			while (!inst.booted->load(std::memory_order_acquire))
				std::this_thread::sleep_for(std::chrono::microseconds(500));
		}
		if (inst.sleep_seq < state_log.Seq() || inst.applied_map != tone_map.load()) { cand = i; break; }
#else
		if (inst.sleep_seq < state_log.Seq()) { cand = i; break; }
#endif
	}
	if (cand < 0) return;

	std::unique_lock lk(warmer_mutex, std::try_to_lock);
	if (!lk.owns_lock() || warmer_has_job) return;
	warmer_job.instance   = cand;
	warmer_job.target_seq = state_log.Seq();
	state_log.CopySince(instances[cand].sleep_seq, warmer_job.entries);
	instances[cand].warming->store(true, std::memory_order_release);
	warmer_has_job = true;
	lk.unlock();
	warmer_cv.notify_one();
}

//----------------------------------------------------------------------------
// Dynamic instance management
//----------------------------------------------------------------------------
void NukedSc55::ManageInstances(const int incoming_note_ons)
{
	const int cap    = partials_per_instance;
	const int margin = 2;

	int free = 0;
	for (int i = 0; i < NumInstances(); ++i) {
		if (instances[i].awake) {
			free += cap - router.MeasuredLoad(i);
		}
	}
	int awake_count = 0;
	for (const auto& inst : instances) awake_count += inst.awake;
	// Keep ~4 free partials per awake instance, so re-triggers of held keys
	// can stay on their instance and the firmware decides about them exactly
	// like on the hardware.
	// Waking happens synchronously before this block's notes are routed,
	// so only the notes of this block plus a small margin must fit.
	(void)awake_count;
	const int need = incoming_note_ons * 2 + margin;

	// Waking now happens on demand in the router, note by note.

	// Put at most one idle instance (highest index first) to sleep once it
	// has been silent for a while (incl. reverb tail) and the remaining
	// awake ones keep a quarter unit of headroom. Waking is cheap thanks to
	// the background warmer, so no large hysteresis is needed. Key ownership
	// does not block sleeping: the instance is verifiably silent.
	for (int i = NumInstances() - 1; i >= min_active; --i) {
		auto& inst = instances[i];
		if (!inst.awake) continue;
		const bool above_limit = i >= router.Allowed();
		// Output below -80 dBFS for the whole period: anything still left
		// on this instance (e.g. an endless quiet release) is inaudible.
		if (inst.silent_frames >= sleep_after_frames &&
		    (above_limit || free - cap + router.MeasuredLoad(i) >= need + cap / 4)) {
			router.ForgetInstance(i);
			Sleep(i);
			break;
		}
	}
}

void NukedSc55::Sleep(const int i)
{
	auto& inst     = instances[i];
	inst.awake     = false;
	inst.sleep_seq = state_log.Seq();
	inst.queue.clear();
	inst.buf_l.clear();
	inst.buf_r.clear();
	inst.read_pos      = 0;
	inst.silent_frames = 0;
	router.SetAwake(i, false);
	++sleeps;
	log("Poly: instance %d -> sleep", i);
}

void NukedSc55::Wake(const int i)
{
	const auto wake_t0 = std::chrono::steady_clock::now();
	int replayed_bytes = 0;
	auto& inst = instances[i];
#ifdef NUKED_SC55_ENGINE_88PRO
	while (!inst.booted->load(std::memory_order_acquire)) { // still booting in the background
		std::this_thread::sleep_for(std::chrono::microseconds(500));
	}
	while (inst.warming->load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}
	inst.buf_l.clear();
	inst.buf_r.clear();
	{
		size_t bytes = 0;
		state_log.ForEachSince(inst.sleep_seq, [&](const StateLog::Entry& e) {
			emu88_parse_stream_on_port(inst.ctx, e.port, e.bytes.data(), static_cast<uint32_t>(e.bytes.size()));
			bytes += e.bytes.size();
			replayed_bytes += static_cast<int>(e.bytes.size());
			if (e.is_reset) {
				E88Drain(inst, bytes, 0.06);
				bytes = 0;
			}
		});
		E88Drain(inst, bytes, 0.005);
	}
	// Map not yet on this unit (warmer did not get to it): switch it while
	// it plays instead of blocking the audio thread.
	E88StartMapSequence(inst, tone_map.load()); // no-op if already on the map
#else
	auto& mcu  = inst.emu->GetMCU();

	const auto step_until_drained = [&](double extra_seconds) {
		while (mcu.uart_read_ptr != mcu.uart_write_ptr) {
			MCU_Step(mcu);
		}
		const auto extra = static_cast<size_t>(render_sample_rate_hz * extra_seconds);
		const auto target = inst.buf_l.size() + extra;
		while (inst.buf_l.size() < target) {
			MCU_Step(mcu);
		}
		inst.buf_l.clear();
		inst.buf_r.clear();
	};

	// A background sync may be running on this instance: let it finish.
	while (inst.warming->load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}
	inst.buf_l.clear();
	inst.buf_r.clear();

	// Replay what the instance missed since its last sync (compacted).
	state_log.ForEachSince(inst.sleep_seq, [&](const StateLog::Entry& e) {
		inst.emu->PostMIDI(std::span{e.bytes.data(), e.bytes.size()});
		replayed_bytes += static_cast<int>(e.bytes.size());
		if (e.is_reset) {
			step_until_drained(0.06); // firmware needs time after GS reset
		}
	});
	step_until_drained(0.005);
#endif

	inst.read_pos      = 0;
	inst.silent_frames = 0;
	inst.awake         = true;
	router.SetAwake(i, true);
	router.SetMeasuredLoad(i, ActivePartials(i));
	++wakes;
	inst.last_wake_ms = std::chrono::duration<double, std::milli>(
	        std::chrono::steady_clock::now() - wake_t0).count();
	inst.last_wake_bytes = replayed_bytes;
	log("Poly: instance %d -> awake", i);
}

//----------------------------------------------------------------------------
// WorkerPool
//----------------------------------------------------------------------------
void WorkerPool::Start(const int num_threads)
{
	Stop();
	quit = false;
	for (int i = 0; i < num_threads; ++i) {
		threads.emplace_back([this] { WorkerLoop(); });
	}
}

void WorkerPool::Stop()
{
	{
		std::lock_guard lk(m);
		quit = true;
	}
	cv_start.notify_all();
	for (auto& t : threads) {
		t.join();
	}
	threads.clear();
}

void WorkerPool::Drain()
{
	for (int i = next.fetch_add(1); i < job_count; i = next.fetch_add(1)) {
		(*job)(i);
	}
}

void WorkerPool::WorkerLoop()
{
	uint64_t seen = 0;
	for (;;) {
		{
			std::unique_lock lk(m);
			cv_start.wait(lk, [&] { return quit || generation != seen; });
			if (quit) {
				return;
			}
			seen = generation;
		}
		Drain();
		{
			std::lock_guard lk(m);
			if (--busy == 0) {
				cv_done.notify_one();
			}
		}
	}
}

void WorkerPool::Run(const int count, const std::function<void(int)>& fn)
{
	if (threads.empty() || count <= 1) {
		for (int i = 0; i < count; ++i) {
			fn(i);
		}
		return;
	}
	{
		std::lock_guard lk(m);
		job       = &fn;
		job_count = count;
		next.store(0);
		busy = static_cast<int>(threads.size());
		++generation;
	}
	cv_start.notify_all();
	Drain(); // the audio thread works too
	std::unique_lock lk(m);
	cv_done.wait(lk, [&] { return busy == 0; });
	job = nullptr;
}

void NukedSc55::ResampleAndPublishFrames(const uint32_t num_out_frames,
                                         float* out_left, float* out_right)
{
	log("RenderAndPublishFrames: num_out_frames: %d", num_out_frames);

	const auto input_len  = static_cast<spx_uint32_t>(render_buf[0].size());
	const auto output_len = num_out_frames;

	log("  input_len: %d", input_len);

	spx_uint32_t in_len  = input_len;
	spx_uint32_t out_len = output_len;

	speex_resampler_process_float(
	        resampler, 0, render_buf[0].data(), &in_len, out_left, &out_len);

	in_len  = input_len;
	out_len = output_len;

	speex_resampler_process_float(
	        resampler, 1, render_buf[1].data(), &in_len, out_right, &out_len);

	// Speex returns the number actually consumed and written samples in
	// `in_len` and `out_len`, respectively. There are three outcomes:
	//
	// 1) The input buffer hasn't been fully consumed, but the output buffer
	//    has been completely filled.
	//
	// 2) The output buffer hasn't been filled completely, but all input
	//    samples have been consumed.
	//
	// 3) All input samples have been consumed and the output buffer has been
	//    completely filled.
	//
	if (out_len < output_len) {
		// Case 2: The output buffer hasn't been filled completely; we
		// need to generate more input samples.
		//
		const auto num_out_frames_remaining = output_len - out_len;
		const auto curr_out_pos             = out_len;

		// "It's the only way to be sure"
		const auto render_frame_count = static_cast<int>(
		        std::ceil(static_cast<double>(num_out_frames_remaining) *
		                  resample_ratio));

		render_buf[0].clear();
		render_buf[1].clear();

		RenderAudio(render_frame_count);

		in_len  = static_cast<spx_uint32_t>(render_buf[0].size());
		out_len = num_out_frames_remaining;

		speex_resampler_process_float(resampler,
		                              0,
		                              render_buf[0].data(),
		                              &in_len,
		                              out_left + curr_out_pos,
		                              &out_len);

		in_len  = static_cast<spx_uint32_t>(render_buf[1].size());
		out_len = num_out_frames_remaining;

		speex_resampler_process_float(resampler,
		                              1,
		                              render_buf[1].data(),
		                              &in_len,
		                              out_right + curr_out_pos,
		                              &out_len);
	}

	if (in_len < input_len) {
		// Case 1: The input buffer hasn't been fully consumed; we have
		// leftover input samples that we need to keep for the next
		// Process() call.
		//
		if (in_len > 0) {
			render_buf[0].erase(render_buf[0].begin(),
			                    render_buf[0].begin() + in_len);
			render_buf[1].erase(render_buf[1].begin(),
			                    render_buf[1].begin() + in_len);
		}

	} else {
		// Case 3: All input samples have been consumed and the output
		// buffer has been completely filled.
		//
		render_buf[0].clear();
		render_buf[1].clear();
	}
}
