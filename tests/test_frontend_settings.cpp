#include <fstream>
#include <iostream>
#include <stdexcept>
#include "lambo_config.h"
#include "lambo_player_name.h"
#include "lambo_paths.h"
#include "ui/lambo_ui.h"
#include "ui/lambo_frontend_input.h"
#include "ui/lambo_frontend_overlay.h"
#include "librecomp/game.hpp"
#include "recompui/config.h"
#include "recompinput/profiles.h"
#include "recompinput/input_events.h"

SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;
namespace lambo::ui { void create_frontend_settings(); void refresh_frontend_settings(); }
static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

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
        lambo::ui::create_frontend_driving_settings();
        lambo::ui::create_frontend_pedal_settings();
        recompui::config::finalize();
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
        require(std::get<double>(graphics.get_option_value("window_width")) == 1920, "window width import");
        require(std::get<double>(graphics.get_option_value("window_height")) == 1080, "window height import");
        require(std::get<std::string>(graphics.get_option_value("texture_pack")) == "seed-pack", "texture pack import");
        require(std::get<std::string>(graphics.get_option_value("texture_dump")) == "seed-dump", "texture dump import");
        auto& driver = recompui::config::get_config("driver");
        driver.set_option_value("name", std::string{});
        lambo::ui::refresh_frontend_settings();
        require(std::get<std::string>(driver.get_temp_option_value("name")).empty(),
                "driver refresh clobbered an active text edit");
        driver.revert_temp_config();
        lambo::ui::refresh_frontend_settings();
        require(std::get<std::string>(driver.get_option_value("name")) == "RACER",
                "driver refresh missed an external name save");
        for (const char* key : {"api_option", "hpfb_option", "developer_mode", "window_width", "window_height", "texture_pack", "texture_dump"})
            require(graphics.has_option(key) && !graphics.is_config_option_hidden(graphics.get_config_schema().options_by_id.at(key)), "missing primary graphics option");
        graphics.set_option_value("ds_option", uint32_t(2));
        require(lambo::config::current_graphics().ds_option == 3, "unapplied edit escaped");
        graphics.revert_temp_config();
        require(std::get<uint32_t>(graphics.get_temp_option_value("ds_option")) == 3, "discard failed");
        graphics.set_option_value("ds_option", uint32_t(4));
        graphics.save_config();
        require(lambo::config::current_graphics().ds_option == 4, "apply failed");
        graphics.set_option_value("window_width", 2560.0);
        graphics.set_option_value("window_height", 1440.0);
        graphics.set_option_value("texture_pack", std::string("applied-pack"));
        graphics.set_option_value("texture_dump", std::string("applied-dump"));
        graphics.save_config();
        lambo::config::flush_pending_graphics_updates();
        require(lambo::config::window_size().width == 2560 && lambo::config::window_size().height == 1440,
                "restart window options did not apply");
        require(lambo::config::texture_pack_path() == "applied-pack", "texture pack option did not apply");
        require(lambo::config::texture_dump_dir() == "applied-dump", "texture dump option did not apply");
        lambo::config::update_saved_window_mode(WindowMode::Fullscreen);
        lambo::ui::refresh_frontend_settings();
        require(std::get<uint32_t>(graphics.get_option_value("wm_option")) == uint32_t(WindowMode::Fullscreen), "external fullscreen refresh");
        graphics.set_option_value("ds_option", uint32_t(3));
        lambo::config::update_saved_window_mode(WindowMode::Windowed);
        lambo::ui::refresh_frontend_settings();
        require(std::get<uint32_t>(graphics.get_temp_option_value("ds_option")) == 3, "refresh discarded pending edit");
        graphics.save_config();
        require(lambo::config::current_graphics().wm_option == WindowMode::Windowed, "apply undid external fullscreen toggle");
        graphics.set_option_value("ds_option", uint32_t(4));
        graphics.save_config();
        auto& enhancements = recompui::config::get_config("enhancements");
        for (const char* key : {"automatic_pit_stops", "fog_match", "sky_match", "no_lod", "draw_distance", "fog_scale", "camera_distance", "camera_height", "camera_fov"})
            require(enhancements.has_option(key), "missing enhancement");
        require(!lambo::config::automatic_pit_stops(), "pit assistance defaults off");
        enhancements.set_option_value("automatic_pit_stops", true);
        require(lambo::config::automatic_pit_stops(), "pit assistance live update");
        enhancements.set_option_value("camera_distance", .8);
        enhancements.set_option_value("circuit_2", true);
        require(lambo::config::camera_distance_scale() == .8, "camera update");
        require(lambo::config::no_lod_circuit(1), "circuit update");
        lambo::config::flush_pending_graphics_updates();
        nlohmann::json saved;
        { std::ifstream file(path / "graphics.json"); file >> saved; }
        require(saved.at("future_option") == "preserve me", "unknown settings lost");
        require(saved.at("automatic_pit_stops") == true, "pit assistance persistence");
        require(saved.at("ds_option") == 4, "graphics persistence");
        require(!std::filesystem::exists(path / "enhancements.json"), "duplicate enhancement owner");
        require(recompui::config::get_config("pedals").has_option("brake_saturation"), "brake calibration missing");
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
        std::cout << "PASS frontend settings, Apply/Discard, legacy graphics preservation and independent profiles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
