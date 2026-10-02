#include "lambo_window_resize.h"

namespace lambo::window_resize {

void Resizer::seed(int width, int height) {
    applied_width_ = width;
    applied_height_ = height;
    seeded_ = true;
}

bool Resizer::should_resize(int requested_width, int requested_height,
                            bool fullscreen, bool minimized, bool maximized) {
    // Deferring leaves the request unconsumed, so these states keep the pending
    // request instead of dropping it.
    if (fullscreen || minimized || maximized) return false;
    // Window created without recording its size: adopt the first request rather
    // than resizing a window that is already there, which also keeps the applied
    // size meaningful for the change that follows.
    if (!seeded_) {
        seed(requested_width, requested_height);
        return false;
    }
    if (requested_width == applied_width_ && requested_height == applied_height_) return false;
    applied_width_ = requested_width;
    applied_height_ = requested_height;
    return true;
}

Resizer& resizer() {
    static Resizer instance;
    return instance;
}

} // namespace lambo::window_resize
