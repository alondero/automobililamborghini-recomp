#include "lambo_launch_options.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "lambo_pak_io.h"

namespace {

bool is_controller_pak_container(const char* path) {
    const std::string_view input{path};
    const size_t dot = input.find_last_of('.');
    const size_t slash = input.find_last_of("/\\");
    if (dot == input.npos || (slash != input.npos && slash > dot)) return false;
    std::string extension{input.substr(dot)};
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension != ".mpk" && extension != ".pak" &&
        extension != ".srm" && extension != ".n64") return false;
    const LamboPakIoResult result = lambo_pak_probe_file(path);
    if (!result.ok) return false;
    switch (result.format) {
    case LAMBO_PAK_FORMAT_RAW:
    case LAMBO_PAK_FORMAT_FOUR_PORT:
        return extension == ".mpk" || extension == ".pak";
    case LAMBO_PAK_FORMAT_RETROARCH:
        return extension == ".srm";
    case LAMBO_PAK_FORMAT_DEXDRIVE:
        return extension == ".n64";
    }
    return false;
}

std::optional<LamboLogLevel> parse_log_level(std::string_view value) {
    if (value == "error") return LAMBO_LEVEL_ERROR;
    if (value == "warn" || value == "warning") return LAMBO_LEVEL_WARN;
    if (value == "info") return LAMBO_LEVEL_INFO;
    if (value == "debug") return LAMBO_LEVEL_DEBUG;
    if (value == "trace") return LAMBO_LEVEL_TRACE;
    return std::nullopt;
}

} // namespace

namespace lambo {

std::string parse_launch_options(int argc, const char* const* argv, LaunchOptions& options) {
    options = {};
    bool positional_only = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr || argv[i][0] == '\0') continue;
        const std::string_view argument{argv[i]};
        if (positional_only || !argument.starts_with('-')) {
            if (is_controller_pak_container(argv[i])) {
                options.import_save = argument;
            } else if (options.rom.empty()) {
                options.rom = argument;
            } else {
                return "extra ROM argument '" + std::string(argument) + "'";
            }
            continue;
        }
        if (argument == "--") {
            positional_only = true;
            continue;
        }

        const size_t equals = argument.find('=');
        const std::string_view flag = argument.substr(0, equals);
        std::string* path = nullptr;
        if (flag == "--rom") path = &options.rom;
        else if (flag == "--data-dir") path = &options.data_dir;
        else if (flag == "--import-save") path = &options.import_save;
        else if (flag == "--controller-pak") path = &options.controller_pak;
        else if (flag == "--track-patch") path = &options.track_patch;

        if (path || flag == "--log-level") {
            std::string_view value;
            if (equals != argument.npos) {
                value = argument.substr(equals + 1);
            } else if (i + 1 < argc && argv[i + 1] != nullptr && argv[i + 1][0] != '-') {
                value = argv[++i];
            }
            if (value.empty()) {
                return std::string(flag) + (path ? " requires a path" : " requires a level");
            }
            if (path) {
                if (flag == "--rom" && !options.rom.empty()) return "specify only one ROM path";
                *path = value;
            } else {
                const auto level = parse_log_level(value);
                if (!level) return "--log-level must be error, warn, info, debug, or trace";
                options.log_level = *level;
            }
        } else if (argument == "-h" || argument == "--help") {
            options.action = LaunchAction::Help;
        } else if (argument == "--version") {
            options.action = LaunchAction::Version;
        } else if (argument == "--fullscreen" || argument == "--windowed") {
            options.fullscreen = argument == "--fullscreen";
        } else if (argument == "--play" || argument == "--launcher") {
            options.startup_mode = argument == "--play" ? StartupMode::Automatic
                                                          : StartupMode::InteractiveLauncher;
        } else if (argument == "--portable") {
            options.portable = true;
        } else if (argument == "--console") {
            options.console = true;
        } else if (argument == "--verbose" || argument == "--lambo-debug") {
            options.log_level = LAMBO_LEVEL_DEBUG;
        } else {
            return "unknown option '" + std::string(argument) + "'";
        }
    }
    if (options.portable && !options.data_dir.empty()) {
        return "--portable and --data-dir cannot be used together";
    }
    if (!options.import_save.empty() && !options.controller_pak.empty()) {
        return "--import-save and --controller-pak cannot be used together";
    }
    return {};
}

const char* launch_help() {
    return "Usage: lamborghini_modern [options] [ROM]\n"
           "\n"
           "  --rom PATH             Load the specified USA ROM\n"
           "  --fullscreen           Start in borderless fullscreen for this run\n"
           "  --windowed             Start in a window for this run\n"
           "  --play                 Boot directly into the game\n"
           "  --launcher             Open the built-in launcher\n"
           "  --data-dir PATH        Select a folder for settings, saves, mods, and logs\n"
           "  --portable             Keep settings, saves, mods, and logs beside the game\n"
           "  --import-save PATH     Import a Controller Pak save into the active profile\n"
           "  --controller-pak PATH  Use a Controller Pak file directly\n"
           "  --track-patch PATH     Load a Track Lab .altrk file\n"
           "  --console              Enable terminal logging on Windows\n"
           "  --verbose              Set the log level to debug\n"
           "  --log-level LEVEL      Set the log level (error, warn, info, debug, trace)\n"
           "  -h, --help             Show this help and exit\n"
           "  --version              Show the version and exit\n"
           "\n"
           "Relative paths use the working directory. Use --flag=PATH for a path starting\n"
           "with a dash, or -- before a positional path.\n";
}

} // namespace lambo
