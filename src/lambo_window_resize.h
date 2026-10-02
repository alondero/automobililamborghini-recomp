#ifndef LAMBO_WINDOW_RESIZE_H
#define LAMBO_WINDOW_RESIZE_H

namespace lambo::window_resize {

// Decides when a requested windowed size should be pushed onto the live SDL
// window. SDL stays in main.cpp (only the main-thread pump may touch it); the
// rule lives here so it can be tested without a display server.
//
// The decision is request-driven rather than window-state-driven: a resize is
// applied only when the requested size differs from the size last applied. That
// is what keeps a size the player dragged by hand from being fought on the next
// frame, and what keeps the pump from resizing every frame. The live window
// therefore never has to be compared against the request.
//
// A request is consumed when it is attempted, so a refused resize is reported
// once per pick instead of retried (and logged) every frame; SDL rejects a given
// size deterministically. States where the window cannot take the size --
// fullscreen, minimised, maximised -- defer instead of consuming it, so the
// request is applied once the window is an ordinary windowed window again.
// Android's fixed landscape fullscreen surface therefore defers forever, which
// is what that platform wants.
//
// Threading: one instance per window, driven only from the thread that owns the
// window (the main-thread pump), so the state needs no synchronisation.
class Resizer {
public:
    // Record the size the window was created at, so the first pump frame is not
    // mistaken for a pending resize. Also the reset a new window needs.
    void seed(int width, int height);

    // True when the window should be resized to the requested size now.
    bool should_resize(int requested_width, int requested_height,
                       bool fullscreen, bool minimized, bool maximized);

private:
    int applied_width_ = 0;
    int applied_height_ = 0;
    bool seeded_ = false;
};

// The reconciler for the single SDL window the port owns.
Resizer& resizer();

} // namespace lambo::window_resize

#endif
