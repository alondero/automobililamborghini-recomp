#ifndef LAMBO_UI_CONTROLS_H
#define LAMBO_UI_CONTROLS_H

#include <optional>
#include <string>
#include <string_view>

#include "controls/lambo_controls.h"

namespace lambo::ui {

struct ControlsView {
    std::string controller_list;
    std::string selected_name;
    std::string selected_status;
    std::string selected_layout;
    std::string selected_guid;
    std::string bindings;
    std::string raw_preview;
    std::string evaluated_preview;
    std::string throttle_preview;
    std::string persistence_status;
    std::string warnings;
    std::string capture_message;
    bool capture_visible{};
    bool conflict_visible{};
};

std::optional<lambo::controls::Command> control_action_from_name(std::string_view action);
bool apply_control_action(std::string_view action);
ControlsView controls_view(const lambo::controls::UiSnapshot& snapshot);
std::string escape_rml(std::string_view text);

} // namespace lambo::ui

#endif
