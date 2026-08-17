#include <chrono>
#include <iostream>
#include <vector>

#include "ui/lambo_ui_input.h"

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_events(const std::vector<lambo::ui::NavigationEvent>& actual,
                   std::initializer_list<lambo::ui::NavigationEvent> expected,
                   const char* message) {
    expect(actual == std::vector<lambo::ui::NavigationEvent>(expected), message);
}

} // namespace

int main() {
    using namespace std::chrono_literals;
    using lambo::ui::NavigationAxis;
    using lambo::ui::NavigationEvent;
    using lambo::ui::NavigationEventType;
    using lambo::ui::NavigationKey;

    constexpr NavigationEvent down_press{NavigationEventType::Press, NavigationKey::Down};
    constexpr NavigationEvent down_release{NavigationEventType::Release, NavigationKey::Down};
    const lambo::ui::ControllerNavigation::TimePoint start{};
    expect(!lambo::ui::navigation_axis_active(8999),
           "stick jitter remains below the navigation dead zone");
    expect(lambo::ui::navigation_axis_active(9000),
           "stick motion at the dead-zone boundary is meaningful navigation");

    lambo::ui::ControllerNavigation buttons;
    expect_events(buttons.button_down(NavigationKey::Down, start), {down_press},
                  "D-pad press emits one navigation press");
    expect_events(buttons.button_down(NavigationKey::Down, start), {},
                  "duplicate D-pad down is ignored");
    expect_events(buttons.update(start + 499ms), {},
                  "D-pad does not repeat before the initial delay");
    expect_events(buttons.update(start + 500ms), {down_press},
                  "D-pad repeats after the initial delay");
    expect_events(buttons.button_up(NavigationKey::Down, start + 600ms), {down_release},
                  "D-pad release emits a navigation release");
    expect_events(buttons.update(start + 1s), {},
                  "released D-pad input never repeats");

    lambo::ui::ControllerNavigation axis;
    expect_events(axis.axis_motion(NavigationAxis::Vertical, 16000, start), {down_press},
                  "stick crossing the dead zone presses Down");
    expect_events(axis.axis_motion(NavigationAxis::Vertical, 0, start + 10ms), {down_release},
                  "stick returning to neutral releases Down");
    expect_events(axis.update(start + 1s), {},
                  "neutral stick input never repeats");

    lambo::ui::ControllerNavigation activation;
    constexpr NavigationEvent activate_press{NavigationEventType::Press, NavigationKey::Activate};
    constexpr NavigationEvent activate_release{NavigationEventType::Release, NavigationKey::Activate};
    expect_events(activation.button_down(NavigationKey::Activate, start), {activate_press},
                  "A presses Activate once");
    expect_events(activation.update(start + 1s), {},
                  "Activate is not a repeatable navigation key");
    activation.reset();
    expect_events(activation.button_down(NavigationKey::Activate, start + 2s), {activate_press},
                  "closing the UI clears held controller state before reopening");
    expect_events(activation.button_up(NavigationKey::Activate, start + 2s), {activate_release},
                  "A release clears Activate");

    lambo::ui::ControllerNavigation chord;
    constexpr NavigationEvent right_press{NavigationEventType::Press, NavigationKey::Right};
    constexpr NavigationEvent right_release{NavigationEventType::Release, NavigationKey::Right};
    expect_events(chord.button_down(NavigationKey::Down, start), {down_press},
                  "first chord direction is pressed");
    expect_events(chord.button_down(NavigationKey::Right, start + 10ms), {right_press},
                  "second chord direction is pressed");
    expect_events(chord.button_up(NavigationKey::Right, start + 20ms), {right_release},
                  "releasing the latest chord direction emits its release");
    expect_events(chord.update(start + 519ms), {},
                  "remaining chord direction receives a fresh repeat delay");
    expect_events(chord.update(start + 520ms), {down_press},
                  "remaining held direction resumes repeating");

    return failures == 0 ? 0 : 1;
}
