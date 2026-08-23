#include <iostream>

#include "lambo_input_gate.h"

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main() {
    constexpr uint32_t physical = 0x76543210;
    lambo::input_gate::set_ui_capture(false);
    lambo::input_gate::publish_physical_snapshot(physical);
    expect(lambo::input_gate::guest_snapshot() == physical,
           "physical state is published when capture is disabled");

    lambo::input_gate::set_ui_capture(true);
    lambo::input_gate::publish_physical_snapshot(0xFFFFFFFFu);
    expect(lambo::input_gate::guest_input_suppressed(),
           "capture reports guest input suppression");
    expect(lambo::input_gate::guest_snapshot() == 0,
           "captured input publishes a neutral snapshot");

    lambo::input_gate::set_ui_capture(false);
    lambo::input_gate::publish_physical_snapshot(0xFFFFFFFFu);
    expect(lambo::input_gate::guest_input_suppressed(),
           "capture release starts a neutral release barrier");
    expect(lambo::input_gate::guest_snapshot() == 0,
           "first frame after capture is neutral");
    expect(lambo::input_gate::guest_input_suppressed(),
           "the neutral barrier covers all reads in that frame");
    lambo::input_gate::publish_physical_snapshot(0x00010000u);
    expect(lambo::input_gate::guest_snapshot() == 0,
           "held analog input also remains suppressed");
    lambo::input_gate::publish_physical_snapshot(0);
    expect(lambo::input_gate::guest_input_suppressed(),
           "first neutral sample is published under the barrier");
    lambo::input_gate::publish_physical_snapshot(0);
    expect(!lambo::input_gate::guest_input_suppressed(),
           "a later neutral sample releases the barrier");
    expect(lambo::input_gate::guest_snapshot() == 0,
           "barrier release sample remains neutral");
    lambo::input_gate::publish_physical_snapshot(physical);
    expect(lambo::input_gate::guest_snapshot() == physical,
           "physical state resumes on a later sample");

    lambo::input_gate::set_ui_capture(true);
    lambo::input_gate::set_ui_capture(false);
    lambo::input_gate::publish_physical_snapshot(0);
    lambo::input_gate::set_ui_capture(true);
    lambo::input_gate::publish_physical_snapshot(physical);
    expect(lambo::input_gate::guest_snapshot() == 0,
           "reopening during release barrier remains suppressed");
    lambo::input_gate::set_ui_capture(false);
    lambo::input_gate::publish_physical_snapshot(0);
    lambo::input_gate::publish_physical_snapshot(0);

    return failures == 0 ? 0 : 1;
}
