#include "lambo_startup.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

bool env_has_value(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

} // namespace

namespace lambo {

bool environment_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') return false;
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized != "0" && normalized != "false" && normalized != "no" &&
           normalized != "off";
}

StartupMode startup_mode_from_environment() {
    // These knobs drive a deterministic harness/probe run and must never wait
    // for a human to press Play. Keep this list as the single bypass policy.
    constexpr const char* boolean_variables[] = {
        "CI",
        "LAMBO_HEADLESS",
        "LAMBO_LIGHTING_SELFTEST",
        "LAMBO_CRASH_TEST",
        "LAMBO_SELFTEST",
        "LAMBO_STEERING_PROBE",
    };
    for (const char* variable : boolean_variables) {
        if (environment_flag_enabled(variable)) return StartupMode::Automatic;
    }

    // These variables carry structured or numeric values. Zero can be a valid
    // first field (for example, a neutral button mask with a scripted stick),
    // so any non-empty value selects deterministic automatic startup.
    constexpr const char* scripted_variables[] = {
        "LAMBO_WARP",
        "LAMBO_MODERN_INPUT",
        "LAMBO_INPUT_PULSE",
        "LAMBO_ANALOG_THROTTLE",
        "LAMBO_STEERING_SEQUENCE",
        "LAMBO_MODERN_MAX_VIS",
    };
    for (const char* variable : scripted_variables) {
        if (env_has_value(variable)) return StartupMode::Automatic;
    }
    return StartupMode::InteractiveLauncher;
}

StartupController::StartupController(StartupMode mode, StartGameAction start_game)
    : mode_(mode), start_game_(std::move(start_game)) {}

bool StartupController::runtime_ready() {
    bool expected = false;
    if (!runtime_ready_.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
        return false;
    }

    if (mode_ == StartupMode::Automatic) {
        return begin_start();
    }

    StartupState expected_state = StartupState::WaitingForRuntime;
    state_.compare_exchange_strong(expected_state, StartupState::WaitingForPlay,
                                   std::memory_order_acq_rel);
    return false;
}

bool StartupController::request_play() {
    if (mode_ != StartupMode::InteractiveLauncher ||
        !runtime_ready_.load(std::memory_order_acquire)) {
        return false;
    }
    return begin_start();
}

bool StartupController::begin_start() {
    StartupState expected = mode_ == StartupMode::Automatic
        ? StartupState::WaitingForRuntime
        : StartupState::WaitingForPlay;
    if (!state_.compare_exchange_strong(expected, StartupState::Starting,
                                         std::memory_order_acq_rel)) {
        return false;
    }

    if (start_game_) start_game_();
    state_.store(StartupState::Started, std::memory_order_release);
    return true;
}

bool StartupController::request_exit() {
    StartupState current = state_.load(std::memory_order_acquire);
    while (current != StartupState::Exiting) {
        if (state_.compare_exchange_weak(current, StartupState::Exiting,
                                         std::memory_order_acq_rel)) {
            return true;
        }
    }
    return false;
}

bool StartupController::watchdog_armed() const {
    if (mode_ == StartupMode::Automatic) return true;
    const StartupState current = state_.load(std::memory_order_acquire);
    return current == StartupState::Starting || current == StartupState::Started;
}

} // namespace lambo
