#include "lambo_launch_options.h"
#include "lambo_pak_io.h"
#include "lambo_paths.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <vector>

int main() {
    using lambo::LaunchAction;
    using lambo::StartupMode;
    lambo::LaunchOptions options;
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };
    auto parse = [&](std::initializer_list<const char*> arguments) {
        std::vector<const char*> argv{"game"};
        argv.insert(argv.end(), arguments.begin(), arguments.end());
        return lambo::parse_launch_options(static_cast<int>(argv.size()), argv.data(), options);
    };

    expect(parse({}).empty() && options.action == LaunchAction::Run &&
           options.rom.empty() && !options.fullscreen && !options.startup_mode,
           "no arguments preserve configured defaults");
    expect(parse({"--rom", "library/USA ROM.z64", "--fullscreen", "--play", "--data-dir=My Profile"}).empty() &&
           options.rom == "library/USA ROM.z64" && options.data_dir == "My Profile" &&
           options.fullscreen == true && options.startup_mode == StartupMode::Automatic,
           "one command selects the ROM, fullscreen, direct play, and a profile");
    expect(parse({"--fullscreen", "--windowed", "--play", "--launcher", "legacy.z64"}).empty() &&
           options.rom == "legacy.z64" && options.fullscreen == false &&
           options.startup_mode == StartupMode::InteractiveLauncher,
           "the last window and startup option wins with a positional ROM");
    expect(parse({"--rom=-demo.n64"}).empty() && options.rom == "-demo.n64",
           "an explicit ROM can start with a dash");
    expect(parse({"--", "--portable"}).empty() && options.rom == "--portable" && !options.portable,
           "the end-of-options marker protects a positional ROM");
    expect(parse({"-h"}).empty() && options.action == LaunchAction::Help, "short help parses");
    expect(parse({"--help"}).empty() && options.action == LaunchAction::Help, "long help parses");
    expect(parse({"--version"}).empty() && options.action == LaunchAction::Version, "version parses");

    expect(parse({"--portable", "--console", "--lambo-debug", "--log-level", "trace", "--verbose",
                  "--log-level=warning", "--controller-pak", "save.mpk", "--track-patch=track.altrk"}).empty() &&
           options.portable && options.console && options.log_level == LAMBO_LEVEL_WARN &&
           options.controller_pak == "save.mpk" && options.track_patch == "track.altrk" && options.rom.empty(),
           "legacy switches retain their values and logging precedence");

    expect(parse({"--fullscreeen"}).find("--fullscreeen") != std::string::npos, "unknown flags are reported");
    expect(parse({"--rom", "--fullscreen"}).find("--rom requires") != std::string::npos,
           "another switch cannot be consumed as the ROM path");
    expect(!parse({"--rom="}).empty(), "empty equals values are rejected");
    expect(!parse({"--log-level"}).empty() && !parse({"--log-level=bogus"}).empty(),
           "missing and invalid log levels are rejected");
    expect(!parse({"--rom=explicit.z64", "second.z64"}).empty() &&
           !parse({"first.z64", "--rom=explicit.z64"}).empty(), "two ROM paths are ambiguous");
    expect(!parse({"--data-dir", "profile", "--portable"}).empty(), "profile modes conflict");
    expect(!parse({"--import-save", "source.mpk", "--controller-pak", "active.mpk"}).empty(),
           "the existing Pak mode conflict is rejected");

    const auto pak_path = std::filesystem::temp_directory_path() /
        ("lambo-launch-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mpk");
    const auto pak_arg = lambo::paths::path_to_utf8(pak_path);
    {
        char bytes[LAMBO_PAK_SIZE]{};
        // Inverted checksum for an otherwise empty Pak ID block.
        bytes[0x3e] = static_cast<char>(0xff);
        bytes[0x3f] = static_cast<char>(0xf2);
        std::ofstream(pak_path, std::ios::binary).write(bytes, sizeof(bytes));
    }
    expect(parse({pak_arg.c_str(), "legacy.z64"}).empty() && options.rom == "legacy.z64" &&
           options.import_save == pak_arg, "a dropped Pak remains a positional save import");
    expect(parse({"--rom", pak_arg.c_str()}).empty() && options.rom == pak_arg && options.import_save.empty(),
           "--rom bypasses positional Pak detection");
    std::filesystem::remove(pak_path);
    return failures == 0 ? 0 : 1;
}
