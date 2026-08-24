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
    capture.button_event(selected, LogicalButton::A, true); // The press that opened capture cannot bind itself.
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
    expect(capture.sample(raw, profile) == CaptureResult::Conflict,
           "digital half-axis conflicts with a full stick binding on the same axis");
    expect(capture.accept_conflict(profile) == CaptureResult::Committed,
           "keep-both confirmation commits the negative signed half");
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
    expect(capture.sample(raw, profile) == CaptureResult::Conflict,
           "full-axis capture conflicts with digital halves on the same physical axis");
    expect(capture.accept_conflict(profile) == CaptureResult::Committed,
           "keep-both confirmation commits the analog candidate");
    expect(profile.analog[0] == AnalogSource{LogicalAxis::RightY, false, 8000},
           "stick X capture stores its orientation and deadzone separately");

    capture.begin(Target::Throttle, selected);
    capture.sample(raw, profile); capture.sample(raw, profile);
    capture.axis_event(selected, LogicalAxis::TriggerLeft, 20000);
    raw.axes[static_cast<std::size_t>(LogicalAxis::TriggerLeft)] = 20000;
    capture.sample(raw, profile);
    raw = {};
    capture.sample(raw, profile);
    expect(capture.sample(raw, profile) == CaptureResult::Conflict,
           "continuous throttle capture uses the existing source-conflict flow");
    expect(capture.accept_conflict(profile) == CaptureResult::Committed,
           "confirmed throttle source commits after release");
    expect(profile.throttle.source == ThrottleSource{
               LogicalAxis::TriggerLeft, AxisDirection::Positive},
           "throttle capture retains continuous axis direction");

    capture.begin(Target::Throttle, selected);
    capture.sample(raw, profile); capture.sample(raw, profile);
    capture.axis_event(selected, LogicalAxis::TriggerRight, 20000);
    raw.axes[static_cast<std::size_t>(LogicalAxis::TriggerRight)] = 20000;
    capture.sample(raw, profile);
    raw = {};
    capture.sample(raw, profile);
    expect(capture.sample(raw, profile) == CaptureResult::Conflict,
           "default RT-to-R assignment conflicts with analog throttle capture");
    expect(capture.move_conflict(profile) == CaptureResult::Committed,
           "move-binding confirmation commits the throttle source");
    expect(profile.throttle.source == ThrottleSource{
               LogicalAxis::TriggerRight, AxisDirection::Positive},
           "move binding assigns RT to throttle");
    expect(profile.digital[5].size() == 1 &&
           std::get<ButtonSource>(profile.digital[5][0]).button == LogicalButton::RightShoulder,
           "move binding removes only the conflicting RT half-axis from N64 R");

    capture.begin(Target::R, selected);
    capture.sample(raw, profile); capture.sample(raw, profile);
    capture.axis_event(selected, LogicalAxis::TriggerRight, 20000);
    raw.axes[static_cast<std::size_t>(LogicalAxis::TriggerRight)] = 20000;
    capture.sample(raw, profile);
    raw = {};
    capture.sample(raw, profile);
    expect(capture.sample(raw, profile) == CaptureResult::Conflict,
           "moving RT back to a digital target detects the existing throttle assignment");
    expect(capture.move_conflict(profile) == CaptureResult::Committed,
           "move binding transfers RT back out of throttle");
    expect(!profile.throttle.source,
           "reverse move unassigns the conflicting throttle source");
    expect(profile.digital[5].size() == 2,
           "reverse move appends RT alongside the existing R shoulder binding");

    capture.begin(Target::A, selected);
    expect(capture.cancel() == CaptureResult::Cancelled && !capture.active(),
           "disconnect/page-exit cancellation is non-mutating");
    return failures == 0 ? 0 : 1;
}
