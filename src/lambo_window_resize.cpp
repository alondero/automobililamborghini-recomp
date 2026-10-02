#include "lambo_window_resize.h"

namespace lambo::window_resize {

void Reconciler::seed(int width, int height) {
    applied_width_ = width;
    applied_height_ = height;
    seeded_ = true;
}

Plan Reconciler::plan(bool want_fullscreen, int requested_width, int requested_height,
                      const WindowState& state) {
    Plan plan_;
    if (!has_fullscreen_request_ || last_fullscreen_request_ != want_fullscreen) {
        has_fullscreen_request_ = true;
        last_fullscreen_request_ = want_fullscreen;
        if (want_fullscreen != state.fullscreen) {
            plan_.mode = want_fullscreen ? ModeAction::EnterFullscreen
                                         : ModeAction::LeaveFullscreen;
        }
    }

    // The size is planned against the window as it will be once `mode` has been
    // applied, not as it was read: a size picked together with Fullscreen would
    // otherwise be consumed against a window that is already fullscreen, where
    // SDL ignores it, and never be applied at all.
    const bool fullscreen_after =
        plan_.mode == ModeAction::LeaveFullscreen ? false
        : plan_.mode == ModeAction::EnterFullscreen ? true
                                                   : state.fullscreen;
    // Deferring leaves the request unconsumed.
    if (fullscreen_after || state.minimized || state.maximized) return plan_;

    // Window created without a recorded size: adopt the first request rather
    // than resizing a window that is already there.
    if (!seeded_) {
        seed(requested_width, requested_height);
        return plan_;
    }
    if (requested_width != applied_width_ || requested_height != applied_height_) {
        applied_width_ = requested_width;
        applied_height_ = requested_height;
        plan_.resize = true;
    }
    return plan_;
}

Reconciler& reconciler() {
    static Reconciler instance;
    return instance;
}

} // namespace lambo::window_resize
