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

    {
        char a0[] = "game";
        char a1[] = "--log-level=bogus";
        char* args[] = {a0, a1};
        expect(!lambo_log_parse_args(2, args), "invalid log level is rejected");
        expect(std::string(lambo_log_last_error()).find("must be") != std::string::npos,
               "invalid log level reports a useful error");
    }

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "lambo-log-test";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    set_env("LAMBO_LOG_DIR", directory.string());

    char a0[] = "game";
    char a1[] = "--verbose";
    char a2[] = "--log-level=error";
    char* args[] = {a0, a1, a2};
    expect(lambo_log_parse_args(3, args), "valid log arguments are accepted");
    expect(lambo_log_level() == LAMBO_LEVEL_ERROR,
           "last log level argument wins deterministically");
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
    clear_env("LAMBO_LOG_DIR");
    std::filesystem::remove_all(directory, ec);
    return failures == 0 ? 0 : 1;
}
