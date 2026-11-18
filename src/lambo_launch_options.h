#ifndef LAMBO_LAUNCH_OPTIONS_H
#define LAMBO_LAUNCH_OPTIONS_H

#include <optional>
#include <string>

#include "lambo_log.h"
#include "lambo_startup.h"

namespace lambo {

enum class LaunchAction { Run, Help, Version };

struct LaunchOptions {
    LaunchAction action = LaunchAction::Run;
    std::string rom;
    std::string data_dir;
    std::string import_save;
    std::string controller_pak;
    std::string track_patch;
    std::optional<bool> fullscreen;
    std::optional<StartupMode> startup_mode;
    bool portable = false;
    bool console = false;
    LamboLogLevel log_level = LAMBO_LEVEL_WARN;
};

// Returns an empty string on success or a diagnostic on invalid input.
std::string parse_launch_options(int argc, const char* const* argv, LaunchOptions& options);
const char* launch_help();

} // namespace lambo

#endif
