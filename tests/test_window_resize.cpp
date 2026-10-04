#include <iostream>

#include "lambo_window_resize.h"

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

using lambo::window_resize::ModeAction;
using lambo::window_resize::Reconciler;
using lambo::window_resize::WindowState;

// An ordinary windowed window: not fullscreen, not minimized, not maximized.
constexpr WindowState windowed{};
constexpr WindowState fullscreen{.fullscreen = true};
constexpr WindowState minimized{.minimized = true};
constexpr WindowState maximized{.maximized = true};

// Stands in for the pump: one plan per frame, then apply whatever the plan asked
// for to a fake window, so a test can reproduce what the player would see.
struct FakeWindow {
    WindowState state{};
    int width = 0;
    int height = 0;

    void apply(const lambo::window_resize::Plan& plan, int requested_width, int requested_height) {
        if (plan.mode == ModeAction::EnterFullscreen) state.fullscreen = true;
        if (plan.mode == ModeAction::LeaveFullscreen) state.fullscreen = false;
        if (plan.resize) { width = requested_width; height = requested_height; }
    }
};
}

int main() {
    // Startup: the window was created at the configured size, so the first frame
    // must not resize it.
    {
        Reconciler reconciler;
        reconciler.seed(1600, 900);
        expect(!reconciler.plan(false, 1600, 900, windowed).resize,
               "startup resized a window already at the requested size");
    }

    // A pick in the settings menu applies once, then goes quiet.
    {
        Reconciler reconciler;
        reconciler.seed(1600, 900);
        expect(reconciler.plan(false, 1920, 1080, windowed).resize,
               "a newly picked window size did not apply");
        expect(!reconciler.plan(false, 1920, 1080, windowed).resize,
               "the applied window size was reapplied on the next frame");
        expect(reconciler.plan(false, 1280, 720, windowed).resize,
               "a second pick did not apply");
    }

    // One dimension changing is a change, either way round.
    {
        Reconciler width_only;
        width_only.seed(1600, 900);
        expect(width_only.plan(false, 1601, 900, windowed).resize,
               "a width-only change was ignored");

        Reconciler height_only;
        height_only.seed(1600, 900);
        expect(height_only.plan(false, 1600, 901, windowed).resize,
               "a height-only change was ignored");
    }

    // A size the player dragged by hand is not fought: the rule compares the
    // request with the last size applied, never with the live window, so an
    // unchanged request stays quiet however the window was moved.
    {
        Reconciler reconciler;
        reconciler.seed(1600, 900);
        expect(!reconciler.plan(false, 1600, 900, windowed).resize,
               "an unchanged request resized a hand-dragged window");
    }

    // A minimized or maximized window defers without consuming the request, so
    // the request survives until the window can take the size.
    {
        Reconciler reconciler;
        reconciler.seed(1600, 900);
        expect(!reconciler.plan(false, 1920, 1080, minimized).resize,
               "a resize was planned for a minimized window");
        expect(!reconciler.plan(false, 1920, 1080, maximized).resize,
               "a resize was planned for a maximized window");
        expect(reconciler.plan(false, 1920, 1080, windowed).resize,
               "a request deferred by a minimized/maximized window was dropped");
    }

    // A window created without a recorded size adopts the first request instead
    // of resizing on the first frame, and still applies the next change.
    {
        Reconciler reconciler;
        expect(!reconciler.plan(false, 1600, 900, windowed).resize,
               "an unseeded reconciler resized on the first frame");
        expect(reconciler.plan(false, 1920, 1080, windowed).resize,
               "an unseeded reconciler lost the next change");
    }

    // Re-seeding is the reset a new window needs: the new size is applied state,
    // not a pending request.
    {
        Reconciler reconciler;
        reconciler.seed(1600, 900);
        expect(reconciler.plan(false, 1920, 1080, windowed).resize,
               "a pick before the reseed did not apply");
        reconciler.seed(1280, 720);
        expect(!reconciler.plan(false, 1280, 720, windowed).resize,
               "a reseeded window resized to its own size");
        expect(reconciler.plan(false, 640, 360, windowed).resize,
               "a reseeded reconciler lost the next change");
    }

    // Window mode: a mode action is planned only when the requested mode changes
    // and the window is not already in it. An unchanged request is deliberately
    // left alone, because that is the F11/Alt+Enter case, where SDL was already
    // toggled and the mode persisted before the pump saw the request.
    {
        Reconciler reconciler;
        reconciler.seed(1600, 900);
        expect(reconciler.plan(false, 1600, 900, windowed).mode == ModeAction::None,
               "startup planned a fullscreen toggle for a windowed window");
        expect(reconciler.plan(true, 1600, 900, windowed).mode == ModeAction::EnterFullscreen,
               "entering fullscreen was not planned");
        expect(reconciler.plan(true, 1600, 900, fullscreen).mode == ModeAction::None,
               "entering fullscreen was planned for an already-fullscreen window");
        expect(reconciler.plan(false, 1600, 900, fullscreen).mode == ModeAction::LeaveFullscreen,
               "leaving fullscreen was not planned");
        expect(reconciler.plan(false, 1600, 900, fullscreen).mode == ModeAction::None,
               "an unchanged mode request toggled the window again");
    }

    // Regression: one Apply can change the window mode and the size together.
    // The size must not be planned against a window that is about to be
    // fullscreen, or it is consumed by a window SDL will not resize and the
    // player never gets it.
    {
        Reconciler reconciler;
        reconciler.seed(1600, 900);
        FakeWindow window;
        window.width = 1600;
        window.height = 900;

        // Frame 1: Fullscreen + 1920x1080 picked in one Apply.
        const auto entering = reconciler.plan(true, 1920, 1080, window.state);
        expect(entering.mode == ModeAction::EnterFullscreen,
               "entering fullscreen alongside a size was not planned");
        expect(!entering.resize,
               "a size was planned against a window entering fullscreen");
        window.apply(entering, 1920, 1080);

        // Frame 2: still fullscreen, so the pending size waits.
        const auto still_fullscreen = reconciler.plan(true, 1920, 1080, window.state);
        expect(still_fullscreen.mode == ModeAction::None, "fullscreen re-toggled itself");
        expect(!still_fullscreen.resize, "a size was planned while fullscreen");
        window.apply(still_fullscreen, 1920, 1080);

        // Frame 3: the player leaves fullscreen, and the size lands with it.
        const auto leaving = reconciler.plan(false, 1920, 1080, window.state);
        expect(leaving.mode == ModeAction::LeaveFullscreen, "leaving fullscreen was not planned");
        expect(leaving.resize,
               "the size picked in fullscreen was dropped instead of applied on exit");
        window.apply(leaving, 1920, 1080);
        expect(window.width == 1920 && window.height == 1080,
               "leaving fullscreen did not restore the picked size");

        // The request is applied once, not on every later frame.
        expect(!reconciler.plan(false, 1920, 1080, window.state).resize,
               "the size was reapplied after it landed");
    }

    // The same Apply in the other direction: Windowed + a new size, while the
    // window is fullscreen. Leaving fullscreen makes the window ordinary, so the
    // size can be applied in the same frame rather than the next one.
    {
        Reconciler reconciler;
        reconciler.seed(1600, 900);
        const auto leaving = reconciler.plan(false, 1280, 720, fullscreen);
        expect(leaving.mode == ModeAction::LeaveFullscreen, "leaving fullscreen was not planned");
        expect(leaving.resize, "the size was dropped on the frame that left fullscreen");
    }

    // Developer resize schedule used by lifecycle captures.
    {
        using lambo::window_resize::parse_scheduled_resize;
        const auto scheduled = parse_scheduled_resize("300:1280x720");
        expect(scheduled && scheduled->frame == 300 && scheduled->width == 1280 && scheduled->height == 720,
               "scheduled resize did not parse");
        for (const char* bad : {"", "300", "300:1280", "x:1280x720", "300:0x720", "300:1280x720junk",
                                "-1:1280x720", "300:99999x720"}) {
            expect(!parse_scheduled_resize(bad), "malformed scheduled resize accepted");
        }
        expect(!parse_scheduled_resize(nullptr), "absent schedule accepted");
    }

    if (failures == 0) std::cout << "window resize reconciliation: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
