#ifndef LAMBO_WINDOW_RESIZE_H
#define LAMBO_WINDOW_RESIZE_H

namespace lambo::window_resize {

// What the window owner must do about the requested window mode this frame.
enum class ModeAction { None, EnterFullscreen, LeaveFullscreen };

// The window state one snapshot of SDL window flags describes. Read fresh each
// frame, before the mode action is applied.
struct WindowState {
    bool fullscreen = false;
    bool minimized = false;
    bool maximized = false;
};

// What to do about the window this frame, decided together so the two cannot be
// applied out of order: apply `mode` first, then `resize`.
struct Plan {
    ModeAction mode = ModeAction::None;
    bool resize = false;
};

// Decides the window mode and size requests for the window the port owns. SDL
// stays in main.cpp; only the thread that owns the window drives this.
//
// Invariants:
//  - the size is compared with the last size applied, never with the live
//    window, so a size the player dragged by hand is not fought;
//  - a resize is planned only for a window that will be windowed once `mode` has
//    been applied, so a size picked in the same Apply as Fullscreen waits instead
//    of being consumed by a window SDL will not resize;
//  - a deferred request stays pending and is planned as soon as the window can
//    take it.
// The rationale is in docs/recompfrontend.md.
class Reconciler {
public:
    // Record the size the window was created at, so the first frame is not
    // mistaken for a pending resize. Also the reset a new window needs.
    void seed(int width, int height);

    // Plan one frame from a single read of the window. `want_fullscreen` and the
    // requested size both come from the settings schema.
    Plan plan(bool want_fullscreen, int requested_width, int requested_height,
              const WindowState& state);

private:
    // Unset until the first plan, so the first frame still reconciles the mode.
    bool last_fullscreen_request_ = false;
    bool has_fullscreen_request_ = false;
    int applied_width_ = 0;
    int applied_height_ = 0;
    bool seeded_ = false;
};

// The reconciler for the single SDL window the port owns.
Reconciler& reconciler();

} // namespace lambo::window_resize

#endif
