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
void update_capture();
void open_launcher();
void open_settings();
void close_top_page();
bool is_initialized();
bool is_visible();
bool captures_input();
void shutdown();

} // namespace lambo::ui

#endif
