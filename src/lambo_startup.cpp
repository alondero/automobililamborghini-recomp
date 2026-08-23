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

bool env_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') return false;
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized != "0" && normalized != "false" && normalized != "no" &&
           normalized != "off";
}

} // namespace

namespace lambo {

StartupMode startup_mode_from_environment() {
    // 1. Explicit env var override wins (for tests / CLI).
    if (std::getenv("LAMBO_LAUNCHER") != nullptr) {
        return env_enabled("LAMBO_LAUNCHER") ? StartupMode::InteractiveLauncher : StartupMode::Automatic;
    }
    // 2. Default is direct auto-boot straight into gameplay.
    return StartupMode::Automatic;
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
    state_.store(StartupState::Exiting, std::memory_order_release);
    return true;
}

bool StartupController::watchdog_armed() const {
    return mode_ == StartupMode::Automatic &&
           state_.load(std::memory_order_acquire) != StartupState::Started;
}

} // namespace lambo
