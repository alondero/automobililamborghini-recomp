#include "lambo_controls.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

#include "json/json.hpp"

#include "lambo_config.h"
#include "lambo_file.h"

namespace {

using namespace lambo::controls;
using json = nlohmann::json;

std::mutex command_mutex;
std::deque<Command> command_queue;
std::mutex snapshot_mutex;
UiSnapshot published_snapshot;

constexpr std::array<std::string_view, 18> target_names{
    "a", "b", "z", "start", "l", "r", "dpad_up", "dpad_down",
    "dpad_left", "dpad_right", "c_up", "c_down", "c_left", "c_right",
    "stick_x", "stick_y", "throttle", "brake",
};
constexpr std::array<std::string_view, 18> target_labels{
    "A", "B", "Z", "Start", "L", "R", "D-pad Up", "D-pad Down",
    "D-pad Left", "D-pad Right", "C Up", "C Down", "C Left", "C Right",
    "N64 Stick X", "N64 Stick Y", "Throttle", "Brake",
};
constexpr std::array<std::string_view, 15> button_names{
    "a", "b", "x", "y", "back", "guide", "start", "left_stick", "right_stick",
    "left_shoulder", "right_shoulder", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
};
constexpr std::array<std::string_view, 6> axis_names{
    "left_x", "left_y", "right_x", "right_y", "trigger_left", "trigger_right",
};

constexpr std::array<std::uint16_t, digital_target_count> n64_bits{
    0x8000, 0x4000, 0x2000, 0x1000, 0x0020, 0x0010, 0x0800,
    0x0400, 0x0200, 0x0100, 0x0008, 0x0004, 0x0002, 0x0001,
};

template <typename Enum, std::size_t Size>
std::optional<Enum> enum_from_name(std::string_view name,
                                   const std::array<std::string_view, Size>& names) {
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end()) return std::nullopt;
    return static_cast<Enum>(found - names.begin());
}

std::size_t digital_index(Target target) { return static_cast<std::size_t>(target); }

bool active(const DigitalSource& source, const RawState& state) {
    if (const auto* button = std::get_if<ButtonSource>(&source)) {
        return state.buttons[static_cast<std::size_t>(button->button)];
    }
    const auto& half = std::get<AxisHalfSource>(source);
    const std::int16_t value = state.axes[static_cast<std::size_t>(half.axis)];
    return half.direction == AxisDirection::Positive
        ? value > half.threshold
        : value < -static_cast<std::int32_t>(half.threshold);
}

std::int8_t evaluate_axis(const AnalogSource& source, const RawState& state) {
    const std::int16_t raw = state.axes[static_cast<std::size_t>(source.axis)];
    if (raw > -static_cast<std::int32_t>(source.deadzone) && raw < source.deadzone) return 0;
    float value = static_cast<float>(raw) / 32767.0f;
    value = std::clamp(value, -1.0f, 1.0f);
    if (source.invert) value = -value;
    return static_cast<std::int8_t>(value * 80.0f);
}

json source_json(const DigitalSource& source) {
    if (const auto* button = std::get_if<ButtonSource>(&source)) {
        return {{"type", "button"}, {"button", button_name(button->button)}};
    }
    const auto& axis = std::get<AxisHalfSource>(source);
    return {{"type", "axis"}, {"axis", axis_name(axis.axis)},
            {"direction", axis.direction == AxisDirection::Positive ? "positive" : "negative"},
            {"threshold", axis.threshold}};
}

bool same_source_identity(const DigitalSource& left, const DigitalSource& right) {
    if (left.index() != right.index()) return false;
    if (const auto* button = std::get_if<ButtonSource>(&left))
        return button->button == std::get<ButtonSource>(right).button;
    const auto& left_axis = std::get<AxisHalfSource>(left);
    const auto& right_axis = std::get<AxisHalfSource>(right);
    return left_axis.axis == right_axis.axis && left_axis.direction == right_axis.direction;
}

std::optional<DigitalSource> parse_source(const json& value) {
    if (!value.is_object()) return std::nullopt;
    const auto type = value.find("type");
    if (type == value.end() || !type->is_string()) return std::nullopt;
    if (*type == "button") {
        const auto field = value.find("button");
        if (field == value.end() || !field->is_string()) return std::nullopt;
        const auto button = button_from_name(field->get<std::string>());
        if (!button) return std::nullopt;
        return ButtonSource{*button};
    }
    if (*type == "axis") {
        const auto axis_field = value.find("axis");
        const auto direction_field = value.find("direction");
        const auto threshold_field = value.find("threshold");
        if (axis_field == value.end() || !axis_field->is_string() ||
            direction_field == value.end() || !direction_field->is_string() ||
            threshold_field == value.end() || !threshold_field->is_number_integer()) return std::nullopt;
        const auto axis = axis_from_name(axis_field->get<std::string>());
        const std::string direction = direction_field->get<std::string>();
        std::int64_t threshold{};
        try { threshold = threshold_field->get<std::int64_t>(); }
        catch (const json::exception&) { return std::nullopt; }
        if (!axis || (direction != "positive" && direction != "negative") ||
        threshold < 0 || threshold > std::numeric_limits<std::int16_t>::max()) return std::nullopt;
        return AxisHalfSource{*axis,
            direction == "positive" ? AxisDirection::Positive : AxisDirection::Negative,
            static_cast<std::int16_t>(threshold)};
    }
    return std::nullopt;
}

std::optional<AnalogSource> parse_analog(const json& value) {
    if (!value.is_object()) return std::nullopt;
    const auto axis_field = value.find("axis");
    const auto invert_field = value.find("invert");
    const auto deadzone_field = value.find("deadzone");
    if (axis_field == value.end() || !axis_field->is_string() ||
        invert_field == value.end() || !invert_field->is_boolean() ||
        deadzone_field == value.end() || !deadzone_field->is_number_integer()) return std::nullopt;
    const auto axis = axis_from_name(axis_field->get<std::string>());
    std::int64_t deadzone{};
    try { deadzone = deadzone_field->get<std::int64_t>(); }
    catch (const json::exception&) { return std::nullopt; }
    if (!axis || deadzone < 0 || deadzone > std::numeric_limits<std::int16_t>::max()) return std::nullopt;
    return AnalogSource{*axis, invert_field->get<bool>(), static_cast<std::int16_t>(deadzone)};
}

std::optional<ThrottleConfig> parse_throttle(const json& value) {
    if (!value.is_object()) return std::nullopt;
    const auto mode_field = value.find("mode");
    const auto source_field = value.find("source");
    const auto deadzone_field = value.find("deadzone");
    const auto saturation_field = value.find("saturation");
    if (mode_field == value.end() || !mode_field->is_string() ||
        source_field == value.end() ||
        deadzone_field == value.end() || !deadzone_field->is_number() ||
        saturation_field == value.end() || !saturation_field->is_number()) return std::nullopt;

    ThrottleConfig result{};
    const std::string mode = mode_field->get<std::string>();
    if (mode == "digital") result.mode = ThrottleMode::Digital;
    else if (mode == "analog") result.mode = ThrottleMode::Analog;
    else return std::nullopt;
    try {
        result.deadzone = deadzone_field->get<float>();
        result.saturation = saturation_field->get<float>();
    } catch (const json::exception&) {
        return std::nullopt;
    }
    if (!std::isfinite(result.deadzone) || !std::isfinite(result.saturation) ||
        result.deadzone < 0.0f || result.deadzone > 0.5f ||
        result.saturation < result.deadzone || result.saturation > 1.0f) return std::nullopt;

    if (source_field->is_null()) return result;
    if (!source_field->is_object()) return std::nullopt;
    const auto axis_field = source_field->find("axis");
    const auto direction_field = source_field->find("direction");
    if (axis_field == source_field->end() || !axis_field->is_string() ||
        direction_field == source_field->end() || !direction_field->is_string()) return std::nullopt;
    const auto axis = axis_from_name(axis_field->get<std::string>());
    const std::string direction = direction_field->get<std::string>();
    if (!axis || (direction != "positive" && direction != "negative")) return std::nullopt;
    result.source = ThrottleSource{
        *axis, direction == "positive" ? AxisDirection::Positive : AxisDirection::Negative};
    return result;
}

std::optional<BrakeConfig> parse_brake(const json& value) {
    if (!value.is_object()) return std::nullopt;
    const auto mode_field = value.find("mode");
    const auto source_field = value.find("source");
    const auto deadzone_field = value.find("deadzone");
    const auto saturation_field = value.find("saturation");
    if (mode_field == value.end() || !mode_field->is_string() ||
        source_field == value.end() ||
        deadzone_field == value.end() || !deadzone_field->is_number() ||
        saturation_field == value.end() || !saturation_field->is_number()) return std::nullopt;

    BrakeConfig result{};
    const std::string mode = mode_field->get<std::string>();
    if (mode == "digital") result.mode = BrakeMode::Digital;
    else if (mode == "analog") result.mode = BrakeMode::Analog;
    else return std::nullopt;
    try {
        result.deadzone = deadzone_field->get<float>();
        result.saturation = saturation_field->get<float>();
    } catch (const json::exception&) {
        return std::nullopt;
    }
    if (!std::isfinite(result.deadzone) || !std::isfinite(result.saturation) ||
        result.deadzone < 0.0f || result.deadzone > 0.5f ||
        result.saturation < result.deadzone || result.saturation > 1.0f) return std::nullopt;

    if (source_field->is_null()) return result;
    if (!source_field->is_object()) return std::nullopt;
    const auto axis_field = source_field->find("axis");
    const auto direction_field = source_field->find("direction");
    if (axis_field == source_field->end() || !axis_field->is_string() ||
        direction_field == source_field->end() || !direction_field->is_string()) return std::nullopt;
    const auto axis = axis_from_name(axis_field->get<std::string>());
    const std::string direction = direction_field->get<std::string>();
    if (!axis || (direction != "positive" && direction != "negative")) return std::nullopt;
    result.source = BrakeSource{
        *axis, direction == "positive" ? AxisDirection::Positive : AxisDirection::Negative};
    return result;
}

void merge_profile(json& destination, const Profile& profile) {
    if (!destination.is_object()) destination = json::object();
    json& digital = destination["digital"];
    if (!digital.is_object()) digital = json::object();
    for (std::size_t i = 0; i < digital_target_count; ++i) {
        const json previous = digital.contains(std::string(target_names[i])) &&
                              digital[std::string(target_names[i])].is_array()
            ? digital[std::string(target_names[i])] : json::array();
        std::vector<bool> used(previous.size());
        json values = json::array();
        for (const auto& source : profile.digital[i]) {
            json serialized = source_json(source);
            for (std::size_t old = 0; old < previous.size(); ++old) {
                const auto parsed = parse_source(previous[old]);
                if (!used[old] && parsed && same_source_identity(*parsed, source)) {
                    json preserved = previous[old];
                    for (const auto& [key, value] : serialized.items()) preserved[key] = value;
                    serialized = std::move(preserved);
                    used[old] = true;
                    break;
                }
            }
            values.push_back(std::move(serialized));
        }
        digital[std::string(target_names[i])] = std::move(values);
    }
    json& analog = destination["analog"];
    if (!analog.is_object()) analog = json::object();
    for (std::size_t i = 0; i < analog_target_count; ++i) {
        const auto& source = profile.analog[i];
        const std::string name(target_names[digital_target_count + i]);
        if (!source) {
            analog[name] = nullptr;
            continue;
        }
        json binding = analog.contains(name) && analog[name].is_object() ? analog[name] : json::object();
        binding["axis"] = axis_name(source->axis);
        binding["invert"] = source->invert;
        binding["deadzone"] = source->deadzone;
        analog[name] = std::move(binding);
    }
    json& throttle = destination["throttle"];
    if (!throttle.is_object()) throttle = json::object();
    throttle["mode"] = profile.throttle.mode == ThrottleMode::Analog ? "analog" : "digital";
    throttle["deadzone"] = profile.throttle.deadzone;
    throttle["saturation"] = profile.throttle.saturation;
    if (profile.throttle.source) {
        json source = throttle.contains("source") && throttle["source"].is_object()
            ? throttle["source"] : json::object();
        source["axis"] = axis_name(profile.throttle.source->axis);
        source["direction"] = profile.throttle.source->direction == AxisDirection::Positive
            ? "positive" : "negative";
        throttle["source"] = std::move(source);
    } else {
        throttle["source"] = nullptr;
    }
    json& brake = destination["brake"];
    if (!brake.is_object()) brake = json::object();
    brake["mode"] = profile.brake.mode == BrakeMode::Analog ? "analog" : "digital";
    brake["deadzone"] = profile.brake.deadzone;
    brake["saturation"] = profile.brake.saturation;
    if (profile.brake.source) {
        json source = brake.contains("source") && brake["source"].is_object()
            ? brake["source"] : json::object();
        source["axis"] = axis_name(profile.brake.source->axis);
        source["direction"] = profile.brake.source->direction == AxisDirection::Positive
            ? "positive" : "negative";
        brake["source"] = std::move(source);
    } else {
        brake["source"] = nullptr;
    }
}

} // namespace

namespace lambo::controls {

Profile default_profile() {
    Profile profile{};
    const auto button = [&](Target target, LogicalButton source) {
        profile.digital[digital_index(target)].push_back(ButtonSource{source});
    };
    const auto half = [&](Target target, LogicalAxis axis, AxisDirection direction, std::int16_t threshold) {
        profile.digital[digital_index(target)].push_back(AxisHalfSource{axis, direction, threshold});
    };
    button(Target::A, LogicalButton::A);
    button(Target::B, LogicalButton::B);
    half(Target::Z, LogicalAxis::TriggerLeft, AxisDirection::Positive, 8000);
    button(Target::Start, LogicalButton::Start);
    button(Target::L, LogicalButton::LeftShoulder);
    button(Target::R, LogicalButton::RightShoulder);
    half(Target::R, LogicalAxis::TriggerRight, AxisDirection::Positive, 8000);
    button(Target::DpadUp, LogicalButton::DpadUp);
    button(Target::DpadDown, LogicalButton::DpadDown);
    button(Target::DpadLeft, LogicalButton::DpadLeft);
    button(Target::DpadRight, LogicalButton::DpadRight);
    button(Target::CUp, LogicalButton::Y);
    half(Target::CUp, LogicalAxis::RightY, AxisDirection::Negative, 12000);
    button(Target::CDown, LogicalButton::RightStick);
    half(Target::CDown, LogicalAxis::RightY, AxisDirection::Positive, 12000);
    button(Target::CLeft, LogicalButton::X);
    half(Target::CLeft, LogicalAxis::RightX, AxisDirection::Negative, 12000);
    half(Target::CRight, LogicalAxis::RightX, AxisDirection::Positive, 12000);
    profile.analog[0] = AnalogSource{LogicalAxis::LeftX, false, 8000};
    profile.analog[1] = AnalogSource{LogicalAxis::LeftY, true, 8000};
    profile.throttle = {ThrottleMode::Digital, std::nullopt, 0.05f, 1.0f};
    profile.brake = {BrakeMode::Digital, std::nullopt, 0.05f, 1.0f};
    return profile;
}

float throttle_source_magnitude(const ThrottleConfig& throttle, const RawState& state) {
    if (!throttle.source) return 0.0f;
    const std::int16_t raw = state.axes[static_cast<std::size_t>(throttle.source->axis)];
    return throttle.source->direction == AxisDirection::Positive
        ? std::max(0.0f, static_cast<float>(raw) / 32767.0f)
        : std::max(0.0f, -static_cast<float>(raw) / 32768.0f);
}

float evaluate_throttle_source(const ThrottleConfig& throttle, const RawState& state) {
    const float magnitude = throttle_source_magnitude(throttle, state);
    // Neutral remains neutral even for the valid zero-width shape dz=saturation=0.
    // Otherwise saturation owns its endpoint; equal positive dz/saturation is a step.
    if (magnitude <= 0.0f) return 0.0f;
    if (magnitude >= throttle.saturation) return 1.0f;
    if (magnitude <= throttle.deadzone) return 0.0f;
    const float span = throttle.saturation - throttle.deadzone;
    return span > 0.0f ? (magnitude - throttle.deadzone) / span : 1.0f;
}

float brake_source_magnitude(const BrakeConfig& brake, const RawState& state) {
    if (!brake.source) return 0.0f;
    const std::int16_t raw = state.axes[static_cast<std::size_t>(brake.source->axis)];
    return brake.source->direction == AxisDirection::Positive
        ? std::max(0.0f, static_cast<float>(raw) / 32767.0f)
        : std::max(0.0f, -static_cast<float>(raw) / 32768.0f);
}

float evaluate_brake_source(const BrakeConfig& brake, const RawState& state) {
    const float magnitude = brake_source_magnitude(brake, state);
    if (magnitude <= 0.0f) return 0.0f;
    if (magnitude >= brake.saturation) return 1.0f;
    if (magnitude <= brake.deadzone) return 0.0f;
    const float span = brake.saturation - brake.deadzone;
    return span > 0.0f ? (magnitude - brake.deadzone) / span : 1.0f;
}

EvaluatedState evaluate(const Profile& profile, const RawState& state) {
    EvaluatedState result{};
    for (std::size_t target = 0; target < digital_target_count; ++target) {
        if (std::any_of(profile.digital[target].begin(), profile.digital[target].end(),
                        [&](const DigitalSource& source) { return active(source, state); })) {
            result.buttons |= n64_bits[target];
        }
    }
    result.stick_x = profile.analog[0] ? evaluate_axis(*profile.analog[0], state) : 0;
    result.stick_y = profile.analog[1] ? evaluate_axis(*profile.analog[1], state) : 0;
    result.throttle_mode = profile.throttle.mode;
    if (profile.throttle.mode == ThrottleMode::Analog) {
        result.throttle = evaluate_throttle_source(profile.throttle, state);
        if ((result.buttons & 0xA000u) != 0) result.throttle = 1.0f;
    } else {
        result.throttle = (result.buttons & 0xA000u) != 0 ? 1.0f : 0.0f;
    }
    result.brake_mode = profile.brake.mode;
    if (profile.brake.mode == BrakeMode::Analog) {
        result.brake = evaluate_brake_source(profile.brake, state);
        if ((result.buttons & 0x4000u) != 0) result.brake = 1.0f;
    } else {
        result.brake = (result.buttons & 0x4000u) != 0 ? 1.0f : 0.0f;
    }
    return result;
}

std::uint32_t pack(const EvaluatedState& state) {
    return state.buttons | (std::uint32_t(std::uint8_t(state.stick_x)) << 16) |
           (std::uint32_t(std::uint8_t(state.stick_y)) << 24);
}

std::string_view target_name(Target target) { return target_names.at(static_cast<std::size_t>(target)); }
std::string_view target_label(Target target) { return target_labels.at(static_cast<std::size_t>(target)); }
std::string_view button_name(LogicalButton button) { return button_names.at(static_cast<std::size_t>(button)); }
std::string_view axis_name(LogicalAxis axis) { return axis_names.at(static_cast<std::size_t>(axis)); }
std::optional<Target> target_from_name(std::string_view name) { return enum_from_name<Target>(name, target_names); }
std::optional<LogicalButton> button_from_name(std::string_view name) { return enum_from_name<LogicalButton>(name, button_names); }
std::optional<LogicalAxis> axis_from_name(std::string_view name) { return enum_from_name<LogicalAxis>(name, axis_names); }

std::optional<std::string> canonicalize_guid(std::string_view guid) {
    if (guid.size() != 32) return std::nullopt;
    std::string result;
    result.reserve(guid.size());
    for (const unsigned char c : guid) {
        if (!std::isxdigit(c)) return std::nullopt;
        result.push_back(static_cast<char>(std::tolower(c)));
    }
    return result;
}

std::filesystem::path controls_config_path() {
    if (const char* override_path = std::getenv("LAMBO_CONTROLS_CONFIG")) return override_path;
    return lambo::config::app_config_dir() / "controls.json";
}

LoadResult load_config(const std::filesystem::path& path) {
    LoadResult result{};
    if (!std::filesystem::exists(path)) return result;
    std::ifstream input(path, std::ios::binary);
    std::ostringstream bytes;
    bytes << input.rdbuf();
    const std::string original = bytes.str();
    json root;
    try { root = json::parse(original); }
    catch (const json::exception&) {
        result.status = LoadStatus::Malformed;
        result.config.read_only = true;
        return result;
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer()) {
        result.status = LoadStatus::InvalidVersion;
        result.config.read_only = true;
        return result;
    }
    std::int64_t version_number{};
    try { version_number = version->get<std::int64_t>(); }
    catch (const json::exception&) {
        result.status = LoadStatus::InvalidVersion;
        result.config.read_only = true;
        return result;
    }
    if (version_number != 1) {
        result.status = version_number > 1 ? LoadStatus::FutureVersion : LoadStatus::InvalidVersion;
        result.config.read_only = true;
        return result;
    }
    result.status = LoadStatus::Loaded;
    result.config.preserved_document = original;
    if (const auto preferred = root.find("preferred_controller_guid");
        preferred != root.end() && preferred->is_string()) {
        if (auto guid = canonicalize_guid(preferred->get<std::string>())) result.config.preferred_controller_guid = *guid;
        else result.warnings.emplace_back("ignored invalid preferred controller GUID");
    }
    const auto profiles = root.find("profiles");
    if (profiles == root.end() || !profiles->is_object()) return result;
    for (const auto& [raw_guid, value] : profiles->items()) {
        const auto guid = canonicalize_guid(raw_guid);
        if (!guid || !value.is_object()) {
            result.warnings.emplace_back("ignored invalid controller profile " + raw_guid);
            continue;
        }
        Profile profile = default_profile();
        if (const auto digital = value.find("digital"); digital != value.end() && digital->is_object()) {
            for (std::size_t target = 0; target < digital_target_count; ++target) {
                const auto bindings = digital->find(std::string(target_names[target]));
                if (bindings == digital->end()) continue;
                if (!bindings->is_array()) {
                    result.warnings.emplace_back("invalid digital target " + std::string(target_names[target]));
                    continue;
                }
                DigitalBindings parsed;
                for (const auto& binding : *bindings) {
                    const auto source = parse_source(binding);
                    if (!source) {
                        result.warnings.emplace_back("invalid binding for " + std::string(target_names[target]));
                    } else if (std::find(parsed.begin(), parsed.end(), *source) == parsed.end()) {
                        parsed.push_back(*source);
                    } else {
                        result.warnings.emplace_back("duplicate binding for " + std::string(target_names[target]));
                    }
                }
                profile.digital[target] = std::move(parsed);
            }
        }
        if (const auto analog = value.find("analog"); analog != value.end() && analog->is_object()) {
            for (std::size_t target = 0; target < analog_target_count; ++target) {
                const auto binding = analog->find(std::string(target_names[digital_target_count + target]));
                if (binding == analog->end()) continue;
                if (binding->is_null()) profile.analog[target].reset();
                else if (const auto parsed = parse_analog(*binding)) profile.analog[target] = *parsed;
                else result.warnings.emplace_back("invalid analog target " +
                                                  std::string(target_names[digital_target_count + target]));
            }
        }
        if (const auto throttle = value.find("throttle"); throttle != value.end()) {
            if (const auto parsed = parse_throttle(*throttle)) profile.throttle = *parsed;
            else result.warnings.emplace_back("invalid throttle configuration");
        }
        if (const auto brake = value.find("brake"); brake != value.end()) {
            if (const auto parsed = parse_brake(*brake)) profile.brake = *parsed;
            else result.warnings.emplace_back("invalid brake configuration");
        }
        for (std::size_t target = 0; target < digital_target_count; ++target) {
            for (const auto& source : profile.digital[target]) {
                if (has_cross_target_duplicate(profile, static_cast<Target>(target), source)) {
                    result.warnings.emplace_back("binding for " + std::string(target_names[target]) +
                                                 " is also used by another target");
                }
            }
        }
        if (profile.analog[0] && profile.analog[1] &&
            profile.analog[0]->axis == profile.analog[1]->axis) {
            result.warnings.emplace_back("analog axis is shared by stick_x and stick_y");
        }
        if (profile.throttle.source && std::any_of(profile.analog.begin(), profile.analog.end(),
            [&](const std::optional<AnalogSource>& analog) {
                return analog && profile.throttle.source->axis == analog->axis;
            })) {
            result.warnings.emplace_back("throttle axis is also used by an N64 stick target");
        }
        if (profile.brake.source && std::any_of(profile.analog.begin(), profile.analog.end(),
            [&](const std::optional<AnalogSource>& analog) {
                return analog && profile.brake.source->axis == analog->axis;
            })) {
            result.warnings.emplace_back("brake axis is also used by an N64 stick target");
        }
        result.config.profiles[*guid] = std::move(profile);
    }
    return result;
}

LoadResult load_config() { return load_config(controls_config_path()); }

SaveResult save_config(ControlsConfig& config, const std::filesystem::path& path) {
    SaveResult result{false, path, {}};
    if (config.read_only) {
        result.error = "controls file is read-only because its format could not be loaded";
        return result;
    }
    json root = json::object();
    if (!config.preserved_document.empty()) {
        try { root = json::parse(config.preserved_document); }
        catch (const json::exception&) { root = json::object(); }
    }
    root["version"] = 1;
    root["preferred_controller_guid"] = config.preferred_controller_guid;
    json& profiles = root["profiles"];
    if (!profiles.is_object()) profiles = json::object();
    for (const auto& [guid, profile] : config.profiles) merge_profile(profiles[guid], profile);

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) { result.error = ec.message(); return result; }
    }
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { result.error = "could not open temporary file"; return result; }
        output << root.dump(2) << '\n';
        output.flush();
        if (!output) { result.error = "could not flush temporary file"; output.close(); std::filesystem::remove(temporary, ec); return result; }
    }
    if (!lambo::file::atomic_replace(temporary, path, result.error)) {
        std::filesystem::remove(temporary, ec);
        return result;
    }
    result.saved = true;
    config.preserved_document = root.dump(2) + '\n';
    return result;
}

SaveResult save_config(ControlsConfig& config) { return save_config(config, controls_config_path()); }

Profile& profile_for_guid(ControlsConfig& config, std::string_view canonical_guid) {
    auto [entry, inserted] = config.profiles.try_emplace(std::string(canonical_guid));
    if (inserted) entry->second = default_profile();
    return entry->second;
}

const Profile& profile_for_guid(const ControlsConfig& config, std::string_view canonical_guid) {
    const auto found = config.profiles.find(std::string(canonical_guid));
    if (found != config.profiles.end()) return found->second;
    static const Profile defaults = default_profile();
    return defaults;
}

bool has_cross_target_duplicate(const Profile& profile, Target target,
                                const DigitalSource& source) {
    for (std::size_t index = 0; index < digital_target_count; ++index) {
        if (index == digital_index(target)) continue;
        if (std::any_of(profile.digital[index].begin(), profile.digital[index].end(),
            [&](const DigitalSource& binding) { return same_source_identity(binding, source); })) {
            return true;
        }
    }
    return false;
}

void Capture::begin(Target target, std::int32_t controller_instance) {
    reset();
    target_ = target;
    controller_instance_ = controller_instance;
    phase_ = CapturePhase::WaitingForNeutral;
}

CaptureResult Capture::cancel() {
    if (!active()) return CaptureResult::None;
    reset();
    return CaptureResult::Cancelled;
}

CaptureResult Capture::button_event(std::int32_t controller_instance,
                                    LogicalButton button, bool pressed) {
    if (!active()) return CaptureResult::None;
    if (controller_instance != controller_instance_) return CaptureResult::None;
    if (pressed && button == LogicalButton::Back) return cancel();
    if (phase_ != CapturePhase::Listening || !pressed ||
        !target_.has_value() || static_cast<std::size_t>(*target_) >= digital_target_count) {
        return CaptureResult::None;
    }
    candidate_ = DigitalSource{ButtonSource{button}};
    neutral_samples_ = 0;
    phase_ = CapturePhase::WaitingForRelease;
    return CaptureResult::None;
}

CaptureResult Capture::axis_event(std::int32_t controller_instance,
                                  LogicalAxis axis, std::int16_t value) {
    if (!active()) return CaptureResult::None;
    if (controller_instance != controller_instance_ || phase_ != CapturePhase::Listening ||
        std::abs(static_cast<std::int32_t>(value)) < activation_threshold || !target_) {
        return CaptureResult::None;
    }
    if (static_cast<std::size_t>(*target_) < digital_target_count) {
        const std::int16_t threshold = axis == LogicalAxis::TriggerLeft || axis == LogicalAxis::TriggerRight
            ? 8000 : 12000;
        candidate_ = DigitalSource{AxisHalfSource{
            axis, value > 0 ? AxisDirection::Positive : AxisDirection::Negative, threshold}};
    } else if (*target_ == Target::Throttle) {
        candidate_ = ThrottleSource{
            axis, value > 0 ? AxisDirection::Positive : AxisDirection::Negative};
    } else if (*target_ == Target::Brake) {
        candidate_ = BrakeSource{
            axis, value > 0 ? AxisDirection::Positive : AxisDirection::Negative};
    } else {
        candidate_ = AnalogSource{axis, *target_ == Target::StickY, 8000};
    }
    neutral_samples_ = 0;
    phase_ = CapturePhase::WaitingForRelease;
    return CaptureResult::None;
}

bool Capture::all_neutral(const RawState& state) const {
    return std::none_of(state.buttons.begin(), state.buttons.end(), [](bool pressed) { return pressed; }) &&
           std::all_of(state.axes.begin(), state.axes.end(), [](std::int16_t value) {
               return std::abs(static_cast<std::int32_t>(value)) <= neutral_threshold;
           });
}

CaptureResult Capture::sample(const RawState& state, Profile& profile) {
    if (phase_ != CapturePhase::WaitingForNeutral && phase_ != CapturePhase::WaitingForRelease) {
        return CaptureResult::None;
    }
    if (!all_neutral(state)) {
        neutral_samples_ = 0;
        return CaptureResult::None;
    }
    if (++neutral_samples_ < 2) return CaptureResult::None;
    neutral_samples_ = 0;
    if (phase_ == CapturePhase::WaitingForNeutral) {
        phase_ = CapturePhase::Listening;
        return CaptureResult::None;
    }
    return finish_candidate(profile);
}

CaptureResult Capture::finish_candidate(Profile& profile) {
    if (!target_ || !candidate_) return cancel();
    const std::size_t index = static_cast<std::size_t>(*target_);
    if (index < digital_target_count) {
        const auto& source = std::get<DigitalSource>(*candidate_);
        const auto& target_bindings = profile.digital[index];
        if (std::any_of(target_bindings.begin(), target_bindings.end(),
            [&](const DigitalSource& binding) { return same_source_identity(binding, source); })) {
            reset();
            return CaptureResult::Noop;
        }
        if (has_cross_target_duplicate(profile, *target_, source)) {
            phase_ = CapturePhase::Conflict;
            return CaptureResult::Conflict;
        }
        if (const auto* half = std::get_if<AxisHalfSource>(&source)) {
            const bool throttle_conflict = profile.throttle.source &&
                profile.throttle.source->axis == half->axis &&
                profile.throttle.source->direction == half->direction;
            const bool brake_conflict = profile.brake.source &&
                profile.brake.source->axis == half->axis &&
                profile.brake.source->direction == half->direction;
            const bool analog_conflict = std::any_of(profile.analog.begin(), profile.analog.end(),
                [&](const std::optional<AnalogSource>& analog) {
                    return analog && analog->axis == half->axis;
                });
            if (throttle_conflict || brake_conflict || analog_conflict) {
                phase_ = CapturePhase::Conflict;
                return CaptureResult::Conflict;
            }
        }
    } else if (*target_ == Target::Throttle) {
        const auto& source = std::get<ThrottleSource>(*candidate_);
        if (profile.throttle.source == source) {
            reset();
            return CaptureResult::Noop;
        }
        bool conflict = std::any_of(profile.analog.begin(), profile.analog.end(),
            [&](const std::optional<AnalogSource>& analog) {
                return analog && analog->axis == source.axis;
            });
        conflict = conflict || (profile.brake.source &&
            profile.brake.source->axis == source.axis &&
            profile.brake.source->direction == source.direction);
        for (std::size_t target = 0; target < digital_target_count && !conflict; ++target) {
            for (const auto& binding : profile.digital[target]) {
                if (const auto* half = std::get_if<AxisHalfSource>(&binding);
                    half && half->axis == source.axis && half->direction == source.direction) {
                    conflict = true;
                    break;
                }
            }
        }
        if (conflict) {
            phase_ = CapturePhase::Conflict;
            return CaptureResult::Conflict;
        }
    } else if (*target_ == Target::Brake) {
        const auto& source = std::get<BrakeSource>(*candidate_);
        if (profile.brake.source == source) {
            reset();
            return CaptureResult::Noop;
        }
        bool conflict = std::any_of(profile.analog.begin(), profile.analog.end(),
            [&](const std::optional<AnalogSource>& analog) {
                return analog && analog->axis == source.axis;
            });
        conflict = conflict || (profile.throttle.source &&
            profile.throttle.source->axis == source.axis &&
            profile.throttle.source->direction == source.direction);
        for (std::size_t target = 0; target < digital_target_count && !conflict; ++target) {
            for (const auto& binding : profile.digital[target]) {
                if (const auto* half = std::get_if<AxisHalfSource>(&binding);
                    half && half->axis == source.axis && half->direction == source.direction) {
                    conflict = true;
                    break;
                }
            }
        }
        if (conflict) {
            phase_ = CapturePhase::Conflict;
            return CaptureResult::Conflict;
        }
    } else {
        const auto& source = std::get<AnalogSource>(*candidate_);
        const std::size_t analog = index - digital_target_count;
        if (profile.analog[analog] && *profile.analog[analog] == source) {
            reset();
            return CaptureResult::Noop;
        }
        for (std::size_t other = 0; other < analog_target_count; ++other) {
            if (other != analog && profile.analog[other] &&
                profile.analog[other]->axis == source.axis) {
                phase_ = CapturePhase::Conflict;
                return CaptureResult::Conflict;
            }
        }
        const bool throttle_conflict = profile.throttle.source &&
            profile.throttle.source->axis == source.axis;
        const bool brake_conflict = profile.brake.source &&
            profile.brake.source->axis == source.axis;
        bool digital_conflict = false;
        for (const auto& bindings : profile.digital) {
            digital_conflict = digital_conflict || std::any_of(bindings.begin(), bindings.end(),
                [&](const DigitalSource& binding) {
                    const auto* half = std::get_if<AxisHalfSource>(&binding);
                    return half && half->axis == source.axis;
                });
        }
        if (throttle_conflict || brake_conflict || digital_conflict) {
            phase_ = CapturePhase::Conflict;
            return CaptureResult::Conflict;
        }
    }
    return commit(profile);
}

CaptureResult Capture::commit(Profile& profile) {
    const std::size_t index = static_cast<std::size_t>(*target_);
    if (index < digital_target_count) {
        profile.digital[index].push_back(std::get<DigitalSource>(*candidate_));
    } else if (*target_ == Target::Throttle) {
        profile.throttle.source = std::get<ThrottleSource>(*candidate_);
    } else if (*target_ == Target::Brake) {
        profile.brake.source = std::get<BrakeSource>(*candidate_);
    } else {
        profile.analog[index - digital_target_count] = std::get<AnalogSource>(*candidate_);
    }
    reset();
    return CaptureResult::Committed;
}

CaptureResult Capture::accept_conflict(Profile& profile) {
    if (phase_ != CapturePhase::Conflict || !candidate_ || !target_) return CaptureResult::None;
    return commit(profile);
}

CaptureResult Capture::move_conflict(Profile& profile) {
    if (phase_ != CapturePhase::Conflict || !candidate_ || !target_) return CaptureResult::None;

    const std::size_t target_index = static_cast<std::size_t>(*target_);
    if (*target_ == Target::Throttle) {
        const auto& source = std::get<ThrottleSource>(*candidate_);
        for (auto& bindings : profile.digital) {
            std::erase_if(bindings, [&](const DigitalSource& binding) {
                const auto* half = std::get_if<AxisHalfSource>(&binding);
                return half && half->axis == source.axis && half->direction == source.direction;
            });
        }
        for (auto& analog : profile.analog) {
            if (analog && analog->axis == source.axis) analog.reset();
        }
        if (profile.brake.source && profile.brake.source->axis == source.axis &&
            profile.brake.source->direction == source.direction) {
            profile.brake.source.reset();
        }
    } else if (*target_ == Target::Brake) {
        const auto& source = std::get<BrakeSource>(*candidate_);
        for (auto& bindings : profile.digital) {
            std::erase_if(bindings, [&](const DigitalSource& binding) {
                const auto* half = std::get_if<AxisHalfSource>(&binding);
                return half && half->axis == source.axis && half->direction == source.direction;
            });
        }
        for (auto& analog : profile.analog) {
            if (analog && analog->axis == source.axis) analog.reset();
        }
        if (profile.throttle.source && profile.throttle.source->axis == source.axis &&
            profile.throttle.source->direction == source.direction) {
            profile.throttle.source.reset();
        }
    } else if (target_index < digital_target_count) {
        const auto& source = std::get<DigitalSource>(*candidate_);
        for (std::size_t index = 0; index < digital_target_count; ++index) {
            if (index == target_index) continue;
            std::erase_if(profile.digital[index], [&](const DigitalSource& binding) {
                return same_source_identity(binding, source);
            });
        }
        if (const auto* half = std::get_if<AxisHalfSource>(&source)) {
            if (profile.throttle.source && profile.throttle.source->axis == half->axis &&
                profile.throttle.source->direction == half->direction) {
                profile.throttle.source.reset();
            }
            if (profile.brake.source && profile.brake.source->axis == half->axis &&
                profile.brake.source->direction == half->direction) {
                profile.brake.source.reset();
            }
            for (auto& analog : profile.analog) {
                if (analog && analog->axis == half->axis) analog.reset();
            }
        }
    } else {
        const auto& source = std::get<AnalogSource>(*candidate_);
        for (std::size_t index = 0; index < analog_target_count; ++index) {
            if (index != target_index - digital_target_count && profile.analog[index] &&
                profile.analog[index]->axis == source.axis) {
                profile.analog[index].reset();
            }
        }
        for (auto& bindings : profile.digital) {
            std::erase_if(bindings, [&](const DigitalSource& binding) {
                const auto* half = std::get_if<AxisHalfSource>(&binding);
                return half && half->axis == source.axis;
            });
        }
        if (profile.throttle.source && profile.throttle.source->axis == source.axis) {
            profile.throttle.source.reset();
        }
        if (profile.brake.source && profile.brake.source->axis == source.axis) {
            profile.brake.source.reset();
        }
    }
    return commit(profile);
}

CaptureResult Capture::reject_conflict() {
    if (phase_ != CapturePhase::Conflict) return CaptureResult::None;
    reset();
    return CaptureResult::Cancelled;
}

void Capture::reset() {
    phase_ = CapturePhase::Idle;
    target_.reset();
    controller_instance_ = 0;
    neutral_samples_ = 0;
    candidate_.reset();
}

void enqueue_command(Command command) {
    std::lock_guard lock(command_mutex);
    command_queue.push_back(std::move(command));
}

std::vector<Command> take_commands() {
    std::lock_guard lock(command_mutex);
    std::vector<Command> result;
    result.reserve(command_queue.size());
    while (!command_queue.empty()) {
        result.push_back(std::move(command_queue.front()));
        command_queue.pop_front();
    }
    return result;
}

void publish_ui_snapshot(UiSnapshot snapshot) {
    std::lock_guard lock(snapshot_mutex);
    published_snapshot = std::move(snapshot);
}

UiSnapshot ui_snapshot() {
    std::lock_guard lock(snapshot_mutex);
    return published_snapshot;
}

} // namespace lambo::controls
