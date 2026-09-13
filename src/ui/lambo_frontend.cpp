#include "lambo_ui.h"
#include "lambo_frontend_input.h"
#include "lambo_config.h"
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
std::atomic<bool> ready{false}, capture{false};
std::atomic<int> pending{-1};
lambo::StartupController* startup = nullptr;

void request(Page page) {
    capture.store(true, std::memory_order_release);
    pending.store(static_cast<int>(page), std::memory_order_release);
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
    const int action = pending.exchange(-1, std::memory_order_acq_rel);
    refresh_frontend_settings();
    if (action == -2) recompui::try_close_current_context();
    else if (action >= 0) {
        const auto page = static_cast<Page>(action);
        if (page == Page::Home) recompui::show_context(recompui::get_launcher_context_id(), "");
        else {
            recompui::config::open();
            const char* id = "general";
            switch (page) {
            case Page::Graphics: id = "graphics"; break;
            case Page::Enhancements: id = "enhancements"; break;
            case Page::Controls: id = "controls-framework"; break;
            case Page::Haptics: id = "pedals"; break;
            case Page::Player: id = "driver"; break;
            default: break;
            }
            recompui::config::set_tab(id);
        }
    }
    draw_hook(commands, framebuffer);
    capture.store(recompui::is_context_capturing_input(), std::memory_order_release);
}

void deinitialize() {
    std::lock_guard lock(frontend_mutex());
    ready.store(false, std::memory_order_release);
    capture.store(false, std::memory_order_release);
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
    create_frontend_settings();
    create_frontend_pedal_settings();
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
    recompui::register_launcher_init_callback([](recompui::LauncherMenu* menu) {
        auto context = recompui::get_launcher_context_id();
        auto* container = menu->get_menu_container();
        container->set_top(35.0f, recompui::Unit::Percent);
        container->set_display(recompui::Display::Flex);
        container->set_flex_direction(recompui::FlexDirection::Column);
        container->set_align_items(recompui::AlignItems::Center);
        container->set_gap(16.0f);
        container->set_as_navigation_container(recompui::NavigationType::Vertical);
        auto button = [&](const char* label, std::function<void()> action) {
            context.create_element<recompui::Button>(container, label, recompui::ButtonStyle::Primary)->add_pressed_callback(action);
        };
        button("Play", [] { if (startup && startup->request_play()) recompui::hide_all_contexts(); });
        button("Settings", [] { recompui::config::open(); });
        button("Assign 1-4 players", [] { recompinput::playerassignment::start(); });
    });
    RT64::SetRenderHooks(initialize, render, deinitialize);
}

bool handle_event(const SDL_Event& event) {
    // No mod loader is enabled by this migration. Do not route dropped files
    // into the frontend's mod installer (whose tab is deliberately absent).
    if (event.type == SDL_DROPFILE || event.type == SDL_DROPTEXT) { SDL_free(event.drop.file); return true; }
    if (event.type == SDL_DROPBEGIN || event.type == SDL_DROPCOMPLETE) return true;
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
void open_player() { request(Page::Player); }
void close_top_page() { pending.store(-2, std::memory_order_release); }
void toggle_settings() { if (captures_input()) close_top_page(); else open_settings(); }
bool is_initialized() { return ready.load(std::memory_order_acquire); }
bool overlay_visible_intent() { return captures_input(); }
bool captures_input() { return pending.load(std::memory_order_acquire) >= 0 || capture.load(std::memory_order_acquire); }
void shutdown() { RT64::SetRenderHooks(nullptr, nullptr, nullptr); if (ready.load()) deinitialize(); }
void save_frontend_preferences() {
    std::lock_guard lock(frontend_mutex());
    recompui::config::get_general_config().save_config();
    recompinput::profiles::save_controls_config(lambo::config::app_config_dir() / "controls-framework.json");
    lambo::config::flush_pending_graphics_updates();
}
}
