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
    Player,
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
void open_player();
void close_top_page();
// Toggles the overlay: opens Settings from gameplay, hides it outright from any
// depth. Owns the decision internally so callers need not read the render
// thread's visibility, which lags by a frame.
void toggle_settings();
bool is_initialized();
// Whether the overlay is showing or about to be, including a request the render
// thread has not consumed yet. This is the accessor to branch on from the event
// pump: visibility is published asynchronously by the render thread, so reading
// the applied state directly would lag by a frame.
bool overlay_visible_intent();
bool captures_input();
void shutdown();

} // namespace lambo::ui

#endif
