#ifndef LAMBO_UI_H
#define LAMBO_UI_H

#include <cstdint>

union SDL_Event;
struct SDL_Window;

namespace lambo {
class StartupController;
}

namespace lambo::ui {

enum class Page {
    Home,
    Settings,
    Graphics,
    Enhancements,
    Controls,
    Haptics,
};

enum class EntryPoint {
    Startup,
    InGameOverlay,
};

void set_window(SDL_Window* window);
void install_render_hooks();
void set_startup_controller(lambo::StartupController* controller);
bool handle_event(const SDL_Event& event);
void open_launcher();
void open_settings();
void open_controls();
void open_graphics();
void open_enhancements();
void open_haptics();
void close_top_page();
bool is_initialized();
bool is_visible();
bool captures_input();
void shutdown();

// Applies the current `texture_upscaler` graphics.json/env value to the
// running RT64 context. New texture uploads will use the new mode; textures
// already in the cache keep their original samples until they are evicted.
void refresh_texture_upscaler();

} // namespace lambo::ui

#endif
