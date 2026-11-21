#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "lambo_log.h"

namespace {
void set_env(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void clear_env(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
}

int main() {
    int failures = 0;
    auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "lambo-log-test";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    set_env("LAMBO_LOG_DIR", directory.string());

    lambo_log_configure(true, LAMBO_LEVEL_DEBUG);
    expect(lambo_log_console_requested() && lambo_log_enabled,
           "debug logging can be configured with a console");
    lambo_log_configure(false, LAMBO_LEVEL_ERROR);
    expect(!lambo_log_console_requested() && lambo_log_level() == LAMBO_LEVEL_ERROR,
           "logger uses the configured threshold and console setting");
    expect(!lambo_log_would_emit(LAMBO_LEVEL_WARN), "error threshold filters warnings");
    expect(lambo_log_would_emit(LAMBO_LEVEL_ERROR), "error threshold keeps errors");
    expect(lambo_log_initialize(), "logger creates the session file");
    expect(std::string(lambo_log_path()).find("lamborghini-") != std::string::npos,
           "logger exposes the session file path");

    lambo_log_write(LAMBO_LEVEL_WARN, "test", "hidden warning\n");
    lambo_log_write(LAMBO_LEVEL_ERROR, "test", "visible error %d\n", 7);
    std::ifstream input(lambo_log_path());
    std::stringstream contents;
    contents << input.rdbuf();
    const std::string text = contents.str();
    expect(text.find("hidden warning") == std::string::npos,
           "filtered messages do not reach the file");
    expect(text.find("[error] [test] visible error 7") != std::string::npos,
           "enabled messages include level and tag");

    input.close();
    lambo_log_shutdown();
    expect(lambo_log_initialize(), "logger can be initialized again after shutdown");
    lambo_log_shutdown();
    clear_env("LAMBO_LOG_DIR");
    std::filesystem::remove_all(directory, ec);
    return failures == 0 ? 0 : 1;
}
