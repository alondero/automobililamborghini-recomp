#include <stdexcept>
#include <vector>

#include "lambo_config.h"
#include "lambo_cheats.h"
#include "lambo_player_name.h"
#include "recompui/config.h"

namespace lambo::ui {
namespace {
using recomp::config::Config;
using recomp::config::ConfigValueVariant;
using recomp::config::OptionChangeContext;
using namespace ultramodern::renderer;
GraphicsConfig seeded_graphics;

// Common desktop resolutions for the windowed-mode size picker (16:9 plus
// legacy 16:10/4:3 favourites). The resolved size stays in graphics.json
// (window_width/window_height) exactly as before; the picker only writes it.
struct WindowPreset { uint32_t value; int width; int height; };
constexpr WindowPreset kWindowPresets[] = {
    {0, 640, 360}, {1, 1280, 720}, {2, 1600, 900}, {3, 1920, 1080},
    {4, 2560, 1440}, {5, 3840, 2160}, {6, 1280, 800}, {7, 1920, 1200},
    {8, 1280, 960}, {9, 1600, 1200}, {10, 1920, 1440},
};
constexpr uint32_t kWindowPresetCustom = 11;

uint32_t window_preset_from_size(int width, int height) {
    for (const auto& preset : kWindowPresets) {
        if (preset.width == width && preset.height == height) return preset.value;
    }
    return kWindowPresetCustom;
}

// Index of a picker's entry in the registered schema, resolved by value.
// update_enum_option_disabled takes an index, so reordering kWindowPresets
// would otherwise silently disable the wrong row. Throws if the schema and the
// table disagree, which can only happen from a code change.
size_t window_preset_option_index(const Config& page, uint32_t value) {
    const auto& schema = page.get_config_schema();
    const auto& options = std::get<recomp::config::ConfigOptionEnum>(
        schema.options.at(schema.options_by_id.at("window_size")).variant).options;
    for (size_t index = 0; index < options.size(); ++index) {
        if (options[index].value == value) return index;
    }
    throw std::runtime_error("window size preset is missing from the picker schema");
}

void sync_value(Config& page, const std::string& id, ConfigValueVariant value) {
    if (page.get_option_value(id) == value) return;
    page.update_option_value(id, value);
    if (page.requires_confirmation) page.apply_option_value(id);
}

void seed_graphics() {
    namespace port = lambo::config;
    auto& page = recompui::config::get_graphics_config();
    const auto cfg = lambo::config::current_graphics();
#define ENUM(field) sync_value(page, #field, static_cast<uint32_t>(cfg.field))
    ENUM(res_option); ENUM(wm_option); ENUM(hr_option); ENUM(api_option);
    ENUM(ar_option); ENUM(msaa_option); ENUM(rr_option); ENUM(hpfb_option);
    ENUM(ds_option);
#undef ENUM
    sync_value(page, "rr_manual_value", double(cfg.rr_manual_value));
    // Picker state for the (possibly hand-edited) live size. The picker applies
    // with this page's Apply button; seeding only updates the displayed value.
    const uint32_t live_preset = window_preset_from_size(port::window_size().width, port::window_size().height);
    sync_value(page, "window_size", live_preset);
    // Custom means "keep the current size", so grey it out unless Custom is
    // already the live size. Selecting it over a preset would be a no-op.
    page.update_enum_option_disabled("window_size",
        uint32_t(window_preset_option_index(page, kWindowPresetCustom)),
        live_preset != kWindowPresetCustom);
    // No texture_pack entry: the Mods tab owns pack installation, activation and
    // ordering. The legacy graphics.json key stays readable by lambo_config and
    // is preserved verbatim by the save path, so an existing pack keeps loading.
    sync_value(page, "texture_dump", port::texture_dump_dir());
    page.revert_temp_config();
    seeded_graphics = cfg;
}

void apply_graphics() {
    auto& page = recompui::config::get_graphics_config();
    auto cfg = lambo::config::current_graphics();
#define ENUM(field) { const auto value = static_cast<decltype(cfg.field)>(std::get<uint32_t>(page.get_option_value(#field))); if (value != seeded_graphics.field) cfg.field = value; }
    ENUM(res_option); ENUM(wm_option); ENUM(hr_option); ENUM(api_option);
    ENUM(ar_option); ENUM(msaa_option); ENUM(rr_option); ENUM(hpfb_option);
    ENUM(ds_option);
#undef ENUM
    const int rate = int(std::get<double>(page.get_option_value("rr_manual_value")));
    if (rate != seeded_graphics.rr_manual_value) cfg.rr_manual_value = rate;
    // developer_mode is not read here: the Debug tab owns it, and cfg starts
    // from current_graphics() so Apply carries the Debug tab's value through.
    lambo::config::apply_graphics(cfg);
}

void boolean(Config& page, const std::string& id, const std::string& label,
             bool initial, std::function<void(bool)> setter) {
    page.add_bool_option(id, label, "", initial);
    page.add_option_change_callback(id, [setter](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
        if (context == OptionChangeContext::Permanent) setter(std::get<bool>(value));
    });
}

void number(Config& page, const std::string& id, const std::string& label,
            double initial, double min, double max, double step, std::function<void(double)> setter) {
    page.add_number_option(id, label, "", min, max, step, 2, false, initial);
    page.add_option_change_callback(id, [setter](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
        if (context == OptionChangeContext::Permanent) setter(std::get<double>(value));
    });
}

// description is defaulted so this stays drop-in with the helpers above; the
// driver name is the only string option here and its validation rule is only
// discoverable from the description.
void text(Config& page, const std::string& id, const std::string& label,
           std::string initial, std::function<void(std::string)> setter,
           const std::string& description = "") {
    page.add_string_option(id, label, description, initial);
    page.add_option_change_callback(id, [setter](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
        if (context == OptionChangeContext::Permanent) setter(std::get<std::string>(value));
    });
}

// Whether this option alone holds an unapplied edit. is_dirty() is page-wide, so
// on General it would also report the rumble, deadzone and background-input
// sliders and wrongly freeze the name field.
bool option_pending_edit(const Config& page, const std::string& id) {
    const auto& by_id = page.get_config_schema().options_by_id;
    const auto it = by_id.find(id);
    return it != by_id.end() && page.modified_options.contains(it->second);
}
}

void refresh_frontend_settings() {
    namespace port = lambo::config;
    auto& graphics = recompui::config::get_graphics_config();
    if (!graphics.is_dirty() && seeded_graphics != port::current_graphics()) seed_graphics();
    auto& enhancements = recompui::config::get_config("enhancements");
    auto& cheats = recompui::config::get_config("cheats");
    auto& debug = recompui::config::get_config("debug");
    sync_value(debug, "developer_mode", port::developer_mode());
    for (const auto& entry : lambo::cheats::catalog)
        sync_value(cheats, entry.id, lambo::cheats::enabled(entry.cheat));
    sync_value(enhancements, "fog_match", port::widescreen_fog_match());
    sync_value(enhancements, "sky_match", port::widescreen_sky_match());
    sync_value(enhancements, "no_lod", port::no_lod());
    sync_value(enhancements, "automatic_pit_stops", port::automatic_pit_stops());
    sync_value(enhancements, "menu_stick_sensitivity", port::menu_stick_sensitivity());
    for (int i = 0; i < 6; ++i) sync_value(enhancements, "circuit_" + std::to_string(i + 1), port::no_lod_circuit(i));
    sync_value(enhancements, "draw_distance", port::global_draw_distance());
    sync_value(enhancements, "fog_scale", port::global_fog_scale());
    sync_value(enhancements, "camera_distance", port::camera_distance_scale());
    sync_value(enhancements, "camera_height", port::camera_height_scale());
    sync_value(enhancements, "camera_fov", port::camera_fov_add());
    auto& general = recompui::config::get_general_config();
    // Refreshed only while the option is clean, so a Championship save can
    // appear in the page without replacing a text edit still being typed.
    if (!option_pending_edit(general, "name")) sync_value(general, "name", lambo::player::saved_name());
    sync_value(general, "show_launcher", port::show_launcher());
}

void create_frontend_settings() {
    namespace settings = recompui::config;
    namespace port = lambo::config;
    // The driver name is a staged text edit: player.json is written once when
    // Apply publishes the field, not on every keystroke. General is the only page
    // the framework offers for it now, so it takes the confirmation footer the
    // Driver tab had, and rumble/deadzone/background input share that footer.
    // This reference dies at the next create_*_tab call, so finish here.
    auto& general = settings::create_general_tab({.has_rumble_strength = true, .has_gyro_sensitivity = false, .has_mouse_sensitivity = false});
    general.requires_confirmation = true;
    text(general, "name", "Driver name", lambo::player::saved_name(), [](std::string name) {
        // save_config() re-applies every option, so publish only a real change.
        if (name != lambo::player::saved_name()) lambo::player::set_saved_name(name);
    }, "Player one: 1-12 letters or spaces. Also saved by the Championship name editor.");
    boolean(general, "show_launcher", "Show launcher at startup", port::show_launcher(), port::set_show_launcher);
    auto& graphics = settings::create_graphics_tab();
    // The port remains the single owner of graphics.json, including unknown keys,
    // environment overrides, enhancement values and restart-only API changes.
    graphics.external_storage = true;
    graphics.update_option_description("api_option", "Graphics backend. Changes take effect after restarting the application.");
    // The framework's Graphics page carries its own developer_mode option. It is
    // a diagnostic overlay rather than a graphics setting, so the Debug tab owns
    // the control and this copy is hidden to avoid two owners of one value.
    //
    // Hiding rather than deleting is forced: Config has no remove_option. The
    // hidden copy stays inert because the port replaces both graphics-tab
    // callbacks below, so the framework's apply_graphics_config() -- the only
    // other reader of this field -- cannot run from load/save. Its one direct
    // caller, graphics::toggle_fullscreen(), is reached only via
    // recompinput::handle_event, and main.cpp consumes F11/Alt-Enter before the
    // frontend handler sees them, routing F11 to update_saved_window_mode()
    // instead. If that interception is ever removed, re-check this: the
    // framework path would then publish this never-seeded default as the live
    // developer_mode.
    graphics.update_option_hidden("developer_mode", true);
    // Window size picker: common desktop resolutions. The picker obeys this
    // page's confirmation flow -- a pick only stages the value; Apply resolves
    // it into the saved window_width/window_height (see the save callback).
    // "(restart)" is in the label because the resolved size is only read at
    // SDL_CreateWindow in main.cpp, as the old width/height sliders stated.
    std::vector<recomp::config::ConfigOptionEnumOption> window_preset_options;
    for (const auto& preset : kWindowPresets) {
        const std::string preset_key = std::to_string(preset.width) + "x" + std::to_string(preset.height);
        window_preset_options.emplace_back(preset.value, preset_key, preset_key);
    }
    window_preset_options.emplace_back(kWindowPresetCustom, "Custom", "Custom");
    graphics.add_enum_option("window_size", "Window size (restart)",
        "Windowed size, applied with the Apply button. Pick a common resolution; a size typed directly into graphics.json shows as Custom and is kept unless you pick a preset.",
        window_preset_options,
        window_preset_from_size(port::window_size().width, port::window_size().height));
    // Registered last: seed_graphics reads the window_size schema entry, so the
    // load callback must not run before the option exists.
    graphics.set_load_callback(seed_graphics);
    graphics.add_string_option("texture_dump", "Texture dump directory (restart)", "Destination for dumped textures. Leave blank to disable. Texture packs are installed and enabled in the Mods tab.", port::texture_dump_dir());
    graphics.set_save_callback([] {
        apply_graphics();
        auto& page = recompui::config::get_graphics_config();
        // The window-size picker resolves at Apply time. Custom (or an unknown
        // value) keeps the live size, so a discarded pick or an unrelated save
        // never clobbers a size the player set by hand.
        const uint32_t picked_preset = std::get<uint32_t>(page.get_option_value("window_size"));
        lambo::config::WindowSize size = lambo::config::window_size();
        for (const auto& preset : kWindowPresets) {
            if (preset.value == picked_preset) {
                size = {preset.width, preset.height};
                break;
            }
        }
        lambo::config::set_window_size(size);
        lambo::config::set_texture_dump_dir(std::get<std::string>(page.get_option_value("texture_dump")));
        // Seed after every field has been published so Apply leaves the UI
        // and the port snapshot in agreement with the saved values.
        seed_graphics();
    });

    auto& enhancements = settings::create_config_tab("Enhancements", "enhancements", false);
    enhancements.external_storage = true;
    boolean(enhancements, "automatic_pit_stops", "Automatic pit-stops (refuelling and tyres)", port::automatic_pit_stops(), port::set_automatic_pit_stops);
    boolean(enhancements, "fog_match", "Match multiplayer fog to single player", port::widescreen_fog_match(), port::set_widescreen_fog_match);
    boolean(enhancements, "sky_match", "Show sky in 3-4 player races", port::widescreen_sky_match(), port::set_widescreen_sky_match);
    boolean(enhancements, "no_lod", "Full track geometry", port::no_lod(), port::set_no_lod);
    for (int circuit = 0; circuit < 6; ++circuit) {
        boolean(enhancements, "circuit_" + std::to_string(circuit + 1), "Full geometry: circuit " + std::to_string(circuit + 1),
            port::no_lod_circuit(circuit), [circuit](bool value) { port::set_no_lod_circuit(circuit, value); });
    }
    number(enhancements, "draw_distance", "Draw distance (0 = unlimited)", port::global_draw_distance(), 0, 3, .25, port::set_global_draw_distance);
    number(enhancements, "fog_scale", "Fog density", port::global_fog_scale(), 0, 2, .05, port::set_global_fog_scale);
    number(enhancements, "camera_distance", "Camera distance", port::camera_distance_scale(), .2, 3, .05, port::set_camera_distance_scale);
    number(enhancements, "camera_height", "Camera height", port::camera_height_scale(), .2, 3, .05, port::set_camera_height_scale);
    number(enhancements, "camera_fov", "Additional field of view (degrees)", port::camera_fov_add(), -20, 60, 1, port::set_camera_fov_add);
    number(enhancements, "menu_stick_sensitivity", "Menu stick sensitivity", port::menu_stick_sensitivity(), 1.0, 2.5, 0.1, port::set_menu_stick_sensitivity);

    auto& cheats = settings::create_config_tab("Cheats", "cheats", false);
    cheats.external_storage = true; // Session-only: never restore cheats on launch.
    for (const auto& entry : lambo::cheats::catalog) {
        boolean(cheats, entry.id, entry.name, lambo::cheats::enabled(entry.cheat),
            [cheat = entry.cheat](bool value) { lambo::cheats::set_enabled(cheat, value); });
        cheats.update_option_description(entry.id, entry.description);
    }

    // Diagnostic options, kept out of the player-facing Graphics page. The port
    // still owns the graphics.json key, so the tab borrows the schema only.
    auto& debug = settings::create_config_tab("Debug", "debug", false);
    debug.external_storage = true;
    boolean(debug, "developer_mode", "Developer mode", port::developer_mode(), port::set_developer_mode);
    debug.update_option_description("developer_mode", "RT64 developer overlay. Changes take effect after restarting the application.");

    // Button bindings stays its own tab for now. Grouping it with Driving under
    // one Controls destination is tracked in issue #248 and is not done here.
    settings::create_controls_tab("Button bindings");
    settings::create_mods_tab();
}
}
