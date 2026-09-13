// Standalone experiment: real RecompFrontend menus/input, no game or ROM loaded.
#include <SDL.h>
#include <SDL_syswm.h>
#include <filesystem>
#include <cstdio>
#include "librecomp/game.hpp"
#include "recompui/recompui.h"
#include "recompui/config.h"
#include "recompui/program_config.h"
#include "recompui/renderer.h"
#include "recompinput/input_events.h"
#include "recompinput/profiles.h"
#include "elements/ui_button.h"

SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;

int main() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return 1;
    window = SDL_CreateWindow("Lamborghini - RecompFrontend experiment",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) return 1;
    // Always isolated, including when launched from a portable game installation.
    char* base = SDL_GetBasePath();
    if (!base) return 1;
    // The upstream Windows asset resolver is relative to the working directory.
    std::filesystem::current_path(std::filesystem::path(base));
    const auto config_dir = std::filesystem::path(base) / "preview-config";
    SDL_free(base);
    std::filesystem::create_directories(config_dir);
    recomp::register_config_path(config_dir);
    recompui::programconfig::set_program_name("Lamborghini Frontend Preview");
    recompui::programconfig::set_program_id(u8"LamborghiniFrontendPreview");
    recompui::register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");
    recompui::register_extra_font("LatoLatin-Bold.ttf");
    recompinput::players::set_player_count_range(1, 4);
    recompinput::players::set_single_player_mode(false);
    recompui::config::create_general_tab({});
    recompui::config::create_graphics_tab();
    recompui::config::create_controls_tab();
    // Sound is omitted until connected to the game's audio sink.
    recompui::config::finalize();
    recompui::register_launcher_init_callback([](recompui::LauncherMenu* menu) {
        auto context = recompui::get_launcher_context_id();
        auto* container = menu->get_menu_container();
        container->set_top(45.0f, recompui::Unit::Percent);
        container->set_display(recompui::Display::Flex);
        container->set_flex_direction(recompui::FlexDirection::Column);
        container->set_align_items(recompui::AlignItems::Center);
        container->set_gap(16.0f);
        container->set_as_navigation_container(recompui::NavigationType::Vertical);
        auto* settings = context.create_element<recompui::Button>(
            menu->get_menu_container(), "Settings and controller profiles", recompui::ButtonStyle::Primary);
        settings->add_pressed_callback([] { recompui::config::open(); });
        auto* players = context.create_element<recompui::Button>(
            menu->get_menu_container(), "Assign 1-4 players", recompui::ButtonStyle::Primary);
        players->add_pressed_callback([] { recompinput::playerassignment::start(); });
    });
    SDL_SysWMinfo wm{};
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(window, &wm)) return 1;
    recomp::Configuration config{};
    config.project_version = {0, 0, 1, "-frontend-preview"};
    config.window_handle = {wm.info.win.window, GetCurrentThreadId()};
    config.renderer_callbacks.create_render_context = [](uint8_t* rdram,
        ultramodern::renderer::WindowHandle handle, bool developer) {
        return recompui::renderer::create_render_context(rdram, handle,
            ultramodern::renderer::PresentationMode::Console, developer);
    };
    config.gfx_callbacks.update_gfx = [](void*) {
        recompinput::handle_events();
        recompinput::poll_inputs();
        recompinput::update_rumble();
    };
    config.input_callbacks.poll_input = recompinput::poll_inputs;
    config.input_callbacks.get_input = recompinput::profiles::get_n64_input;
    config.input_callbacks.set_rumble = recompinput::set_rumble;
    config.audio_callbacks.set_frequency = [](uint32_t) {};
    config.audio_callbacks.get_frames_remaining = []() -> size_t { return 0; };
    config.audio_callbacks.queue_samples = [](int16_t*, size_t) {};
    recomp::start(config);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
