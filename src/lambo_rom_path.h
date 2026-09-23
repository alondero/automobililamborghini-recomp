#ifndef LAMBO_ROM_PATH_H
#define LAMBO_ROM_PATH_H

#include <filesystem>

namespace lambo {

inline constexpr const char* default_rom_filename = "Automobili Lamborghini (USA).z64";

// Preserve the legacy path (including its validation errors) when it exists.
// Byte order is detected by librecomp from contents, not from this suffix.
inline const char* find_default_rom_filename(const std::filesystem::path& directory = ".") {
    std::error_code error;
    const bool existing_default = std::filesystem::exists(directory / default_rom_filename, error);
    if (existing_default || error) return default_rom_filename;

    constexpr const char* alternate = "Automobili Lamborghini (USA).n64";
    if (std::filesystem::is_regular_file(directory / alternate, error) && !error) return alternate;
    return default_rom_filename;
}

} // namespace lambo

#endif
