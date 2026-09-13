#pragma once

#include <atomic>

#include "lambo_ui.h"

namespace lambo::ui {

enum class OverlayRequestKind { None, Close, Page };

struct OverlayRequest {
    OverlayRequestKind kind = OverlayRequestKind::None;
    Page page = Page::Settings;
};

// Cross-thread request/capture state for the frontend overlay. Requests are
// posted by the SDL pump and consumed by the presentation callback; the input
// gate stays closed until that callback publishes the context's actual state.
class OverlayCaptureGate {
public:
    void request(Page page) {
        capture_.store(true, std::memory_order_release);
        pending_.store(static_cast<int>(page), std::memory_order_release);
    }

    void request_close() {
        pending_.store(kCloseRequest, std::memory_order_release);
    }

    OverlayRequest take_request() {
        const int request = pending_.exchange(kNoRequest, std::memory_order_acq_rel);
        if (request == kCloseRequest) return {OverlayRequestKind::Close, Page::Settings};
        if (request >= 0) return {OverlayRequestKind::Page, static_cast<Page>(request)};
        return {};
    }

    void publish_context_capture(bool capturing) {
        capture_.store(capturing, std::memory_order_release);
    }

    bool captures_input() const {
        return pending_.load(std::memory_order_acquire) >= 0 ||
               capture_.load(std::memory_order_acquire);
    }

private:
    static constexpr int kNoRequest = -1;
    static constexpr int kCloseRequest = -2;
    std::atomic<int> pending_{kNoRequest};
    std::atomic<bool> capture_{false};
};

} // namespace lambo::ui
