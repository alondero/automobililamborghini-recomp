#include "lambo_startup.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

bool env_enabled(const char* value) {
    if (value[0] == '\0') return false;
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized != "0" && normalized != "false" && normalized != "no" &&
           normalized != "off";
}

} // namespace

namespace lambo {

StartupMode resolve_startup_mode(std::optional<StartupMode> requested, bool show_launcher) {
    if (requested) return *requested;
    if (const char* value = std::getenv("LAMBO_LAUNCHER")) {
        show_launcher = env_enabled(value);
    }
    return show_launcher ? StartupMode::InteractiveLauncher : StartupMode::Automatic;
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
    if (state_.load(std::memory_order_acquire) != StartupState::WaitingForPlay ||
        !runtime_ready_.load(std::memory_order_acquire)) {
        return false;
    }
    return begin_start();
}

bool StartupController::begin_start() {
    StartupState expected = state_.load(std::memory_order_acquire);
    if (expected != StartupState::WaitingForRuntime && expected != StartupState::WaitingForPlay)
        return false;
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

void StartupController::start_failed() {
    StartupState expected = StartupState::Started;
    state_.compare_exchange_strong(expected, StartupState::WaitingForPlay,
                                  std::memory_order_acq_rel);
}

bool StartupController::watchdog_armed() const {
    return mode_ == StartupMode::Automatic &&
           state_.load(std::memory_order_acquire) != StartupState::Started;
}

} // namespace lambo
