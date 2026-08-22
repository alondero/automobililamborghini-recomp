#ifndef LAMBO_STARTUP_H
#define LAMBO_STARTUP_H

#include <atomic>
#include <functional>

namespace lambo {

enum class StartupMode {
    InteractiveLauncher,
    Automatic,
};

enum class StartupState {
    WaitingForRuntime,
    WaitingForPlay,
    Starting,
    Started,
    Exiting,
};

// Keep all environment-driven bypass policy in one place. This is deliberately
// independent of SDL and the renderer so probe tests can exercise it directly.
bool environment_flag_enabled(const char* name);
StartupMode startup_mode_from_environment();

class StartupController {
public:
    using StartGameAction = std::function<void()>;

    explicit StartupController(StartupMode mode,
                               StartGameAction start_game = {});

    // Called once the host runtime and its window/render resources are ready.
    // Returns true when this call initiated automatic startup.
    bool runtime_ready();

    // Requests the first graphical Play transition. Repeated requests are safe
    // and return false after startup has begun.
    bool request_play();

    // Explicit application shutdown, including a launcher quit before the first
    // VI, is distinct from a failed boot probe.
    bool request_exit();

    StartupMode mode() const { return mode_; }
    StartupState state() const { return state_.load(std::memory_order_acquire); }
    bool runtime_is_ready() const { return runtime_ready_.load(std::memory_order_acquire); }
    bool watchdog_armed() const;

private:
    bool begin_start();

    StartupMode mode_;
    StartGameAction start_game_;
    std::atomic<StartupState> state_{StartupState::WaitingForRuntime};
    std::atomic<bool> runtime_ready_{false};
};

} // namespace lambo

#endif
