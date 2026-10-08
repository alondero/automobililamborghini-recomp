#include "lambo_ui.h"
#include "lambo_frontend_input.h"
#include "lambo_launcher_menu.h"
#include "lambo_frontend_overlay.h"
#include "lambo_config.h"
#include "lambo_mods.h"
#include "lambo_startup.h"
#include "recompui/recompui.h"
#include "recompui/config.h"
#include "recompui/program_config.h"
#include "recompinput/input_events.h"
#include "recompinput/profiles.h"
#include "librecomp/game.hpp"
#include "rt64_render_hooks.h"
#include <atomic>

// RecompFrontend's host contract. The renderer remains the port's RT64 context.
SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;
void init_hook(plume::RenderInterface*, plume::RenderDevice*);
void draw_hook(plume::RenderCommandList*, plume::RenderFramebuffer*);
void deinit_hook();

namespace lambo::ui {
std::recursive_mutex& frontend_mutex() { static std::recursive_mutex mutex; return mutex; }
void create_frontend_settings();
void refresh_frontend_settings();
namespace {
std::atomic<bool> ready{false};
std::atomic<bool> settings_refresh_requested{false};
std::mutex mod_error_mutex;
std::string mod_error;
OverlayCaptureGate overlay;
lambo::StartupController* startup = nullptr;

void request(Page page) {
    overlay.request(page);
}

void initialize(plume::RenderInterface* interface, plume::RenderDevice* device) {
    std::lock_guard lock(frontend_mutex());
    auto& graphics = recompui::config::get_graphics_config();
    const bool samples = device->getCapabilities().sampleLocations;
    graphics.update_option_disabled("msaa_option", !samples);
    if (samples) {
        const auto counts = device->getSampleCountsSupported(plume::RenderFormat::R8G8B8A8_UNORM) &
                            device->getSampleCountsSupported(plume::RenderFormat::D32_FLOAT);
        using AA = ultramodern::renderer::Antialiasing;
        graphics.update_enum_option_disabled("msaa_option", uint32_t(AA::MSAA2X), !(counts & plume::RenderSampleCount::Bits::COUNT_2));
        graphics.update_enum_option_disabled("msaa_option", uint32_t(AA::MSAA4X), !(counts & plume::RenderSampleCount::Bits::COUNT_4));
        graphics.update_enum_option_disabled("msaa_option", uint32_t(AA::MSAA8X), !(counts & plume::RenderSampleCount::Bits::COUNT_8));
    }
    SDL_DisplayMode display{};
    if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &display) == 0)
        recompui::config::graphics::update_refresh_rate(display.refresh_rate);
    init_hook(interface, device);
    ready.store(true, std::memory_order_release);
}

void render(plume::RenderCommandList* commands, plume::RenderFramebuffer* framebuffer) {
    std::lock_guard lock(frontend_mutex());
    // The runtime invokes its error callback before resetting its game status.
    // Wait for that reset before offering mod changes and an explicit retry.
    if (!ultramodern::is_game_started()) {
        std::string error;
        { std::lock_guard error_lock(mod_error_mutex); error.swap(mod_error); }
        if (!error.empty()) {
            if (startup) startup->start_failed();
            recompui::show_context(recompui::get_launcher_context_id(), "");
            recompui::config::open();
            recompui::config::set_tab("mods");
            recompui::update_mod_list(false);
            recompui::open_info_prompt("Unable to start with these mods", error, "OK", {}, recompui::ButtonStyle::Tertiary);
        }
    }
    const OverlayRequest action = overlay.take_request();
    const bool input_refresh = settings_refresh_requested.exchange(false, std::memory_order_acq_rel);
    // The Graphics load callback runs only in config::finalize(), not on open.
    // Shown frontend contexts currently capture input, so refresh before queued
    // opens and while visible; this is the page's only freshness mechanism.
    if (input_refresh || action.kind == OverlayRequestKind::Page || recompui::is_context_capturing_input())
        refresh_frontend_settings();
    if (action.kind == OverlayRequestKind::Close) {
        // The render callback does not own a ContextId opened through the
        // thread-local ContextId API, so try_close_current_context() cannot
        // dismiss the modal here. Close the config modal explicitly and hide
        // any nested context (for example the player-assignment prompt).
        // A dirty confirmation-backed tab may open Apply/Discard instead of
        // closing. Do not hide that prompt; it is the only path that can
        // resolve the pending edit. For a clean modal, remove any nested
        // context after the config close succeeds.
        if (recompui::config::close()) recompui::hide_all_contexts();
    }
    else if (action.kind == OverlayRequestKind::Page) {
        const auto page = action.page;
        if (page == Page::Home) recompui::show_context(recompui::get_launcher_context_id(), "");
        else {
            recompui::config::open();
            const char* id = "general";
            switch (page) {
            case Page::Graphics: id = "graphics"; break;
            case Page::Enhancements: id = "enhancements"; break;
            case Page::Controls: id = recompui::config::controls::id.c_str(); break;
            // Pedals merged into the Driving tab, so the historical Haptics
            // route is now an alias onto it rather than a separate destination.
            case Page::Haptics: id = "driving-controls"; break;
            case Page::Mods: id = "mods"; break;
            default: break;
            }
            recompui::config::set_tab(id);
        }
    }
    draw_hook(commands, framebuffer);
    overlay.publish_context_capture(recompui::is_context_capturing_input());
}

void deinitialize() {
    std::lock_guard lock(frontend_mutex());
    ready.store(false, std::memory_order_release);
    overlay.publish_context_capture(false);
    deinit_hook();
}
}

void set_window(SDL_Window* value) { window = value; }
void set_startup_controller(lambo::StartupController* value) { startup = value; }

void install_render_hooks() {
    recompui::programconfig::set_program_name("Automobili Lamborghini Recompiled");
    recompui::programconfig::set_program_id(u8"LamborghiniRecomp");
    recompui::register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");
    recompui::register_extra_font("LatoLatin-Bold.ttf");
    recompinput::players::set_player_count_range(1, 4);
    recompinput::players::set_single_player_mode(false);
    configure_frontend_input_defaults();
    recompui::update_game_mod_id(lambo::mods::game_id);
    create_frontend_settings();
    const bool new_profiles = !std::filesystem::exists(lambo::config::app_config_dir() / "controls-framework.json");
    recompui::config::finalize();
    if (new_profiles) import_frontend_profiles();
    // Existing keyboard controls become a real, editable P1 profile. Do not
    // reset it on later launches. Other keyboard players have separate profiles.
    using namespace recompinput;
    const int keyboard = profiles::get_or_create_mp_keyboard_profile_index(0);
    if (new_profiles) {
        // Only seed an empty profile, including upgrades from the preview format.
        bool empty = true;
        for (int i = 0; i < int(GameInput::COUNT); ++i)
            for (size_t binding = 0; binding < num_bindings_per_input; ++binding)
                empty &= profiles::get_input_binding(keyboard, GameInput(i), binding).is_empty();
        if (empty) {
            const std::pair<GameInput, SDL_Scancode> keys[] = {
                {GameInput::A, SDL_SCANCODE_X}, {GameInput::B, SDL_SCANCODE_C},
                {GameInput::Z, SDL_SCANCODE_Z}, {GameInput::START, SDL_SCANCODE_RETURN},
                {GameInput::L, SDL_SCANCODE_Q}, {GameInput::R, SDL_SCANCODE_E},
                {GameInput::C_UP, SDL_SCANCODE_I}, {GameInput::C_DOWN, SDL_SCANCODE_K},
                {GameInput::C_LEFT, SDL_SCANCODE_J}, {GameInput::C_RIGHT, SDL_SCANCODE_L},
                {GameInput::X_AXIS_NEG, SDL_SCANCODE_LEFT}, {GameInput::X_AXIS_POS, SDL_SCANCODE_RIGHT},
                {GameInput::Y_AXIS_POS, SDL_SCANCODE_UP}, {GameInput::Y_AXIS_NEG, SDL_SCANCODE_DOWN}};
            for (auto [input, key] : keys) profiles::set_input_binding(keyboard, input, 0, InputField::keyboard(key));
            profiles::set_input_binding(keyboard, GameInput::X_AXIS_NEG, 1, InputField::keyboard(SDL_SCANCODE_A));
            profiles::set_input_binding(keyboard, GameInput::X_AXIS_POS, 1, InputField::keyboard(SDL_SCANCODE_D));
            profiles::set_input_binding(keyboard, GameInput::Y_AXIS_POS, 1, InputField::keyboard(SDL_SCANCODE_W));
            profiles::set_input_binding(keyboard, GameInput::Y_AXIS_NEG, 1, InputField::keyboard(SDL_SCANCODE_S));
            profiles::save_controls_config(lambo::config::app_config_dir() / "controls-framework.json");
        }
    }
    // Paddock-split launcher: a brand rail beside numbered
    // menu rows. Player assignment stays reachable through the Controls row,
    // matching the upstream game-options menu, which opens the controls tab
    // rather than starting assignment directly.
    recompui::register_launcher_init_callback([](recompui::LauncherMenu* menu) {
        LauncherActions actions;
        actions.request_play = [] { return startup && startup->request_play(); };
        actions.hide_launcher = [] { recompui::hide_all_contexts(); };
        actions.open_settings = [](LauncherSettingsPage page) {
            recompui::config::open();
            switch (page) {
            case LauncherSettingsPage::Current: break;
            case LauncherSettingsPage::Mods:
                recompui::config::set_tab("mods");
                break;
            case LauncherSettingsPage::Controls:
                recompui::config::set_tab(recompui::config::controls::id);
                break;
            }
        };
        actions.request_quit = [] {
            // The SDL pump in main.cpp turns SDL_QUIT into request_exit()
            // plus the deterministic exit sequence, exactly like closing
            // the window.
            SDL_Event quit{};
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
        };
        build_paddock_launcher(menu, actions);
    });
    RT64::SetRenderHooks(initialize, render, deinitialize);
}

bool handle_event(const SDL_Event& event) {
    // SDL owns dropped text; the shared installer owns file-drop payloads.
    if (event.type == SDL_DROPTEXT) { SDL_free(event.drop.file); return true; }
    // RecompFrontend can open settings from a remapped menu binding inside
    // draw_hook, after the host request check. Refresh ahead of those queued
    // presses so the opening frame uses current values. Coalesce presses and
    // leave held input, motion and idle gameplay on the cheap path.
    if ((event.type == SDL_KEYDOWN && !event.key.repeat) || event.type == SDL_CONTROLLERBUTTONDOWN)
        settings_refresh_requested.store(true, std::memory_order_release);
    SDL_Event copy = event;
    recompinput::handle_event(copy);
    return captures_input();
}
void open_launcher() { request(Page::Home); }
void open_settings() { request(Page::Settings); }
void open_controls() { request(Page::Controls); }
void open_graphics() { request(Page::Graphics); }
void open_enhancements() { request(Page::Enhancements); }
void open_haptics() { request(Page::Haptics); }
void open_mods() { request(Page::Mods); }
void report_mod_load_error(const char* message) {
    std::lock_guard lock(mod_error_mutex);
    mod_error = message;
}
void close_top_page() { overlay.request_close(); }
void toggle_settings() { if (captures_input()) close_top_page(); else open_settings(); }
bool is_initialized() { return ready.load(std::memory_order_acquire); }
bool overlay_visible_intent() { return captures_input(); }
bool captures_input() { return overlay.captures_input(); }
void shutdown() { RT64::SetRenderHooks(nullptr, nullptr, nullptr); if (ready.load()) deinitialize(); }
void save_frontend_preferences() {
    std::lock_guard lock(frontend_mutex());
    recompui::config::get_general_config().save_config();
    recompinput::profiles::save_controls_config(lambo::config::app_config_dir() / "controls-framework.json");
    lambo::config::flush_pending_graphics_updates();
}
}
