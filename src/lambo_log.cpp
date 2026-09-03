#include "lambo_log.h"
#include "lambo_paths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

std::atomic_bool g_console_requested{false};
std::atomic_bool g_stderr_sink{false};
bool g_initialized = false;
std::mutex g_mutex;
std::ofstream g_file;
std::string g_path;
std::string g_error;

const char* level_name(LamboLogLevel level) {
    switch (level) {
        case LAMBO_LEVEL_ERROR: return "error";
        case LAMBO_LEVEL_WARN:  return "warn";
        case LAMBO_LEVEL_INFO:  return "info";
        case LAMBO_LEVEL_DEBUG: return "debug";
        case LAMBO_LEVEL_TRACE: return "trace";
    }
    return "unknown";
}

bool parse_level(const char* value, LamboLogLevel* out) {
    if (value == nullptr || out == nullptr) return false;
    struct Name { const char* text; LamboLogLevel level; };
    constexpr Name names[] = {
        {"error", LAMBO_LEVEL_ERROR}, {"warn", LAMBO_LEVEL_WARN},
        {"warning", LAMBO_LEVEL_WARN}, {"info", LAMBO_LEVEL_INFO},
        {"debug", LAMBO_LEVEL_DEBUG}, {"trace", LAMBO_LEVEL_TRACE}
    };
    for (const Name& name : names) {
        if (std::strcmp(value, name.text) == 0) {
            *out = name.level;
            return true;
        }
    }
    return false;
}

std::filesystem::path log_directory() {
    if (const char* override_dir = std::getenv("LAMBO_LOG_DIR");
        override_dir != nullptr && override_dir[0] != '\0') {
        return std::filesystem::path{override_dir};
    }
    return lambo::paths::app_state_dir() / "logs";
}

unsigned long process_id() {
#if defined(_WIN32)
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

void open_console() {
#if defined(_WIN32)
    // A test harness or terminal may already have supplied a pipe/console
    // handle even when GetConsoleWindow() is null. Preserve that inherited
    // sink; only create a new window for an Explorer-style launch.
    HANDLE error_handle = GetStdHandle(STD_ERROR_HANDLE);
    const bool inherited_sink = error_handle != nullptr &&
        error_handle != INVALID_HANDLE_VALUE && GetFileType(error_handle) != FILE_TYPE_UNKNOWN;
    // Existing consoles already have correctly inherited CRT handles. This
    // also preserves deliberate stdout/stderr redirection from a shell.
    if (GetConsoleWindow() != nullptr || inherited_sink) {
        g_stderr_sink.store(true, std::memory_order_release);
        return;
    }
    {
        // A GUI process started from cmd/PowerShell often has no inherited
        // standard handles. Attach to that parent before falling back to a
        // new console, so --console behaves like a normal CLI switch.
        if (AttachConsole(ATTACH_PARENT_PROCESS) == 0 && AllocConsole() == 0) return;
    }
    g_stderr_sink.store(true, std::memory_order_release);
    FILE* stream = nullptr;
    (void)freopen_s(&stream, "CONIN$", "r", stdin);
    (void)freopen_s(&stream, "CONOUT$", "w", stdout);
    (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    clearerr(stdin);
    clearerr(stdout);
    clearerr(stderr);
#endif
}

void prune_old_logs(const std::filesystem::path& directory) {
    std::vector<std::filesystem::directory_entry> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        const auto filename = entry.path().filename();
        if (entry.is_regular_file(ec) && filename.extension() == ".log" &&
            filename.stem().string().rfind("lamborghini-", 0) == 0)
            files.push_back(entry);
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path().filename() > b.path().filename();
    });
    for (size_t i = 10; i < files.size(); ++i) (void)std::filesystem::remove(files[i], ec);
}

} // namespace

extern "C" {
volatile int g_log_threshold = LAMBO_LEVEL_WARN;
}
bool lambo_log_enabled = false;

extern "C" bool lambo_log_parse_args(int argc, char** argv) {
    g_error.clear();
    g_console_requested = false;
    LamboLogLevel requested = LAMBO_LEVEL_WARN;
    bool valid = true;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == nullptr) continue;
        if (std::strcmp(arg, "--console") == 0) {
            g_console_requested = true;
        } else if (std::strcmp(arg, "--verbose") == 0 ||
                   std::strcmp(arg, "--lambo-debug") == 0) {
            requested = LAMBO_LEVEL_DEBUG;
        } else if (std::strncmp(arg, "--log-level=", 12) == 0) {
            if (!parse_level(arg + 12, &requested)) {
                g_error = "--log-level must be error, warn, info, debug, or trace";
                valid = false;
            }
        } else if (std::strcmp(arg, "--log-level") == 0) {
            if (i + 1 >= argc) {
                g_error = "--log-level requires error, warn, info, debug, or trace";
                valid = false;
            } else if (!parse_level(argv[++i], &requested)) {
                g_error = "--log-level requires error, warn, info, debug, or trace";
                valid = false;
            }
        }
    }
    __atomic_store_n(&g_log_threshold, static_cast<int>(requested), __ATOMIC_RELEASE);
    lambo_log_enabled = requested >= LAMBO_LEVEL_DEBUG;
    return valid;
}

extern "C" void lambo_log_parse_flag(int argc, char** argv) {
    (void)lambo_log_parse_args(argc, argv);
}

extern "C" const char* lambo_log_last_error(void) { return g_error.c_str(); }

extern "C" bool lambo_log_initialize(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) return g_file.good() || g_console_requested.load(std::memory_order_acquire);
    if (g_console_requested.load(std::memory_order_acquire)) open_console();
#if defined(_WIN32)
    if (!g_stderr_sink.load(std::memory_order_acquire)) {
        HANDLE error_handle = GetStdHandle(STD_ERROR_HANDLE);
        g_stderr_sink.store(error_handle != nullptr && error_handle != INVALID_HANDLE_VALUE &&
                                GetFileType(error_handle) != FILE_TYPE_UNKNOWN,
                            std::memory_order_release);
    }
#else
    g_stderr_sink.store(true, std::memory_order_release);
#endif
    const std::filesystem::path directory = log_directory();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (!ec) {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream name;
        name << "lamborghini-" << std::put_time(&tm, "%Y%m%d-%H%M%S")
             << "-" << process_id() << ".log";
        const std::filesystem::path path = directory / name.str();
        g_file.open(path, std::ios::out | std::ios::app);
        if (g_file.good()) {
            g_path = path.string();
            prune_old_logs(directory);
        }
    }
    g_initialized = true;
    return g_file.good() || g_stderr_sink.load(std::memory_order_acquire);
}

extern "C" const char* lambo_log_path(void) {
    // Return a per-thread snapshot so callers never retain a pointer into the
    // mutable path string while shutdown/reinitialization holds g_mutex.
    thread_local std::string path_snapshot;
    std::lock_guard<std::mutex> lock(g_mutex);
    path_snapshot = g_path;
    return path_snapshot.c_str();
}
extern "C" void lambo_log_shutdown(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file.good()) g_file.flush();
    g_file.close();
    g_path.clear();
    g_stderr_sink.store(false, std::memory_order_release);
    g_initialized = false;
}
extern "C" bool lambo_log_console_requested(void) {
    return g_console_requested.load(std::memory_order_acquire);
}
extern "C" LamboLogLevel lambo_log_level(void) {
    return static_cast<LamboLogLevel>(__atomic_load_n(&g_log_threshold, __ATOMIC_ACQUIRE));
}

extern "C" void lambo_log_write(LamboLogLevel level, const char* tag, const char* format, ...) {
    if (!lambo_log_would_emit(level)) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) return;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char timestamp[32]{};
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm);
    char message[4096]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    std::ostringstream line;
    line << '[' << timestamp << "] [" << level_name(level) << "] ["
         << (tag != nullptr ? tag : "log") << "] " << message;
    const std::string text = line.str();
    if (g_file.good()) g_file << text;
    if (g_stderr_sink.load(std::memory_order_acquire))
        std::fwrite(text.data(), 1, text.size(), stderr);
    if (level == LAMBO_LEVEL_ERROR) {
        if (g_file.good()) g_file.flush();
        if (g_stderr_sink.load(std::memory_order_acquire)) std::fflush(stderr);
    }
}
