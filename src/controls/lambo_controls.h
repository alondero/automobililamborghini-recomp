#ifndef LAMBO_CONTROLS_H
#define LAMBO_CONTROLS_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lambo::controls {

enum class Target {
    A, B, Z, Start, L, R,
    DpadUp, DpadDown, DpadLeft, DpadRight,
    CUp, CDown, CLeft, CRight,
    StickX, StickY,
    Throttle,
    Brake,
    Count,
};

enum class LogicalButton {
    A, B, X, Y, Back, Guide, Start, LeftStick, RightStick,
    LeftShoulder, RightShoulder, DpadUp, DpadDown, DpadLeft, DpadRight,
    Count,
};

enum class LogicalAxis {
    LeftX, LeftY, RightX, RightY, TriggerLeft, TriggerRight, Count,
};

enum class AxisDirection { Negative, Positive };
enum class ThrottleMode { Digital, Analog };
enum class BrakeMode { Digital, Analog };

struct ButtonSource {
    LogicalButton button{};
    bool operator==(const ButtonSource&) const = default;
};

struct AxisHalfSource {
    LogicalAxis axis{};
    AxisDirection direction{};
    std::int16_t threshold{};
    bool operator==(const AxisHalfSource&) const = default;
};

struct AnalogSource {
    LogicalAxis axis{};
    bool invert{};
    std::int16_t deadzone{};
    bool operator==(const AnalogSource&) const = default;
};

struct ThrottleSource {
    LogicalAxis axis{};
    AxisDirection direction{AxisDirection::Positive};
    bool operator==(const ThrottleSource&) const = default;
};

struct ThrottleConfig {
    ThrottleMode mode{ThrottleMode::Digital};
    std::optional<ThrottleSource> source;
    float deadzone{0.05f};
    float saturation{1.0f};
    bool operator==(const ThrottleConfig&) const = default;
};

struct BrakeSource {
    LogicalAxis axis{};
    AxisDirection direction{AxisDirection::Positive};
    bool operator==(const BrakeSource&) const = default;
};

struct BrakeConfig {
    BrakeMode mode{BrakeMode::Digital};
    std::optional<BrakeSource> source;
    float deadzone{0.05f};
    float saturation{1.0f};
    bool operator==(const BrakeConfig&) const = default;
};

using DigitalSource = std::variant<ButtonSource, AxisHalfSource>;
using DigitalBindings = std::vector<DigitalSource>;

constexpr std::size_t digital_target_count = 14;
constexpr std::size_t analog_target_count = 2;

struct Profile {
    std::array<DigitalBindings, digital_target_count> digital;
    std::array<std::optional<AnalogSource>, analog_target_count> analog;
    ThrottleConfig throttle;
    BrakeConfig brake;
    bool operator==(const Profile&) const = default;
};

struct RawState {
    std::array<bool, static_cast<std::size_t>(LogicalButton::Count)> buttons{};
    std::array<std::int16_t, static_cast<std::size_t>(LogicalAxis::Count)> axes{};
};

struct EvaluatedState {
    std::uint16_t buttons{};
    std::int8_t stick_x{};
    std::int8_t stick_y{};
    ThrottleMode throttle_mode{ThrottleMode::Digital};
    float throttle{};
    BrakeMode brake_mode{BrakeMode::Digital};
    float brake{};
    bool operator==(const EvaluatedState&) const = default;
};

Profile default_profile();
EvaluatedState evaluate(const Profile& profile, const RawState& state);
float throttle_source_magnitude(const ThrottleConfig& throttle, const RawState& state);
float evaluate_throttle_source(const ThrottleConfig& throttle, const RawState& state);
float brake_source_magnitude(const BrakeConfig& brake, const RawState& state);
float evaluate_brake_source(const BrakeConfig& brake, const RawState& state);
std::uint32_t pack(const EvaluatedState& state);

std::string_view target_name(Target target);
std::string_view target_label(Target target);
std::string_view button_name(LogicalButton button);
std::string_view axis_name(LogicalAxis axis);
std::optional<Target> target_from_name(std::string_view name);
std::optional<LogicalButton> button_from_name(std::string_view name);
std::optional<LogicalAxis> axis_from_name(std::string_view name);
std::optional<std::string> canonicalize_guid(std::string_view guid);

struct ControlsConfig {
    std::string preferred_controller_guid;
    std::unordered_map<std::string, Profile> profiles;
    // The valid v1 source document is retained and merged on save so fields added by
    // newer tools or other builds survive a round trip.
    std::string preserved_document;
    bool read_only{};
};

enum class LoadStatus { Missing, Loaded, Malformed, InvalidVersion, FutureVersion };

struct LoadResult {
    ControlsConfig config;
    LoadStatus status{LoadStatus::Missing};
    std::vector<std::string> warnings;
};

struct SaveResult {
    bool saved{};
    std::filesystem::path path;
    std::string error;
};

std::filesystem::path controls_config_path();
LoadResult load_config(const std::filesystem::path& path);
LoadResult load_config();
SaveResult save_config(ControlsConfig& config, const std::filesystem::path& path);
SaveResult save_config(ControlsConfig& config);

Profile& profile_for_guid(ControlsConfig& config, std::string_view canonical_guid);
const Profile& profile_for_guid(const ControlsConfig& config, std::string_view canonical_guid);
bool has_cross_target_duplicate(const Profile& profile, Target target,
                                const DigitalSource& source);

enum class CapturePhase {
    Idle, WaitingForNeutral, Listening, WaitingForRelease, Conflict,
};
enum class CaptureResult { None, Committed, Noop, Conflict, Cancelled };

class Capture {
public:
    static constexpr std::int16_t activation_threshold = 16000;
    static constexpr std::int16_t neutral_threshold = 8000;

    void begin(Target target, std::int32_t controller_instance);
    CaptureResult cancel();
    // Active capture consumes every controller button/axis event. Events from a
    // different instance are consumed but cannot become candidates.
    CaptureResult button_event(std::int32_t controller_instance, LogicalButton button,
                               bool pressed);
    CaptureResult axis_event(std::int32_t controller_instance, LogicalAxis axis,
                             std::int16_t value);
    CaptureResult sample(const RawState& selected_controller_state, Profile& profile);
    CaptureResult accept_conflict(Profile& profile);
    CaptureResult move_conflict(Profile& profile);
    CaptureResult reject_conflict();

    bool active() const { return phase_ != CapturePhase::Idle; }
    CapturePhase phase() const { return phase_; }
    std::optional<Target> target() const { return target_; }
    const std::optional<std::variant<DigitalSource, AnalogSource, ThrottleSource, BrakeSource>>& candidate() const {
        return candidate_;
    }

private:
    bool all_neutral(const RawState& state) const;
    CaptureResult finish_candidate(Profile& profile);
    CaptureResult commit(Profile& profile);
    void reset();

    CapturePhase phase_{CapturePhase::Idle};
    std::optional<Target> target_;
    std::int32_t controller_instance_{};
    int neutral_samples_{};
    std::optional<std::variant<DigitalSource, AnalogSource, ThrottleSource, BrakeSource>> candidate_;
};

enum class ControllerLayout { Xbox, PlayStation, Nintendo, Generic };

struct ControllerInfo {
    std::int32_t instance{};
    std::string guid;
    std::string name;
    ControllerLayout layout{ControllerLayout::Generic};
    bool active{};
};

enum class CommandKind {
    Select, Add, Remove, Threshold, Deadzone, Invert, ResetTarget,
    Saturation, ThrottleMode, ClearThrottleSource,
    BrakeMode, ClearBrakeSource,
    ResetProfile, ConflictAccept, ConflictMove, CaptureCancel,
};

struct Command {
    CommandKind kind{};
    Target target{};
    std::int32_t instance{};
    std::size_t binding_index{};
    int value{};
};

struct UiSnapshot {
    std::vector<ControllerInfo> controllers;
    std::optional<std::int32_t> selected_instance;
    std::string selected_guid;
    Profile profile{default_profile()};
    RawState raw;
    EvaluatedState evaluated;
    CapturePhase capture_phase{CapturePhase::Idle};
    std::optional<Target> capture_target;
    std::uint64_t config_revision{};
    std::uint64_t sample_revision{};
    bool read_only{};
    bool saved{};
    std::filesystem::path config_path;
    std::string status;
    std::vector<std::string> warnings;
};

void enqueue_command(Command command);
std::vector<Command> take_commands();
void publish_ui_snapshot(UiSnapshot snapshot);
UiSnapshot ui_snapshot();

} // namespace lambo::controls

#endif
