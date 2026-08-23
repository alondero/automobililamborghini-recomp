#include <iostream>
#include <string>

#include "ui/lambo_ui_controls.h"
#include "ultramodern/config.hpp"

namespace ultramodern::renderer {
void set_graphics_config(const GraphicsConfig&) {}
}

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    using namespace lambo::controls;
    using namespace lambo::ui;
    expect(control_action_from_name("control:select:42")->kind == CommandKind::Select,
           "select action parses");
    expect(control_action_from_name("add:c_up")->target == Target::CUp, "add target parses");
    expect(control_action_from_name("remove:a:2")->binding_index == 2, "remove index parses");
    expect(control_action_from_name("threshold:z:0:9000")->value == 9000, "threshold parses");
    expect(control_action_from_name("deadzone:stick_x:7000")->value == 7000, "deadzone parses");
    expect(control_action_from_name("invert:stick_y")->kind == CommandKind::Invert, "invert parses");
    expect(control_action_from_name("mode:throttle:1")->kind == CommandKind::ThrottleMode,
           "analog throttle mode parses");
    expect(control_action_from_name("deadzone:throttle:250")->value == 250,
           "bounded throttle deadzone parses");
    expect(control_action_from_name("saturation:throttle:900")->kind == CommandKind::Saturation,
           "throttle saturation parses");
    expect(control_action_from_name("clear-source:throttle")->kind == CommandKind::ClearThrottleSource,
           "explicit unassigned throttle source parses");
    expect(control_action_from_name("conflict-move")->kind == CommandKind::ConflictMove,
           "move-binding conflict action parses");
    expect(!control_action_from_name("deadzone:throttle:501"),
           "throttle deadzone above 50 percent is rejected");
    expect(!control_action_from_name("select:not-a-number"), "malformed number is rejected");
    expect(!control_action_from_name("invert:a"), "digital invert is rejected");
    expect(!control_action_from_name("remove:a:2:extra"), "extra fields are rejected");
    expect(escape_rml("<bad & \"name\">") == "&lt;bad &amp; &quot;name&quot;&gt;",
           "dynamic RML is escaped");

    UiSnapshot snapshot{};
    snapshot.controllers.push_back({42, "030000005e0400008e02000014010000",
                                    "Pad <One>", ControllerLayout::Xbox, true});
    snapshot.selected_instance = 42;
    snapshot.selected_guid = snapshot.controllers[0].guid;
    snapshot.status = "Saved";
    snapshot.config_path = "controls.json";
    const ControlsView view = controls_view(snapshot);
    expect(view.controller_list.find("Pad &lt;One&gt;") != std::string::npos,
           "controller name is escaped in generated rows");
    expect(view.bindings.find("control:add:a") != std::string::npos,
           "generated bindings reference typed actions");
    expect(view.bindings.find("N64 Stick X") != std::string::npos,
           "all targets include analog sticks");
    expect(view.bindings.find("Driving") != std::string::npos &&
           view.bindings.find("keyboard X always provide full throttle") != std::string::npos,
           "driving section explains analog mode and digital fallback");
    expect(view.bindings.find("control:reset-target:throttle") != std::string::npos,
           "driving section exposes the throttle reset action");
    expect(view.throttle_preview.find("throttle-meter") != std::string::npos,
           "throttle preview exposes live raw and effective meters");
    return failures == 0 ? 0 : 1;
}
