#include "lambo_savestate_io.h"

#include "lambo_log.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace {

uint64_t monotonic_us() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

struct Job {
    std::string path;
    uint8_t* data = nullptr;
    size_t size = 0;

    Job() = default;
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    Job(Job&& other) noexcept
        : path(std::move(other.path)), data(other.data), size(other.size) {
        other.data = nullptr;
        other.size = 0;
    }
    Job& operator=(Job&& other) noexcept {
        if (this != &other) {
            std::free(data);
            path = std::move(other.path);
            data = other.data;
            size = other.size;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }
    ~Job() { std::free(data); }
};

struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    Job pending;
    bool has_pending = false;
    bool writing = false;
    bool stopping = false;
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

bool write_job(const Job& job) {
    const std::string tmp = job.path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) {
        LAMBO_LOG_ERROR("state", "save: cannot open %s for write\n", tmp.c_str());
        return false;
    }
    bool ok = std::fwrite(job.data, 1, job.size, f) == job.size;
    // Flush before rename so the rename publishes a fully-written file.
    ok = (std::fflush(f) == 0) && ok;
    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        LAMBO_LOG_ERROR("state", "save: write failed for %s\n", job.path.c_str());
        std::remove(tmp.c_str());
        return false;
    }
    // Publish atomically. POSIX rename() replaces the target in place; Windows
    // rename() fails if the target exists, so only there do we remove-then-retry.
    // Crucially we do NOT remove the existing good save until the first rename
    // has failed, and on final failure we KEEP the .tmp (it holds the full
    // snapshot) -- so a previously-working save is never lost with nothing to
    // show for it.
    if (std::rename(tmp.c_str(), job.path.c_str()) != 0) {
        std::remove(job.path.c_str());
        if (std::rename(tmp.c_str(), job.path.c_str()) != 0) {
            LAMBO_LOG_ERROR("state", "save: could not publish %s; snapshot kept at %s\n",
                            job.path.c_str(), tmp.c_str());
            return false;
        }
    }
    LAMBO_LOG_DEBUG("state", "published %zu bytes to %s\n", job.size, job.path.c_str());
    return true;
}

void worker_main() {
    State& s = state();
    std::unique_lock lock(s.mutex);
    for (;;) {
        s.changed.wait(lock, [&s] { return s.has_pending || s.stopping; });
        if (!s.has_pending) {
            if (s.stopping) return;
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
        if (s.stopping && !s.has_pending) return;
    }
}

void start_worker(State& s) {
    if (s.worker.joinable()) return;
    s.stopping = false;
    s.worker = std::thread(worker_main);
    if (!s.exit_handler_registered) {
        std::atexit(lambo_savestate_io_shutdown);
        s.exit_handler_registered = true;
    }
}

} // namespace

extern "C" int lambo_savestate_io_submit(const char* path, uint8_t* data, size_t size) {
    State& s = state();
    if (path == nullptr || path[0] == '\0' || data == nullptr || size == 0) {
        std::free(data);
        LAMBO_LOG_ERROR("state", "save: invalid asynchronous job\n");
        return -1;
    }

    std::lock_guard lock(s.mutex);
    start_worker(s);
    if (s.has_pending) {
        // Keep memory bounded under repeated F7 presses: the newest image wins,
        // and the worker will still publish the last image requested.
        LAMBO_LOG_DEBUG("state", "save: replacing a queued snapshot\n");
    }
    s.pending = Job{};
    s.pending.path = path;
    s.pending.data = data;
    s.pending.size = size;
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
    lambo_savestate_io_flush();
    {
        std::lock_guard lock(s.mutex);
        s.stopping = true;
        s.changed.notify_all();
    }
    if (s.worker.joinable()) s.worker.join();
    std::lock_guard lock(s.mutex);
    s.stopping = false;
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
