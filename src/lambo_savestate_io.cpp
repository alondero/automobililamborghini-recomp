#include "lambo_savestate_io.h"

#include "lambo_log.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

uint64_t monotonic_us() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

fs::path path_from_utf8(const char* text) {
#if defined(_WIN32)
    const auto* begin = reinterpret_cast<const char8_t*>(text);
    return fs::path(std::u8string(begin, begin + std::strlen(text)));
#else
    return fs::path(text);
#endif
}

std::string path_to_utf8(const fs::path& path) {
#if defined(_WIN32)
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
#else
    return path.string();
#endif
}

struct Job {
    fs::path path;
    std::unique_ptr<uint8_t, decltype(&std::free)> data{nullptr, &std::free};
    size_t size = 0;
};

enum class WorkerState {
    Idle,
    Running,
    Stopping,
};

struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    Job pending;
    WorkerState worker_state = WorkerState::Idle;
    bool has_pending = false;
    bool writing = false;
    bool shutdown_in_progress = false;
    bool exit_handler_registered = false;
#if defined(LAMBO_SAVESTATE_IO_TESTING)
    LamboSavestateBeforeWriteHook before_write = nullptr;
#endif
};

// Leaked rather than destroyed: atexit joins the worker first, and keeping the
// state alive avoids a static-destruction race with a late exit path.
State& state() {
    static State* value = new State;
    return *value;
}

bool replace_file(const fs::path& temporary, const fs::path& destination) {
#if defined(_WIN32)
    // CRT rename() cannot replace an existing target. MoveFileEx is the
    // atomic replacement primitive (same call shape as lambo_pak_io.c).
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(temporary.c_str(), destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        const DWORD code = GetLastError();
        if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION) {
            LAMBO_LOG_ERROR("state", "save: cannot publish %s (Win32 error %lu); "
                            "snapshot kept at %s\n",
                            path_to_utf8(destination).c_str(),
                            static_cast<unsigned long>(code),
                            path_to_utf8(temporary).c_str());
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10 * (attempt + 1)));
    }
    LAMBO_LOG_ERROR("state", "save: cannot publish %s; snapshot kept at %s\n",
                    path_to_utf8(destination).c_str(),
                    path_to_utf8(temporary).c_str());
    return false;
#else
    std::error_code error;
    fs::rename(temporary, destination, error);
    if (error) {
        LAMBO_LOG_ERROR("state", "save: cannot publish %s (%s); snapshot kept at %s\n",
                        path_to_utf8(destination).c_str(),
                        error.message().c_str(),
                        path_to_utf8(temporary).c_str());
        return false;
    }
    return true;
#endif
}

bool write_job(const Job& job) {
    fs::path temporary = job.path;
    temporary += ".tmp";

    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) {
        LAMBO_LOG_ERROR("state", "save: cannot open %s for write\n",
                        path_to_utf8(temporary).c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(job.data.get()),
              static_cast<std::streamsize>(job.size));
    out.flush();
    if (!out) {
        LAMBO_LOG_ERROR("state", "save: write failed for %s\n",
                        path_to_utf8(job.path).c_str());
        out.close();
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    out.close();
    if (!out) {
        LAMBO_LOG_ERROR("state", "save: could not close %s\n",
                        path_to_utf8(temporary).c_str());
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    if (!replace_file(temporary, job.path)) return false;
    LAMBO_LOG_DEBUG("state", "published %zu bytes to %s\n",
                    job.size, path_to_utf8(job.path).c_str());
    return true;
}

void worker_main() {
    State& s = state();
    std::unique_lock lock(s.mutex);
    for (;;) {
        s.changed.wait(lock, [&s] {
            return s.has_pending || s.worker_state == WorkerState::Stopping;
        });
        if (!s.has_pending) {
            if (s.worker_state == WorkerState::Stopping) return;
            continue;
        }

        Job job = std::move(s.pending);
        s.has_pending = false;
        s.writing = true;
#if defined(LAMBO_SAVESTATE_IO_TESTING)
        const LamboSavestateBeforeWriteHook before_write = s.before_write;
#endif
        lock.unlock();

#if defined(LAMBO_SAVESTATE_IO_TESTING)
        if (before_write != nullptr) before_write();
#endif
        const uint64_t started = monotonic_us();
        (void)write_job(job);
        LAMBO_LOG_DEBUG("state", "save write took %llu us\n",
                        (unsigned long long)(monotonic_us() - started));

        lock.lock();
        s.writing = false;
        s.changed.notify_all();
        if (s.worker_state == WorkerState::Stopping && !s.has_pending) return;
    }
}

bool start_worker(State& s) {
    if (s.worker_state == WorkerState::Running) return true;
    if (s.worker_state == WorkerState::Stopping || s.shutdown_in_progress) return false;
    s.worker = std::thread(worker_main);
    s.worker_state = WorkerState::Running;
    if (!s.exit_handler_registered) {
        std::atexit(lambo_savestate_io_shutdown);
        s.exit_handler_registered = true;
    }
    return true;
}

} // namespace

extern "C" int lambo_savestate_io_submit(const char* path, uint8_t* data, size_t size) {
    State& s = state();
    if (path == nullptr || path[0] == '\0' || data == nullptr || size == 0) {
        std::free(data);
        LAMBO_LOG_ERROR("state", "save: invalid asynchronous job\n");
        return -1;
    }

    // Own the buffer before any allocation that can throw, so every early
    // return releases the caller's malloc'd image.
    Job incoming;
    incoming.data.reset(data);
    incoming.size = size;
    try {
        incoming.path = path_from_utf8(path);
    } catch (const std::exception& error) {
        LAMBO_LOG_ERROR("state", "save: invalid path %s (%s)\n", path, error.what());
        return -1;
    }

    std::lock_guard lock(s.mutex);
    if (!start_worker(s)) {
        LAMBO_LOG_WARN("state", "save: writer is shutting down; dropping %s\n",
                       path_to_utf8(incoming.path).c_str());
        return -1;
    }
    if (s.has_pending) {
        // Keep memory bounded under repeated F7 presses: the newest image wins,
        // and the worker will still publish the last image requested.
        LAMBO_LOG_DEBUG("state", "save: replacing a queued snapshot\n");
    }
    s.pending = std::move(incoming);
    s.has_pending = true;
    s.changed.notify_one();
    return 0;
}

extern "C" void lambo_savestate_io_flush(void) {
    State& s = state();
    std::unique_lock lock(s.mutex);
    if (!s.has_pending && !s.writing) return;
    s.changed.notify_all();
    s.changed.wait(lock, [&s] { return !s.has_pending && !s.writing; });
}

extern "C" void lambo_savestate_io_shutdown(void) {
    State& s = state();
    std::unique_lock lock(s.mutex);
    if (s.shutdown_in_progress) {
        s.changed.wait(lock, [&s] { return !s.shutdown_in_progress; });
        return;
    }
    s.shutdown_in_progress = true;
    lock.unlock();

    // Drain accepted work before preventing new submissions. A concurrent
    // submit that set has_pending before we marked shutdown is still written
    // by the worker because stopping only exits once the queue is empty.
    lambo_savestate_io_flush();

    lock.lock();
    s.worker_state = WorkerState::Stopping;
    s.changed.notify_all();
    lock.unlock();

    if (s.worker.joinable()) s.worker.join();

    lock.lock();
    s.worker_state = WorkerState::Idle;
    s.shutdown_in_progress = false;
    s.changed.notify_all();
}

extern "C" uint64_t lambo_savestate_io_now_us(void) {
    return monotonic_us();
}

#if defined(LAMBO_SAVESTATE_IO_TESTING)
extern "C" void lambo_savestate_io_set_before_write_hook(
    LamboSavestateBeforeWriteHook hook) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.before_write = hook;
}
#endif
