#include "ui/lambo_ui_requests.h"

namespace lambo::ui {

void OverlayRequestQueue::request_show(int page, int entry_point) {
    std::lock_guard lock(mutex_);
    pending_ = Request{Kind::ShowPage, page, entry_point};
    visible_intent_ = true;
}

void OverlayRequestQueue::request_back() {
    std::lock_guard lock(mutex_);
    pending_ = Request{Kind::Back, 0, 0};
}

void OverlayRequestQueue::request_dismiss() {
    std::lock_guard lock(mutex_);
    pending_ = Request{Kind::Dismiss, 0, 0};
    visible_intent_ = false;
}

bool OverlayRequestQueue::toggle(int page, int entry_point) {
    std::lock_guard lock(mutex_);
    const bool showing = !visible_intent_;
    if (showing) {
        pending_ = Request{Kind::ShowPage, page, entry_point};
    } else {
        pending_ = Request{Kind::Dismiss, 0, 0};
    }
    visible_intent_ = showing;
    return showing;
}

OverlayRequestQueue::Request OverlayRequestQueue::peek() const {
    std::lock_guard lock(mutex_);
    return pending_;
}

OverlayRequestQueue::Request OverlayRequestQueue::take() {
    std::lock_guard lock(mutex_);
    const Request request = pending_;
    pending_ = Request{};
    return request;
}

void OverlayRequestQueue::note_visibility(bool visible) {
    std::lock_guard lock(mutex_);
    if (pending_.kind == Kind::None) visible_intent_ = visible;
}

bool OverlayRequestQueue::visible_intent() const {
    std::lock_guard lock(mutex_);
    return visible_intent_;
}

} // namespace lambo::ui
