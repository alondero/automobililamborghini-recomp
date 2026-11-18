#ifndef LAMBO_ROM_PATH_H
#define LAMBO_ROM_PATH_H

#include <filesystem>
#include <optional>

namespace lambo {

inline constexpr const char* default_rom_filename = "Automobili Lamborghini (USA).z64";

// Preserve the legacy path (including its validation errors) when it exists.
// Byte order is detected by librecomp from contents, not from this suffix.
inline std::optional<std::filesystem::path> find_rom_in(const std::filesystem::path& directory) {
    std::error_code error;
    const auto primary = directory / default_rom_filename;
    const bool existing_default = std::filesystem::exists(primary, error);
    if (existing_default || error) return primary;

    const auto alternate = directory / "Automobili Lamborghini (USA).n64";
    if (std::filesystem::is_regular_file(alternate, error)) return alternate;
    return std::nullopt;
}

inline std::filesystem::path find_default_rom_path(
    const std::optional<std::filesystem::path>& executable_dir,
    const std::filesystem::path& working_dir = {}) {
    if (const auto rom = find_rom_in(working_dir)) return *rom;
    if (executable_dir) {
        if (const auto rom = find_rom_in(*executable_dir)) return *rom;
    }
    return working_dir / default_rom_filename;
}

} // namespace lambo

#endif
