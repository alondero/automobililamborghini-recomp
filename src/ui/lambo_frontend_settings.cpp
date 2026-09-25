#include "lambo_config.h"
#include "lambo_player_name.h"
#include "recompui/config.h"

namespace lambo::ui {
namespace {
using recomp::config::Config;
using recomp::config::ConfigValueVariant;
using recomp::config::OptionChangeContext;
using namespace ultramodern::renderer;
GraphicsConfig seeded_graphics;

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
    sync_value(page, "developer_mode", cfg.developer_mode);
    sync_value(page, "window_width", double(port::window_size().width));
    sync_value(page, "window_height", double(port::window_size().height));
    sync_value(page, "texture_pack", port::texture_pack_path());
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
    const bool developer = std::get<bool>(page.get_option_value("developer_mode"));
    if (developer != seeded_graphics.developer_mode) cfg.developer_mode = developer;
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
}

void refresh_frontend_settings() {
    namespace port = lambo::config;
    auto& graphics = recompui::config::get_graphics_config();
    if (!graphics.is_dirty() && seeded_graphics != port::current_graphics()) seed_graphics();
    auto& enhancements = recompui::config::get_config("enhancements");
    sync_value(enhancements, "fog_match", port::widescreen_fog_match());
    sync_value(enhancements, "sky_match", port::widescreen_sky_match());
    sync_value(enhancements, "no_lod", port::no_lod());
    for (int i = 0; i < 6; ++i) sync_value(enhancements, "circuit_" + std::to_string(i + 1), port::no_lod_circuit(i));
    sync_value(enhancements, "draw_distance", port::global_draw_distance());
    sync_value(enhancements, "fog_scale", port::global_fog_scale());
    sync_value(enhancements, "camera_distance", port::camera_distance_scale());
    sync_value(enhancements, "camera_height", port::camera_height_scale());
    sync_value(enhancements, "camera_fov", port::camera_fov_add());
    auto& driver = recompui::config::get_config("driver");
    // The name editor is confirmation-backed. Refresh it only while clean so
    // a Championship save can appear in the page without overwriting an
    // in-progress text edit (and without touching player.json per frame).
    if (!driver.is_dirty()) sync_value(driver, "name", lambo::player::saved_name());
    sync_value(driver, "show_launcher", port::show_launcher());
}

void create_frontend_settings() {
    namespace settings = recompui::config;
    namespace port = lambo::config;
    settings::create_general_tab({.has_rumble_strength = true, .has_gyro_sensitivity = false, .has_mouse_sensitivity = false});
    auto& graphics = settings::create_graphics_tab();
    // The port remains the single owner of graphics.json, including unknown keys,
    // environment overrides, enhancement values and restart-only API changes.
    graphics.external_storage = true;
    graphics.set_load_callback(seed_graphics);
    graphics.update_option_description("api_option", "Graphics backend. Changes take effect after restarting the application.");
    graphics.update_option_description("developer_mode", "RT64 developer overlay. Changes take effect after restarting the application.");
    graphics.add_number_option("window_width", "Window width (restart)", "Initial window dimensions after restart.", 320, 7680, 1, 0, false, port::window_size().width);
    graphics.add_number_option("window_height", "Window height (restart)", "Initial window dimensions after restart.", 240, 4320, 1, 0, false, port::window_size().height);
    graphics.add_string_option("texture_pack", "Texture pack path (restart)", "RT64 texture replacement pack. Environment overrides take priority.", port::texture_pack_path());
    graphics.add_string_option("texture_dump", "Texture dump directory (restart)", "Destination for dumped textures. Leave blank to disable.", port::texture_dump_dir());
    graphics.set_save_callback([] {
        apply_graphics();
        auto& page = recompui::config::get_graphics_config();
        lambo::config::set_window_size({int(std::get<double>(page.get_option_value("window_width"))), int(std::get<double>(page.get_option_value("window_height")))});
        lambo::config::set_texture_pack_path(std::get<std::string>(page.get_option_value("texture_pack")));
        lambo::config::set_texture_dump_dir(std::get<std::string>(page.get_option_value("texture_dump")));
        // Seed after every field has been published so Apply leaves the UI
        // and the port snapshot in agreement with the saved values.
        seed_graphics();
    });

    auto& enhancements = settings::create_config_tab("Enhancements", "enhancements", false);
    enhancements.external_storage = true;
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

    settings::create_controls_tab("Button bindings");
    settings::create_mods_tab();
    auto& driver = settings::create_config_tab("Driver", "driver", true);
    driver.external_storage = true;
    driver.add_string_option("name", "Driver name", "Player one: 1-12 letters or spaces. Also saved by the Championship name editor.", lambo::player::saved_name());
    driver.add_option_change_callback("name", [](ConfigValueVariant value, ConfigValueVariant, OptionChangeContext context) {
        if (context == OptionChangeContext::Permanent) lambo::player::set_saved_name(std::get<std::string>(value));
    });
    boolean(driver, "show_launcher", "Show launcher at startup", port::show_launcher(), port::set_show_launcher);
}
}
