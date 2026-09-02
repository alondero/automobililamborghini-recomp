#include "lambo_pak_storage.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr auto kSaveDebounce = std::chrono::milliseconds(250);

struct StorageState {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    std::string path{"lambo_controller_pak.mpk"};
    std::array<uint8_t, LAMBO_PAK_SIZE> pending{};
    std::chrono::steady_clock::time_point deadline{};
    uint64_t generation = 0;
    bool dirty = false;
    bool writing = false;
    bool stopping = false;
    bool exit_handler_registered = false;
    LamboPakIoResult last_result{};
};

StorageState& state() {
    static StorageState value;
    return value;
}

LamboPakIoResult idle_result() {
    LamboPakIoResult result{};
    result.ok = 1;
    result.format = LAMBO_PAK_FORMAT_RAW;
    result.container_size = LAMBO_PAK_SIZE;
    return result;
}

void publish_snapshot(StorageState& storage, std::unique_lock<std::mutex>& lock) {
    const auto image = storage.pending;
    const std::string path = storage.path;
    storage.dirty = false;
    storage.writing = true;
    lock.unlock();
    const LamboPakIoResult result = lambo_pak_write_file(path.c_str(), image.data());
    if (!result.ok) std::fprintf(stderr, "[pak] save failed: %s\n", result.error);
    lock.lock();
    storage.last_result = result;
    storage.writing = false;
    storage.changed.notify_all();
}

void storage_worker() {
    StorageState& storage = state();
    std::unique_lock lock(storage.mutex);
    for (;;) {
        storage.changed.wait(lock, [&storage] { return storage.dirty || storage.stopping; });
        if (!storage.dirty && storage.stopping) return;

        if (!storage.stopping) {
            const uint64_t generation = storage.generation;
            const auto deadline = storage.deadline;
            storage.changed.wait_until(lock, deadline, [&storage, generation] {
                return storage.stopping || storage.generation != generation;
            });
            if (!storage.stopping && storage.generation != generation) continue;
        }

        publish_snapshot(storage, lock);
        if (storage.stopping && !storage.dirty) return;
    }
}

void start_worker(StorageState& storage) {
    if (storage.worker.joinable()) return;
    storage.stopping = false;
    storage.worker = std::thread(storage_worker);
    if (!storage.exit_handler_registered) {
        std::atexit(lambo_pak_storage_shutdown);
        storage.exit_handler_registered = true;
    }
}

} // namespace

extern "C" void lambo_pak_storage_configure(const char* path) {
    if (path == nullptr || path[0] == '\0') return;
    lambo_pak_storage_shutdown();
    StorageState& storage = state();
    std::lock_guard lock(storage.mutex);
    storage.path = path;
    storage.last_result = idle_result();
}

extern "C" const char* lambo_pak_storage_path(void) {
    return state().path.c_str();
}

extern "C" LamboPakIoResult lambo_pak_storage_load(uint8_t image[LAMBO_PAK_SIZE]) {
    StorageState& storage = state();
    std::lock_guard lock(storage.mutex);
    return lambo_pak_read_file(storage.path.c_str(), image);
}

extern "C" void lambo_pak_storage_schedule_save(const uint8_t image[LAMBO_PAK_SIZE]) {
    StorageState& storage = state();
    std::lock_guard lock(storage.mutex);
    start_worker(storage);
    std::memcpy(storage.pending.data(), image, storage.pending.size());
    storage.dirty = true;
    storage.generation++;
    storage.deadline = std::chrono::steady_clock::now() + kSaveDebounce;
    storage.changed.notify_all();
}

extern "C" LamboPakIoResult lambo_pak_storage_flush(void) {
    StorageState& storage = state();
    std::unique_lock lock(storage.mutex);
    while (storage.writing) storage.changed.wait(lock);
    while (storage.dirty) {
        publish_snapshot(storage, lock);
        while (storage.writing) storage.changed.wait(lock);
    }
    return storage.last_result.ok ? storage.last_result :
           (storage.last_result.error[0] != '\0' ? storage.last_result : idle_result());
}

extern "C" void lambo_pak_storage_shutdown(void) {
    StorageState& storage = state();
    (void)lambo_pak_storage_flush();
    {
        std::lock_guard lock(storage.mutex);
        storage.stopping = true;
        storage.changed.notify_all();
    }
    if (storage.worker.joinable()) storage.worker.join();
    {
        std::lock_guard lock(storage.mutex);
        storage.stopping = false;
    }
}
