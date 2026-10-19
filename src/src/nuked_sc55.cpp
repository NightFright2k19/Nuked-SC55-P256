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

#ifdef NUKED_SC55_DEVICE_8850
constexpr bool kHas8850Map = true;  // tone map for all parts from the SETUP menu (Bank Select LSB)
#else
constexpr bool kHas8850Map = false;
#endif
#ifdef NUKED_SC55_DEVICE_88
constexpr bool kHas88Map = true;    // tone map for all parts from the SETUP menu (ALL + INST MAP, Issue #59)
#else
constexpr bool kHas88Map = false;
#endif

#ifdef NUKED_SC55_DEVICE_8850
constexpr uint32_t kRam8850CurPart = 0x01060349; // part shown on the panel, 0..63 = A01..D16
#endif

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
extern std::mutex plugin_path_mutex;

NukedSc55::NukedSc55(const clap_plugin_t _plugin_class,
                     const clap_host_t* _host, const Model _model)
{
	log_init();

	{
		std::lock_guard lock(plugin_path_mutex);
		path = plugin_path;
	}
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
#elif defined(NUKED_SC55_DEVICE_LA)
	partials_per_instance = 32;  // MT-32 / CM-32L: 32 partials per unit
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
#ifdef NUKED_SC55_DEVICE_LA
	// MT-32 / CM-32L: all units run from the start and get every message. A sleeping unit would
	// have to be woken by replaying the SysEx state at MIDI wire speed (issue #69: faster corrupts
	// the patch memory), which takes seconds; one unit costs little CPU.
	dynamic_instances = false;
#endif
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
#if defined(NUKED_SC55_EMBED_ROMS) && defined(NUKED_SC55_DEVICE_LA)
		// Single-file build: one ROM set per LA device (location 0 = MT-32 old, 1 = MT-32 new,
		// 2 = CM-32L), each as written by p256_builder.la_blob(): "NKLA1" header, version text,
		// sizes, then control, wave and reverb. Devices without a set stay unavailable.
		for (int k = 0; k < g_embedded_rom_count; ++k) {
			const auto& r = g_embedded_roms[k];
			const size_t n = static_cast<size_t>(r.end - r.begin);
			if (r.location >= kLaDevices || n < 40 || std::memcmp(r.begin, "NKLA1\0\0\0", 8) != 0) continue;
			uint32_t sz[3];
			std::memcpy(sz, r.begin + 24, sizeof(sz));
			if (40ull + sz[0] + sz[1] + sz[2] > n) continue;
			const uint8_t* ctl = r.begin + 40;
			if (emu88_set_la_rom_images(kLaDeviceIds[r.location], ctl, sz[0], ctl + sz[0], sz[1],
			                            ctl + sz[0] + sz[1], sz[2]) != EMU88_RC_OK) {
				log("88emu: embedded LA ROM set %d invalid", r.location);
				continue;
			}
			la_have[r.location] = true;
			std::memcpy(la_version[r.location], r.begin + 8, 15);
		}
		if (!la_have[0] && !la_have[1] && !la_have[2]) {
			instances.clear();
			return false;
		}
#elif defined(NUKED_SC55_EMBED_ROMS)
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
#elif defined(NUKED_SC55_DEVICE_8820)
		if (emu88_set_sc8820_rom_images(rp[0], rn[0], rp[1], rn[1], rp[2], rn[2], rp[3], rn[3]) !=
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
			emu88_add_rom_path((base / "SC-8820").string().c_str());
			emu88_add_rom_path((base / "MT-32").string().c_str());
			emu88_add_rom_path(base.string().c_str());
		}
#ifdef NUKED_SC55_DEVICE_LA
		for (int d = 0; d < kLaDevices; ++d) {
			la_have[d] = emu88_is_device_available(kLaDeviceIds[d]) != 0;
			std::snprintf(la_version[d], sizeof(la_version[d]), "%s", la_have[d] ? "?" : "");
		}
#endif
#endif
	}
	emu_ok     = true;
	max_voices = MaxSelectableVoices();
#ifdef NUKED_SC55_DEVICE_LA
	max_voices = partials_per_instance; // default 32 voices = one unit, like the device
#endif
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
		KeepPowerOnState(opts);

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
		KeepPowerOnState(opts);

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

// The emulator's Reset() only resets the CPUs' registers: RAM, the cycle counters (the main CPU
// keeps counting, the sub-CPU starts again from 0), UART state and the PCM chip would stay from the
// previous run. A re-activated plugin then boots its units from that state, and the SC-55mk2
// firmware could crash shortly after. A copy of the state right after loading the ROMs lets every
// boot start like a unit that was just switched on.
void NukedSc55::KeepPowerOnState(const EMU_Options& opts)
{
	power_on = std::make_unique<Emulator>();
	if (!power_on->Init(opts)) {
		power_on.reset();
		return;
	}
	power_on->CopyStateFrom(*instances[0].emu);
}

void NukedSc55::Shutdown()
{
#ifdef _WIN32
	if (editor) {
		editor::Destroy(editor);
		editor = nullptr;
	}
#endif
	std::lock_guard units_lock(units_mx);
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
	std::lock_guard units_lock(units_mx); // the GUI does not read units while they are (re)booted
	lcd_ready = false;
	StopWarmer(); // no background access while (re)booting
	disp_pace = get_env_var("NUKED_SC55_NO_DISPPACE") != "1";
	disp_pending.clear();
	disp_pending.reserve(kDispMax);

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
#ifdef NUKED_SC55_DEVICE_LA
	{   // the device chosen in the SETUP menu; without its ROM set the first one that is there
		last_sample_rate = requested_sample_rate;
		last_max_frames  = max_frame_count;
		int dev = std::clamp(la_device.load(), 0, kLaDevices - 1);
		for (const int d : {dev, 2, 1, 0})
			if (la_have[d]) { dev = d; break; }
		la_device = dev;
		la_active = dev;
		applied_buttons = 0;
	}
#endif
#ifdef NUKED_SC55_EMBED_ROMS
	// (only units that sleep may boot in the background: awake ones are rendered right away)
	const bool background_boot = get_env_var("NUKED_SC55_NO_BGBOOT") != "1" && dynamic_instances;
#else
	const bool background_boot = false; // ROM scan from files is not thread-safe
#endif
	if (background_boot) {
		// The sleeping units start booting at the end of Activate(), once they have been put to
		// sleep: from then on only their boot thread touches them until `booted` is set (issue #63).
		for (int i = 0; i < min_active; ++i) { // the units that stay awake: now
			if (!E88BootUnit(i)) return false;
			instances[i].booted->store(true, std::memory_order_release);
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
		if (power_on) inst.emu->CopyStateFrom(*power_on); // re-activation: as after power-on (issue #63)
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
		inst.uart_wait.clear();
		inst.uart_wait_pos = 0;
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
			inst.uart_wait.clear();
			inst.uart_wait_pos = 0;
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
	router.SetTrackSystemMode(HasSystemMode());
	state_log.SetTrackSystemMode(HasSystemMode());
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
		instances[i].warming->store(0);
		if (dynamic_instances && i >= min_active) {
			Sleep(i);
		}
	}
	sleeps = 0;
	render_frame_count = 0;
	held_events.clear();
	script_steps.clear();
	hold_until = 0;
	warmer_next_frame  = 0;
#ifdef NUKED_SC55_DEVICE_8850
	dot_pages = {};
	dot_page  = 0;
	DotShow(false);
#endif
#ifdef NUKED_SC55_ENGINE_88PRO
	if (background_boot) {
		for (int i = min_active; i < NumInstances(); ++i) {
			boot_threads.emplace_back([this, i] {
				E88BootUnit(i);
				instances[i].booted->store(true, std::memory_order_release);
			});
		}
	}
#endif
	if (dynamic_instances) StartWarmer();
	ui_awake_mask = 1; // until the first block: the units of the last activation are gone
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

	if (resampler) { // re-activation: a new resampler for the new rate, no leftovers of the old stream
		speex_resampler_destroy(resampler);
		resampler = nullptr;
	}
	render_buf[0].clear();
	render_buf[1].clear();
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

#ifdef NUKED_SC55_DEVICE_LA
	if (la_device.load() != la_active.load()) LaSwitchDevice(); // SETUP menu "Device"
#endif
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
#ifndef NUKED_SC55_ENGINE_88PRO
	// Front panel of unit 0: edits of the last block passed on, MUTE flags, GUI input
	E55PanelPump();
	E55PanelInput();
#endif
#ifdef NUKED_SC55_DEVICE_8850
	// Front panel of unit 0: GUI input, edits passed on to the other units, MUTE / SOLO
	E88PanelInput();
	E88SyncPump(instances[0], true, render_frame_count);
	E88PanelMutes();
#endif
#ifdef NUKED_SC55_DEVICE_88
	// Front panel of unit 0: edits of the last block passed on, GUI input, ALL + MUTE
	E88EditPump();
	E88PanelTune();
	E88PanelInput();
	E88PanelMutes();
#endif
#ifdef NUKED_SC55_DEVICE_8820
	// Front panel of unit 0: INST MAP / PREVIEW from the GUI; the map shown is adopted for all units
	E8820PanelInput();
	// Pro interface: edits from the part display / buttons to all units, part parameters for the display
	E8820SendUiMidi();
	if (ui_style.load(std::memory_order_relaxed) != 0) E8820ReadParts(); // Pro and SC-D70 show part data
#endif
#ifdef NUKED_SC55_DEVICE_LA
	// MT-32 switches and SELECT from the GUI: pressed on all units alike (they stay identical)
	LaPanelInput();
#endif
#ifdef NUKED_SC55_DEVICE_88PRO
	// Front panel of unit 0: edits of the last block passed on, GUI input
	E88EditPump();
	E88PanelTune();
	E88PanelInput();
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
			int part = ui_preview_part.load();
#ifdef NUKED_SC55_DEVICE_8850
			if (part < 0 && !instances.empty() && instances[0].ctx) { // part shown on unit 0's panel
				uint8_t cur = 0;
				emu88_peek_work_ram(instances[0].ctx, kRam8850CurPart, &cur, 1);
				part = cur;
			}
#endif
			part         = std::clamp(part, 0, kNumParts - 1); // SC-8820: part A01
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
#ifdef NUKED_SC55_DEVICE_8850
		// SC-8850: the map is a per-part setting (Bank Select LSB, 1 = SC-55, 2 = SC-88, 3 = SC-88 Pro,
		// 0 = SC-8850): send it for every part with the tracked bank / program
		if (!instances.empty() && instances[0].applied_map != map) {
			// (sleeping units get the map with the replay; one may be synced in the background now)
			for (auto& inst : instances)
				if (inst.awake) inst.applied_map = map;
			const uint8_t lsb = static_cast<uint8_t>((map + 1) & 3);
			for (int part = 0; part < kNumParts; ++part) {
				const auto& up = ui_parts[part];
				const uint8_t c = static_cast<uint8_t>(part % 16);
				const uint16_t port = static_cast<uint16_t>(part / 16);
				InjectShort(static_cast<uint8_t>(0xB0 | c), 0, up.bank.load(), port);
				InjectShort(static_cast<uint8_t>(0xB0 | c), 32, lsb, port);
				InjectShort(static_cast<uint8_t>(0xC0 | c), up.prog.load(), 0, port);
			}
		}
#endif
		for (auto& inst : instances) {
			if (inst.awake && inst.ctx && inst.seq_len == 0 && inst.applied_map != map)
				E88StartMapSequence(inst, map);
		}
	}
#endif

	// Events of this block: held events that are due, then the host's (held back
	// themselves while a reset from the SETUP menu is running or older ones wait)
	block_events.clear();
	held_events.reserve(held_events.size() + num_events); // no reallocation below (pointers)
	size_t released = 0;
	for (; released < held_events.size(); ++released) {
		auto& h = held_events[released];
		if (h.frame >= render_frame_count + num_render_frames) break;
		if (h.midi.header.type == CLAP_EVENT_MIDI_SYSEX) {
			h.sysex.buffer = h.bytes.data();
			block_events.push_back({&h.sysex.header, static_cast<uint32_t>(h.frame - render_frame_count)});
		} else {
			block_events.push_back({&h.midi.header, static_cast<uint32_t>(h.frame - render_frame_count)});
		}
	}
	for (uint32_t i = 0; i < num_events; ++i) {
		const auto event = process->in_events->get(process->in_events, i);
		auto frame = static_cast<uint32_t>(
		        static_cast<double>(event->time) * resample_ratio);
		if (num_render_frames > 0) {
			frame = std::min(frame, num_render_frames - 1);
		}
		const bool midi = event->space_id == CLAP_CORE_EVENT_SPACE_ID &&
		                  (event->type == CLAP_EVENT_MIDI || event->type == CLAP_EVENT_MIDI_SYSEX);
		if (midi && (released < held_events.size() || render_frame_count + frame < hold_until)) {
			HeldEvent h{std::max(render_frame_count + frame, hold_until), {}, {}, {}};
			if (event->type == CLAP_EVENT_MIDI_SYSEX) {
				h.sysex = *reinterpret_cast<const clap_event_midi_sysex_t*>(event);
				h.bytes.assign(h.sysex.buffer, h.sysex.buffer + h.sysex.size);
				h.midi.header.type = CLAP_EVENT_MIDI_SYSEX; // marker
			} else {
				h.midi = *reinterpret_cast<const clap_event_midi_t*>(event);
			}
			held_events.push_back(std::move(h));
			continue;
		}
		block_events.push_back({event, frame});
	}

	if (dynamic_instances) {
		int note_ons = 0;
		for (const auto& [ev, frame] : block_events) {
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
	for (const auto& [event, frame] : block_events) {
		ProcessEvent(event, frame);
	}
	held_events.erase(held_events.begin(), held_events.begin() + static_cast<std::ptrdiff_t>(released));

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
			voices += SoundingPartials(i);
		}
		ui_voices.store(voices, std::memory_order_relaxed);
		uint64_t mask = 0;
		for (int i = 0; i < NumInstances(); ++i)
			if (instances[i].awake) mask |= uint64_t{1} << i;
		ui_awake_mask.store(mask, std::memory_order_relaxed);
#ifdef NUKED_SC55_DEVICE_LA
		ui_awake.store(std::min(NumAwake(), router.Allowed()), std::memory_order_relaxed); // all run; shown: in use
#else
		ui_awake.store(NumAwake(), std::memory_order_relaxed);
#endif
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

	{   // GAIN knob: analog-style output level after the emulation, ramped over the block
#ifdef NUKED_SC55_ENGINE_88PRO
		const float target = GainFactor(gain_db.load(std::memory_order_relaxed));
#else
		// SC-55: ALL + MUTE on the panel silences the whole module (unit 0 mutes itself)
		const float target = all_mute55 ? 0.0f : GainFactor(gain_db.load(std::memory_order_relaxed));
#endif
		if (applied_gain < 0.0f) applied_gain = target; // first block: no ramp
		const float step = (target - applied_gain) / static_cast<float>(std::max<uint32_t>(1, num_frames));
		for (uint32_t i = 0; i < num_frames; ++i) {
			const float g = (step == 0.0f) ? target : applied_gain + step * static_cast<float>(i + 1);
			out_left[i] *= g;
			out_right[i] *= g;
		}
		applied_gain = target;
	}
#ifdef NUKED_SC55_DEVICE_8820
	{   // SC-D70 panel: output level meters (the GUI takes the value and resets it)
		float pl = 0.0f, pr = 0.0f;
		for (uint32_t i = 0; i < num_frames; ++i) {
			pl = std::max(pl, std::abs(out_left[i]));
			pr = std::max(pr, std::abs(out_right[i]));
		}
		if (pl > ui_peak[0].load(std::memory_order_relaxed)) ui_peak[0].store(pl, std::memory_order_relaxed);
		if (pr > ui_peak[1].load(std::memory_order_relaxed)) ui_peak[1].store(pr, std::memory_order_relaxed);
	}
#endif

	return CLAP_PROCESS_CONTINUE;
}

bool NukedSc55::LoadState([[maybe_unused]] const clap_istream_t* stream)
{
	if (!emu_ok) {
		return false;
	}

	char buf[256] = {};
	size_t got = 0;
	while (got < sizeof(buf) - 1) {
		const int64_t r = stream->read(stream, buf + got, sizeof(buf) - 1 - got);
		if (r <= 0) break;
		got += static_cast<size_t>(r);
	}
	return ParseState(buf);
}

bool NukedSc55::SaveState([[maybe_unused]] const clap_ostream_t* stream)
{
	if (!emu_ok) {
		return 0;
	}

	char buf[kStateSize];
	const int n = StateString(buf, sizeof(buf));
	int64_t done = 0;
	while (done < n) {
		const int64_t w = stream->write(stream, buf + done, static_cast<uint64_t>(n - done));
		if (w <= 0) return false;
		done += w;
	}
	return true;
}

// Settings only (CLAP state and VST2 chunk): "NSC55P1 max_voices=<n> ..."
int NukedSc55::StateString(char* buf, const size_t size) const
{
#if defined(NUKED_SC55_DEVICE_8820)
	// INST MAP (all parts) is the one panel setting; synchronised on all units, kept here
	int n = std::snprintf(buf, size, "NSC55P1 max_voices=%d map=%d gain=%d style=%d", max_voices.load(),
	                      tone_map.load(), static_cast<int>(std::lround(gain_db.load() * 10.0f)), ui_style.load());
#elif defined(NUKED_SC55_DEVICE_8850)
	// INST MAP (all parts) is the one panel setting; synchronised on all units, kept here
	int n = std::snprintf(buf, size, "NSC55P1 max_voices=%d map=%d gain=%d", max_voices.load(),
	                      tone_map.load(), static_cast<int>(std::lround(gain_db.load() * 10.0f)));
#elif defined(NUKED_SC55_DEVICE_LA)
	int n = std::snprintf(buf, size, "NSC55P1 max_voices=%d gain=%d dev=%d", max_voices.load(),
	                      static_cast<int>(std::lround(gain_db.load() * 10.0f)), la_device.load());
#elif defined(NUKED_SC55_DEVICE_88)
	// the tone map for all parts (SETUP menu) is kept here; the panel's own settings live in the firmware
	int n = std::snprintf(buf, size, "NSC55P1 max_voices=%d map=%d gain=%d", max_voices.load(),
	                      tone_map.load(), static_cast<int>(std::lround(gain_db.load() * 10.0f)));
#elif defined(NUKED_SC55_ENGINE_88PRO)
	int n = std::snprintf(buf, size, "NSC55P1 max_voices=%d map=%d gain=%d pnote=%d",
	                      max_voices.load(), tone_map.load(),
	                      static_cast<int>(std::lround(gain_db.load() * 10.0f)), preview_note.load());
#else
	int n = std::snprintf(buf, size, "NSC55P1 max_voices=%d gain=%d", max_voices.load(),
	                      static_cast<int>(std::lround(gain_db.load() * 10.0f)));
#endif
	if (n < 0 || static_cast<size_t>(n) >= size) return static_cast<int>(size > 0 ? size - 1 : 0); // (truncated)
	if (HasSystemMode()) n += std::snprintf(buf + n, size - n, " smode=%d", system_mode.load());
	return std::clamp(n, 0, static_cast<int>(size > 0 ? size - 1 : 0));
}

bool NukedSc55::ParseState(const char* buf)
{
	int v = 0;
	if (std::sscanf(buf, "NSC55P1 max_voices=%d", &v) != 1) return false;
	SetMaxVoices(v);
#ifdef NUKED_SC55_ENGINE_88PRO
	if (const char* m = std::strstr(buf, "map="); (HasToneMap() || kHas8850Map || kHas88Map) && m && m[4] >= '0' &&
	                                                    m[4] <= (kMaxToneMap + '0')) {
		tone_map = m[4] - '0';
	}
	if (const char* m = std::strstr(buf, "gain="); m) {
		gain_db = std::clamp(std::atoi(m + 5) / 10.0f, kGainMinDb, kGainMaxDb);
	} else if (const char* v = std::strstr(buf, "vol="); v) {
		gain_db = GainFromLegacyVolume(std::atoi(v + 4));
	}
	if (const char* m = std::strstr(buf, "pnote="); m) preview_note = std::clamp(std::atoi(m + 6), 0, 127);
#ifdef NUKED_SC55_DEVICE_8820
	if (const char* m = std::strstr(buf, "style="); m) ui_style = std::clamp(std::atoi(m + 6), 1, kNumStyles8820 - 1); // 0 (Basic, issue #87) -> SC-8820
#endif
#ifdef NUKED_SC55_DEVICE_LA
	// device: switched (units booted again) at the next block; without its ROMs the default applies
	if (const char* m = std::strstr(buf, "dev="); m && m[4] >= '0' && m[4] < '0' + kLaDevices && la_have[m[4] - '0'])
		la_device = m[4] - '0';
#endif
#else
	if (const char* m = std::strstr(buf, "gain="); m) // older states: no GAIN = 0 dB
		gain_db = std::clamp(std::atoi(m + 5) / 10.0f, kGainMinDb, kGainMaxDb);
#endif
	// SETUP menu marks only; loading sends nothing (start-up stays as it is)
	if (const char* m = std::strstr(buf, "smode="); HasSystemMode() && m && m[6] >= '0' && m[6] <= '1')
		system_mode = m[6] - '0';
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
#ifdef NUKED_SC55_DEVICE_8820
			if (!injecting) ui_midi_in.fetch_add(1, std::memory_order_relaxed);
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
			if (disp_pace && IsDisplaySysEx(msg)) {
#ifdef NUKED_SC55_DEVICE_8850
				DotSysEx(msg);
#endif
				if (DispReady()) // nothing waiting: straight to unit 0, sample-accurate
					QueueMidi(PolyRouter::Mask{1}, msg.data(), msg.size(), render_frame, port);
				else
					DispQueue(msg, port);
				break;
			}
#ifdef NUKED_SC55_DEVICE_8850
			host_change[kNumParts] = render_frame_count + render_frame + 1;
#endif
#ifdef NUKED_SC55_DEVICE_8820
			if (!injecting) ui_midi_in.fetch_add(1, std::memory_order_relaxed);
			TrackGsGm(msg);
#endif
			state_log.AddSysEx(msg, port);
			{
				const bool reset = StateLog::IsReset(msg);
				if (reset) UiResetParts();
				if (reset) disp_pending.clear(); // pictures sent before a reset are void
				if (const int mode = StateLog::SystemModeOf(msg); mode >= 0 && HasSystemMode())
					system_mode = mode; // System Mode Set from the host: menu mark follows
#ifdef NUKED_SC55_DEVICE_8850
				if (reset) DotShow(false);
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

#ifndef NUKED_SC55_ENGINE_88PRO
//----------------------------------------------------------------------------
// SC-55 / SC-55mk2 front panel (unit 0)
//----------------------------------------------------------------------------
namespace {
// Parameter memory of the SC-55 v1.21 and SC-55mk2 v1.01 firmware (SRAM, the same layout in
// both, found by comparing it before and after panel edits and GS messages): system
// parameters at the start, GS part block x (40 1x ..) at 0x40 + 0x70 x.
constexpr int kSram55Part = 0x40, kSram55PartSize = 0x70;
struct Sys55 { uint8_t off, a1, a2; }; // offset -> GS 40 a1 a2
constexpr Sys55 kSys55[] = {
	{0x02, 0x00, 0x04}, // MASTER VOLUME (ALL + LEVEL)
	{0x05, 0x00, 0x05}, // MASTER KEY SHIFT (ALL + KEY SHIFT)
	{0x06, 0x00, 0x06}, // MASTER PAN (ALL + PAN)
	{0x2a, 0x01, 0x30}, // REVERB MACRO (system menu), sets the reverb parameters below
	{0x2b, 0x01, 0x31}, {0x2c, 0x01, 0x32}, {0x2d, 0x01, 0x33}, // REVERB CHARACTER, PRE-LPF, LEVEL (ALL + REVERB)
	{0x2e, 0x01, 0x34}, {0x2f, 0x01, 0x35},                     // REVERB TIME, DELAY FEEDBACK
	{0x32, 0x01, 0x38}, // CHORUS MACRO (system menu), sets the chorus parameters below
	{0x33, 0x01, 0x39}, {0x34, 0x01, 0x3a}, {0x35, 0x01, 0x3b}, // CHORUS PRE-LPF, LEVEL (ALL + CHORUS), FEEDBACK
	{0x36, 0x01, 0x3c}, {0x37, 0x01, 0x3d}, {0x38, 0x01, 0x3e}, // CHORUS DELAY, RATE, DEPTH
	{0x39, 0x01, 0x3f},                                         // CHORUS SEND LEVEL TO REVERB
};
constexpr int kSys55Tune = 0x00;    // MASTER TUNE (system menu): 16 bit, the value of 40 00 00..03 (0x400 = 440 Hz)
constexpr int kSys55Reserve = 0x18; // PARTIAL RESERVE of GS blocks 0..15 (part menu) = 40 01 10..1F
constexpr uint8_t kPart55[][2] = { // offset in the block -> GS 40 1x a2
	{0x0c, 0x02}, // Rx. CHANNEL (MIDI CH)
	{0x0e, 0x16}, // PITCH KEY SHIFT
	{0x10, 0x19}, // PART LEVEL
	{0x11, 0x1c}, // PART PANPOT
	{0x12, 0x1a}, {0x13, 0x1b}, // VELOCITY SENSE DEPTH, OFFSET (part menu)
	{0x14, 0x1d}, {0x15, 0x1e}, // KEY RANGE LOW, HIGH (part menu)
	{0x16, 0x21}, // CHORUS SEND
	{0x17, 0x22}, // REVERB SEND
	{0x18, 0x30}, {0x19, 0x31}, {0x1a, 0x32}, {0x1b, 0x33}, // VIBRATO RATE, DEPTH, TVF CUTOFF, RESONANCE (part menu)
	{0x1c, 0x34}, {0x1d, 0x35}, {0x1e, 0x36}, {0x1f, 0x37}, // ATTACK, DECAY, RELEASE, VIBRATO DELAY (part menu)
};
constexpr uint8_t kCtrl55[][2] = { // offset in the block -> GS 40 2x a2 (part menu)
	{0x35, 0x04}, // MOD LFO1 PITCH DEPTH (Mod. Depth)
	{0x3c, 0x10}, // BEND PITCH CONTROL (Bend Range)
};
// Offset 0x0d of the block: bit 7 = POLY (40 1x 13), bits 0-1 = ASSIGN MODE (14),
// bits 5-6 = USE FOR RHYTHM PART (15; bit 4 is set with it) (Part Mode, M/P Mode)
constexpr int kPart55Mode = 0x0d;
constexpr int kPart55Fine = 0x0f; // FINE TUNE, 0x80 = 0 (copied, see E55ApplyRxSwitches)
// Offsets 0x0a / 0x0b of the block: Rx switches of the part (bit -> GS 40 1x a2), 0 = off.
// Bit 1 of 0x0a (Rx NOTE MESSAGE) is the part MUTE, passed on separately.
constexpr uint8_t kRx55A[8] = {0x12, 0x00, 0x07, 0x06, 0x05, 0x04, 0x03, 0x0a};
constexpr uint8_t kRx55B[8] = {0x09, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11};
constexpr int kPart55Bank = 0x08, kPart55Prog = 0x09; // INSTRUMENT: sent as CC 0 + program change
constexpr int kPart55Flags = 0x0a;                    // bit 1 clear = part muted (MUTE)
constexpr uint8_t kMute55Bit = 0x02;
constexpr uint8_t kLamp55All = 0x40, kLamp55Mute = 0x20; // panel port bits, active low
constexpr double kPanel55Seconds = 0.6; // edits are passed on until this long after the last switch
constexpr uint32_t kBtn55MidiCh = (1u << 8) | (1u << 9);
} // namespace

// System menu Rx switches without a GS parameter (Rx Inst Change, Rx GS Reset, Rx SysEx;
// mk2 also Rx GM On and Rx Function Control): their place in the parameter memory, found
// per firmware by toggling them in the menu. They are copied from unit 0 to the other units,
// as is the part FINE TUNE of the mk2 part menu (its stored form has no simple GS value).
std::span<const uint16_t> NukedSc55::E55RxSwitches() const
{
	static constexpr uint16_t v100[] = {0x4df4, 0x4df5, 0x4df6};
	static constexpr uint16_t v110[] = {0x4df6, 0x4df7, 0x4df8}; // also v1.20, v1.21
	static constexpr uint16_t v200[] = {0x4e66, 0x4e67, 0x4e68};
	static constexpr uint16_t mk2[]  = {0x5c2c, 0x5c2d, 0x5c2e, 0x5c9d, 0x5c9e};
	switch (model) {
	case Model::Sc55_v1_00: return v100;
	case Model::Sc55_v1_10:
	case Model::Sc55_v1_20:
	case Model::Sc55_v1_21: return v110;
	case Model::Sc55_v2_00: return v200;
	case Model::Sc55mk2_v1_01: return mk2;
	default: return {};
	}
}

void NukedSc55::E55ApplyRxSwitches(Instance& inst)
{
	if (!inst.emu || &inst == &instances[0] || !instances[0].emu) return;
	const uint8_t* src = instances[0].emu->GetMCU().sram;
	uint8_t* dst       = inst.emu->GetMCU().sram;
	for (const uint16_t a : E55RxSwitches()) dst[a] = src[a];
	for (int x = 0; x < 16; ++x) {
		const int a = kSram55Part + kSram55PartSize * x + kPart55Fine;
		dst[a] = src[a];
	}
}

// Audio thread, start of a block: GUI switches to unit 0, lamps back to the GUI
void NukedSc55::E55PanelInput()
{
	if (instances.empty() || !instances[0].emu) return;
	auto& mcu = instances[0].emu->GetMCU();
	uint32_t buttons = ui_panel_buttons.load(std::memory_order_relaxed);
	// ALL + MIDI CH changes the device ID, which has no GS parameter: unit 0 would then ignore
	// the host's GS messages and the other units not. Not given to the firmware, as on the SC-88.
	if (ui_panel_leds.load(std::memory_order_relaxed) & 1u) buttons &= ~kBtn55MidiCh;
	if (buttons != 0 || buttons != applied_buttons) {
		panel_active_until = render_frame_count + 1 +
		                     static_cast<uint64_t>(render_sample_rate_hz * kPanel55Seconds);
	}
	if (buttons != applied_buttons || buttons != 0) mcu.button_pressed = buttons | panel_button_bits;
	applied_buttons = buttons;
	// The firmware ORs the ALL / MUTE lamps into every panel column write (mk1: gate array
	// address, mk2: sub-CPU port 0). In ALL mode the MUTE lamp shows ALL + MUTE.
	const uint8_t port = mcu.is_mk1 ? mcu.io_sd : mcu.p0_data;
	const bool all = !(port & kLamp55All), mute = !(port & kLamp55Mute);
	if (all) all_mute55 = mute;
	ui_panel_leds.store((all ? 1u : 0u) | (mute ? 2u : 0u), std::memory_order_relaxed);
}

// Audio thread, start of a block: what unit 0's firmware changed during the last block (after
// panel input) goes to the other units and the state log; part MUTE flags to the other units.
void NukedSc55::E55PanelPump()
{
	if (instances.empty() || !instances[0].emu) return;
	const uint8_t* sram = instances[0].emu->GetMCU().sram;
	if (!panel_seen_valid) {
		std::memcpy(panel_seen.data(), sram, panel_seen.size());
		panel_seen_valid = true;
	}
	const auto others = ~PolyRouter::Mask{1};
	const auto sysex = [&](uint8_t a1, uint8_t a2, const uint8_t* d, int n) { // GS DT1 40 a1 a2 d[0..n-1]
		uint8_t m[32] = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, a1, a2};
		int k = 8, sum = 0x40 + a1 + a2;
		for (int i = 0; i < n; ++i) sum += (m[k++] = d[i] & 0x7f);
		m[k++] = static_cast<uint8_t>((128 - sum % 128) % 128);
		m[k++] = 0xf7;
		const std::span<const uint8_t> msg{m, static_cast<size_t>(k)};
		state_log.AddSysEx(msg, 0);
		QueueMidi(router.RouteSysEx(msg, 0) & others, m, static_cast<size_t>(k), 0, 0);
	};
	const auto dt1 = [&](uint8_t a1, uint8_t a2, uint8_t v) { sysex(a1, a2, &v, 1); };
	const auto shrt = [&](uint8_t s, uint8_t d1, uint8_t d2, size_t len) {
		const uint8_t m[3] = {s, d1, d2};
		state_log.AddShort(m, 0);
		UiTrackShort(m, 0);
		QueueMidi(router.RouteShort(m, 0) & others, m, len, 0, 0);
	};
	const auto changed = [&](int off, int n) {
		return std::memcmp(sram + off, panel_seen.data() + off, static_cast<size_t>(n)) != 0;
	};
	if (render_frame_count < panel_active_until) {
		if (changed(kSys55Tune, 2)) {
			const int tune = sram[kSys55Tune] << 8 | sram[kSys55Tune + 1];
			const uint8_t d[4] = {static_cast<uint8_t>(tune >> 12 & 15), static_cast<uint8_t>(tune >> 8 & 15),
			                      static_cast<uint8_t>(tune >> 4 & 15), static_cast<uint8_t>(tune & 15)};
			sysex(0x00, 0x00, d, 4);
		}
		for (const auto& p : kSys55)
			if (sram[p.off] != panel_seen[p.off]) dt1(p.a1, p.a2, sram[p.off]);
		if (changed(kSys55Reserve, 16)) sysex(0x01, 0x10, sram + kSys55Reserve, 16);
		bool raw = false;
		for (int x = 0; x < 16; ++x) raw |= changed(kSram55Part + kSram55PartSize * x + kPart55Fine, 1);
		const auto rxs = E55RxSwitches();
		for (size_t k = 0; k <= rxs.size(); ++k) {
			if (k < rxs.size() ? sram[rxs[k]] == rx_seen55[k] : !raw) continue;
			for (int i = 1; i < NumInstances(); ++i)
				if (instances[i].awake) E55ApplyRxSwitches(instances[i]);
			break;
		}
		for (int x = 0; x < 16; ++x) {
			const int b = kSram55Part + kSram55PartSize * x;
			for (const auto& p : kPart55)
				if (sram[b + p[0]] != panel_seen[b + p[0]])
					dt1(static_cast<uint8_t>(0x10 | x), p[1], sram[b + p[0]]);
			for (const auto& p : kCtrl55)
				if (sram[b + p[0]] != panel_seen[b + p[0]])
					dt1(static_cast<uint8_t>(0x20 | x), p[1], sram[b + p[0]]);
			const uint8_t m = sram[b + kPart55Mode], was = panel_seen[b + kPart55Mode];
			if ((m ^ was) & 0x80) dt1(static_cast<uint8_t>(0x10 | x), 0x13, m >> 7);
			if ((m ^ was) & 0x03) dt1(static_cast<uint8_t>(0x10 | x), 0x14, m & 3);
			if ((m ^ was) & 0x60) dt1(static_cast<uint8_t>(0x10 | x), 0x15, m >> 5 & 3);
			for (int k = 0; k < 16; ++k) {
				const int off = k < 8 ? 0x0a : 0x0b;
				const uint8_t a2 = k < 8 ? kRx55A[k] : kRx55B[k - 8], bit = uint8_t(1u << (k & 7));
				if (a2 != 0 && ((sram[b + off] ^ panel_seen[b + off]) & bit))
					dt1(static_cast<uint8_t>(0x10 | x), a2, (sram[b + off] & bit) ? 1 : 0);
			}
			if (sram[b + kPart55Bank] != panel_seen[b + kPart55Bank] ||
			    sram[b + kPart55Prog] != panel_seen[b + kPart55Prog]) {
				const uint8_t ch = sram[b + 0x0c];
				if (ch < 16) {
					shrt(static_cast<uint8_t>(0xb0 | ch), 0, sram[b + kPart55Bank] & 0x7f, 3);
					shrt(static_cast<uint8_t>(0xc0 | ch), sram[b + kPart55Prog] & 0x7f, 0, 2);
				}
			}
		}
	}
	std::memcpy(panel_seen.data(), sram, panel_seen.size());
	{
		const auto rxs = E55RxSwitches();
		for (size_t k = 0; k < rxs.size() && k < rx_seen55.size(); ++k) rx_seen55[k] = sram[rxs[k]];
	}

	uint16_t rx = 0; // bit x set = GS block x not muted
	for (int x = 0; x < 16; ++x)
		if (sram[kSram55Part + kSram55PartSize * x + kPart55Flags] & kMute55Bit) rx |= uint16_t(1u << x);
	if (rx != applied_mute55) {
		for (int i = 1; i < NumInstances(); ++i)
			if (instances[i].awake) E55ApplyMutes(instances[i], rx);
		applied_mute55 = rx;
	}
}

void NukedSc55::E55ApplyMutes(Instance& inst, const uint16_t rx)
{
	if (!inst.emu) return;
	uint8_t* sram = inst.emu->GetMCU().sram;
	for (int x = 0; x < 16; ++x) {
		uint8_t& f = sram[kSram55Part + kSram55PartSize * x + kPart55Flags];
		f = (rx & (1u << x)) ? (f | kMute55Bit) : (f & ~kMute55Bit);
	}
}
#endif

#ifdef NUKED_SC55_ENGINE_88PRO
int NukedSc55::UnitToneMap(const int i) const
{
	// tracked tone map of the unit (the panel LEDs do not show it reliably)
	return instances[i].ctx ? instances[i].applied_map : -1;
}
#endif

bool NukedSc55::GetLcd(const int unit, LcdSnapshot& out)
{
	std::unique_lock lock(units_mx, std::try_to_lock);
	if (!lock.owns_lock() || !lcd_ready.load() || unit < 0 || unit >= NumInstances()) return false;
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
	// text as presented: rows settle per firmware refresh (issue #36, see lcd_t::LCD_Shown)
	std::memcpy(out.dd, lcd.LCD_Refreshes ? lcd.LCD_Shown : lcd.LCD_Data, sizeof(out.dd));
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

// Partials that count as load. After an abrupt stop (All Sound Off) the SC-55
// firmware can leave partials parked at a tiny non-zero TVA level that never
// decays; an instance that has been silent for the whole sleep period has no
// audible voice left, so those must not block sleeping or steer routing.
int NukedSc55::SoundingPartials(const int i) const
{
	const auto& inst = instances[i];
	if (!inst.awake || inst.silent_frames >= sleep_after_frames) return 0;
	return ActivePartials(i);
}

void NukedSc55::MeasureLoad()
{
	for (int i = 0; i < NumInstances(); ++i) {
		router.SetMeasuredLoad(i, SoundingPartials(i));
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
	for (const auto& ev : inst.queue) {
		while (available() < ev.frame) {
			StepUnit(inst);
		}
		PostToUnit(inst, std::span{midi_bytes.data() + ev.offset, ev.size});
	}
	inst.queue.clear();

	while (available() < num_frames) {
		StepUnit(inst);
	}
}

// The SC-55 core's MIDI receive buffer is a ring of uart_buffer_size bytes that MCU_PostUART
// overwrites without a check: more than 8 KiB at once (a large SysEx block from the host, or a
// long replay when a unit wakes up) used to lose everything before the last 8 KiB silently.
// Bytes that do not fit now wait here and are handed on as the firmware takes bytes in, like
// MIDI arriving over the cable. With room to spare (the normal case) nothing changes.
static size_t UartFree(const mcu_t& mcu)
{
	const uint32_t used = (mcu.uart_write_ptr + uart_buffer_size - mcu.uart_read_ptr) % uart_buffer_size;
	return uart_buffer_size - 1 - used;
}

void NukedSc55::PostToUnit(Instance& inst, std::span<const uint8_t> data)
{
	if (inst.uart_wait.empty()) {
		const size_t n = std::min(UartFree(inst.emu->GetMCU()), data.size());
		inst.emu->PostMIDI(data.first(n));
		data = data.subspan(n);
		if (data.empty()) return;
		inst.uart_wait_pos = 0;
	}
	inst.uart_wait.insert(inst.uart_wait.end(), data.begin(), data.end());
}

void NukedSc55::FeedUart(Instance& inst)
{
	const size_t n = std::min(UartFree(inst.emu->GetMCU()), inst.uart_wait.size() - inst.uart_wait_pos);
	if (n == 0) return;
	inst.emu->PostMIDI(std::span{inst.uart_wait.data() + inst.uart_wait_pos, n});
	inst.uart_wait_pos += n;
	if (inst.uart_wait_pos == inst.uart_wait.size()) {
		inst.uart_wait.clear();
		inst.uart_wait_pos = 0;
	}
}

void NukedSc55::StepUnit(Instance& inst)
{
	MCU_Step(inst.emu->GetMCU());
	if (!inst.uart_wait.empty()) FeedUart(inst);
}

// Steps until the MIDI input is consumed. A unit whose firmware stopped reading the UART must not
// hold the audio thread forever: after a generous budget (far above any legitimate backlog, which
// is about 13 million steps for 10 KB) the rest is dropped.
void NukedSc55::DrainUnit(Instance& inst)
{
	constexpr uint64_t max_steps = 100'000'000;
	for (uint64_t n = 0; !UnitDrained(inst); ++n) {
		if (n >= max_steps) {
			auto& mcu = inst.emu->GetMCU();
			std::fprintf(stderr, "NukedSc55: MIDI input not consumed (%zu waiting, ring %u..%u), dropped\n",
			             inst.uart_wait.size() - inst.uart_wait_pos, unsigned(mcu.uart_read_ptr), unsigned(mcu.uart_write_ptr));
			inst.uart_wait.clear();
			inst.uart_wait_pos = 0;
			mcu.uart_read_ptr = mcu.uart_write_ptr;
			return;
		}
		StepUnit(inst);
	}
}

bool NukedSc55::UnitDrained(const Instance& inst)
{
	const auto& mcu = inst.emu->GetMCU();
	return mcu.uart_read_ptr == mcu.uart_write_ptr && inst.uart_wait.empty();
}

void NukedSc55::RenderAudio(const uint32_t num_frames)
{
	log("RenderAudio: num_frames: %d", num_frames);

	DispFeed();
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

	// Silence detection (for putting idle instances to sleep). Measured as
	// the peak-to-peak span since the silent run began, so any constant
	// output offset (unit 0 keeps its idle DC, and the idle level after
	// playing can differ from the one measured at boot) can't hide silence.
	for (auto& inst : instances) {
		if (!inst.awake || num_frames == 0) continue; // (an empty block has no sample to look at)
		float lo_l = inst.buf_l[inst.read_pos], hi_l = lo_l;
		float lo_r = inst.buf_r[inst.read_pos], hi_r = lo_r;
		float peak = 0.0f;
		for (uint32_t f = 0; f < num_frames; ++f) {
			const float l = inst.buf_l[inst.read_pos + f];
			const float r = inst.buf_r[inst.read_pos + f];
			lo_l = std::min(lo_l, l); hi_l = std::max(hi_l, l);
			lo_r = std::min(lo_r, r); hi_r = std::max(hi_r, r);
			peak = std::max(peak, std::max(std::abs(l - inst.dc_l), std::abs(r - inst.dc_r)));
		}
		inst.last_peak = peak;
		if (inst.silent_frames == 0) {
			inst.sil_lo_l = lo_l; inst.sil_hi_l = hi_l;
			inst.sil_lo_r = lo_r; inst.sil_hi_r = hi_r;
		} else {
			inst.sil_lo_l = std::min(inst.sil_lo_l, lo_l); inst.sil_hi_l = std::max(inst.sil_hi_l, hi_l);
			inst.sil_lo_r = std::min(inst.sil_lo_r, lo_r); inst.sil_hi_r = std::max(inst.sil_hi_r, hi_r);
		}
		// "Silent": span below four output LSBs of the SC-55 (~-66 dBFS
		// peak-to-peak); the mk1 output keeps toggling by one LSB even
		// without any sounding voice.
		const bool quiet = inst.sil_hi_l - inst.sil_lo_l < 5e-4f &&
		                   inst.sil_hi_r - inst.sil_lo_r < 5e-4f;
		if (quiet) {
			inst.silent_frames += num_frames;
		} else {
			// restart the run with this block alone
			inst.silent_frames = (hi_l - lo_l < 5e-4f && hi_r - lo_r < 5e-4f) ? num_frames : 0;
			inst.sil_lo_l = lo_l; inst.sil_hi_l = hi_l;
			inst.sil_lo_r = lo_r; inst.sil_hi_r = hi_r;
		}
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
#ifdef NUKED_SC55_DEVICE_8850
	for (auto& inst : instances) // a reset returns the parts to the SC-8850 map (sleeping units: with the replay)
		if (inst.awake) inst.applied_map = 3;
#endif
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

#ifdef NUKED_SC55_ENGINE_88PRO
// SC-88 Pro front-panel switches (88emu Sc88ProButton bits)
namespace {
constexpr uint32_t Btn88Map  = 1u << 1; // Sc88ProButton::Sc88Map
constexpr uint32_t Btn55Map  = 1u << 2; // Sc88ProButton::Sc55Map
constexpr uint32_t BtnAll    = 1u << 6; // Sc88ProButton::InstAll
}
#endif

// Falcosoft's "GS MT-32 Mode" (GS_MT32_Mode.syx, 27 DT1 messages): a GS Reset, then the MT-32 / CM-64 tones (bank 127 / 126, drum set 128) and the pitch bend
// range of every part. Used by the SETUP menu entry "MT-32" (a one-time reset like GM / GS).
static const uint8_t kMt32Mode[] = {
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x10, 0x00, 0x00, 0x7f, 0x31, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x11, 0x00, 0x7f, 0x00, 0x30, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x12, 0x00, 0x7f, 0x44, 0x6b, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x13, 0x00, 0x7f, 0x30, 0x7e, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x14, 0x00, 0x7f, 0x5f, 0x4e, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x15, 0x00, 0x7f, 0x4e, 0x5e, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x16, 0x00, 0x7f, 0x29, 0x02, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x17, 0x00, 0x7f, 0x03, 0x27, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x18, 0x00, 0x7f, 0x6e, 0x3b, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x19, 0x00, 0x7f, 0x7a, 0x2e, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x1a, 0x00, 0x7e, 0x00, 0x28, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x1b, 0x00, 0x7e, 0x00, 0x27, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x1c, 0x00, 0x7e, 0x00, 0x26, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x1d, 0x00, 0x7e, 0x00, 0x25, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x1e, 0x00, 0x7e, 0x00, 0x24, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x1f, 0x00, 0x7e, 0x00, 0x23, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x20, 0x10, 0x4c, 0x44, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x21, 0x10, 0x4c, 0x43, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x22, 0x10, 0x4c, 0x42, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x23, 0x10, 0x4c, 0x41, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x24, 0x10, 0x4c, 0x40, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x25, 0x10, 0x4c, 0x3f, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x26, 0x10, 0x4c, 0x3e, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x27, 0x10, 0x4c, 0x3d, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x28, 0x10, 0x4c, 0x3c, 0xf7,
	0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x29, 0x10, 0x4c, 0x3b, 0xf7,
};

// The GS Reset goes out at once, the rest 0.1 s later (as long as the firmware needs for a reset), then
// one message every 2 ms. Each message is logged like one from the host, so units that wake later get
// the same tones, and the host's MIDI waits until the last one is through.
void NukedSc55::SendMt32Mode()
{
	const int ports = state_log.DoubleModule() ? 2 : 1;
	const uint64_t gap   = std::max<uint64_t>(1, static_cast<uint64_t>(render_sample_rate_hz * 0.002));
	const uint64_t start = render_frame_count + static_cast<uint64_t>(render_sample_rate_hz * 0.1);
	constexpr uint64_t kUnplaced = ~uint64_t{0};
	for (uint8_t port = 0; port < ports; ++port) {
		uint64_t frame = start;
		for (size_t i = 0; i < sizeof(kMt32Mode);) {
			size_t end = i;
			while (kMt32Mode[end] != 0xf7) ++end;
			++end;
			const bool reset = (i == 0);
			script_steps.push_back({reset ? render_frame_count : frame, port, {kMt32Mode + i, kMt32Mode + end}});
			if (!reset) frame += gap;
#ifdef NUKED_SC55_ENGINE_88PRO
			// 88emu devices: the part's tone again with Bank LSB 1 (SC-55 map). The SC-88 boots with its
			// own map, which has no bank 127 / 126 (silent without this); on the others the LCD then
			// shows the MT-32 names (Issue #47), the sound stays the same.
			if (const uint8_t* m = kMt32Mode + i; end - i == 12 && m[5] == 0x40 && (m[6] & 0xf0) == 0x10 && m[7] == 0x00) {
				const int x = m[6] & 0x0f;
				const auto ch = static_cast<uint8_t>(x == 0 ? 9 : (x <= 9 ? x - 1 : x));
				script_steps.push_back({kUnplaced, port, {uint8_t(0xb0 | ch), 0x00, m[8]}});
				script_steps.push_back({kUnplaced, port, {uint8_t(0xb0 | ch), 0x20, 0x01}});
				script_steps.push_back({kUnplaced, port, {uint8_t(0xc0 | ch), m[9]}});
			}
#endif
			i = end;
		}
		// the tone messages go after the last SysEx (frames assigned in sending order)
		for (auto& s : script_steps)
			if (s.port == port && s.frame == kUnplaced) { s.frame = frame; frame += gap; }
		std::stable_sort(script_steps.begin(), script_steps.end(),
		                 [](const ScriptStep& a, const ScriptStep& b) { return a.frame < b.frame; });
		hold_until = std::max(hold_until, script_steps.back().frame + gap * 4);
	}
}

// Audio thread, start of a block: the scripted messages that are due
void NukedSc55::ReleaseScriptSteps()
{
	size_t done = 0;
	for (; done < script_steps.size() && script_steps[done].frame <= render_frame_count; ++done) {
		const auto& s = script_steps[done];
		if (s.bytes[0] != 0xf0) { // channel message (bank select, program change)
			state_log.AddShort(s.bytes.data(), s.port);
			UiTrackShort(s.bytes.data(), s.port);
			QueueMidi(router.RouteShort(s.bytes.data(), s.port), s.bytes.data(), s.bytes.size(), 0, s.port);
			continue;
		}
		const std::span<const uint8_t> msg{s.bytes};
		state_log.AddSysEx(msg, s.port);
		if (StateLog::IsReset(msg)) {
			UiResetParts();
#ifdef NUKED_SC55_DEVICE_8850
			DotShow(false);
#endif
		}
		QueueMidi(router.RouteSysEx(msg, s.port), msg.data(), msg.size(), 0, s.port);
	}
	script_steps.erase(script_steps.begin(), script_steps.begin() + static_cast<std::ptrdiff_t>(done));
}

void NukedSc55::HandleUiCommands()
{
	const int cmd = ui_command.exchange(0);
	if (cmd == 10) SendMt32Mode();
	ReleaseScriptSteps();
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
#ifdef NUKED_SC55_ENGINE_88PRO
	if (cmd == 5 && HasToneMap()) { // front-panel ALL on unit 0 (SC-88 Pro): latching ALL view
		auto& inst = instances[0];
		if (inst.ctx && inst.seq_len == 0) {
			inst.seq_mask[0] = BtnAll;
			inst.seq_len     = 2; // press + release, keeps the tone map
			inst.seq_pos     = 0;
			inst.seq_map     = inst.applied_map;
			inst.seq_left    = static_cast<uint32_t>(emu88_get_device_samplerate(inst.ctx) * 0.15);
			emu88_set_panel_buttons(inst.ctx, BtnAll);
		}
		return;
	}
#endif
#ifdef NUKED_SC55_DEVICE_LA
	if (cmd == 1 || cmd == 11) {
		// MT-32 / CM-32L: "All notes off" (the firmware ignores All Sound Off: notes released, hold
		// pedal off, the queued MIDI dropped) and "Send Reset" (All parameters reset, as the
		// firmware's own power-on initialization)
		for (auto& inst : instances)
			if (inst.ctx) (cmd == 1) ? emu88_play_silence(inst.ctx) : emu88_play_device_reset(inst.ctx);
		if (cmd == 11) {
			UiResetParts();
			hold_until = render_frame_count + static_cast<uint64_t>(render_sample_rate_hz * 0.3);
		}
		return;
	}
#endif
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
	} else if (cmd == 2 || cmd == 6 || (cmd == 7 && HasGm2()) || ((cmd == 8 || cmd == 9) && HasSystemMode())) {
		// SETUP menu "Send Reset" (GM / GS / GM2) and "System Mode" (Mode 1 / Mode 2)
		static const uint8_t gm[]  = {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};
		static const uint8_t gs[]  = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40,
		                              0x00, 0x7f, 0x00, 0x41, 0xf7};
		static const uint8_t gm2[] = {0xf0, 0x7e, 0x7f, 0x09, 0x03, 0xf7};
		static const uint8_t md1[] = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x00,
		                              0x00, 0x7f, 0x00, 0x01, 0xf7};
		static const uint8_t md2[] = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x00,
		                              0x00, 0x7f, 0x01, 0x00, 0xf7};
		std::span<const uint8_t> msg;
		switch (cmd) {
		case 6: msg = gm; break;
		case 7: msg = gm2; break;
		case 8: msg = md1; system_mode = 0; break;
		case 9: msg = md2; system_mode = 1; break;
		default: msg = gs; break;
		}
		// Mode 2 (Double Module): GM On / GS Reset reach only the part group of the MIDI IN
		// they arrive on, so the menu sends them on IN A and IN B to reset the whole module
		const int ports = (cmd != 8 && cmd != 9 && state_log.DoubleModule()) ? 2 : 1;
		for (uint8_t port = 0; port < ports; ++port) {
			state_log.AddSysEx(msg, port);
			QueueMidi(router.RouteSysEx(msg, port), msg.data(), msg.size(), 0, port);
		}
#ifdef NUKED_SC55_DEVICE_8820
		TrackGsGm(msg);
#endif
		UiResetParts();
#ifdef NUKED_SC55_DEVICE_8850
		DotShow(false);
#endif
		// The next host events wait until the firmware has finished the reset (measured with
		// tools/test/reset26: notes are exact from 50-100 ms after GM/GS/GM2, from 150 ms
		// after a System Mode Set on the SC-88 Pro)
		const double hold = (cmd == 8 || cmd == 9) ? 0.2 : 0.1;
		hold_until = render_frame_count + static_cast<uint64_t>(render_sample_rate_hz * hold);
	}
}

#ifdef NUKED_SC55_ENGINE_88PRO
// Settle time after a replayed reset: a System Mode Set (00 00 7F) keeps the firmware
// busy for about 150 ms (tools/test/reset26), GM / GS / GM2 for less than 60 ms
double NukedSc55::ResetSettleSeconds(const StateLog::Entry& e)
{
	const auto& m = e.bytes;
	return (m.size() >= 10 && m[1] == 0x41 && m[5] == 0x00 && m[6] == 0x00 && m[7] == 0x7f) ? 0.2 : 0.06;
}
#endif

bool NukedSc55::HasGm2() const
{
#ifdef NUKED_SC55_ENGINE_88PRO
	return model == Model::Sc8850 || model == Model::Sc8820; // both carry the GM2 logo
#else
	return false;
#endif
}

bool NukedSc55::HasSystemMode() const
{
#ifdef NUKED_SC55_ENGINE_88PRO
	return model == Model::Sc88 || model == Model::Sc88Pro;
#else
	return false;
#endif
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
	case Model::Sc8820: return "SC-8820";
	case Model::Mt32: return "MT-32 / CM-32L";
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

// GAIN knob: -12 .. +12 dB plus the level matching of the model (SC-55: none, so 0 dB = 1.0)
float NukedSc55::GainFactor(const float gain_db)
{
	return std::pow(10.0f, (std::clamp(gain_db, kGainMinDb, kGainMaxDb) + kLevelMatchDb) / 20.0f);
}

#ifdef NUKED_SC55_ENGINE_88PRO

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
#elif defined(NUKED_SC55_DEVICE_8820)
		emu88_select_device(inst.ctx, EMU88_DEVICE_SC8820);
#elif defined(NUKED_SC55_DEVICE_88)
		emu88_select_device(inst.ctx, EMU88_DEVICE_SC88);
#elif defined(NUKED_SC55_DEVICE_LA)
		emu88_select_device(inst.ctx, kLaDeviceIds[std::clamp(la_active.load(), 0, kLaDevices - 1)]);
#else
		emu88_select_device(inst.ctx, EMU88_DEVICE_SC88PRO);
#endif
		emu88_set_stereo_output_samplerate(inst.ctx, 0); // device rate (32 kHz)
#if defined(NUKED_SC55_DEVICE_LA)
		emu88_set_fast_boot_seconds(inst.ctx, kLaBootSeconds);
#endif
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
#if defined(NUKED_SC55_DEVICE_LA)
		inst.applied_map = 0; // no tone maps
#elif defined(NUKED_SC55_DEVICE_8820) || defined(NUKED_SC55_DEVICE_8850)
		inst.applied_map = 3; // boots with its own map (SC-8820 / SC-8850)
#elif defined(NUKED_SC55_DEVICE_88)
		inst.applied_map = 1; // boots with the SC-88 map
#else
		inst.applied_map = 2; // boots with the SC-88 Pro map
#endif
		E88ApplyMapSync(inst, tone_map.load());
#ifdef NUKED_SC55_DEVICE_8850
		if (i == 0) E88SyncBoot(inst); // what unit 0's panel can change, as booted
#endif
#if defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
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

// Tone-map switching like on the hardware front panel. Both map buttons
// toggle: SC-55 MAP switches SC-55 <-> SC-88 Pro (from SC-88: to SC-55),
// SC-88 MAP switches SC-88 <-> SC-88 Pro (from SC-55: to SC-88). The panel
// LEDs do not reliably show the map, so the state of every unit is tracked
// (boot = SC-88 Pro); nothing else presses these buttons and GS resets keep
// the map. ALL (latching, LED 0) makes the press apply to all parts.
// SC-88: its only map key INST MAP (same switch bit as SC-55 MAP) toggles SC-55 <-> SC-88.
static uint32_t E88MapKey(const int from, const int to)
{
#ifdef NUKED_SC55_DEVICE_88
	(void)from; (void)to;
	return Btn55Map;
#else
	if (to == 0) return Btn55Map;
	if (to == 1) return Btn88Map;
	return from == 0 ? Btn55Map : Btn88Map; // back to SC-88 Pro
#endif
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
#ifdef NUKED_SC55_DEVICE_8820
		{
			const uint32_t l = emu88_get_panel_leds(inst.ctx);
			if (inst.seq_len > 0 && inst.seq_pos % 2 == 1) inst.led_acc |= l; // released: map shown
			inst.led_win |= l;
			if ((inst.led_acc_frames += n) >= 1280) { // ~40 ms at 32 kHz
				inst.leds_shown     = inst.led_win;
				inst.led_win        = 0;
				inst.led_acc_frames = 0;
			}
		}
#endif
		if (inst.seq_len > 0 && (inst.seq_left = inst.seq_left > n ? inst.seq_left - n : 0) == 0) {
			const double rate = emu88_get_device_samplerate(inst.ctx);
			++inst.seq_pos;
			if (inst.seq_pos >= inst.seq_len) {
#ifdef NUKED_SC55_DEVICE_8820
				if (const int shown = E8820ShownMap(inst.led_acc); shown >= 0) inst.applied_map = shown;
#else
				inst.applied_map = inst.seq_map;
#endif
				inst.seq_len     = 0;
			} else if (inst.seq_pos % 2 == 1) { // release (switches the next step holds stay down)
				const int step = inst.seq_pos / 2;
#ifdef NUKED_SC55_DEVICE_8820
				emu88_set_panel_buttons(inst.ctx, 0); // every press is released (same button again)
				(void)step;
#else
				emu88_set_panel_buttons(inst.ctx, step + 1 < 4 && inst.seq_pos + 1 < inst.seq_len
				                                      ? inst.seq_mask[step] & inst.seq_mask[step + 1] : 0);
#endif
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
	if (!HasToneMap() && !kHas88Map) { // SC-8850: tone maps are part settings of its own panel
		inst.applied_map = map;
		return;
	}
	if (inst.seq_len > 0 || map == inst.applied_map) return;
#ifdef NUKED_SC55_DEVICE_8820
	{
		// INST MAP steps 8820 -> 88 Pro -> 88 -> 55 -> 8820. A press while the map is not shown only
		// shows it. Closed loop: one press at a time; the map the unit then shows (read from its lamps
		// while the button is released) becomes applied_map, and the caller presses again until it is
		// the wanted one.
		inst.seq_mask[0] = kBtn8820InstMap;
		inst.seq_map  = -1;     // set from the lamps at the end of the step
		inst.led_acc  = 0;
		inst.seq_len  = 2;
		inst.seq_pos  = 0;
		inst.seq_left = static_cast<uint32_t>(emu88_get_device_samplerate(inst.ctx) * 0.15);
		emu88_set_panel_buttons(inst.ctx, inst.seq_mask[0]);
		return;
	}
#endif
	// The map buttons act on all parts only in ALL mode: enter it for the press and leave it
	// again, unless ALL was already on (the ALL view chosen on the panel stays)
	const bool all_on = (emu88_get_panel_leds(inst.ctx) & 1u) != 0;
	int k = 0;
	if (!all_on) inst.seq_mask[k++] = BtnAll;
	inst.seq_mask[k++] = E88MapKey(inst.applied_map, map);
	if (!all_on) inst.seq_mask[k++] = BtnAll; // leave ALL mode again
	inst.seq_map  = map;
	inst.seq_len  = 2 * k; // press + release per button
	inst.seq_pos  = 0;
	inst.seq_left = static_cast<uint32_t>(emu88_get_device_samplerate(inst.ctx) * 0.15);
	emu88_set_panel_buttons(inst.ctx, inst.seq_mask[0]);
}

// Blocking variant (boot / background warmer only): renders until applied.
void NukedSc55::E88ApplyMapSync(Instance& inst, const int map)
{
	for (int round = 0; round < kMapRounds && inst.applied_map != map; ++round) {
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

#ifdef NUKED_SC55_ENGINE_88PRO
// Lamps of unit 0's front panel (SC-88 Pro: ALL view; SC-8850, SC-88: all switches)
uint32_t NukedSc55::PanelLeds() const
{
	std::unique_lock lock(units_mx, std::try_to_lock);
	if (!lock.owns_lock() || !lcd_ready.load() || instances.empty() || !instances[0].ctx) return 0;
#if defined(NUKED_SC55_DEVICE_8820)
	return ui_leds8820.load(std::memory_order_relaxed); // multiplexed: ORed over a short window
#elif defined(NUKED_SC55_DEVICE_LA)
	uint32_t leds = 0; // MIDI MESSAGE: every unit only lights for the notes it plays
	for (int i = 0; i < NumInstances(); ++i)
		if (instances[i].ctx && instances[i].booted->load(std::memory_order_acquire))
			leds |= emu88_get_panel_leds(instances[i].ctx);
	return leds;
#else
	return emu88_get_panel_leds(instances[0].ctx);
#endif
}
#endif

#if defined(NUKED_SC55_DEVICE_8850) || defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
//----------------------------------------------------------------------------
// Front panel of unit 0 (SC-8850, SC-88, SC-88 Pro)
//----------------------------------------------------------------------------
// A DT1 for all units but unit 0 (which made the change) and for the state log.
void NukedSc55::E88Forward(const uint8_t port, const uint8_t* msg, const size_t size)
{
	const std::span<const uint8_t> m{msg, size};
	state_log.AddSysEx(m, port);
	const auto mask = router.RouteSysEx(m, port) & ~PolyRouter::Mask{1};
	QueueMidi(mask, msg, size, 0, port);
	log("%s panel: %zu bytes to the other units", ModelName(), size);
}

#if defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
namespace {
constexpr uint32_t kBtn88Select = 1u << 25, kBtn88InstL = 1u << 3, kBtn88All = 1u << 6;
// Work RAM: MASTER TUNE (40 00 00..03 as a 16-bit value, 0x400 = 440 Hz)
#ifdef NUKED_SC55_DEVICE_88
constexpr uint32_t kRam88Tune = 0x8040;
#else
constexpr uint32_t kRam88Tune = 0x5040;
#endif
constexpr double kPanel88Seconds = 0.6; // master tune is passed on until this long after the last switch
} // namespace

// Audio thread, before the GUI switches reach unit 0: ALL pressed while unit 0 asks
// "..., Sure?" confirms a panel function; the plugin repeats it on the other units.
void NukedSc55::E88PanelConfirm(const uint32_t buttons)
{
	auto& inst = instances[0];
	const uint32_t pressed = buttons & ~confirm_prev;
	confirm_prev           = buttons;
	if (buttons != 0)
		panel_used_until = render_frame_count + 1 + static_cast<uint64_t>(render_sample_rate_hz * kPanel88Seconds);
	if (!(pressed & kBtn88All) || !inst.ctx) return;
	uint8_t dd[80], cg[64];
	int on = 0;
	if (!emu88_get_display_memory(inst.ctx, 0, dd, cg, &on)) return;
	const std::string_view row(reinterpret_cast<const char*>(dd), 24);
	if (row.find("Sure?") == std::string_view::npos) return;
	static const uint8_t gm[]  = {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};
	static const uint8_t md1[] = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0x7f, 0x00, 0x01, 0xf7};
	static const uint8_t md2[] = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0x7f, 0x01, 0x00, 0xf7};
	if (row.find("Init GM") != std::string_view::npos) {
		const int ports = state_log.DoubleModule() ? 2 : 1;
		for (uint8_t port = 0; port < ports; ++port) E88Forward(port, gm, sizeof(gm));
		UiResetParts();
	} else if (row.find("Mode1") != std::string_view::npos || row.find("Mode2") != std::string_view::npos) {
		const bool two = row.find("Mode2") != std::string_view::npos;
		E88Forward(0, two ? md2 : md1, sizeof(md1));
		system_mode = two ? 1 : 0; // SETUP menu mark follows
		UiResetParts();
	} else if (row.find("CM-64") != std::string_view::npos) {
		state_log.AddSysEx(StateLog::kCm64Marker, 0);
		for (int i = 1; i < NumInstances(); ++i)
			if (instances[i].awake) E88StartCm64(instances[i]);
		UiResetParts();
		log("%s panel: Initialize CM-64 on the other units", ModelName());
	}
}

// Panel sequence SELECT + INST < (Initialize CM-64), ALL (confirm) on another unit
void NukedSc55::E88StartCm64(Instance& inst)
{
	if (!inst.ctx) return;
	inst.seq_mask[0] = kBtn88Select; // SELECT held, then INST <
	inst.seq_mask[1] = kBtn88Select | kBtn88InstL;
	inst.seq_mask[2] = kBtn88All;
	inst.seq_len     = 6; // press + release per step, keeps the tone map
	inst.seq_pos     = 0;
	inst.seq_map     = inst.applied_map;
	inst.seq_left    = static_cast<uint32_t>(emu88_get_device_samplerate(inst.ctx) * 0.15);
	emu88_set_panel_buttons(inst.ctx, inst.seq_mask[0]);
}

// Blocking variant (wake / background warmer): renders until the function has run.
void NukedSc55::E88Cm64Sync(Instance& inst)
{
	while (inst.seq_len > 0) { // a tone map sequence still running
		E88RenderFrames(inst, inst.seq_left);
		inst.buf_l.clear();
		inst.buf_r.clear();
	}
	E88StartCm64(inst);
	while (inst.seq_len > 0) {
		E88RenderFrames(inst, inst.seq_left);
		inst.buf_l.clear();
		inst.buf_r.clear();
	}
	E88Drain(inst, 0, 0.3);
}

// MASTER TUNE changed in unit 0's menu -> DT1 to the other units and the state log
void NukedSc55::E88PanelTune()
{
	auto& inst = instances[0];
	if (!inst.ctx) return;
	uint8_t t[2] = {};
	emu88_peek_work_ram(inst.ctx, kRam88Tune, t, 2);
	if (t[0] == tune_seen[0] && t[1] == tune_seen[1]) return;
	tune_seen[0] = t[0];
	tune_seen[1] = t[1];
	if (render_frame_count >= panel_used_until) return; // from the host: every unit has it
	const int tune = t[0] << 8 | t[1];
	uint8_t m[14] = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x00,
	                 static_cast<uint8_t>(tune >> 12 & 15), static_cast<uint8_t>(tune >> 8 & 15),
	                 static_cast<uint8_t>(tune >> 4 & 15), static_cast<uint8_t>(tune & 15), 0, 0xf7};
	int sum = 0;
	for (int k = 5; k < 12; ++k) sum += m[k];
	m[12] = static_cast<uint8_t>((128 - (sum & 0x7f)) & 0x7f);
	E88Forward(0, m, sizeof(m));
}
#endif

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
#ifdef NUKED_SC55_DEVICE_88PRO
// SC-88 Pro (control ROM 1.02): switch bits (88emu Sc88ProButton). In ALL mode INSTRUMENT
// and MIDI CH change states of the whole unit without a GS parameter (all instruments,
// device ID), which could not be passed on; they are not given to the firmware there.
constexpr uint32_t kBtn88pInst   = (1u << 3) | (1u << 4);
constexpr uint32_t kBtn88pMidiCh = (1u << 8) | (1u << 9);
// The same panel-edit log as on the SC-88 (same entry format), a ring of 400 bytes
// (measured: the write pointer runs from 02C4 to 0454 and starts again at 02C4).
constexpr uint32_t kRam88EditLog   = 0x02c4;
constexpr uint32_t kRam88EditWrite = 0x02c0;
constexpr int kRam88EditSize       = 0x190;
#endif
} // namespace

#ifdef NUKED_SC55_DEVICE_8850
namespace {
// SC-8850 switches (88emu Sc8850Button bits) that never edit a parameter: PART < >, EXIT,
// SHIFT, SOLO, MUTE (taken from the work RAM, E88PanelMutes), PREVIEW
constexpr uint32_t kBtn8850NoEdit = (1u << 1) | (1u << 2) | (1u << 7) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 20);
constexpr uint32_t kBtn8850Enter  = 1u << 8;
constexpr uint32_t kLed8850Drum   = 1u << 1;
} // namespace
#endif

// Audio thread, start of a block: GUI switches (and the SC-8850's VALUE detents) to unit 0
void NukedSc55::E88PanelInput()
{
	auto& inst = instances[0];
	if (!inst.ctx) return;
	uint32_t buttons = ui_panel_buttons.load(std::memory_order_relaxed);
#if defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
	E88PanelConfirm(buttons);
#endif
#ifdef NUKED_SC55_DEVICE_88
	// In ALL mode, EQ, INST MAP and MIDI CH change panel states of the whole unit that have no
	// GS parameter (EQ off for everything, all tone maps, device ID) and could not be passed on.
	// EQ is applied as the EQ switch of all 32 parts instead; the others stay on unit 0's panel.
	const uint32_t pressed = buttons & ~gui_buttons;
	gui_buttons            = buttons;
	if (inst.seq_len > 0) { // a panel sequence of the plugin (tone map) has the switches
		applied_buttons = ~0u;
		return;
	}
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
#elif defined(NUKED_SC55_DEVICE_88PRO)
	// A panel sequence of the plugin (tone map, ALL, PART) has the switches; held ones follow it
	if (inst.seq_len > 0) {
		applied_buttons = ~0u;
		return;
	}
	if (emu88_get_panel_leds(inst.ctx) & 1u) buttons &= ~(kBtn88pInst | kBtn88pMidiCh);
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

#if defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
//----------------------------------------------------------------------------
// SC-88 / SC-88 Pro front panel: synchronisation of panel edits
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
	int avail = (w - edit_cursor + kRam88EditSize) % kRam88EditSize;
	const auto at = [&](int k) { return ring[(pos + k) % kRam88EditSize]; };
	// A tone map sequence logs the map / tone of the parts it changes; every unit runs its own
	if (inst.seq_len > 0 && inst.seq_map != inst.applied_map) {
		edit_cursor = w;
		return;
	}
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
		pos = (pos + size) % kRam88EditSize;
		avail -= size;
	}
	edit_cursor = static_cast<int>(kRam88EditLog) + pos;
}

#ifdef NUKED_SC55_DEVICE_88
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
	std::unique_lock lock(units_mx, std::try_to_lock);
	if (!lock.owns_lock() || !lcd_ready.load() || instances.empty() || !instances[0].ctx) return false;
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
		else if ((req.a1 & 0xf0) == 0x40 && a <= 0x01) from = 0x00, to = 0x01; // tone map, tone map-0
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
	// INST MAP (Issue #17): the firmware keeps a tone map written by DT1 pending until the part
	// selects its tone again, so the tone number (read earlier in the same pass) follows the map.
	if (req.a0 == 0x40 && (req.a1 & 0xf0) == 0x40 && req.a2 == 0x00 && n >= 2 && send[0]) {
		const int bank = sync_base[SyncIndex(req.port, 0x40, 0x10 | (req.a1 & 0x0f), 0x00)];
		const int prog = sync_base[SyncIndex(req.port, 0x40, 0x10 | (req.a1 & 0x0f), 0x01)];
		if (bank >= 0 && prog >= 0) {
			uint8_t tone[12] = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, static_cast<uint8_t>(0x10 | (req.a1 & 0x0f)), 0x00,
			                    static_cast<uint8_t>(bank), static_cast<uint8_t>(prog), 0, 0xf7};
			int sum = 0;
			for (int k = 5; k < 10; ++k) sum += tone[k];
			tone[10] = static_cast<uint8_t>((128 - (sum & 0x7f)) & 0x7f);
			E88Forward(req.port, tone, sizeof(tone));
		}
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
	warmer_jobs.clear();
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
#if defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
		if (StateLog::IsCm64Marker(e.bytes)) {
			E88Drain(inst, bytes, 0.005);
			bytes = 0;
			E88Cm64Sync(inst);
			continue;
		}
#endif
		emu88_parse_stream_on_port(inst.ctx, e.port, e.bytes.data(), static_cast<uint32_t>(e.bytes.size()));
		bytes += e.bytes.size();
		if (e.is_reset) {
			E88Drain(inst, bytes, ResetSettleSeconds(e));
			bytes = 0;
		}
	}
	E88Drain(inst, bytes, 0.005);
	return;
#endif
	const auto step_until_drained = [&](double extra_seconds) {
		DrainUnit(inst);
		const auto target = inst.buf_l.size() +
		                    static_cast<size_t>(render_sample_rate_hz * extra_seconds);
		while (inst.buf_l.size() < target) StepUnit(inst);
		inst.buf_l.clear();
		inst.buf_r.clear();
	};
	for (const auto& e : entries) {
		PostToUnit(inst, std::span{e.bytes.data(), e.bytes.size()});
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
			warmer_cv.wait(lk, [&] { return warmer_quit || !warmer_jobs.empty(); });
			if (warmer_quit) return;
			job = std::move(warmer_jobs.front());
			warmer_jobs.pop_front();
		}
		auto& inst = instances[job.instance];
#ifdef NUKED_SC55_ENGINE_88PRO
		// A unit may still be booting in the background: wait for it here, not in the audio thread.
		// The job (entries up to target_seq) was fixed when it was scheduled, so the result does not
		// depend on how long the boot takes.
		while (!inst.booted->load(std::memory_order_acquire)) {
			{
				std::lock_guard lk(warmer_mutex);
				if (warmer_quit) return;
			}
			std::this_thread::sleep_for(std::chrono::microseconds(500));
		}
#endif
		ReplayInto(inst, job.entries);
		inst.sleep_seq = job.target_seq;
		inst.warming->fetch_sub(1, std::memory_order_release);
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
#ifndef NUKED_SC55_ENGINE_88PRO
		if (inst.warming->load(std::memory_order_acquire)) return; // one at a time
#endif
#ifdef NUKED_SC55_ENGINE_88PRO
		// Still booting in the background: if it needs warming now, the job is handed over all
		// the same and the warmer waits for the boot (the audio thread never does), so the warming
		// sequence (and therefore the output) is exactly that of a unit booted in Activate().
		// Its map is set by the replay; until booted only the boot thread touches the unit.
		// The decision uses sched_seq (what the jobs already handed over will have replayed), not
		// the real progress of the warmer, so the schedule does not depend on how long a boot or
		// a replay takes: it is the one a synchronous warmer would produce.
		// (applied_map belongs to the boot thread / warmer while the unit boots or a job is waiting)
		const bool map_stale = inst.booted->load(std::memory_order_acquire) &&
		                       inst.warming->load(std::memory_order_acquire) == 0 &&
		                       inst.applied_map != tone_map.load();
		if (inst.sched_seq < state_log.Seq() || map_stale) { cand = i; break; }
#else
		if (inst.sleep_seq < state_log.Seq()) { cand = i; break; }
#endif
	}
	if (cand < 0) return;

	std::unique_lock lk(warmer_mutex, std::try_to_lock);
	if (!lk.owns_lock()) return;
#ifndef NUKED_SC55_ENGINE_88PRO
	if (!warmer_jobs.empty()) return;
#endif
	WarmJob& job = warmer_jobs.emplace_back();
	job.instance   = cand;
	job.target_seq = state_log.Seq();
#ifdef NUKED_SC55_ENGINE_88PRO
	state_log.CopySince(instances[cand].sched_seq, job.entries);
	instances[cand].sched_seq = job.target_seq;
#else
	state_log.CopySince(instances[cand].sleep_seq, job.entries);
#endif
	instances[cand].warming->fetch_add(1, std::memory_order_release);
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
	inst.sched_seq = inst.sleep_seq;
	inst.queue.clear(); // (bytes still waiting for its MIDI buffer stay: they are state, fed on waking)
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
	while (inst.warming->load(std::memory_order_acquire) != 0) {
		std::this_thread::yield();
	}
	inst.buf_l.clear();
	inst.buf_r.clear();
	{
		size_t bytes = 0;
		state_log.ForEachSince(inst.sleep_seq, [&](const StateLog::Entry& e) {
#if defined(NUKED_SC55_DEVICE_88) || defined(NUKED_SC55_DEVICE_88PRO)
			if (StateLog::IsCm64Marker(e.bytes)) {
				E88Drain(inst, bytes, 0.005);
				bytes = 0;
				E88Cm64Sync(inst);
				return;
			}
#endif
			emu88_parse_stream_on_port(inst.ctx, e.port, e.bytes.data(), static_cast<uint32_t>(e.bytes.size()));
			bytes += e.bytes.size();
			replayed_bytes += static_cast<int>(e.bytes.size());
			if (e.is_reset) {
				E88Drain(inst, bytes, ResetSettleSeconds(e));
				bytes = 0;
			}
		});
		E88Drain(inst, bytes, 0.005);
	}
	// Map not yet on this unit (warmer did not get to it): switch it while
	// it plays instead of blocking the audio thread.
	E88StartMapSequence(inst, tone_map.load()); // no-op if already on the map
#else
	const auto step_until_drained = [&](double extra_seconds) {
		DrainUnit(inst);
		const auto extra = static_cast<size_t>(render_sample_rate_hz * extra_seconds);
		const auto target = inst.buf_l.size() + extra;
		while (inst.buf_l.size() < target) {
			StepUnit(inst);
		}
		inst.buf_l.clear();
		inst.buf_r.clear();
	};

	// A background sync may be running on this instance: let it finish.
	while (inst.warming->load(std::memory_order_acquire) != 0) {
		std::this_thread::yield();
	}
	inst.buf_l.clear();
	inst.buf_r.clear();

	// Replay what the instance missed since its last sync (compacted).
	state_log.ForEachSince(inst.sleep_seq, [&](const StateLog::Entry& e) {
		PostToUnit(inst, std::span{e.bytes.data(), e.bytes.size()});
		replayed_bytes += static_cast<int>(e.bytes.size());
		if (e.is_reset) {
			step_until_drained(0.06); // firmware needs time after GS reset
		}
	});
	step_until_drained(0.005);
	E55ApplyMutes(inst, applied_mute55); // panel MUTE flags are not part of the state log
	E55ApplyRxSwitches(inst);            // nor the system menu's Rx switches
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

// The audio thread waits for the workers at the end of every block, so a worker must not be preempted by
// other programs' threads: with normal priority one could lose a scheduler time slice while the audio
// thread (high priority in a host) waits for it, and the block missed its deadline (measured under CPU
// load with tools/polybench: SC-55, SC-88 Pro, SC-8850). On Windows the workers therefore join the
// "Pro Audio" MMCSS class like a host's audio thread, or run at THREAD_PRIORITY_HIGHEST where MMCSS is
// not available. They only run while units render; between blocks they wait.
namespace {
class WorkerPriority {
public:
	WorkerPriority()
	{
#ifdef _WIN32
		if (HMODULE av = LoadLibraryA("avrt.dll")) {
			using SetFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
			const auto set = reinterpret_cast<SetFn>(reinterpret_cast<void*>(GetProcAddress(av, "AvSetMmThreadCharacteristicsW")));
			revert = reinterpret_cast<RevertFn>(reinterpret_cast<void*>(GetProcAddress(av, "AvRevertMmThreadCharacteristics")));
			DWORD task = 0;
			if (set) mmcss = set(L"Pro Audio", &task);
			if (mmcss) lib = av;
			else FreeLibrary(av);
		}
		if (!mmcss) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
	}
	~WorkerPriority()
	{
#ifdef _WIN32
		if (mmcss && revert) revert(mmcss);
		if (lib) FreeLibrary(lib);
#endif
	}
	WorkerPriority(const WorkerPriority&)            = delete;
	WorkerPriority& operator=(const WorkerPriority&) = delete;

private:
#ifdef _WIN32
	using RevertFn = BOOL(WINAPI*)(HANDLE);
	HANDLE mmcss    = nullptr;
	HMODULE lib     = nullptr;
	RevertFn revert = nullptr;
#endif
};
} // namespace

void WorkerPool::WorkerLoop()
{
	const WorkerPriority priority;
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

	// Speex returns the number of actually consumed and written samples in `in_len` and
	// `out_len`. Either the output is full (input may be left over for the next call) or all
	// input has been consumed (then more is rendered and resampled into the rest of the output).
	// Consumed input is removed after every pass, so leftovers are always kept; should the output
	// still not be full after the second pass, the rest is silence rather than whatever the host
	// buffer held (issue #63).
	uint32_t written = 0;
	for (int pass = 0; pass < 2 && written < num_out_frames; ++pass) {
		if (pass > 0) {
			// "It's the only way to be sure"
			const auto more = static_cast<uint32_t>(std::ceil(
			        static_cast<double>(num_out_frames - written) * resample_ratio));
			RenderAudio(more);
		}
		const auto input_len = static_cast<spx_uint32_t>(render_buf[0].size());
		log("  input_len: %d", input_len);

		spx_uint32_t in_len  = input_len;
		spx_uint32_t out_len = num_out_frames - written;
		speex_resampler_process_float(
		        resampler, 0, render_buf[0].data(), &in_len, out_left + written, &out_len);

		in_len  = input_len;
		out_len = num_out_frames - written;
		speex_resampler_process_float(
		        resampler, 1, render_buf[1].data(), &in_len, out_right + written, &out_len);

		if (in_len >= input_len) {
			render_buf[0].clear();
			render_buf[1].clear();
		} else if (in_len > 0) {
			render_buf[0].erase(render_buf[0].begin(), render_buf[0].begin() + in_len);
			render_buf[1].erase(render_buf[1].begin(), render_buf[1].begin() + in_len);
		}
		written += out_len;
	}
	for (uint32_t i = written; i < num_out_frames; ++i) {
		out_left[i]  = 0.0f;
		out_right[i] = 0.0f;
	}
}

// ---- Display SysEx pacing (all models, issue #42) ----

bool NukedSc55::IsDisplaySysEx(const std::span<const uint8_t> msg)
{
	return msg.size() >= 10 && msg.size() <= kDispBytes && msg[0] == 0xf0 && msg[1] == 0x41 &&
	       msg[3] == 0x45 && msg[4] == 0x12 && msg.back() == 0xf7;
}

void NukedSc55::DispQueue(const std::span<const uint8_t> msg, const uint8_t port)
{
	// newest data for the same address replaces a message still waiting
	for (auto& d : disp_pending) {
		if (d.port == port && d.size == msg.size() && d.bytes[5] == msg[5] && d.bytes[6] == msg[6] &&
		    d.bytes[7] == msg[7]) {
			std::copy(msg.begin(), msg.end(), d.bytes.begin());
			return;
		}
	}
	if (disp_pending.size() >= kDispMax) disp_pending.erase(disp_pending.begin());
	DispMsg d;
	d.port = port;
	d.size = static_cast<uint16_t>(msg.size());
	std::copy(msg.begin(), msg.end(), d.bytes.begin());
	disp_pending.push_back(d);
}

size_t NukedSc55::MidiBacklog(const int i) const
{
	const auto& inst = instances[i];
	if (!inst.awake) return 0;
#ifdef NUKED_SC55_ENGINE_88PRO
	if (!inst.ctx) return 0;
	size_t n = emu88_get_midi_in_backlog(inst.ctx);
#else
	const auto& mcu = inst.emu->GetMCU();
	size_t n = (mcu.uart_write_ptr + uart_buffer_size - mcu.uart_read_ptr) % uart_buffer_size;
	n += inst.uart_wait.size() - inst.uart_wait_pos;
#endif
	for (const auto& ev : inst.queue) n += ev.size;
	return n;
}

bool NukedSc55::DispReady() const
{
	if (!disp_pending.empty() || instances.empty() || !instances[0].awake) return false;
#ifdef NUKED_SC55_ENGINE_88PRO
	if (!instances[0].booted->load(std::memory_order_acquire)) return false;
#endif
	return MidiBacklog(0) < kDispBacklog;
}

void NukedSc55::DispFeed()
{
	if (disp_pending.empty() || instances.empty() || !instances[0].awake) return;
#ifdef NUKED_SC55_ENGINE_88PRO
	if (!instances[0].booted->load(std::memory_order_acquire)) return;
#endif
	size_t backlog = MidiBacklog(0);
	size_t n = 0;
	while (n < disp_pending.size() && backlog < kDispBacklog) {
		const auto& d = disp_pending[n++];
		QueueMidi(PolyRouter::Mask{1}, d.bytes.data(), d.size, 0, d.port);
		backlog += d.size;
	}
	disp_pending.erase(disp_pending.begin(), disp_pending.begin() + static_cast<std::ptrdiff_t>(n));
}

#ifdef NUKED_SC55_DEVICE_8820
// ---- SC-8820 front panel: INST MAP and VOLUME push (PREVIEW) of unit 0 -------------------------
// The GUI's switches go to unit 0 as on the hardware. When unit 0 shows the map (after a press),
// that map becomes the plugin's tone map; the other units follow with their own presses.
void NukedSc55::E8820PanelInput()
{
	auto& inst = instances[0];
	if (!inst.ctx) return;
	if (inst.seq_len > 0) { // a map sequence of the plugin has the switches
		applied_buttons = ~0u;
		return;
	}
	ui_leds8820.store(inst.leds_shown, std::memory_order_relaxed);
	const uint32_t buttons = ui_panel_buttons.load(std::memory_order_relaxed) & 3u;
	if (buttons != applied_buttons) {
		emu88_set_panel_buttons(inst.ctx, buttons);
		applied_buttons = buttons;
	}
	// Only a press in the GUI chooses the map; maps that unit 0 shows while the plugin itself
	// steps it towards the tone map are intermediate states.
	if (buttons & kBtn8820InstMap)
		gui_map_until = render_frame_count + static_cast<uint64_t>(render_sample_rate_hz * 1.5);
	if (render_frame_count > gui_map_until) return;
	const int shown = E8820ShownMap(inst.leds_shown);
	if (shown < 0) return;
	inst.applied_map = shown;
	if (shown != tone_map.load()) tone_map.store(shown);
}
#endif

#ifdef NUKED_SC55_DEVICE_8820
// ---- SC-8820, Pro interface: part display (issue #49) ------------------------------------------------
// The SC-8820 has no display, but its firmware keeps every part's parameters and the name of the
// tone it plays in work RAM (found by comparing RAM dumps before and after Program Change, Bank
// Select, CC 7/10/91/93, GS "Use for Rhythm Part"): one 0x450-byte record per part, per port in
// GS block order (part 10, parts 1-9, parts 11-16), and the drum-set names of the two rhythm maps
// per port in records of 0x50c bytes. All units hold the same state, unit 0 is read.
namespace {
constexpr uint32_t kRam8820Part = 0x010448e4, kRam8820PartSize = 0x450;
constexpr uint32_t kRam8820DrumName = 0x0104dca0, kRam8820DrumSize = 0x50c;
// offsets in a part record
constexpr uint32_t kP20Msb = 0x00, kP20Prog = 0x01, kP20Index = 0x04, kP20Flags = 0x05, kP20Vol = 0x08,
                   kP20Pan = 0x09, kP20Cho = 0x0e, kP20Rev = 0x0f, kP20Lsb = 0x70, kP20Name = 0xa8;
constexpr uint8_t kP20Rhythm = 0x10, kP20RhythmMap2 = 0x40; // flags: rhythm part, rhythm map 2 (else 1)
} // namespace

// SC-D70 panel: GS / GM lamps follow GM On, GM2 On and GM Off (universal non-realtime 7E xx 09 01 / 03 / 02)
// and GS Reset or System Mode Set (Roland DT1 40 00 7F / 00 00 7F)
void NukedSc55::TrackGsGm(std::span<const uint8_t> m)
{
	if (m.size() >= 6 && m[1] == 0x7e && m[3] == 0x09) {
		if (m[4] == 0x01 || m[4] == 0x03) ui_gm_mode = 1;
		else if (m[4] == 0x02) ui_gm_mode = 0;
	} else if (m.size() >= 10 && m[1] == 0x41 && m[3] == 0x42 && m[4] == 0x12 && m[6] == 0x00 && m[7] == 0x7f &&
	           (m[5] == 0x40 || m[5] == 0x00)) {
		ui_gm_mode = 0;
	}
}

void NukedSc55::E8820ReadParts()
{
	auto& inst = instances[0];
	if (!inst.ctx || !inst.booted->load()) return;
	const auto every = static_cast<uint64_t>(render_sample_rate_hz / 30.0); // ~30 times a second
	if (render_frame_count < parts_read_at + every && parts_read_at != 0) return;
	parts_read_at = std::max<uint64_t>(render_frame_count, 1);
	std::array<PartInfo8820, 32> info{};
	for (int part = 0; part < 32; ++part) {
		const int ch    = part % 16;
		const int block = ch == 9 ? 0 : (ch < 9 ? ch + 1 : ch); // GS part block: 0 = part 10
		const uint32_t rec = kRam8820Part + static_cast<uint32_t>((part / 16) * 16 + block - 1) * kRam8820PartSize;
		uint8_t r[kP20Name + 12] = {};
		emu88_peek_work_ram(inst.ctx, rec, r, sizeof(r));
		auto& pi = info[part];
		pi.valid  = r[kP20Index] == part;
		pi.msb    = r[kP20Msb] & 0x7f;
		pi.prog   = r[kP20Prog] & 0x7f;
		pi.lsb    = r[kP20Lsb] & 0x7f;
		pi.vol    = r[kP20Vol] & 0x7f;
		pi.pan    = r[kP20Pan] & 0x7f;
		pi.rev    = r[kP20Rev] & 0x7f;
		pi.cho    = r[kP20Cho] & 0x7f;
		pi.rhythm = (r[kP20Flags] & kP20Rhythm) != 0;
		const uint8_t* name = r + kP20Name;
		uint8_t drum[12];
		if (pi.rhythm) {
			const int map = (part / 16) * 2 + ((r[kP20Flags] & kP20RhythmMap2) ? 1 : 0);
			emu88_peek_work_ram(inst.ctx, kRam8820DrumName + static_cast<uint32_t>(map) * kRam8820DrumSize, drum, 12);
			name = drum;
		}
		for (int i = 0; i < 12; ++i) pi.name[i] = (name[i] >= 0x20 && name[i] < 0x7f) ? static_cast<char>(name[i]) : ' ';
		int n = 12;
		while (n > 0 && pi.name[n - 1] == ' ') --n;
		pi.name[n] = 0;
	}
	if (part_info_mx.try_lock()) {
		part_info8820 = info;
		part_info_mx.unlock();
	} else {
		parts_read_at = 0; // GUI was reading: try again next block
	}
}

NukedSc55::PartInfo8820 NukedSc55::PartInfo(const int part)
{
	std::lock_guard lock(part_info_mx);
	return part_info8820[static_cast<size_t>(std::clamp(part, 0, 31))];
}

void NukedSc55::UiSendShort(const uint8_t s, const uint8_t d1, const uint8_t d2, const int port)
{
	const uint32_t w = ui_midi_w.load(std::memory_order_relaxed);
	if (w - ui_midi_r.load(std::memory_order_acquire) >= ui_midi.size()) return; // full: dropped
	ui_midi[w % ui_midi.size()].store(static_cast<uint32_t>(port & 0xff) << 24 | uint32_t{d2} << 16 |
	                                  uint32_t{d1} << 8 | s, std::memory_order_relaxed);
	ui_midi_w.store(w + 1, std::memory_order_release);
}

void NukedSc55::E8820SendUiMidi()
{
	uint32_t r       = ui_midi_r.load(std::memory_order_relaxed);
	const uint32_t w = ui_midi_w.load(std::memory_order_acquire);
	for (; r != w; ++r) {
		const uint32_t m = ui_midi[r % ui_midi.size()].load(std::memory_order_relaxed);
		InjectShort(static_cast<uint8_t>(m), static_cast<uint8_t>(m >> 8), static_cast<uint8_t>(m >> 16),
		            static_cast<uint16_t>(m >> 24));
	}
	ui_midi_r.store(r, std::memory_order_release);
}
#endif

#ifdef NUKED_SC55_DEVICE_8820
// Map shown by the PART B indicator (lamps ORed over a window), -1 if none is shown.
int NukedSc55::E8820ShownMap(const uint32_t leds)
{
	if ((leds & kLed8820MapShown) == 0) return -1;
	switch ((leds >> 4) & 0xfu) {
	case 0x1: return 0; // 55
	case 0x2: return 1; // 88
	case 0x4: return 2; // 88Pro
	case 0x8: return 3; // 8820
	default: return -1;
	}
}
#endif

#ifdef NUKED_SC55_DEVICE_LA
//----------------------------------------------------------------------------
// MT-32 / CM-32L (issue #69)
//----------------------------------------------------------------------------
bool NukedSc55::LaAvailable(const int d) const
{
	return d >= 0 && d < kLaDevices && la_have[d];
}

const char* NukedSc55::LaVersion(const int d) const
{
	return LaAvailable(d) ? la_version[d] : "";
}

// SETUP menu "Device": the units are created again as the chosen device, like swapping the module
// (what the host sent before is gone; hosts and games send their setup at the start of a song).
// Audio thread; Activate() boots the units in parallel.
void NukedSc55::LaSwitchDevice()
{
	if (last_sample_rate <= 0.0) return; // not activated yet: Activate() takes the chosen device
	log("MT-32: device %d -> %d", la_active.load(), la_device.load());
	Activate(last_sample_rate, 1, last_max_frames);
}

// The MT-32's switches and SELECT (the device's knob) on all units. The knob is a potentiometer
// (0..1023, starts at the top = "vol:100"); one SELECT detent moves it by kLaKnobStep.: they start identical and get
// the same messages, so the same presses keep their parameters identical.
void NukedSc55::LaPanelInput()
{
	const uint32_t buttons = ui_panel_buttons.load(std::memory_order_relaxed);
	const int detents      = ui_encoder.exchange(0);
	if (buttons == applied_buttons && detents == 0) return;
	for (auto& inst : instances) {
		if (!inst.ctx) continue;
		if (buttons != applied_buttons) emu88_set_panel_buttons(inst.ctx, buttons);
		if (detents != 0) emu88_turn_panel_encoder(inst.ctx, detents * kLaKnobStep);
	}
	applied_buttons = buttons;
}

bool NukedSc55::GetLaText(char* buf, const size_t size)
{
	std::unique_lock lock(units_mx, std::try_to_lock);
	if (!lock.owns_lock() || !lcd_ready.load() || instances.empty() || !instances[0].ctx || size == 0) return false;
	buf[0] = 0;
	emu88_get_display_text(instances[0].ctx, 0, buf, size);
	return true;
}

bool NukedSc55::GetLaDots(uint8_t* dots, const size_t size)
{
	std::unique_lock lock(units_mx, std::try_to_lock);
	if (!lock.owns_lock() || !lcd_ready.load() || instances.empty() || !instances[0].ctx) return false;
	int w = 0, h = 0, on = 0;
	if (!emu88_get_display_pixels(instances[0].ctx, 0, dots, size, &w, &h, &on) || w != 120 || h < 8) return false;
	if (!on) std::memset(dots, 0, size);
	return true;
}
#endif
