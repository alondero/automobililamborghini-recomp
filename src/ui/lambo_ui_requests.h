#ifndef LAMBO_UI_REQUESTS_H
#define LAMBO_UI_REQUESTS_H

#include <cstdint>
#include <mutex>

namespace lambo::ui {

// Single-slot request queue for the options overlay.
//
// Producers (the menu thread and the event pump) and the consumer (the RT64
// render thread inside draw_hook) never share a document. Exactly one intent is
// ever outstanding, and that is the point: separate "show page" / "back" /
// "dismiss" flags are applied in a fixed order regardless of which the user
// pressed last, so a dismiss followed by an open would load a document and then
// unload it again on the very same frame.
//
// Page and entry-point values are plain ints so this header stays free of RmlUi
// and of lambo_ui.h's enums. The UI layer casts. That keeps the whole queue
// unit testable without a window or a renderer.
class OverlayRequestQueue {
public:
    enum class Kind : uint8_t {
        None,
        ShowPage,
        Back,
        Dismiss,
    };

    struct Request {
        Kind kind = Kind::None;
        int page = 0;
        int entry_point = 0;

        bool operator==(const Request&) const = default;
    };

    // --- Producer side ------------------------------------------------------

    // Show a page, replacing any outstanding request.
    void request_show(int page, int entry_point);

    // Step back one page. Whether that closes the overlay depends on the page
    // stack, so the resulting visibility is left for note_visibility to settle.
    void request_back();

    // Hide the overlay outright.
    void request_dismiss();

    // Flip between showing `page` and hidden. Returns true when the overlay is
    // now headed for visible.
    //
    // The decision reads the accumulated intent rather than the render thread's
    // reported visibility, which lags by a frame: reading that would let two
    // presses inside one frame both observe "hidden" and open twice instead of
    // toggling.
    bool toggle(int page, int entry_point);

    // --- Consumer side ------------------------------------------------------

    // The outstanding request, leaving it in place.
    Request peek() const;

    // The outstanding request, clearing it.
    Request take();

    // Report the visibility the consumer actually applied. Only adopted when
    // nothing is outstanding, so a newer producer intent can never be
    // overwritten by an older observation.
    void note_visibility(bool visible);

    // Producers' view of whether the overlay should be showing, including any
    // request that has not been consumed yet.
    bool visible_intent() const;

private:
    mutable std::mutex mutex_;
    Request pending_{};
    bool visible_intent_ = false;
};

} // namespace lambo::ui

#endif
