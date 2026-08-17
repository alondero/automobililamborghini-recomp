#include <cstdlib>
#include <iostream>

#include "lambo_startup.h"

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void set_environment(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

void clear_startup_environment() {
    constexpr const char* variables[] = {
        "CI",
        "LAMBO_HEADLESS",
        "LAMBO_LIGHTING_SELFTEST",
        "LAMBO_WARP",
        "LAMBO_MODERN_INPUT",
        "LAMBO_INPUT_PULSE",
        "LAMBO_MODERN_MAX_VIS",
        "LAMBO_CRASH_TEST",
        "LAMBO_SELFTEST",
    };
    for (const char* variable : variables) set_environment(variable, nullptr);
}
}

int main() {
    clear_startup_environment();
    expect(lambo::startup_mode_from_environment() == lambo::StartupMode::InteractiveLauncher,
           "ordinary graphical startup uses the launcher");

    set_environment("LAMBO_MODERN_INPUT", "0:53:0");
    expect(lambo::startup_mode_from_environment() == lambo::StartupMode::Automatic,
           "structured input beginning with zero still bypasses the launcher");
    set_environment("LAMBO_MODERN_INPUT", nullptr);

    set_environment("CI", "true");
    expect(lambo::startup_mode_from_environment() == lambo::StartupMode::Automatic,
           "CI runs bypass the launcher");
    set_environment("CI", nullptr);

    set_environment("LAMBO_CRASH_TEST", "fault");
    expect(lambo::startup_mode_from_environment() == lambo::StartupMode::Automatic,
           "named crash probes bypass the launcher");
    set_environment("LAMBO_CRASH_TEST", nullptr);

    set_environment("LAMBO_SELFTEST", "false");
    expect(lambo::startup_mode_from_environment() == lambo::StartupMode::InteractiveLauncher,
           "exact false boolean values do not bypass the launcher");
    set_environment("LAMBO_SELFTEST", nullptr);

    int starts = 0;
    lambo::StartupController interactive(lambo::StartupMode::InteractiveLauncher,
                                         [&] { ++starts; });
    expect(interactive.state() == lambo::StartupState::WaitingForRuntime,
           "graphical startup initially waits for runtime");
    expect(!interactive.request_play(), "Play cannot start before runtime readiness");
    expect(!interactive.watchdog_armed(), "idle interactive launcher has no watchdog");
    expect(!interactive.runtime_ready(), "interactive runtime readiness waits for Play");
    expect(interactive.state() == lambo::StartupState::WaitingForPlay,
           "interactive runtime enters WaitingForPlay");
    expect(interactive.request_play(), "first Play starts the game");
    expect(!interactive.request_play(), "repeated Play is idempotent");
    expect(starts == 1, "start action is called exactly once");
    expect(interactive.state() == lambo::StartupState::Started,
           "interactive startup reaches Started");

    int automatic_starts = 0;
    lambo::StartupController automatic(lambo::StartupMode::Automatic,
                                       [&] { ++automatic_starts; });
    expect(automatic.watchdog_armed(), "automatic mode keeps the watchdog armed");
    expect(automatic.runtime_ready(), "automatic mode starts when runtime is ready");
    expect(automatic_starts == 1, "automatic startup calls start action once");
    expect(!automatic.runtime_ready(), "duplicate runtime readiness is ignored");

    lambo::StartupController quit(lambo::StartupMode::InteractiveLauncher);
    quit.runtime_ready();
    expect(quit.request_exit(), "launcher quit transitions to Exiting");
    expect(quit.state() == lambo::StartupState::Exiting,
           "launcher quit has an explicit exit state");
    expect(!quit.request_play(), "Play after quit cannot start the game");

    return failures == 0 ? 0 : 1;
}
