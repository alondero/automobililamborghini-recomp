#ifndef LAMBO_MENU_H
#define LAMBO_MENU_H

union SDL_Event;
struct SDL_Window;

namespace lambo::menu {

// Attach the lightweight native application menu where the platform supports it
// (currently Win32). Other platforms deliberately remain no-op until the port has
// a renderer-integrated UI layer.
void attach(SDL_Window* window);
bool handle_event(const SDL_Event& event);
void toggle_fullscreen();

} // namespace lambo::menu

#endif
