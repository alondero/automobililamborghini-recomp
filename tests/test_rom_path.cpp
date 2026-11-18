#include "lambo_rom_path.h"

#include <chrono>
#include <cstdio>
#include <fstream>

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("lambo-rom-path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto executable_dir = directory / "exe";
    std::filesystem::create_directories(executable_dir);
    int failures = 0;
    auto expect = [&](const std::filesystem::path& path, const char* message) {
        if (lambo::find_default_rom_path(executable_dir, directory) != path) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };
    constexpr const char* alternate = "Automobili Lamborghini (USA).n64";
    const auto primary = directory / lambo::default_rom_filename;
    const auto beside_exe = executable_dir / lambo::default_rom_filename;
    expect(primary, "no files retains the legacy missing-ROM path");
    std::ofstream(beside_exe).put('x');
    expect(beside_exe, "ROM beside the executable is found from a different directory");
    std::ofstream(directory / alternate).put('x');
    expect(directory / alternate, "the working directory takes precedence, including n64");
    std::ofstream(primary).put('x');
    expect(primary, "z64 wins when both exist, even with invalid contents");
    std::filesystem::remove(primary);
    std::filesystem::create_directory(primary);
    expect(primary, "an invalid existing default is not silently bypassed");
    std::filesystem::remove(primary);
    std::filesystem::remove(directory / alternate);
    std::filesystem::create_directory(directory / alternate);
    expect(beside_exe, "a directory is not an alternate ROM file");
    std::filesystem::remove_all(directory);
    if (failures == 0) std::puts("ROM paths: all cases passed");
    return failures == 0 ? 0 : 1;
}
