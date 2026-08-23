#include <iostream>
#include <string>

#include "controls/lambo_controls.h"
#include "ultramodern/config.hpp"

namespace ultramodern::renderer {
void set_graphics_config(const GraphicsConfig&) {}
}

namespace {
int failures = 0;
void expect(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    using namespace lambo::controls;
    constexpr std::int32_t selected = 42;
    Profile profile = default_profile();
    Capture capture;
    RawState raw{};

    capture.begin(Target::A, selected);
    capture.button_event(selected, LogicalButton::A, true); // activation event
    expect(capture.phase() == CapturePhase::WaitingForNeutral,
           "activation event cannot self-bind");
    raw.buttons[static_cast<std::size_t>(LogicalButton::A)] = true;
    capture.sample(raw, profile);
    raw = {};
    capture.sample(raw, profile);
    expect(capture.phase() == CapturePhase::WaitingForNeutral,
           "one neutral sample does not arm capture");
    capture.sample(raw, profile);
    expect(capture.phase() == CapturePhase::Listening, "two neutral samples arm capture");

    capture.axis_event(selected, LogicalAxis::RightX, 15999);
    expect(capture.phase() == CapturePhase::Listening, "axis jitter below activation is ignored");
    capture.axis_event(selected + 1, LogicalAxis::RightX, 20000);
    expect(capture.phase() == CapturePhase::Listening, "wrong controller cannot bind");
    capture.axis_event(selected, LogicalAxis::RightX, 20000);
    expect(capture.phase() == CapturePhase::WaitingForRelease, "signed axis candidate captured");
    raw.axes[static_cast<std::size_t>(LogicalAxis::RightX)] = 20000;
    capture.sample(raw, profile);
    raw = {};
    capture.sample(raw, profile);
    expect(capture.phase() == CapturePhase::WaitingForRelease,
           "one release-neutral sample does not commit");
    expect(capture.sample(raw, profile) == CaptureResult::Conflict,
           "cross-target duplicate requires confirmation");
    expect(capture.accept_conflict(profile) == CaptureResult::Committed,
           "keep-both confirmation commits duplicate");
    expect(profile.digital[0].size() == 2, "confirmed binding is appended");

    capture.begin(Target::B, selected);
    capture.sample(raw, profile); capture.sample(raw, profile);
    capture.button_event(selected, LogicalButton::B, true);
    raw.buttons[static_cast<std::size_t>(LogicalButton::B)] = true;
    capture.sample(raw, profile);
    raw = {};
    capture.sample(raw, profile);
    expect(capture.sample(raw, profile) == CaptureResult::Noop,
           "face B is bindable and exact duplicate is a no-op");

    capture.begin(Target::Z, selected);
    capture.sample(raw, profile); capture.sample(raw, profile);
    capture.axis_event(selected, LogicalAxis::LeftX, -16000);
    expect(capture.phase() == CapturePhase::WaitingForRelease,
           "negative axis crossing at the activation threshold is captured");
    raw.axes[static_cast<std::size_t>(LogicalAxis::LeftX)] = -16000;
    capture.sample(raw, profile);
    raw = {};
    capture.sample(raw, profile);
    expect(capture.sample(raw, profile) == CaptureResult::Committed,
           "negative signed half commits after release");
    expect(std::get<AxisHalfSource>(profile.digital[2].back()).direction == AxisDirection::Negative,
           "captured negative half retains its direction");

    capture.begin(Target::A, selected);
    capture.sample(raw, profile); capture.sample(raw, profile);
    expect(capture.button_event(selected, LogicalButton::Back, true) == CaptureResult::Cancelled,
           "controller Back cancels capture");

    capture.begin(Target::StickX, selected);
    capture.sample(raw, profile); capture.sample(raw, profile);
    capture.axis_event(selected, LogicalAxis::RightY, -20000);
    raw.axes[static_cast<std::size_t>(LogicalAxis::RightY)] = -20000;
    capture.sample(raw, profile);
    raw = {};
    capture.sample(raw, profile);
    expect(capture.sample(raw, profile) == CaptureResult::Committed,
           "analog candidate commits after release");
    expect(profile.analog[0] == AnalogSource{LogicalAxis::RightY, false, 8000},
           "stick X capture stores its orientation and deadzone separately");

    capture.begin(Target::A, selected);
    expect(capture.cancel() == CaptureResult::Cancelled && !capture.active(),
           "disconnect/page-exit cancellation is non-mutating");
    return failures == 0 ? 0 : 1;
}
