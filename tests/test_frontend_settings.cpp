#include <fstream>
#include <iostream>
#include <stdexcept>
#include "lambo_config.h"
#include "lambo_cheats.h"
#include "lambo_player_name.h"
#include "lambo_paths.h"
#include "ui/lambo_ui.h"
#include "ui/lambo_frontend_input.h"
#include "ui/lambo_frontend_overlay.h"
#include "librecomp/config.hpp"
#include "librecomp/game.hpp"
#include "recompui/config.h"
#include "recompinput/profiles.h"
#include "recompinput/input_events.h"

SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;
namespace lambo::ui { void create_frontend_settings(); void refresh_frontend_settings(); }
static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

// The registered picker entries, so the test reads the port's preset table
// instead of restating it.
static const std::vector<recomp::config::ConfigOptionEnumOption>& window_size_choices(const recomp::config::Config& graphics) {
    const auto& schema = graphics.get_config_schema();
    const auto& option = schema.options.at(schema.options_by_id.at("window_size"));
    return std::get<recomp::config::ConfigOptionEnum>(option.variant).options;
}

// The index of a picker entry, which is what get_enum_option_disabled takes.
// Values and indices currently coincide, but the disabled state is recorded by
// index, so resolve it instead of assuming they match.
static uint32_t window_size_index(const recomp::config::Config& graphics, const std::string& key) {
    uint32_t index = 0;
    for (const auto& choice : window_size_choices(graphics)) {
        if (choice.key == key) return index;
        ++index;
    }
    throw std::runtime_error("missing window size choice: " + key);
}

// Resolve a window-size picker entry by its display key (e.g. "1920x1080" or
// "Custom") straight from the registered schema.
static uint32_t window_size_choice(const recomp::config::Config& graphics, const std::string& key) {
    for (const auto& choice : window_size_choices(graphics)) {
        if (choice.key == key) return choice.value;
    }
    throw std::runtime_error("missing window size choice: " + key);
}

// Defined in recompui's base/ui_state.cpp. Declared here so the menu-action
// resolver's controller bindings can be checked without a render context.
int cont_button_to_key(SDL_ControllerButtonEvent& button);

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass an isolated test directory");
        const std::filesystem::path path = argv[1];
        std::filesystem::create_directories(path);
        lambo::paths::set_executable_dir(path);
        lambo::paths::set_portable_forced(true);
        recomp::register_config_path(path);
        // CTest reuses this directory. Seed identities explicitly so a prior
        // run's intentionally remapped startup controller cannot affect this run.
        { std::ofstream file(path / "controls-framework.json");
          file << R"({"version":3,"profiles":[],"controllers":[]})"; }
        std::filesystem::remove(path / "driving-controls.json");
        // Likewise for the General page: a saved rumble_strength would make the
        // staged rumble edit below a no-op. The .bak goes too, because the
        // config loader falls back to it when the main file is missing.
        std::filesystem::remove(path / "general.json");
        std::filesystem::remove(path / "general.json.bak");
        // Pedal settings used to live in pedals.json. A saved file must survive
        // the merge into the Driving page, so seed the legacy shape here.
        { std::ofstream file(path / "pedals.json");
          file << R"({"throttle_mode":"Analog","throttle_axis":"RT","throttle_negative":true,"throttle_deadzone":0.1,"throttle_saturation":0.9,"brake_mode":"Digital","brake_axis":"LT","brake_deadzone":0.2,"brake_saturation":0.85})"; }
        const nlohmann::json initial = {
            {"res_option", "Original2x"}, {"ds_option", 3}, {"msaa_option", "MSAA8X"},
            {"window_width", 1920}, {"window_height", 1080},
            {"texture_pack", "seed-pack"}, {"texture_dump", "seed-dump"},
            {"camera_distance_scale", .65}, {"future_option", "preserve me"},
            {"no_lod_circuit", {true, false, true, false, true, false}}};
        { std::ofstream file(path / "graphics.json"); file << initial; }
        lambo::config::load_and_apply_graphics();
        require(lambo::player::set_saved_name("RACER"), "driver cache fixture");
        lambo::ui::create_frontend_settings();
        recompui::config::finalize();
        auto& cheats = recompui::config::get_config("cheats");
        require(!cheats.requires_confirmation && cheats.external_storage, "cheats must be live and session-only");
        for (const auto& entry : lambo::cheats::catalog) {
            require(!std::get<bool>(cheats.get_option_value(entry.id)), "cheat defaults on");
            cheats.set_option_value(entry.id, true);
            require(lambo::cheats::enabled(entry.cheat), "cheat enable requires Apply");
            cheats.set_option_value(entry.id, false);
            require(!lambo::cheats::enabled(entry.cheat), "cheat disable requires Apply");
            lambo::cheats::set_enabled(entry.cheat, true);
            lambo::ui::refresh_frontend_settings();
            require(std::get<bool>(cheats.get_option_value(entry.id)), "cheat UI refresh failed");
            cheats.set_option_value(entry.id, false);
        }
        require(cheats.save_config() && !std::filesystem::exists(path / "cheats.json"),
                "session cheats were persisted");
        auto& driving = recompui::config::get_config("driving-controls");
        require(!std::get<bool>(driving.get_option_value("gyro")) &&
                !std::get<bool>(driving.get_option_value("auto_accelerate")), "driving assists must default off");
        driving.set_option_value("gyro", true);
        require(!std::get<bool>(driving.get_option_value("gyro")), "unapplied gyro change escaped");
        driving.revert_temp_config();
        require(!std::get<bool>(driving.get_temp_option_value("gyro")), "gyro discard failed");
        driving.set_option_value("gyro", true);
        driving.set_option_value("auto_accelerate", true);
        driving.set_option_value("gyro_range", 45.0);
        require(driving.save_config(), "driving settings save failed");
        driving.update_option_value("gyro", false);
        driving.apply_option_value("gyro");
        require(driving.load_config(), "driving settings reload failed");
        require(std::get<bool>(driving.get_option_value("gyro")) &&
                std::get<bool>(driving.get_option_value("auto_accelerate")) &&
                std::get<double>(driving.get_option_value("gyro_range")) == 45.0,
                "driving options did not persist");
        // The legacy pedals.json fixture above must have seeded the pedal options
        // now living on the merged Driving page.
        require(std::get<uint32_t>(driving.get_option_value("throttle_mode")) == 1 &&
                std::get<uint32_t>(driving.get_option_value("throttle_axis")) == 6 &&
                std::get<bool>(driving.get_option_value("throttle_negative")) &&
                std::get<double>(driving.get_option_value("throttle_deadzone")) == 0.1 &&
                std::get<double>(driving.get_option_value("throttle_saturation")) == 0.9 &&
                std::get<uint32_t>(driving.get_option_value("brake_mode")) == 0 &&
                std::get<uint32_t>(driving.get_option_value("brake_axis")) == 5 &&
                std::get<double>(driving.get_option_value("brake_deadzone")) == 0.2 &&
                std::get<double>(driving.get_option_value("brake_saturation")) == 0.85,
                "legacy pedals.json did not seed the merged Driving page");
        // Precedence: a pedal value the merged page has stored must win over a
        // stale pedals.json when the page reloads.
        { std::ofstream file(path / "driving-controls.json"); file << R"({"throttle_deadzone":0.33})"; }
        { std::ofstream file(path / "pedals.json"); file << R"({"throttle_deadzone":0.99})"; }
        require(driving.load_config(), "driving precedence reload failed");
        require(std::get<double>(driving.get_option_value("throttle_deadzone")) == 0.33,
                "a stale pedals.json overrode a saved driving-controls.json value");

        // The SDL pump posts the toggle and the presentation callback
        // publishes the applied context state. Verify both edges of that
        // contract without requiring a renderer in this settings test.
        lambo::ui::OverlayCaptureGate overlay;
        overlay.request(lambo::ui::Page::Settings);
        require(overlay.captures_input(), "opening settings did not capture input");
        const auto open_request = overlay.take_request();
        require(open_request.kind == lambo::ui::OverlayRequestKind::Page &&
                    open_request.page == lambo::ui::Page::Settings,
                "settings open request was not consumed");
        overlay.publish_context_capture(true);
        overlay.request_close();
        require(overlay.captures_input(), "close released input before render applied it");
        require(overlay.take_request().kind == lambo::ui::OverlayRequestKind::Close,
                "settings close request was not consumed");
        overlay.publish_context_capture(false);
        require(!overlay.captures_input(), "closing settings did not release input capture");

        auto& graphics = recompui::config::get_graphics_config();
        using namespace ultramodern::renderer;
        require(std::get<uint32_t>(graphics.get_option_value("res_option")) == uint32_t(Resolution::Original2x), "resolution import");
        require(std::get<uint32_t>(graphics.get_option_value("ds_option")) == 3, "3x supersampling import");
        require(std::get<uint32_t>(graphics.get_option_value("msaa_option")) == uint32_t(Antialiasing::MSAA8X), "8x MSAA import");
        // The window size is imported from graphics.json into the port snapshot,
        // and the picker seeds to the matching preset (the fixture is 1920x1080).
        require(lambo::config::window_size().width == 1920 && lambo::config::window_size().height == 1080, "window size import");
        require(std::get<uint32_t>(graphics.get_option_value("window_size")) == window_size_choice(graphics, "1920x1080"),
                "window picker did not seed from the imported size");
        // Mods owns texture packs, so Graphics must not present a second
        // selector. The legacy graphics.json key is still read, which is what
        // keeps a pack configured before the Mods route from being lost.
        require(!graphics.has_option("texture_pack"), "graphics must not offer a texture-pack selector");
        require(lambo::config::texture_pack_path() == "seed-pack", "legacy texture pack import");
        require(std::get<std::string>(graphics.get_option_value("texture_dump")) == "seed-dump", "texture dump import");
        // The player name and the startup launcher preference live on General,
        // which is confirmation-backed so the name can be staged instead of
        // written to player.json on every keystroke.
        auto& general = recompui::config::get_general_config();
        require(general.requires_confirmation, "General must stage the Player Name edit");
        require(general.has_option("name") && general.has_option("show_launcher"),
                "Player Name and launcher preference must be on General");
        const auto& general_schema = general.get_config_schema();
        const auto& name_option = general_schema.options.at(general_schema.options_by_id.at("name"));
        require(name_option.name == "Player Name", "the General name option label must be Player Name");
        // Narrow the check: several unrelated config errors also throw
        // std::runtime_error, and one of those must not read as "page removed".
        std::string missing_page;
        try { recompui::config::get_config("driver"); }
        catch (const std::exception& e) { missing_page = e.what(); }
        require(missing_page.find("driver") != std::string::npos,
                "the standalone Driver page still exists");
        std::string missing_pedals;
        try { recompui::config::get_config("pedals"); }
        catch (const std::exception& e) { missing_pedals = e.what(); }
        require(missing_pedals.find("pedals") != std::string::npos,
                "the standalone Pedals page still exists");
        general.set_option_value("name", std::string{});
        lambo::ui::refresh_frontend_settings();
        require(std::get<std::string>(general.get_temp_option_value("name")).empty(),
                "General refresh clobbered an active name edit");
        general.revert_temp_config();
        lambo::ui::refresh_frontend_settings();
        require(std::get<std::string>(general.get_option_value("name")) == "RACER",
                "General refresh missed an external name save");
        // An unapplied name edit must not reach player.json; Apply publishes it.
        general.set_option_value("name", std::string("CHAMPS"));
        require(lambo::player::saved_name() == "RACER", "unapplied name edit reached player.json");
        general.save_config();
        require(lambo::player::saved_name() == "CHAMPS", "applied name edit did not reach player.json");
        // A pending rumble edit must not stop an external name save appearing.
        general.set_option_value("rumble_strength", 60.0);
        require(general.is_dirty(), "rumble edit should be pending");
        lambo::player::set_saved_name("CHAMPER");
        lambo::ui::refresh_frontend_settings();
        require(std::get<std::string>(general.get_temp_option_value("name")) == "CHAMPER",
                "an unrelated pending edit froze the name field");
        // The framework's own General options are staged on the same footer.
        general.save_config();
        { std::ifstream file(path / "general.json"); nlohmann::json saved_general; file >> saved_general;
          require(saved_general.at("rumble_strength") == 60, "staged General option did not reach general.json"); }
        general.revert_temp_config();
        general.set_option_value("show_launcher", true);
        general.save_config();
        require(lambo::config::show_launcher(), "launcher preference did not apply");
        lambo::config::flush_pending_graphics_updates();
        { std::ifstream file(path / "graphics.json"); nlohmann::json launcher; file >> launcher;
          require(launcher.at("show_launcher") == true, "launcher preference did not persist"); }
        general.set_option_value("show_launcher", false);
        general.save_config();
        require(!lambo::config::show_launcher(), "launcher preference did not turn back off");
        // Neither developer_mode nor texture_pack belongs here: the Debug tab is
        // the single visible owner of the former (issue #244), and the Mods tab
        // owns texture packs (issue #245), so Graphics drops it entirely.
        for (const char* key : {"api_option", "hpfb_option", "window_size", "texture_dump"})
            require(graphics.has_option(key) && !graphics.is_config_option_hidden(graphics.get_config_schema().options_by_id.at(key)), "missing primary graphics option");
        // The separate width/height sliders are replaced by the window-size picker.
        require(!graphics.has_option("window_width") && !graphics.has_option("window_height"),
                "the separate window width/height sliders still exist");
        // A picked size resizes the window without a restart (the main-thread pump
        // pushes it onto SDL), so the label must not claim otherwise. Guarded by
        // name because the claim is what the player reads.
        const auto& window_size_option = graphics.get_config_schema().options.at(
            graphics.get_config_schema().options_by_id.at("window_size"));
        require(window_size_option.name == "Window size",
                "the window-size label still claims a restart is needed");
        // The framework still defines developer_mode on the Graphics page; the
        // Debug tab is its single visible owner (issue #244).
        require(graphics.has_option("developer_mode") && graphics.is_config_option_hidden(graphics.get_config_schema().options_by_id.at("developer_mode")),
                "developer mode still offered under graphics");
        auto& debug = recompui::config::get_config("debug");
        require(debug.has_option("developer_mode") && !debug.is_config_option_hidden(debug.get_config_schema().options_by_id.at("developer_mode")),
                "missing developer mode on the debug tab");
        require(!std::filesystem::exists(path / "debug.json"), "duplicate debug owner");
        graphics.set_option_value("ds_option", uint32_t(2));
        require(lambo::config::current_graphics().ds_option == 3, "unapplied edit escaped");
        graphics.revert_temp_config();
        require(std::get<uint32_t>(graphics.get_temp_option_value("ds_option")) == 3, "discard failed");
        graphics.set_option_value("ds_option", uint32_t(4));
        graphics.save_config();
        require(lambo::config::current_graphics().ds_option == 4, "apply failed");
        // Picking a preset stages the choice and resolves it at Apply.
        graphics.set_option_value("window_size", window_size_choice(graphics, "2560x1440"));
        graphics.set_option_value("texture_dump", std::string("applied-dump"));
        graphics.save_config();
        lambo::config::flush_pending_graphics_updates();
        require(lambo::config::window_size().width == 2560 && lambo::config::window_size().height == 1440,
                "window size preset did not apply");
        require(std::get<uint32_t>(graphics.get_option_value("window_size")) == window_size_choice(graphics, "2560x1440"),
                "window picker did not reseed after Apply");
        require(lambo::config::texture_dump_dir() == "applied-dump", "texture dump option did not apply");
        // Discarding a staged pick leaves the saved size untouched.
        graphics.set_option_value("window_size", window_size_choice(graphics, "1280x720"));
        require(graphics.is_dirty(), "window pick did not stage");
        graphics.revert_temp_config();
        require(lambo::config::window_size().width == 2560 && lambo::config::window_size().height == 1440,
                "discarded window pick changed the saved size");
        // get_temp_option_value, not get_option_value: a staged pick lives in
        // temp_storage and never touches the permanent value, so the latter
        // would pass here whether or not the revert did anything. The UI radio
        // reads the temp value too.
        require(std::get<uint32_t>(graphics.get_temp_option_value("window_size")) == window_size_choice(graphics, "2560x1440"),
                "discarded window pick was not reverted in the UI");
        // Custom is a no-op over a preset, so it must not be selectable there.
        require(graphics.get_enum_option_disabled(graphics.get_config_schema().options_by_id.at("window_size"),
                    window_size_index(graphics, "Custom")),
                "Custom must be disabled while a preset is the live size");
        // Every advertised preset must resolve to the size it names and stay a
        // common desktop ratio. Walking the registered options keeps this from
        // restating the port's table, so a mis-paired entry cannot ship.
        size_t presets_checked = 0;
        for (const auto& entry : window_size_choices(graphics)) {
            if (entry.key == "Custom") continue;
            ++presets_checked;
            const size_t x = entry.key.find('x');
            const long long width = std::stoll(entry.key.substr(0, x));
            const long long height = std::stoll(entry.key.substr(x + 1));
            require(width * 9 == height * 16 || width * 10 == height * 16 || width * 3 == height * 4,
                    ("window size preset is not a common desktop ratio: " + entry.key).c_str());
            graphics.set_option_value("window_size", entry.value);
            graphics.save_config();
            lambo::config::flush_pending_graphics_updates();
            require(lambo::config::window_size().width == width && lambo::config::window_size().height == height,
                    ("window size preset did not resolve to its advertised size: " + entry.key).c_str());
        }
        require(presets_checked >= 11, "the picker lost most of its presets");
        // A size that is not a preset shows as Custom and survives saving an
        // unrelated Graphics option: Custom keeps the live size.
        lambo::config::set_window_size({1366, 768});
        lambo::config::flush_pending_graphics_updates();
        require(graphics.load_config(), "graphics reload failed");
        require(std::get<uint32_t>(graphics.get_option_value("window_size")) == window_size_choice(graphics, "Custom"),
                "a non-preset window size did not show as Custom");
        // A custom size is the only case where Custom is meaningful, so it has
        // to be selectable again.
        require(!graphics.get_enum_option_disabled(graphics.get_config_schema().options_by_id.at("window_size"),
                    window_size_index(graphics, "Custom")),
                "Custom must stay enabled when the live size is not a preset");
        graphics.set_option_value("ds_option", uint32_t(2));
        graphics.save_config();
        lambo::config::flush_pending_graphics_updates();
        require(lambo::config::window_size().width == 1366 && lambo::config::window_size().height == 768,
                "an unrelated Graphics save overwrote a custom window size");
        // Applying Graphics must leave the compatibility key exactly as it was, so
        // the pack a player set up earlier still loads. Clearing it is a separate,
        // explicit write -- that is the documented migration to Mods-only.
        nlohmann::json after_graphics_save;
        { std::ifstream file(path / "graphics.json"); file >> after_graphics_save; }
        require(after_graphics_save.at("texture_pack") == "seed-pack",
                "graphics save dropped the legacy texture_pack key");
        require(lambo::config::texture_pack_path() == "seed-pack",
                "graphics save cleared the legacy texture pack");
        lambo::config::set_texture_pack_path("");
        lambo::config::flush_pending_graphics_updates();
        require(lambo::config::texture_pack_path().empty(), "legacy texture pack did not clear");
        nlohmann::json after_migration;
        { std::ifstream file(path / "graphics.json"); file >> after_migration; }
        require(after_migration.at("texture_pack") == "", "cleared texture_pack key was not persisted");
        lambo::config::set_texture_pack_path("seed-pack");
        lambo::config::flush_pending_graphics_updates();
        require(lambo::config::texture_pack_path() == "seed-pack", "texture pack fixture restore failed");
        lambo::config::update_saved_window_mode(WindowMode::Fullscreen);
        lambo::ui::refresh_frontend_settings();
        require(std::get<uint32_t>(graphics.get_option_value("wm_option")) == uint32_t(WindowMode::Fullscreen), "external fullscreen refresh");
        graphics.set_option_value("ds_option", uint32_t(3));
        lambo::config::update_saved_window_mode(WindowMode::Windowed);
        lambo::ui::refresh_frontend_settings();
        require(std::get<uint32_t>(graphics.get_temp_option_value("ds_option")) == 3, "refresh discarded pending edit");
        graphics.save_config();
        require(lambo::config::current_graphics().wm_option == WindowMode::Windowed, "apply undid external fullscreen toggle");
        // Developer Mode lives on the Debug tab, but keeps its graphics.json key
        // and its restart-only runtime behaviour.
        require(!lambo::config::developer_mode(), "developer mode defaults off");
        debug.set_option_value("developer_mode", true);
        require(lambo::config::developer_mode(), "developer mode live update");
        graphics.set_option_value("ds_option", uint32_t(4));
        graphics.save_config();
        // A Graphics Apply starts from the port snapshot, so it must carry the
        // Debug tab's value through instead of writing the seeded one back.
        require(lambo::config::developer_mode(), "graphics apply reverted the debug tab value");
        auto& enhancements = recompui::config::get_config("enhancements");
        for (const char* key : {"automatic_pit_stops", "fog_match", "sky_match", "no_lod", "draw_distance", "fog_scale", "camera_distance", "camera_height", "camera_fov", "menu_stick_sensitivity"})
            require(enhancements.has_option(key), "missing enhancement");
        // Ray-traced shadows are pre-alpha, so they live on the Debug tab and
        // must not appear in the player-facing Enhancements tab.
        for (const char* key : {"rt_shadows", "rt_shadow_rays", "rt_shadow_softness"}) {
            require(debug.has_option(key), "missing ray-traced shadow option on the debug tab");
            require(!enhancements.has_option(key), "ray-traced shadow option leaked into Enhancements");
        }
        require(!lambo::config::rt_shadows() && !std::get<bool>(debug.get_option_value("rt_shadows")),
                "ray-traced shadows must default to Original");
        // Admission accepts every player car, so the text must not narrow it.
        require(debug.get_option("rt_shadows").description.find("first car") == std::string::npos,
                "ray-traced shadow description understates the supported cars");
        // The pre-alpha wording is the reason the setting is off Enhancements.
        // Each row's description is shown on its own, so all three carry the
        // bug/lighting warning rather than relying on the toggle above them.
        require(debug.get_option("rt_shadows").name.find("pre-alpha") != std::string::npos,
                "ray-traced shadow toggle is not labelled pre-alpha");
        for (const char* key : {"rt_shadows", "rt_shadow_rays", "rt_shadow_softness"})
            require(debug.get_option(key).description.find("lighting issues") != std::string::npos,
                    "ray-traced shadow option does not warn about lighting issues");
        debug.set_option_value("rt_shadows", true);
        debug.set_option_value("rt_shadow_rays", uint32_t(16));
        debug.set_option_value("rt_shadow_softness", 1.25);
        require(lambo::config::rt_shadows() && lambo::config::rt_shadow_rays() == 16 &&
                lambo::config::rt_shadow_softness() == 1.25, "ray-traced shadow live update");
        require(!lambo::config::automatic_pit_stops(), "pit assistance defaults off");
        enhancements.set_option_value("automatic_pit_stops", true);
        require(lambo::config::automatic_pit_stops(), "pit assistance live update");
        enhancements.set_option_value("camera_distance", .8);
        enhancements.set_option_value("circuit_2", true);
        enhancements.set_option_value("menu_stick_sensitivity", 1.8);
        require(lambo::config::camera_distance_scale() == .8, "camera update");
        require(lambo::config::no_lod_circuit(1), "circuit update");
        require(lambo::config::menu_stick_sensitivity() == 1.8, "menu stick sensitivity update");
        lambo::config::flush_pending_graphics_updates();
        nlohmann::json saved;
        { std::ifstream file(path / "graphics.json"); file >> saved; }
        require(saved.at("future_option") == "preserve me", "unknown settings lost");
        require(saved.at("automatic_pit_stops") == true, "pit assistance persistence");
        require(saved.at("menu_stick_sensitivity") == 1.8, "menu stick sensitivity persistence");
        require(saved.at("rt_shadows") == true && saved.at("rt_shadow_rays") == 16 &&
                saved.at("rt_shadow_softness") == 1.25, "ray-traced shadow persistence");
        require(saved.at("ds_option") == 4, "graphics persistence");
        require(saved.at("developer_mode") == true, "developer mode persistence");
        // The picker borrows the schema; the port keeps writing the existing
        // window_width/window_height format and must not leak the option id.
        require(saved.at("window_width") == 1366 && saved.at("window_height") == 768,
                "the picker stopped persisting the existing window_width/window_height format");
        require(!saved.contains("window_size"), "the picker leaked its own key into graphics.json");
        require(!std::filesystem::exists(path / "enhancements.json"), "duplicate enhancement owner");
        require(recompui::config::get_config("driving-controls").has_option("brake_saturation"), "brake calibration missing");
        using namespace recompinput;
        const int p1 = profiles::get_or_create_mp_keyboard_profile_index(0);
        const int p2 = profiles::get_or_create_mp_keyboard_profile_index(1);
        profiles::set_input_binding(p1, GameInput::A, 0, InputField::keyboard(SDL_SCANCODE_X));
        profiles::set_input_binding(p2, GameInput::A, 0, InputField::keyboard(SDL_SCANCODE_C));
        require(profiles::get_input_binding(p1, GameInput::A, 0).input_id != profiles::get_input_binding(p2, GameInput::A, 0).input_id, "players share mappings");
        require(profiles::save_controls_config(path / "controls-framework.json"), "profiles save failed");
        lambo::controls::ControlsConfig legacy;
        const std::string guid(32, '0');
        legacy.profiles[guid] = lambo::controls::default_profile();
        legacy.profiles[guid].digital[0] = {lambo::controls::ButtonSource{lambo::controls::LogicalButton::Y}};
        require(lambo::controls::save_config(legacy, path / "controls.json").saved, "legacy fixture save");
        lambo::ui::import_frontend_profiles();
        const int imported = profiles::get_input_profile_by_key("legacy-" + guid);
        require(imported >= 0, "legacy profile missing");
        require(profiles::get_input_binding(imported, GameInput::A, 0).input_id == SDL_CONTROLLER_BUTTON_Y, "legacy button import");
        require(profiles::get_input_binding(imported, GameInput::Y_AXIS_POS, 0).input_id == -(SDL_CONTROLLER_AXIS_LEFTY + 1), "legacy stick inversion");
        require(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0, "SDL input init");
        players::set_player_count_range(1, 4);
        players::set_single_player_mode(false);
        lambo::ui::initialize_frontend_controllers();
        require(players::get_player(0).keyboard_enabled, "no-controller keyboard fallback");
        const int startup_device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
        require(startup_device >= 0, "startup controller attach");
        // Device is present before host startup; deliberately discard added events.
        SDL_FlushEvents(SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEADDED);
        lambo::ui::initialize_frontend_controllers();
        require(players::get_player(0).controller != nullptr, "pre-attached controller not assigned at startup");
        const auto startup_instance = SDL_JoystickGetDeviceInstanceID(startup_device);
        require(get_controller_from_joystick_id(startup_instance) == players::get_player(0).controller,
            "startup controller missing framework state");
        SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(players::get_player(0).controller), SDL_CONTROLLER_BUTTON_A, 1);
        SDL_JoystickUpdate();
        poll_inputs();
        uint16_t startup_buttons = 0; float startup_x = 0, startup_y = 0;
        profiles::get_n64_input(0, &startup_buttons, &startup_x, &startup_y);
        require(startup_buttons & 0x8000, "startup controller cannot drive guest A");
        const auto live_guid = profiles::get_guid_from_sdl_controller(players::get_player(0).controller);
        const nlohmann::json saved_guid = live_guid;
        ControllerGUID restored_guid{};
        recompinput::from_json(saved_guid, restored_guid);
        require(restored_guid.hash == live_guid.hash, "saved controller identity loses lookup hash on reload");
        const int saved_profile = profiles::add_input_profile("startup-saved", "Startup saved", InputDevice::Controller, true);
        profiles::reset_profile_bindings(saved_profile, InputDevice::Controller);
        profiles::set_input_binding(saved_profile, GameInput::A, 0, InputField::controller_digital(SDL_CONTROLLER_BUTTON_Y));
        profiles::add_controller(live_guid, saved_profile);
        require(profiles::save_controls_config(path / "controls-framework.json"), "save startup profile");
        profiles::add_controller(live_guid, profiles::get_sp_controller_profile_index());
        require(profiles::load_controls_config(path / "controls-framework.json"), "reload startup profile");
        lambo::ui::initialize_frontend_controllers();
        require(profiles::get_input_profile_for_player(0, InputDevice::Controller) == saved_profile,
            "startup lost persisted controller profile");
        // A delayed added event must not open a second reference or reset mappings.
        SDL_Event duplicate{};
        duplicate.type = SDL_CONTROLLERDEVICEADDED;
        duplicate.cdevice.which = startup_device;
        recompinput::handle_event(duplicate);
        require(profiles::get_input_profile_for_player(0, InputDevice::Controller) == saved_profile,
            "duplicate device event reset profile");
        SDL_VirtualJoystickDesc preferred_desc{};
        preferred_desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        preferred_desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        preferred_desc.naxes = 6;
        preferred_desc.nbuttons = 15;
        preferred_desc.vendor_id = 0x1209;
        preferred_desc.product_id = 2;
        preferred_desc.name = "Preferred startup controller";
        const int preferred_device = SDL_JoystickAttachVirtualEx(&preferred_desc);
        require(preferred_device >= 0, "preferred controller attach");
        char preferred_guid[33]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(preferred_device), preferred_guid, sizeof(preferred_guid));
        legacy.preferred_controller_guid = preferred_guid;
        legacy.profiles[preferred_guid] = legacy.profiles[guid];
        require(lambo::controls::save_config(legacy, path / "controls.json").saved, "preferred fixture save");
        lambo::ui::import_frontend_profiles();
        lambo::ui::initialize_frontend_controllers();
        const auto preferred_instance = SDL_JoystickGetDeviceInstanceID(preferred_device);
        auto* preferred_controller = get_controller_from_joystick_id(preferred_instance);
        require(preferred_controller && players::get_player(0).controller == preferred_controller,
            "legacy preferred device lost to enumeration order");
        require(players::get_number_of_assigned_players() == 1, "startup unexpectedly assigned extra players");
        require(profiles::get_input_profile_for_player(0, InputDevice::Controller) ==
            profiles::get_input_profile_by_key(std::string("legacy-") + preferred_guid),
            "actual SDL device did not inherit its legacy profile");
        SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(preferred_controller), SDL_CONTROLLER_BUTTON_Y, 1);
        SDL_JoystickUpdate();
        poll_inputs();
        profiles::get_n64_input(0, &startup_buttons, &startup_x, &startup_y);
        require(startup_buttons & 0x8000, "imported startup mapping did not reach guest A");
        remove_controller_state(preferred_instance);
        SDL_GameControllerClose(preferred_controller);
        SDL_JoystickDetachVirtual(preferred_device);
        lambo::ui::initialize_frontend_controllers();
        require(players::get_player(0).controller == get_controller_from_joystick_id(startup_instance),
            "missing preferred device did not fall back to connected controller");
        remove_controller_state(startup_instance);
        SDL_GameControllerClose(players::get_player(0).controller);
        SDL_JoystickDetachVirtual(startup_device);
        lambo::ui::initialize_frontend_controllers();
        profiles::set_input_profile_for_player(0, p1, InputDevice::Keyboard);
        playerassignment::start();
        SDL_Event idle{};
        playerassignment::process_sdl_event(&idle); // Flush the previous modal close.
        SDL_GameController* controllers[4]{};
        for (int i = 0; i < 4; ++i) {
            const int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
            require(device >= 0, "virtual controller attach");
            controllers[i] = SDL_GameControllerOpen(device);
            require(controllers[i] != nullptr, "virtual controller open");
            const auto instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i]));
            add_controller_state(instance, controllers[i]);
            SDL_Event event{};
            event.type = SDL_CONTROLLERBUTTONDOWN;
            event.cbutton.which = instance;
            event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
            playerassignment::process_sdl_event(&event);
        }
        playerassignment::commit_player_assignment();
        require(profiles::get_input_profile_for_player(0, InputDevice::Keyboard) == -1, "reassignment retained keyboard on controller player");
        require(players::get_number_of_assigned_players() == 4, "four-player assignment");
        for (int i = 0; i < 4; ++i) {
            const int profile = profiles::add_input_profile("virtual-" + std::to_string(i), "Virtual", InputDevice::Controller, true);
            profiles::set_input_binding(profile, GameInput::A, 0, InputField::controller_digital(SDL_GameControllerButton(i)));
            profiles::set_input_profile_for_player(i, profile, InputDevice::Controller);
            SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(controllers[i]), i, 1);
        }
        SDL_JoystickUpdate();
        poll_inputs();
        for (int i = 0; i < 4; ++i) {
            uint16_t buttons = 0; float x = 0, y = 0;
            profiles::get_n64_input(i, &buttons, &x, &y);
            require(buttons == 0x8000, "assigned controller did not reach its player");
        }
        SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(controllers[2]), 2, 0);
        SDL_JoystickUpdate();
        poll_inputs();
        for (int i = 0; i < 4; ++i) {
            uint16_t buttons = 0; float x = 0, y = 0;
            profiles::get_n64_input(i, &buttons, &x, &y);
            require(buttons == (i == 2 ? 0 : 0x8000), "cross-player input leakage");
        }
        // Menu actions must resolve through the pressing controller's own profile.
        // An unassigned pad used to read player one's profile index, which this port
        // sets to -1, so the shoulder tab buttons produced no menu action.
        const int menu_device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
        require(menu_device >= 0, "menu controller attach");
        SDL_Event menu_added{};
        menu_added.type = SDL_CONTROLLERDEVICEADDED;
        menu_added.cdevice.which = menu_device;
        recompinput::handle_event(menu_added);
        const auto menu_instance = SDL_JoystickGetDeviceInstanceID(menu_device);
        SDL_GameController* menu_controller = get_controller_from_joystick_id(menu_instance);
        require(menu_controller != nullptr, "menu controller not registered");
        profiles::set_input_profile_for_player(0, -1, InputDevice::Controller);
        SDL_ControllerButtonEvent shoulder{};
        shoulder.which = menu_instance;
        shoulder.button = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        require(cont_button_to_key(shoulder) == SDLK_F16, "unassigned controller lost tab-left");
        shoulder.button = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        require(cont_button_to_key(shoulder) == SDLK_F17, "unassigned controller lost tab-right");
        // The quit confirmation is a recompui prompt and dismisses on the Back menu
        // action, so the pressing controller's Back binding is what has to reach it.
        // The default controller profile binds Back to the west button, and a remapped
        // Back has to move the action with it rather than leave it on that button.
        const int menu_profile = profiles::get_controller_profile_index_from_sdl_controller(menu_controller);
        require(menu_profile >= 0, "menu controller profile not resolved");
        SDL_ControllerButtonEvent back_button{};
        back_button.which = menu_instance;
        back_button.button = SDL_CONTROLLER_BUTTON_WEST;
        // Measure, then restore, then assert. require() returns from main, so
        // asserting inline would leave the shared profile remapped for whatever
        // runs next.
        const int default_back = cont_button_to_key(back_button);
        profiles::set_input_binding(menu_profile, GameInput::BACK_MENU, 0,
            InputField::controller_digital(SDL_CONTROLLER_BUTTON_Y));
        const int remapped_old_button = cont_button_to_key(back_button);
        back_button.button = SDL_CONTROLLER_BUTTON_Y;
        const int remapped_new_button = cont_button_to_key(back_button);
        profiles::reset_input_binding(menu_profile, InputDevice::Controller, GameInput::BACK_MENU);
        back_button.button = SDL_CONTROLLER_BUTTON_WEST;
        const int after_reset = cont_button_to_key(back_button);

        require(default_back == SDLK_F15, "default Back (B) produced no Back menu action");
        require(remapped_old_button == 0, "remapped Back left the previous button bound");
        require(remapped_new_button == SDLK_F15, "remapped Back produced no Back menu action");
        require(after_reset == SDLK_F15, "reset did not restore the default Back binding");
        // An unresolved device has no profile at all; menu actions must be inert while
        // the profile-free D-pad fallback still works.
        SDL_ControllerButtonEvent unknown_pad{};
        unknown_pad.which = 9999;
        unknown_pad.button = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        require(cont_button_to_key(unknown_pad) == 0, "unresolved device shoulder button produced a menu key");
        unknown_pad.button = SDL_CONTROLLER_BUTTON_DPAD_UP;
        require(cont_button_to_key(unknown_pad) == SDLK_UP, "unresolved device lost the d-pad fallback");
        remove_controller_state(menu_instance);
        SDL_GameControllerClose(menu_controller);
        SDL_JoystickDetachVirtual(menu_device);
        SDL_Quit();
        // The preset stages renderer fields and defers live enhancement changes
        // until Apply, so the existing Graphics Discard covers the whole action.
        const auto before_preset = lambo::config::current_graphics();
        const bool before_lod = lambo::config::no_lod();
        const double before_distance = lambo::config::global_draw_distance();
        const bool before_fog_match = lambo::config::widescreen_fog_match();
        const bool before_sky_match = lambo::config::widescreen_sky_match();
        const bool before_rt_shadows = lambo::config::rt_shadows();
        graphics.clear_config_option_updates();
        graphics.set_option_value("performance_preset", uint32_t(1));
        const auto preset_updates = graphics.get_config_option_updates();
        for (const char* key : {"res_option", "ds_option", "msaa_option", "rr_option", "hpfb_option"}) {
            const auto index = graphics.get_config_schema().options_by_id.at(key);
            bool value_updated = false;
            for (const auto& update : preset_updates) {
                if (update.option_index != index) continue;
                for (auto type : update.updates)
                    value_updated |= type == recomp::config::ConfigOptionUpdateType::Value;
            }
            require(value_updated, "preset did not refresh a renderer widget's staged value");
        }
        require(graphics.is_dirty(), "preset did not stage an edit");
        require(lambo::config::current_graphics() == before_preset &&
                lambo::config::no_lod() == before_lod &&
                lambo::config::global_draw_distance() == before_distance,
                "preset changed the running game before Apply");
        graphics.revert_temp_config();
        require(!graphics.is_dirty() && lambo::config::current_graphics() == before_preset &&
                lambo::config::no_lod() == before_lod &&
                lambo::config::global_draw_distance() == before_distance &&
                lambo::config::widescreen_fog_match() == before_fog_match &&
                lambo::config::widescreen_sky_match() == before_sky_match &&
                lambo::config::rt_shadows() == before_rt_shadows,
                "Discard leaked preset changes");
        const double unrelated_manual_rate =
            std::get<double>(graphics.get_temp_option_value("rr_manual_value")) + 1.0;
        graphics.set_option_value("rr_manual_value", unrelated_manual_rate);
        require(graphics.save_config(), "unrelated Graphics Apply after Discard failed");
        require(lambo::config::current_graphics().rr_manual_value == int(unrelated_manual_rate),
                "unrelated Graphics edit was not applied after Discard");
        require(lambo::config::no_lod() == before_lod &&
                lambo::config::global_draw_distance() == before_distance &&
                lambo::config::widescreen_fog_match() == before_fog_match &&
                lambo::config::widescreen_sky_match() == before_sky_match,
                "later Graphics save reapplied discarded preset changes");
        lambo::config::flush_pending_graphics_updates();
        nlohmann::json before_preset_json;
        { std::ifstream file(path / "graphics.json"); file >> before_preset_json; }
        graphics.set_option_value("performance_preset", uint32_t(1));
        require(graphics.save_config(), "preset Apply failed");
        const auto low = lambo::config::current_graphics();
        require(low.res_option == Resolution::Original2x && low.ds_option == lambo::config::kDsMultiplier1x &&
                low.msaa_option == Antialiasing::None && low.rr_option == RefreshRate::Original &&
                low.hpfb_option == HighPrecisionFramebuffer::Off, "low hardware renderer choices");
        require(!lambo::config::no_lod() && lambo::config::global_draw_distance() == 1.0 &&
                !lambo::config::widescreen_fog_match() && !lambo::config::widescreen_sky_match() &&
                !lambo::config::rt_shadows(),
                "preset did not restore stock geometry/distance/multiplayer/shadow policy");
        require(!graphics.is_dirty() && std::get<uint32_t>(graphics.get_option_value("performance_preset")) == 0,
                "preset action was not reset after Apply");
        lambo::config::flush_pending_graphics_updates();
        { std::ifstream file(path / "graphics.json"); nlohmann::json saved; file >> saved;
          for (const char* key : {"future_option", "texture_pack", "camera_distance_scale", "no_lod_circuit", "api_option", "ar_option", "wm_option", "window_width", "window_height"})
              require(saved.at(key) == before_preset_json.at(key), "preset changed an unrelated setting");
          require(saved.at("res_option") == "Original2x" && saved.at("no_lod") == false &&
                  !saved.contains("performance_preset"), "preset must persist normal fields only"); }
        graphics.set_option_value("performance_preset", uint32_t(1));
        graphics.set_option_value("res_option", uint32_t(Resolution::Original));
        require(graphics.save_config() && lambo::config::current_graphics().res_option == Resolution::Original,
                "preset overwrote a later manual adjustment");
        std::cout << "PASS frontend settings, Apply/Discard, low hardware preset, legacy graphics preservation and independent profiles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
