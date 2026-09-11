#include <iostream>

#include "ui/lambo_ui_requests.h"

// The overlay request queue is what makes the menu button's toggle safe across
// the menu thread and the RT64 render thread. These cases pin down the two
// behaviours that the render thread cannot recover from: more than one intent
// outstanding at once, and a toggle deciding from stale visibility.
namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

using Kind = lambo::ui::OverlayRequestQueue::Kind;

// Plain ints stand in for lambo::ui::Page/EntryPoint so this test needs neither
// RmlUi nor the UI header.
constexpr int settings_page = 1;
constexpr int home_page = 0;
constexpr int overlay_entry = 1;
constexpr int startup_entry = 0;

} // namespace

int main() {
    // A dismiss followed by an open, both before the render thread runs. Three
    // independent flags would apply the dismiss last regardless of press order
    // and unload the document the open had just loaded.
    {
        lambo::ui::OverlayRequestQueue queue;
        queue.request_dismiss();
        queue.request_show(settings_page, overlay_entry);
        const auto request = queue.take();
        expect(request.kind == Kind::ShowPage,
               "the newest intent wins when a dismiss is followed by an open");
        expect(request.page == settings_page && request.entry_point == overlay_entry,
               "a show request carries its page and entry point together");
        expect(queue.take().kind == Kind::None, "taking a request clears it");
    }

    // The opposite order must apply the dismiss instead.
    {
        lambo::ui::OverlayRequestQueue queue;
        queue.request_show(settings_page, overlay_entry);
        queue.request_dismiss();
        expect(queue.take().kind == Kind::Dismiss,
               "a dismiss queued after an open replaces it");
    }

    // Back is its own intent and can equally replace, or be replaced by, a
    // pending show or dismiss.
    {
        lambo::ui::OverlayRequestQueue queue;
        queue.request_show(settings_page, overlay_entry);
        queue.take();
        queue.request_back();
        expect(queue.take().kind == Kind::Back, "back is delivered as its own intent");

        queue.request_dismiss();
        queue.request_back();
        expect(queue.take().kind == Kind::Back, "a back queued after a dismiss replaces it");

        queue.request_back();
        queue.request_show(settings_page, overlay_entry);
        expect(queue.take().kind == Kind::ShowPage, "a show queued after a back replaces it");
    }

    // Two presses inside one frame must toggle closed, not open twice. This is
    // the case the event pump got wrong by reading the render thread's
    // asynchronously published visibility, which still reported "hidden".
    {
        lambo::ui::OverlayRequestQueue queue;
        expect(queue.toggle(settings_page, overlay_entry),
               "the first toggle reports the overlay is showing");
        expect(queue.visible_intent(), "the first toggle records a visible intent");
        expect(!queue.toggle(settings_page, overlay_entry),
               "the second toggle in the same frame reports hiding");
        expect(queue.take().kind == Kind::Dismiss,
               "two presses in one frame toggle closed instead of opening twice");
        expect(!queue.visible_intent(), "a toggle to hidden clears the visible intent");
    }

    // Toggling when hidden opens Settings as an in-game overlay.
    {
        lambo::ui::OverlayRequestQueue queue;
        expect(queue.toggle(settings_page, overlay_entry), "toggling from hidden opens");
        const auto request = queue.take();
        expect(request.kind == Kind::ShowPage && request.page == settings_page &&
                   request.entry_point == overlay_entry,
               "toggling open requests the Settings page as the in-game overlay");
    }

    // A dismiss clears the intent, so the next press reopens rather than
    // trying to dismiss an already-hidden overlay.
    {
        lambo::ui::OverlayRequestQueue queue;
        queue.request_show(settings_page, overlay_entry);
        queue.take();
        queue.note_visibility(true);
        queue.request_dismiss();
        expect(!queue.visible_intent(), "a dismiss clears the visible intent");
        expect(queue.toggle(settings_page, overlay_entry), "toggling after a dismiss reopens");
    }

    // The consumer's observation is adopted only when nothing newer is pending;
    // otherwise a producer that ran between take() and note_visibility() would
    // have its intent silently overwritten.
    {
        lambo::ui::OverlayRequestQueue queue;
        queue.request_show(settings_page, overlay_entry);
        queue.note_visibility(false);
        expect(queue.visible_intent(),
               "a pending request is not clobbered by a stale visibility report");

        queue.take();
        queue.note_visibility(false);
        expect(!queue.visible_intent(),
               "an applied visibility is adopted once nothing is pending");
    }

    // Back does not itself decide visibility -- only the page stack can -- so
    // the intent must survive until the consumer reports the outcome.
    {
        lambo::ui::OverlayRequestQueue queue;
        queue.request_show(settings_page, overlay_entry);
        queue.take();
        queue.note_visibility(true);
        queue.request_back();
        expect(queue.visible_intent(),
               "back leaves the visible intent for the consumer to settle");
        expect(queue.take().kind == Kind::Back, "the consumer takes the back intent");
        queue.note_visibility(false);
        expect(!queue.visible_intent(),
               "back at the root hides the overlay and clears the intent");
    }

    // The launcher path uses the same queue with a different page/entry pair.
    {
        lambo::ui::OverlayRequestQueue queue;
        queue.request_show(home_page, startup_entry);
        const auto request = queue.take();
        expect(request.page == home_page && request.entry_point == startup_entry,
               "the launcher's Home page and Startup entry point round-trip");
    }

    // peek() exposes the request without consuming it.
    {
        lambo::ui::OverlayRequestQueue queue;
        queue.request_dismiss();
        expect(queue.peek().kind == Kind::Dismiss, "peek exposes the outstanding request");
        expect(queue.take().kind == Kind::Dismiss, "peek leaves the request in place");
        expect(queue.peek().kind == Kind::None, "the queue is empty once taken");
    }

    return failures == 0 ? 0 : 1;
}
