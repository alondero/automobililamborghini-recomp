#include "lambo_rom_path.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string_view>

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("lambo-rom-path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    int failures = 0;
    auto expect = [&](const char* filename, const char* message) {
        if (std::string_view(lambo::find_default_rom_filename(directory)) != filename) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };
    constexpr const char* alternate = "Automobili Lamborghini (USA).n64";
    expect(lambo::default_rom_filename, "no files retains the legacy missing-ROM path");
    std::ofstream(directory / alternate).put('x');
    expect(alternate, "n64 is found without renaming");
    std::ofstream(directory / lambo::default_rom_filename).put('x');
    expect(lambo::default_rom_filename, "z64 wins when both exist, even with invalid contents");
    std::filesystem::remove(directory / lambo::default_rom_filename);
    std::filesystem::create_directory(directory / lambo::default_rom_filename);
    expect(lambo::default_rom_filename, "an invalid existing default is not silently bypassed");
    std::filesystem::remove(directory / lambo::default_rom_filename);
    std::filesystem::remove(directory / alternate);
    std::filesystem::create_directory(directory / alternate);
    expect(lambo::default_rom_filename, "a directory is not an alternate ROM file");
    std::filesystem::remove(directory / alternate);
    std::filesystem::remove(directory);
    if (failures == 0) std::puts("ROM paths: all cases passed");
    return failures == 0 ? 0 : 1;
}
