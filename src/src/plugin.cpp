#include <cassert>
#include <cstring>
#include <string>

#include "nuked_sc55.h"
#include "plugin.h"

//////////////////////////////////////////////////////////////////////////////
// Plugin descriptors
//////////////////////////////////////////////////////////////////////////////

// Number of plugins in this dynamic library
constexpr auto NumPlugins = 6;

// Product family in the plugin names (each build only contains its own family name)
#if defined(NUKED_SC55_DEVICE_8850)
#define NUKED_PLUGIN_FAMILY "Nuked SC-8850 P256"
#elif defined(NUKED_SC55_ENGINE_88PRO)
#define NUKED_PLUGIN_FAMILY "Nuked SC-88 P256"
#else
#define NUKED_PLUGIN_FAMILY "Nuked SC-55 P256"
#endif
constexpr auto Vendor  = PLUGIN_VENDOR;
constexpr auto Url     = PLUGIN_URL;
constexpr auto Version = PLUGIN_VERSION_STRING;

const char* Features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                          CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                          CLAP_PLUGIN_FEATURE_STEREO,
                          nullptr};

static const clap_plugin_descriptor_t plugin_descriptor_sc55_v1_00 = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = "net.nuked_sc55_poly_clap.sc55_v1_00",
        .name         = NUKED_PLUGIN_FAMILY " — Roland SC-55 v1.00",
        .vendor       = Vendor,
        .url          = Url,
        .manual_url   = Url,
        .support_url  = Url,
        .version      = Version,
        .description = PLUGIN_DESCRIPTION_PREFIX " v1.00 " PLUGIN_DESCRIPTION_SUFFIX,
        .features = Features};

static const clap_plugin_descriptor_t plugin_descriptor_sc55_v1_10 = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = "net.nuked_sc55_poly_clap.sc55_v1_10",
        .name         = NUKED_PLUGIN_FAMILY " — Roland SC-55 v1.10",
        .vendor       = Vendor,
        .url          = Url,
        .manual_url   = Url,
        .support_url  = Url,
        .version      = Version,
        .description = PLUGIN_DESCRIPTION_PREFIX " v1.10 " PLUGIN_DESCRIPTION_SUFFIX,
        .features = Features};

static const clap_plugin_descriptor_t plugin_descriptor_sc55_v1_20 = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = "net.nuked_sc55_poly_clap.sc55_v1_20",
        .name         = NUKED_PLUGIN_FAMILY " — Roland SC-55 v1.20",
        .vendor       = Vendor,
        .url          = Url,
        .manual_url   = Url,
        .support_url  = Url,
        .version      = Version,
        .description = PLUGIN_DESCRIPTION_PREFIX " v1.20 " PLUGIN_DESCRIPTION_SUFFIX,
        .features = Features};

static const clap_plugin_descriptor_t plugin_descriptor_sc55_v1_21 = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = "net.nuked_sc55_poly_clap.sc55_v1_21",
        .name         = NUKED_PLUGIN_FAMILY " — Roland SC-55 v1.21",
        .vendor       = Vendor,
        .url          = Url,
        .manual_url   = Url,
        .support_url  = Url,
        .version      = Version,
        .description = PLUGIN_DESCRIPTION_PREFIX " v1.21 " PLUGIN_DESCRIPTION_SUFFIX,
        .features = Features};

static const clap_plugin_descriptor_t plugin_descriptor_sc55_v2_00 = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = "net.nuked_sc55_poly_clap.sc55_v2_00",
        .name         = NUKED_PLUGIN_FAMILY " — Roland SC-55 v2.00",
        .vendor       = Vendor,
        .url          = Url,
        .manual_url   = Url,
        .support_url  = Url,
        .version      = Version,
        .description = PLUGIN_DESCRIPTION_PREFIX " v2.00 " PLUGIN_DESCRIPTION_SUFFIX,
        .features = Features};

static const clap_plugin_descriptor_t plugin_descriptor_sc55mk2_v1_01 = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = "net.nuked_sc55_poly_clap.sc55mk2_v1_01",
        .name         = NUKED_PLUGIN_FAMILY " — Roland SC-55mk2 v1.01",
        .vendor       = Vendor,
        .url          = Url,
        .manual_url   = Url,
        .support_url  = Url,
        .version      = Version,
        .description = PLUGIN_DESCRIPTION_PREFIX "mk2 v1.01 " PLUGIN_DESCRIPTION_SUFFIX,
        .features = Features};
#ifdef NUKED_SC55_ENGINE_88PRO
static const clap_plugin_descriptor_t plugin_descriptor_sc88pro = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = "net.nuked_sc55_poly_clap.sc88pro",
        .name         = NUKED_PLUGIN_FAMILY " — Roland SC-88 Pro",
        .vendor       = Vendor,
        .url          = Url,
        .manual_url   = Url,
        .support_url  = Url,
        .version      = Version,
        .description = "Roland SC-88 Pro emulation (88emu core) with extended polyphony",
        .features = Features};
static const clap_plugin_descriptor_t plugin_descriptor_sc8850 = {
        .clap_version = CLAP_VERSION_INIT,
        .id           = "net.nuked_sc55_poly_clap.sc8850",
        .name         = NUKED_PLUGIN_FAMILY " — Roland SC-8850",
        .vendor       = Vendor,
        .url          = Url,
        .manual_url   = Url,
        .support_url  = Url,
        .version      = Version,
        .description = "Roland SC-8850 emulation (88emu core) with extended polyphony",
        .features = Features};
#endif


//////////////////////////////////////////////////////////////////////////////
// Extensions
//////////////////////////////////////////////////////////////////////////////

static const clap_plugin_note_ports_t extension_note_ports = {
        .count = [](const clap_plugin_t* plugin, bool is_input) -> uint32_t {
	        // SC-88 Pro: MIDI IN A (parts A01-A16) and MIDI IN B (B01-B16); SC-8850: IN A..D
	        return is_input ? NukedSc55::kNumPorts : 0;
        },

        .get = [](const clap_plugin_t* plugin, uint32_t index, bool is_input,
                  clap_note_port_info_t* info) -> bool {
	        if (!is_input || index >= NukedSc55::kNumPorts) {
		        return false;
	        }

	        info->id = index;

	        // We don't support CLAP_NOTE_DIALECT_CLAP because we want to
	        // force the sending of RAW MIDI messages at all times.
	        info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
	        info->preferred_dialect  = CLAP_NOTE_DIALECT_MIDI;

	        if (NukedSc55::kNumPorts > 1) {
		        snprintf(info->name, sizeof(info->name), "MIDI IN %c", static_cast<char>('A' + index));
	        } else {
		        snprintf(info->name, sizeof(info->name), "%s", "Note Port");
	        }

	        return true;
        }};

static const clap_plugin_audio_ports_t extension_audio_ports = {
        .count = [](const clap_plugin_t* plugin, bool is_input) -> uint32_t {
	        return is_input ? 0 : 1;
        },

        .get = [](const clap_plugin_t* plugin, uint32_t index, bool is_input,
                  clap_audio_port_info_t* info) -> bool {
	        if (is_input || index) {
		        return false;
	        }

	        info->id            = 0;
	        info->channel_count = 2; // stereo
	        info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
	        info->port_type     = CLAP_PORT_STEREO;
	        info->in_place_pair = CLAP_INVALID_ID;

	        snprintf(info->name, sizeof(info->name), "%s", "Audio Output");

	        return true;
        }};

static const clap_plugin_state_t extension_state = {
        .save = [](const clap_plugin_t* plugin, const clap_ostream_t* stream) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->SaveState(stream);
        },

        .load = [](const clap_plugin_t* plugin, const clap_istream_t* stream) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->LoadState(stream);
        }};

//////////////////////////////////////////////////////////////////////////////
// Plugin classes
//////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#include "gui/editor.h"
#ifndef NUKED_SC55_DISPLAY_NAME
#define NUKED_SC55_GUI_NAME_FALLBACK 1
#endif

static NukedSc55* Engine(const clap_plugin_t* plugin)
{
	return static_cast<NukedSc55*>(plugin->plugin_data);
}

static const clap_plugin_gui_t extension_gui = {
        .is_api_supported = [](const clap_plugin_t*, const char* api, bool is_floating) -> bool {
	        return std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0 && !is_floating;
        },
        .get_preferred_api = [](const clap_plugin_t*, const char** api, bool* is_floating) -> bool {
	        *api         = CLAP_WINDOW_API_WIN32;
	        *is_floating = false;
	        return true;
        },
        .create = [](const clap_plugin_t* plugin, const char* api, bool is_floating) -> bool {
	        if (std::strcmp(api, CLAP_WINDOW_API_WIN32) != 0 || is_floating) return false;
	        auto ns = Engine(plugin);
	        if (!ns->editor) {
#ifdef NUKED_SC55_DISPLAY_NAME
		        ns->editor = editor::Create(ns, NUKED_SC55_DISPLAY_NAME);
#else
		        char name[64];
		        std::snprintf(name, sizeof(name), "Nuked-%s", ns->ModelName());
		        ns->editor = editor::Create(ns, name);
#endif
	        }
	        return ns->editor != nullptr;
        },
        .destroy = [](const clap_plugin_t* plugin) {
	        auto ns = Engine(plugin);
	        if (ns->editor) {
		        editor::Destroy(ns->editor);
		        ns->editor = nullptr;
	        }
        },
        .set_scale = [](const clap_plugin_t*, double) -> bool { return false; },
        .get_size = [](const clap_plugin_t*, uint32_t* w, uint32_t* h) -> bool {
	        *w = editor::Width;
	        *h = editor::Height;
	        return true;
        },
        .can_resize = [](const clap_plugin_t*) -> bool { return false; },
        .get_resize_hints = [](const clap_plugin_t*, clap_gui_resize_hints_t*) -> bool { return false; },
        .adjust_size = [](const clap_plugin_t*, uint32_t* w, uint32_t* h) -> bool {
	        *w = editor::Width;
	        *h = editor::Height;
	        return true;
        },
        .set_size = [](const clap_plugin_t*, uint32_t w, uint32_t h) -> bool {
	        return w == (uint32_t)editor::Width && h == (uint32_t)editor::Height;
        },
        .set_parent = [](const clap_plugin_t* plugin, const clap_window_t* window) -> bool {
	        auto ns = Engine(plugin);
	        return ns->editor && editor::Open(ns->editor, window->win32);
        },
        .set_transient = [](const clap_plugin_t*, const clap_window_t*) -> bool { return false; },
        .suggest_title = [](const clap_plugin_t*, const char*) {},
        .show = [](const clap_plugin_t* plugin) -> bool {
	        editor::Show(Engine(plugin)->editor, true);
	        return true;
        },
        .hide = [](const clap_plugin_t* plugin) -> bool {
	        editor::Show(Engine(plugin)->editor, false);
	        return true;
        },
};
#endif

static const void* get_extension(const clap_plugin* plugin, const char* id)
{
#ifdef _WIN32
	if (strcmp(id, CLAP_EXT_GUI) == 0) {
		return &extension_gui;
	}
#endif
	if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
		return &extension_note_ports;

	} else if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
		return &extension_audio_ports;

	} else if (strcmp(id, CLAP_EXT_STATE) == 0) {
		return &extension_state;

	} else {
		return nullptr;
	}
}

//----------------------------------------------------------------------------
// SC-55 v1.00
//----------------------------------------------------------------------------
static const clap_plugin_t my_plugin_class_sc55_v1_00 = {

        .desc = &plugin_descriptor_sc55_v1_00,

        .plugin_data = nullptr,

        .init = [](const clap_plugin* plugin) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Init(plugin);
        },

        .destroy =
                [](const clap_plugin* plugin) {
	                auto the_plugin = (NukedSc55*)plugin->plugin_data;
	                the_plugin->Shutdown();
	                delete the_plugin;
                },

        .activate = [](const clap_plugin* plugin, double sample_rate,
                       uint32_t min_frame_count, uint32_t max_frame_count) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Activate(sample_rate, min_frame_count, max_frame_count);
        },

        .deactivate = [](const clap_plugin* plugin) {},

        .start_processing = [](const clap_plugin* plugin) -> bool {
	        return true;
        },

        .stop_processing = [](const clap_plugin* plugin) {},

        .reset = [](const clap_plugin* plugin) {},

        .process = [](const clap_plugin* plugin,
                      const clap_process_t* process) -> clap_process_status {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Process(process);
        },

        .get_extension = [](const clap_plugin* plugin, const char* id) -> const void* {
	        return get_extension(plugin, id);
        },

        .on_main_thread = [](const clap_plugin* plugin) {}};

//----------------------------------------------------------------------------
// SC-55 v1.10
//----------------------------------------------------------------------------
static const clap_plugin_t my_plugin_class_sc55_v1_10 = {

        .desc = &plugin_descriptor_sc55_v1_10,

        .plugin_data = nullptr,

        .init = [](const clap_plugin* plugin) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Init(plugin);
        },

        .destroy =
                [](const clap_plugin* plugin) {
	                auto the_plugin = (NukedSc55*)plugin->plugin_data;
	                the_plugin->Shutdown();
	                delete the_plugin;
                },

        .activate = [](const clap_plugin* plugin, double sample_rate,
                       uint32_t min_frame_count, uint32_t max_frame_count) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Activate(sample_rate, min_frame_count, max_frame_count);
        },

        .deactivate = [](const clap_plugin* plugin) {},

        .start_processing = [](const clap_plugin* plugin) -> bool {
	        return true;
        },

        .stop_processing = [](const clap_plugin* plugin) {},

        .reset = [](const clap_plugin* plugin) {},

        .process = [](const clap_plugin* plugin,
                      const clap_process_t* process) -> clap_process_status {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Process(process);
        },

        .get_extension = [](const clap_plugin* plugin, const char* id) -> const void* {
	        return get_extension(plugin, id);
        },

        .on_main_thread = [](const clap_plugin* plugin) {}};

//----------------------------------------------------------------------------
// SC-55 v1.20
//----------------------------------------------------------------------------
static const clap_plugin_t my_plugin_class_sc55_v1_20 = {

        .desc = &plugin_descriptor_sc55_v1_20,

        .plugin_data = nullptr,

        .init = [](const clap_plugin* plugin) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Init(plugin);
        },

        .destroy =
                [](const clap_plugin* plugin) {
	                auto the_plugin = (NukedSc55*)plugin->plugin_data;
	                the_plugin->Shutdown();
	                delete the_plugin;
                },

        .activate = [](const clap_plugin* plugin, double sample_rate,
                       uint32_t min_frame_count, uint32_t max_frame_count) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Activate(sample_rate, min_frame_count, max_frame_count);
        },

        .deactivate = [](const clap_plugin* plugin) {},

        .start_processing = [](const clap_plugin* plugin) -> bool {
	        return true;
        },

        .stop_processing = [](const clap_plugin* plugin) {},

        .reset = [](const clap_plugin* plugin) {},

        .process = [](const clap_plugin* plugin,
                      const clap_process_t* process) -> clap_process_status {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Process(process);
        },

        .get_extension = [](const clap_plugin* plugin, const char* id) -> const void* {
	        return get_extension(plugin, id);
        },

        .on_main_thread = [](const clap_plugin* plugin) {}};

//----------------------------------------------------------------------------
// SC-55 v1.21
//----------------------------------------------------------------------------
static const clap_plugin_t my_plugin_class_sc55_v1_21 = {

        .desc = &plugin_descriptor_sc55_v1_21,

        .plugin_data = nullptr,

        .init = [](const clap_plugin* plugin) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Init(plugin);
        },

        .destroy =
                [](const clap_plugin* plugin) {
	                auto the_plugin = (NukedSc55*)plugin->plugin_data;
	                the_plugin->Shutdown();
	                delete the_plugin;
                },

        .activate = [](const clap_plugin* plugin, double sample_rate,
                       uint32_t min_frame_count, uint32_t max_frame_count) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Activate(sample_rate, min_frame_count, max_frame_count);
        },

        .deactivate = [](const clap_plugin* plugin) {},

        .start_processing = [](const clap_plugin* plugin) -> bool {
	        return true;
        },

        .stop_processing = [](const clap_plugin* plugin) {},

        .reset = [](const clap_plugin* plugin) {},

        .process = [](const clap_plugin* plugin,
                      const clap_process_t* process) -> clap_process_status {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Process(process);
        },

        .get_extension = [](const clap_plugin* plugin, const char* id) -> const void* {
	        return get_extension(plugin, id);
        },

        .on_main_thread = [](const clap_plugin* plugin) {}};

//----------------------------------------------------------------------------
// SC-55 v2.00
//----------------------------------------------------------------------------
static const clap_plugin_t my_plugin_class_sc55_v2_00 = {

        .desc = &plugin_descriptor_sc55_v2_00,

        .plugin_data = nullptr,

        .init = [](const clap_plugin* plugin) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Init(plugin);
        },

        .destroy =
                [](const clap_plugin* plugin) {
	                auto the_plugin = (NukedSc55*)plugin->plugin_data;
	                the_plugin->Shutdown();
	                delete the_plugin;
                },

        .activate = [](const clap_plugin* plugin, double sample_rate,
                       uint32_t min_frame_count, uint32_t max_frame_count) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Activate(sample_rate, min_frame_count, max_frame_count);
        },

        .deactivate = [](const clap_plugin* plugin) {},

        .start_processing = [](const clap_plugin* plugin) -> bool {
	        return true;
        },

        .stop_processing = [](const clap_plugin* plugin) {},

        .reset = [](const clap_plugin* plugin) {},

        .process = [](const clap_plugin* plugin,
                      const clap_process_t* process) -> clap_process_status {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Process(process);
        },

        .get_extension = [](const clap_plugin* plugin, const char* id) -> const void* {
	        return get_extension(plugin, id);
        },

        .on_main_thread = [](const clap_plugin* plugin) {}};

//----------------------------------------------------------------------------
// SC-55 mk2 v1.01
//----------------------------------------------------------------------------
static const clap_plugin_t my_plugin_class_sc55mk2_v1_01 = {

        .desc = &plugin_descriptor_sc55mk2_v1_01,

        .plugin_data = nullptr,

        .init = [](const clap_plugin* plugin) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Init(plugin);
        },

        .destroy =
                [](const clap_plugin* plugin) {
	                auto the_plugin = (NukedSc55*)plugin->plugin_data;
	                the_plugin->Shutdown();
	                delete the_plugin;
                },

        .activate = [](const clap_plugin* plugin, double sample_rate,
                       uint32_t min_frame_count, uint32_t max_frame_count) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Activate(sample_rate, min_frame_count, max_frame_count);
        },

        .deactivate = [](const clap_plugin* plugin) {},

        .start_processing = [](const clap_plugin* plugin) -> bool {
	        return true;
        },

        .stop_processing = [](const clap_plugin* plugin) {},

        .reset = [](const clap_plugin* plugin) {},

        .process = [](const clap_plugin* plugin,
                      const clap_process_t* process) -> clap_process_status {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Process(process);
        },

        .get_extension = [](const clap_plugin* plugin, const char* id) -> const void* {
	        return get_extension(plugin, id);
        },

        .on_main_thread = [](const clap_plugin* plugin) {}};
#ifdef NUKED_SC55_ENGINE_88PRO
//----------------------------------------------------------------------------
// SC-88 Pro
//----------------------------------------------------------------------------
static const clap_plugin_t my_plugin_class_sc88pro = {

        .desc = &plugin_descriptor_sc88pro,

        .plugin_data = nullptr,

        .init = [](const clap_plugin* plugin) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Init(plugin);
        },

        .destroy =
                [](const clap_plugin* plugin) {
	                auto the_plugin = (NukedSc55*)plugin->plugin_data;
	                the_plugin->Shutdown();
	                delete the_plugin;
                },

        .activate = [](const clap_plugin* plugin, double sample_rate,
                       uint32_t min_frame_count, uint32_t max_frame_count) -> bool {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Activate(sample_rate, min_frame_count, max_frame_count);
        },

        .deactivate = [](const clap_plugin* plugin) {},

        .start_processing = [](const clap_plugin* plugin) -> bool {
	        return true;
        },

        .stop_processing = [](const clap_plugin* plugin) {},

        .reset = [](const clap_plugin* plugin) {},

        .process = [](const clap_plugin* plugin,
                      const clap_process_t* process) -> clap_process_status {
	        auto the_plugin = (NukedSc55*)plugin->plugin_data;
	        return the_plugin->Process(process);
        },

        .get_extension = [](const clap_plugin* plugin, const char* id) -> const void* {
	        return get_extension(plugin, id);
        },

        .on_main_thread = [](const clap_plugin* plugin) {}};
// SC-8850: the same callbacks, its own descriptor
static const clap_plugin_t my_plugin_class_sc8850 = [] {
	clap_plugin_t c = my_plugin_class_sc88pro;
	c.desc          = &plugin_descriptor_sc8850;
	return c;
}();
#endif


//////////////////////////////////////////////////////////////////////////////
// Plugin factory
//////////////////////////////////////////////////////////////////////////////

static const clap_plugin_factory_t plugin_factory = {

        .get_plugin_count = [](const clap_plugin_factory* factory) -> uint32_t {
#ifdef NUKED_SC55_ONLY_MODEL
	        return 1;
#else
	        return NumPlugins;
#endif
        },

        .get_plugin_descriptor = [](const clap_plugin_factory* factory,
                                    uint32_t index) -> const clap_plugin_descriptor_t* {
#ifdef NUKED_SC55_ONLY_MODEL
	        // Single-model build: expose only model NUKED_SC55_ONLY_MODEL
	        if (index != 0) {
		        return nullptr;
	        }
	        index = NUKED_SC55_ONLY_MODEL;
#ifdef NUKED_SC55_DISPLAY_NAME
	        {
		        // Single-model build with a custom display name
		        static clap_plugin_descriptor_t renamed = {};
		        static bool init = false;
		        if (!init) {
			        const clap_plugin_descriptor_t* orig = nullptr;
			        switch (index) {
			        case 0: orig = &plugin_descriptor_sc55_v1_00; break;
			        case 1: orig = &plugin_descriptor_sc55_v1_10; break;
			        case 2: orig = &plugin_descriptor_sc55_v1_20; break;
			        case 3: orig = &plugin_descriptor_sc55_v1_21; break;
			        case 4: orig = &plugin_descriptor_sc55_v2_00; break;
#ifdef NUKED_SC55_ENGINE_88PRO
			        case 6: orig = &plugin_descriptor_sc88pro; break;
			        case 7: orig = &plugin_descriptor_sc8850; break;
#endif
			        default: orig = &plugin_descriptor_sc55mk2_v1_01; break;
			        }
			        renamed      = *orig;
			        renamed.name = NUKED_SC55_DISPLAY_NAME;
			        init         = true;
		        }
		        return &renamed;
	        }
#endif
#endif
	        if (index == 0) {
		        return &plugin_descriptor_sc55_v1_00;

	        } else if (index == 1) {
		        return &plugin_descriptor_sc55_v1_10;

	        } else if (index == 2) {
		        return &plugin_descriptor_sc55_v1_20;

	        } else if (index == 3) {
		        return &plugin_descriptor_sc55_v1_21;

	        } else if (index == 4) {
		        return &plugin_descriptor_sc55_v2_00;

	        } else if (index == 5) {
		        return &plugin_descriptor_sc55mk2_v1_01;
#ifdef NUKED_SC55_ENGINE_88PRO
	        } else if (index == 6) {
		        return &plugin_descriptor_sc88pro;
	        } else if (index == 7) {
		        return &plugin_descriptor_sc8850;
#endif

	        } else {
		        return nullptr;
	        }
        },

        .create_plugin = [](const clap_plugin_factory* factory, const clap_host_t* host,
                            const char* plugin_id) -> const clap_plugin_t* {
	        if (!clap_version_is_compatible(host->clap_version)) {
		        return nullptr;
	        }

#ifdef NUKED_SC55_ONLY_MODEL
	        if (strcmp(plugin_id,
	                   factory->get_plugin_descriptor(factory, 0)->id) != 0) {
		        return nullptr;
	        }
#endif
	        NukedSc55* the_plugin = nullptr;

	        if (strcmp(plugin_id, plugin_descriptor_sc55_v1_00.id) == 0) {
		        the_plugin = new NukedSc55(my_plugin_class_sc55_v1_00,
		                                   host,
		                                   NukedSc55::Model::Sc55_v1_00);

	        } else if (strcmp(plugin_id, plugin_descriptor_sc55_v1_10.id) == 0) {
		        the_plugin = new NukedSc55(my_plugin_class_sc55_v1_10,
		                                   host,
		                                   NukedSc55::Model::Sc55_v1_10);

	        } else if (strcmp(plugin_id, plugin_descriptor_sc55_v1_20.id) == 0) {
		        the_plugin = new NukedSc55(my_plugin_class_sc55_v1_20,
		                                   host,
		                                   NukedSc55::Model::Sc55_v1_20);

	        } else if (strcmp(plugin_id, plugin_descriptor_sc55_v1_21.id) == 0) {
		        the_plugin = new NukedSc55(my_plugin_class_sc55_v1_21,
		                                   host,
		                                   NukedSc55::Model::Sc55_v1_21);

	        } else if (strcmp(plugin_id, plugin_descriptor_sc55_v2_00.id) == 0) {
		        the_plugin = new NukedSc55(my_plugin_class_sc55_v2_00,
		                                   host,
		                                   NukedSc55::Model::Sc55_v2_00);

	        } else if (strcmp(plugin_id, plugin_descriptor_sc55mk2_v1_01.id) == 0) {
		        the_plugin = new NukedSc55(my_plugin_class_sc55mk2_v1_01,
		                                   host,
		                                   NukedSc55::Model::Sc55mk2_v1_01);
#ifdef NUKED_SC55_ENGINE_88PRO
	        } else if (strcmp(plugin_id, plugin_descriptor_sc88pro.id) == 0) {
		        the_plugin = new NukedSc55(my_plugin_class_sc88pro, host, NukedSc55::Model::Sc88Pro);
	        } else if (strcmp(plugin_id, plugin_descriptor_sc8850.id) == 0) {
		        the_plugin = new NukedSc55(my_plugin_class_sc8850, host, NukedSc55::Model::Sc8850);
#endif
	        } else {
		        return nullptr;
	        }

	        return the_plugin->GetPluginClass();
        }};

//////////////////////////////////////////////////////////////////////////////
// Dynamic library definition
//////////////////////////////////////////////////////////////////////////////

std::string plugin_path = {};

extern "C" const clap_plugin_entry_t clap_entry = {
        .clap_version = CLAP_VERSION_INIT,

        .init = [](const char* _plugin_path) -> bool {
	        plugin_path = std::string(_plugin_path);
	        return true;
        },

        .deinit = []() {},

        .get_factory = [](const char* factory_id) -> const void* {
	        return strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) ? nullptr
	                                                          : &plugin_factory;
        }};
