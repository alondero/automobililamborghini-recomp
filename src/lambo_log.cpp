#include "lambo_log.h"
#include "lambo_paths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
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

std::filesystem::path log_directory() {
    if (const char* override_dir = lambo::paths::profile_environment("LAMBO_LOG_DIR");
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

#if defined(_WIN32)
bool has_standard_handle(DWORD stream) {
    const HANDLE handle = GetStdHandle(stream);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE &&
        GetFileType(handle) != FILE_TYPE_UNKNOWN;
}
#endif

void open_console([[maybe_unused]] bool allow_new_window) {
#if defined(_WIN32)
    const bool has_input = has_standard_handle(STD_INPUT_HANDLE);
    const bool has_output = has_standard_handle(STD_OUTPUT_HANDLE);
    const bool has_error = has_standard_handle(STD_ERROR_HANDLE);
    if (has_output && has_error) {
        g_stderr_sink.store(true, std::memory_order_release);
        return;
    }
    if (GetConsoleCP() == 0 && !AttachConsole(ATTACH_PARENT_PROCESS) &&
        (!allow_new_window || has_error || !AllocConsole())) return;

    // Keep redirected streams, including when only one was supplied.
    FILE* stream = nullptr;
    if (!has_input) (void)freopen_s(&stream, "CONIN$", "r", stdin);
    if (!has_output) (void)freopen_s(&stream, "CONOUT$", "w", stdout);
    const bool error_ready = has_error || freopen_s(&stream, "CONOUT$", "w", stderr) == 0;
    g_stderr_sink.store(error_ready, std::memory_order_release);
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

extern "C" void lambo_log_configure(bool console, LamboLogLevel level) {
    g_console_requested.store(console, std::memory_order_release);
    __atomic_store_n(&g_log_threshold, static_cast<int>(level), __ATOMIC_RELEASE);
    lambo_log_enabled = level >= LAMBO_LEVEL_DEBUG;
}

extern "C" void lambo_log_open_parent_console(void) {
    open_console(false);
}

extern "C" bool lambo_log_initialize(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) return g_file.good() || g_console_requested.load(std::memory_order_acquire);
    if (g_console_requested.load(std::memory_order_acquire)) open_console(true);
#if defined(_WIN32)
    if (!g_stderr_sink.load(std::memory_order_acquire)) {
        g_stderr_sink.store(has_standard_handle(STD_ERROR_HANDLE), std::memory_order_release);
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
            g_path = lambo::paths::path_to_utf8(path);
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
