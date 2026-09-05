#ifndef LAMBO_PATHS_H
#define LAMBO_PATHS_H

#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#include <cwchar>
#endif

// One platform-aware authority for per-user application data. The public
// lambo::config::app_config_dir() forwards here for existing callers, while
// logging/crash reporting can use the state sibling without depending on the
// full graphics-config implementation.
namespace lambo::paths {

inline std::filesystem::path app_config_dir() {
    std::error_code ec;
    if (std::filesystem::exists("portable.txt", ec))
        return std::filesystem::current_path();
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
    std::error_code ec;
    if (std::filesystem::exists("portable.txt", ec))
        return std::filesystem::current_path();
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
