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

// A plain windowed window: not fullscreen, not minimized, not maximized.
constexpr bool windowed = false;
}

int main() {
    using lambo::window_resize::Resizer;

    // Startup: the window was created at the configured size, so the first pump
    // frame must not resize it.
    {
        Resizer resizer;
        resizer.seed(1600, 900);
        expect(!resizer.should_resize(1600, 900, windowed, windowed, windowed),
               "startup resized a window already at the requested size");
    }

    // A pick in the settings menu applies once, then goes quiet.
    {
        Resizer resizer;
        resizer.seed(1600, 900);
        expect(resizer.should_resize(1920, 1080, windowed, windowed, windowed),
               "a newly picked window size did not apply");
        expect(!resizer.should_resize(1920, 1080, windowed, windowed, windowed),
               "the applied window size was reapplied on the next frame");
        expect(resizer.should_resize(1280, 720, windowed, windowed, windowed),
               "a second pick did not apply");
    }

    // A width-only change is a change.
    {
        Resizer resizer;
        resizer.seed(1600, 900);
        expect(resizer.should_resize(1600, 901, windowed, windowed, windowed),
               "a height-only change was ignored");
    }

    // A size the player dragged by hand is not fought: the rule compares the
    // request with the last applied size, never with the live window, so an
    // unchanged request stays quiet however the window was moved.
    {
        Resizer resizer;
        resizer.seed(1600, 900);
        expect(!resizer.should_resize(1600, 900, windowed, windowed, windowed),
               "an unchanged request resized a hand-dragged window");
    }

    // Fullscreen defers without consuming the request, so leaving fullscreen
    // applies the size that was picked while in fullscreen.
    {
        Resizer resizer;
        resizer.seed(1600, 900);
        expect(resizer.should_resize(1920, 1080, windowed, windowed, windowed),
               "the first pick did not apply");
        expect(!resizer.should_resize(1280, 720, /*fullscreen=*/true, windowed, windowed),
               "a resize was attempted while fullscreen");
        expect(resizer.should_resize(1280, 720, windowed, windowed, windowed),
               "a size picked in fullscreen was dropped instead of deferred");
        expect(!resizer.should_resize(1280, 720, windowed, windowed, windowed),
               "the deferred size was applied twice");
    }

    // Minimized and maximized defer the same way: the window cannot take the
    // size yet, and the request must survive until it can.
    {
        Resizer resizer;
        resizer.seed(1600, 900);
        expect(!resizer.should_resize(1920, 1080, windowed, /*minimized=*/true, windowed),
               "a resize was attempted while minimized");
        expect(!resizer.should_resize(1920, 1080, windowed, windowed, /*maximized=*/true),
               "a resize was attempted while maximized");
        expect(resizer.should_resize(1920, 1080, windowed, windowed, windowed),
               "a request deferred by a minimized/maximized window was dropped");
    }

    // A window created without a recorded size adopts the first request instead
    // of resizing on the first frame, and still applies the next change.
    {
        Resizer resizer;
        expect(!resizer.should_resize(1600, 900, windowed, windowed, windowed),
               "an unseeded resizer resized on the first frame");
        expect(resizer.should_resize(1920, 1080, windowed, windowed, windowed),
               "an unseeded resizer lost the next change");
    }

    // Re-seeding is the reset a new window needs: the new size is applied state,
    // not a pending request.
    {
        Resizer resizer;
        resizer.seed(1600, 900);
        expect(resizer.should_resize(1920, 1080, windowed, windowed, windowed),
               "a pick before the reseed did not apply");
        resizer.seed(1280, 720);
        expect(!resizer.should_resize(1280, 720, windowed, windowed, windowed),
               "a reseeded window resized to its own size");
        expect(resizer.should_resize(640, 360, windowed, windowed, windowed),
               "a reseeded resizer lost the next change");
    }

    if (failures == 0) std::cout << "window resize reconciliation: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
