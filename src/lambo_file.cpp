#include "lambo_file.h"

#include <cerrno>
#include <cstdio>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lambo::file {

bool atomic_replace(const std::filesystem::path& source,
                    const std::filesystem::path& destination,
                    std::string& error) {
#if defined(_WIN32)
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    const std::error_code ec{static_cast<int>(GetLastError()), std::system_category()};
#else
    if (std::rename(source.c_str(), destination.c_str()) == 0) return true;
    const std::error_code ec{errno, std::generic_category()};
#endif
    error = "could not publish temporary file '" + source.string() + "' as '" +
            destination.string() + "': " + ec.message();
    return false;
}

} // namespace lambo::file
