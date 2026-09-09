#ifndef LAMBO_PATHS_H
#define LAMBO_PATHS_H

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#if defined(_WIN32)
#include <cwchar>
#endif

// One platform-aware authority for per-user application data. The public
// lambo::config::app_config_dir() forwards here for existing callers, while
// logging/crash reporting can use the state sibling without depending on the
// full graphics-config implementation.
//
// Portable mode (issue #190) keeps everything next to the game instead of on
// the system drive, for USB/D: installs. It follows the de-facto emulator
// convention (Dolphin, DuckStation, PCSX2, Zelda64Recomp): an empty
// portable.txt next to the executable, or the --portable flag /
// LAMBO_PORTABLE env var, forces the exe directory. A portable.txt in the
// launch working directory is still honoured as a fallback so developer and
// test runs from a build tree keep working.
namespace lambo::paths {

namespace detail {

inline std::optional<std::filesystem::path>& executable_dir_slot() {
    static std::optional<std::filesystem::path> slot;
    return slot;
}

inline bool& portable_forced_slot() {
    static bool forced = false;
    return forced;
}

inline bool env_portable_requested() {
    if (const char* value = std::getenv("LAMBO_PORTABLE");
        value != nullptr && value[0] != '\0') {
        std::string text{value};
        for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return !(text == "0" || text == "false" || text == "no" || text == "off");
    }
    return false;
}

inline bool marker_in(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "portable.txt", ec);
}

} // namespace detail

// Startup seam: main() records the executable directory before logging
// initialises (the log directory itself depends on portable resolution).
// Tests use this plus the forced flag to control resolution hermetically.
inline void set_executable_dir(const std::filesystem::path& dir) {
    detail::executable_dir_slot() = dir;
}
inline void clear_executable_dir() { detail::executable_dir_slot().reset(); }
inline void set_portable_forced(bool forced) { detail::portable_forced_slot() = forced; }

// Directory selected by a portable.txt marker: the executable directory wins
// over the launch working directory. Empty when no marker is present.
inline std::optional<std::filesystem::path> portable_marker_dir() {
    const auto& exe = detail::executable_dir_slot();
    if (exe.has_value() && detail::marker_in(*exe)) return exe;
    std::error_code ec;
    if (std::filesystem::exists("portable.txt", ec)) {
        std::error_code cwd_ec;
        std::filesystem::path cwd = std::filesystem::current_path(cwd_ec);
        if (!cwd_ec) return cwd;
        return std::filesystem::path{"."};
    }
    return std::nullopt;
}

inline bool portable_mode() {
    if (detail::portable_forced_slot() || detail::env_portable_requested()) return true;
    return portable_marker_dir().has_value();
}

// Directory portable mode keeps files in: the marker that triggered it, or
// the executable directory (forced mode), falling back to the launch
// directory when the executable directory is unknown (tests).
inline std::filesystem::path portable_root() {
    if (const auto marker = portable_marker_dir(); marker.has_value()) return *marker;
    const auto& exe = detail::executable_dir_slot();
    if (exe.has_value()) return *exe;
    return std::filesystem::current_path();
}

// Short human-readable reason for the startup log line. Only meaningful when
// portable_mode() is true.
inline const char* portable_mode_source() {
    if (detail::portable_forced_slot()) return "--portable flag";
    if (detail::env_portable_requested()) return "LAMBO_PORTABLE";
    const auto& exe = detail::executable_dir_slot();
    if (exe.has_value() && detail::marker_in(*exe)) return "portable.txt next to executable";
    return "portable.txt in launch directory";
}

inline std::filesystem::path app_config_dir() {
    if (portable_mode()) return portable_root();
#if defined(_WIN32)
    if (const wchar_t* localappdata = _wgetenv(L"LOCALAPPDATA");
        localappdata != nullptr && localappdata[0] != L'\0')
        return std::filesystem::path{localappdata} / "LamborghiniRecomp";
#else
    // Runtime history such as logs belongs in the state hierarchy rather than
    // the config hierarchy, which users commonly synchronize as dotfiles.
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME");
        xdg != nullptr && xdg[0] != '\0')
        return std::filesystem::path{xdg} / "LamborghiniRecomp";
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
        return std::filesystem::path{home} / ".config" / "LamborghiniRecomp";
#endif
    return std::filesystem::current_path();
}

inline std::filesystem::path app_state_dir() {
    if (portable_mode()) return portable_root();
#if defined(_WIN32)
    return app_config_dir();
#else
    if (const char* state = std::getenv("XDG_STATE_HOME");
        state != nullptr && state[0] != '\0')
        return std::filesystem::path{state} / "LamborghiniRecomp";
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
        return std::filesystem::path{home} / ".local" / "state" / "LamborghiniRecomp";
    return std::filesystem::current_path();
#endif
}

} // namespace lambo::paths

#endif // LAMBO_PATHS_H
