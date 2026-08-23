#include "lambo_ui_controls.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace {

using namespace lambo::controls;

std::vector<std::string_view> split(std::string_view value) {
    std::vector<std::string_view> result;
    while (true) {
        const std::size_t separator = value.find(':');
        result.push_back(value.substr(0, separator));
        if (separator == std::string_view::npos) return result;
        value.remove_prefix(separator + 1);
    }
}

template <typename Number>
std::optional<Number> number(std::string_view value) {
    Number result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
    return result;
}

const char* layout_name(ControllerLayout layout) {
    switch (layout) {
        case ControllerLayout::Xbox: return "Xbox";
        case ControllerLayout::PlayStation: return "PlayStation";
        case ControllerLayout::Nintendo: return "Nintendo";
        case ControllerLayout::Generic: return "Generic";
    }
    return "Generic";
}

std::string source_label(const DigitalSource& source) {
    if (const auto* button = std::get_if<ButtonSource>(&source)) {
        return "Button " + std::string(button_name(button->button));
    }
    const auto& half = std::get<AxisHalfSource>(source);
    return std::string(axis_name(half.axis)) +
        (half.direction == AxisDirection::Positive ? " +" : " -");
}

} // namespace

namespace lambo::ui {

std::string escape_rml(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c; break;
        }
    }
    return result;
}

namespace {

struct TargetMeta {
    Target target;
    const char* badge_class;
    const char* badge_text;
    const char* name;
};

constexpr std::array<TargetMeta, 6> buttons_targets{{
    {Target::A, "n64-badge-a", "A", "A Button"},
    {Target::B, "n64-badge-b", "B", "B Button"},
    {Target::Start, "n64-badge-start", "START", "Start"},
    {Target::Z, "n64-badge-z", "Z", "Z Trigger"},
    {Target::L, "n64-badge-shoulder", "L", "L Shoulder"},
    {Target::R, "n64-badge-shoulder", "R", "R Shoulder"},
}};

constexpr std::array<TargetMeta, 4> cbuttons_targets{{
    {Target::CUp, "n64-badge-c", "C-U", "C-Up"},
    {Target::CDown, "n64-badge-c", "C-D", "C-Down"},
    {Target::CLeft, "n64-badge-c", "C-L", "C-Left"},
    {Target::CRight, "n64-badge-c", "C-R", "C-Right"},
}};

constexpr std::array<TargetMeta, 4> dpad_targets{{
    {Target::DpadUp, "n64-badge-dpad", "D-U", "D-Pad Up"},
    {Target::DpadDown, "n64-badge-dpad", "D-D", "D-Pad Down"},
    {Target::DpadLeft, "n64-badge-dpad", "D-L", "D-Pad Left"},
    {Target::DpadRight, "n64-badge-dpad", "D-R", "D-Pad Right"},
}};

constexpr std::array<TargetMeta, 2> stick_targets{{
    {Target::StickX, "n64-badge-stick", "X", "N64 Stick X"},
    {Target::StickY, "n64-badge-stick", "Y", "N64 Stick Y"},
}};

// Target::Throttle is rendered by the dedicated DRIVING column below, not a mapper row.
static_assert(buttons_targets.size() + cbuttons_targets.size() + dpad_targets.size() + stick_targets.size() + 1 ==
              static_cast<std::size_t>(Target::Count));

void render_mapper_row(std::ostringstream& out, const TargetMeta& meta,
                       const UiSnapshot& snapshot, const char* disabled) {
    const Target target = meta.target;
    const std::size_t index = static_cast<std::size_t>(target);
    const std::string_view tname = target_name(target);

    out << "<div class=\"mapper-row\">"
        << "<div class=\"mapper-label-col\">"
        << "<span class=\"n64-badge " << meta.badge_class << "\">" << meta.badge_text << "</span>"
        << "<span class=\"mapper-target-name\">" << meta.name << "</span>"
        << "</div>";

    if (index < digital_target_count) {
        const auto& sources = snapshot.profile.digital[index];
        out << "<button" << disabled << " class=\"mapper-slot-btn\" onclick=\"control:add:" << tname << "\">";
        if (sources.empty()) {
            out << "<span class=\"mapper-slot-val slot-empty\">None (Click)</span>";
        } else {
            out << "<span class=\"mapper-slot-val\">" << escape_rml(source_label(sources[0])) << "</span>";
        }
        out << "</button>";
    } else {
        const auto& source = snapshot.profile.analog[index - digital_target_count];
        out << "<button" << disabled << " class=\"mapper-slot-btn\" onclick=\"control:add:" << tname << "\">"
            << "<span class=\"mapper-slot-val\">";
        if (source) out << "Axis " << axis_name(source->axis);
        else out << "<span class=\"slot-empty\">None (Click)</span>";
        out << "</span></button>";
    }
    out << "</div>";
}

} // namespace

std::optional<Command> control_action_from_name(std::string_view action) {
    if (action.starts_with("control:")) action.remove_prefix(8);
    const auto fields = split(action);
    if (fields.empty()) return std::nullopt;
    Command command{};
    if (fields[0] == "select" && fields.size() == 2) {
        const auto instance = number<std::int32_t>(fields[1]);
        if (!instance || *instance < 0) return std::nullopt;
        command.kind = CommandKind::Select; command.instance = *instance; return command;
    }
    if (fields[0] == "reset-profile" && fields.size() == 1) {
        command.kind = CommandKind::ResetProfile; return command;
    }
    if (fields[0] == "conflict-accept" && fields.size() == 1) {
        command.kind = CommandKind::ConflictAccept; return command;
    }
    if (fields[0] == "conflict-move" && fields.size() == 1) {
        command.kind = CommandKind::ConflictMove; return command;
    }
    if (fields[0] == "capture-cancel" && fields.size() == 1) {
        command.kind = CommandKind::CaptureCancel; return command;
    }
    if (fields.size() < 2) return std::nullopt;
    const auto target = target_from_name(fields[1]);
    if (!target) return std::nullopt;
    command.target = *target;
    if (fields[0] == "add" && fields.size() == 2) command.kind = CommandKind::Add;
    else if (fields[0] == "reset-target" && fields.size() == 2) command.kind = CommandKind::ResetTarget;
    else if (fields[0] == "invert" && fields.size() == 2 &&
             (*target == Target::StickX || *target == Target::StickY ||
              *target == Target::Throttle)) command.kind = CommandKind::Invert;
    else if (fields[0] == "mode" && fields.size() == 3 && *target == Target::Throttle) {
        const auto value = number<int>(fields[2]);
        if (!value || (*value != 0 && *value != 1)) return std::nullopt;
        command.kind = CommandKind::ThrottleMode; command.value = *value;
    } else if (fields[0] == "clear-source" && fields.size() == 2 &&
               *target == Target::Throttle) {
        command.kind = CommandKind::ClearThrottleSource;
    } else if (fields[0] == "remove" && fields.size() == 3) {
        if (static_cast<std::size_t>(*target) >= digital_target_count) return std::nullopt;
        const auto index = number<std::size_t>(fields[2]); if (!index) return std::nullopt;
        command.kind = CommandKind::Remove; command.binding_index = *index;
    } else if (fields[0] == "threshold" && fields.size() == 4) {
        if (static_cast<std::size_t>(*target) >= digital_target_count) return std::nullopt;
        const auto index = number<std::size_t>(fields[2]); const auto value = number<int>(fields[3]);
        if (!index || !value || *value < 0 || *value > 32767) return std::nullopt;
        command.kind = CommandKind::Threshold; command.binding_index = *index; command.value = *value;
    } else if (fields[0] == "deadzone" && fields.size() == 3 &&
               (*target == Target::StickX || *target == Target::StickY ||
                *target == Target::Throttle)) {
        const auto value = number<int>(fields[2]);
        const int maximum = *target == Target::Throttle ? 500 : 32767;
        if (!value || *value < 0 || *value > maximum) return std::nullopt;
        command.kind = CommandKind::Deadzone; command.value = *value;
    } else if (fields[0] == "saturation" && fields.size() == 3 &&
               *target == Target::Throttle) {
        const auto value = number<int>(fields[2]);
        if (!value || *value < 0 || *value > 1000) return std::nullopt;
        command.kind = CommandKind::Saturation; command.value = *value;
    } else return std::nullopt;
    return command;
}

bool apply_control_action(std::string_view action) {
    const auto command = control_action_from_name(action);
    if (!command) return false;
    enqueue_command(*command);
    return true;
}

ControlsView controls_view(const UiSnapshot& snapshot) {
    ControlsView view{};
    std::ostringstream controllers;
    for (const auto& controller : snapshot.controllers) {
        controllers << "<button class=\"controller-choice" << (controller.active ? " active" : "")
                    << "\" onclick=\"control:select:" << controller.instance << "\">"
                    << escape_rml(controller.name) << "</button>";
        if (controller.active) {
            view.selected_name = escape_rml(controller.name);
            view.selected_status = "Connected / active";
            view.selected_layout = layout_name(controller.layout);
            view.selected_guid = escape_rml(controller.guid);
        }
    }
    view.controller_list = controllers.str();
    if (!snapshot.selected_instance) {
        view.selected_name = "No controller";
        view.selected_status = "Keyboard remains available";
        view.selected_layout = "-";
        view.selected_guid = "-";
    }

    std::ostringstream bindings;
    const char* disabled = snapshot.read_only || !snapshot.selected_instance ? " disabled=\"disabled\"" : "";

    bindings << "<div class=\"mapper-grid columns\">";

    // Column 1: BUTTONS & TRIGGERS
    bindings << "<div class=\"column mapper-section\">"
             << "<span class=\"mapper-section-title\">BUTTONS &amp; TRIGGERS</span>";
    for (const auto& meta : buttons_targets) {
        render_mapper_row(bindings, meta, snapshot, disabled);
    }
    bindings << "</div>";

    // Column 2: C-BUTTONS
    bindings << "<div class=\"column mapper-section\">"
             << "<span class=\"mapper-section-title\">C-BUTTONS</span>";
    for (const auto& meta : cbuttons_targets) {
        render_mapper_row(bindings, meta, snapshot, disabled);
    }
    bindings << "</div>";

    // Column 3: DIGITAL PAD
    bindings << "<div class=\"column mapper-section\">"
             << "<span class=\"mapper-section-title\">DIGITAL PAD</span>";
    for (const auto& meta : dpad_targets) {
        render_mapper_row(bindings, meta, snapshot, disabled);
    }
    bindings << "</div>";

    // Column 4: ANALOG STICK
    bindings << "<div class=\"column mapper-section\">"
             << "<span class=\"mapper-section-title\">ANALOG STICK</span>";
    for (const auto& meta : stick_targets) {
        render_mapper_row(bindings, meta, snapshot, disabled);
    }
    bindings << "</div>";

    // Column 5: DRIVING (issue #128 analog throttle)
    const auto& throttle = snapshot.profile.throttle;
    const int deadzone = std::clamp(static_cast<int>(std::lround(throttle.deadzone * 1000.0f)), 0, 500);
    const int saturation = std::clamp(static_cast<int>(std::lround(throttle.saturation * 1000.0f)), deadzone, 1000);
    bindings << "<div class=\"column mapper-section driving-target\">"
             << "<span class=\"mapper-section-title\">DRIVING</span>"
             << "<div class=\"binding-target\"><div class=\"binding-heading\"><h2>Driving</h2>"
             << "<button" << disabled << " onclick=\"control:reset-target:throttle\">Reset</button></div>"
             << "<h3>Throttle</h3><div class=\"binding-row\"><span class=\"binding-chip\">Mode</span>"
             << "<button" << disabled << " class=\"" << (throttle.mode == ThrottleMode::Digital ? "active" : "")
             << "\" onclick=\"control:mode:throttle:0\">Digital</button>"
             << "<button" << disabled << " class=\"" << (throttle.mode == ThrottleMode::Analog ? "active" : "")
             << "\" onclick=\"control:mode:throttle:1\">Analog</button></div>"
             << "<div class=\"binding-row\"><span class=\"binding-chip\">";
    if (throttle.source) {
        bindings << "Axis " << axis_name(throttle.source->axis)
                 << (throttle.source->direction == AxisDirection::Positive ? " +" : " -");
    } else {
        bindings << "Unassigned continuous source";
    }
    bindings << "</span>";
    if (throttle.source) {
        bindings << "<button" << disabled << " onclick=\"control:invert:throttle\">Direction: "
                 << (throttle.source->direction == AxisDirection::Positive ? "Positive" : "Negative")
                 << "</button><button" << disabled
                 << " class=\"remove\" onclick=\"control:clear-source:throttle\">Unassign</button>";
    }
    bindings << "<button" << disabled << " class=\"add-binding\" onclick=\"control:add:throttle\">Choose axis</button></div>"
             << "<div class=\"binding-row\"><span class=\"binding-chip\">Deadzone " << deadzone / 10.0f
             << "%</span><button" << disabled << " onclick=\"control:deadzone:throttle:"
             << std::max(0, deadzone - 10) << "\">-</button><button" << disabled
             << " onclick=\"control:deadzone:throttle:" << std::min(500, deadzone + 10) << "\">+</button>"
             << "<span class=\"binding-chip\">Saturation " << saturation / 10.0f << "%</span><button"
             << disabled << " onclick=\"control:saturation:throttle:" << std::max(deadzone, saturation - 10)
             << "\">-</button><button" << disabled << " onclick=\"control:saturation:throttle:"
             << std::min(1000, saturation + 10) << "\">+</button></div>"
             << "<div id=\"controls-throttle-preview\" class=\"throttle-preview\"></div>"
             << "<p class=\"help\">A and keyboard X always provide full throttle. The larger of the analog and digital inputs wins; menus still use digital A.</p></div>";
    bindings << "</div>";

    bindings << "</div>";
    view.bindings = bindings.str();

    std::ostringstream raw;
    raw << "Buttons: ";
    bool any_button = false;
    for (std::size_t i = 0; i < static_cast<std::size_t>(LogicalButton::Count); ++i) {
        if (!snapshot.raw.buttons[i]) continue;
        if (any_button) raw << ", ";
        raw << button_name(static_cast<LogicalButton>(i));
        any_button = true;
    }
    if (!any_button) raw << "none";
    raw << " | Axes: ";
    for (std::size_t i = 0; i < static_cast<std::size_t>(LogicalAxis::Count); ++i) {
        if (i) raw << " | ";
        raw << axis_name(static_cast<LogicalAxis>(i)) << '=' << snapshot.raw.axes[i];
    }
    view.raw_preview = raw.str();
    view.evaluated_preview = "Buttons 0x";
    char mask[5]{}; std::snprintf(mask, sizeof(mask), "%04x", snapshot.evaluated.buttons);
    view.evaluated_preview += mask;
    view.evaluated_preview += " / Stick (" + std::to_string(snapshot.evaluated.stick_x) + ", " +
                              std::to_string(snapshot.evaluated.stick_y) + ")";
    const int raw_throttle = std::clamp(static_cast<int>(std::lround(
        throttle_source_magnitude(snapshot.profile.throttle, snapshot.raw) * 100.0f)), 0, 100);
    const int effective_throttle = std::clamp(static_cast<int>(std::lround(
        snapshot.evaluated.throttle * 100.0f)), 0, 100);
    view.throttle_preview = "<span>Raw " + std::to_string(raw_throttle) +
        "%</span><div class=\"throttle-meter\"><div class=\"throttle-meter-fill\" style=\"width: " +
        std::to_string(raw_throttle) + "%\"></div></div><span>Effective " +
        std::to_string(effective_throttle) +
        "%</span><div class=\"throttle-meter\"><div class=\"throttle-meter-fill effective\" style=\"width: " +
        std::to_string(effective_throttle) + "%\"></div></div>";
    view.persistence_status = escape_rml(snapshot.status + " - " + snapshot.config_path.string());
    for (const auto& warning : snapshot.warnings) view.warnings += "<p>" + escape_rml(warning) + "</p>";
    view.capture_visible = snapshot.capture_phase != CapturePhase::Idle &&
                           snapshot.capture_phase != CapturePhase::Conflict;
    view.conflict_visible = snapshot.capture_phase == CapturePhase::Conflict;
    if (snapshot.capture_target) {
        view.capture_message = "Binding " + std::string(target_label(*snapshot.capture_target));
        if (snapshot.capture_phase == CapturePhase::WaitingForNeutral) view.capture_message += ": release all controls";
        else if (snapshot.capture_phase == CapturePhase::Listening) view.capture_message += ": press a button or move an axis";
        else if (snapshot.capture_phase == CapturePhase::WaitingForRelease) view.capture_message += ": release the control";
    }
    return view;
}

} // namespace lambo::ui
