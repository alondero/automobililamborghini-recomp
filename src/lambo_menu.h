#ifndef LAMBO_MENU_H
#define LAMBO_MENU_H

union SDL_Event;
struct SDL_Window;

namespace lambo::menu {

// Attach the lightweight native quick-access menu where the platform supports it
// (currently Win32). The cross-platform launcher owns the same settings model.
void attach(SDL_Window* window);
bool handle_event(const SDL_Event& event);
void toggle_fullscreen();

} // namespace lambo::menu

#endif
