#include "lambo_pak_storage.h"
#include "lambo_log.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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
    // The game-thread image. Writes go through lambo_pak_storage_write_block()
    // under image_mutex; the worker copies it once at the debounce deadline
    // instead of copying 32 KiB on every 32-byte Joybus block write.
    std::mutex image_mutex;
    const uint8_t* live_image = nullptr;
    std::chrono::steady_clock::time_point deadline{};
    uint64_t generation = 0;
    uint64_t copy_count = 0;
    uint64_t last_copy_us = 0;
    bool dirty = false;
    bool writing = false;
    bool flush_requested = false;
    bool stopping = false;
    bool exit_handler_registered = false;
    LamboPakIoResult last_result{};
#if defined(LAMBO_PAK_STORAGE_TESTING)
    LamboPakStorageWriteHook write = lambo_pak_write_file;
#endif
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

void publish_snapshot(StorageState& storage, std::unique_lock<std::mutex>& lock,
                      const std::array<uint8_t, LAMBO_PAK_SIZE>& image,
                      const std::string& path) {
    storage.writing = true;
    lock.unlock();
    const LamboPakIoResult result =
#if defined(LAMBO_PAK_STORAGE_TESTING)
        storage.write(path.c_str(), image.data());
#else
        lambo_pak_write_file(path.c_str(), image.data());
#endif
    if (!result.ok) LAMBO_LOG_ERROR("pak", "save failed: %s\n", result.error);
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

        if (!storage.stopping && !storage.flush_requested) {
            const uint64_t generation = storage.generation;
            const auto deadline = storage.deadline;
            storage.changed.wait_until(lock, deadline, [&storage, generation] {
                return storage.stopping || storage.flush_requested ||
                       storage.generation != generation;
            });
            if (!storage.stopping && !storage.flush_requested &&
                storage.generation != generation) {
                continue;
            }
        }

        if (!storage.dirty) continue;
        if (storage.live_image == nullptr) {
            storage.dirty = false;
            continue;
        }

        // Snapshot once at the quiet end of the burst. image_mutex also guards
        // game-thread block writes, so this copy cannot tear a 32-byte block.
        auto image = std::make_unique<std::array<uint8_t, LAMBO_PAK_SIZE>>();
        const auto copy_started = std::chrono::steady_clock::now();
        {
            std::lock_guard image_lock(storage.image_mutex);
            std::memcpy(image->data(), storage.live_image, LAMBO_PAK_SIZE);
        }
        storage.last_copy_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - copy_started).count());
        storage.copy_count++;
        const std::string path = storage.path;
        storage.dirty = false;
        LAMBO_LOG_DEBUG("pak", "snapshot copy #%llu took %llu us\n",
                        (unsigned long long)storage.copy_count,
                        (unsigned long long)storage.last_copy_us);
        publish_snapshot(storage, lock, *image, path);
        if (!storage.dirty) storage.flush_requested = false;
        storage.changed.notify_all();
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
    const bool first_dirty = !storage.dirty;
    storage.live_image = image;
    storage.dirty = true;
    storage.generation++;
    storage.deadline = std::chrono::steady_clock::now() + kSaveDebounce;
    // Later blocks in a burst only extend the deadline; the worker does not
    // need a wakeup until it either reaches the quiet deadline or flushes.
    if (first_dirty) storage.changed.notify_all();
}

extern "C" void lambo_pak_storage_write_block(uint8_t image[LAMBO_PAK_SIZE],
                                              unsigned address,
                                              const uint8_t* data,
                                              size_t size) {
    StorageState& storage = state();
    std::lock_guard lock(storage.image_mutex);
    for (size_t k = 0; k < size; k++) {
        image[(address + k) & (LAMBO_PAK_SIZE - 1u)] = data[k];
    }
}

extern "C" uint64_t lambo_pak_storage_copy_count(void) {
    StorageState& storage = state();
    std::lock_guard lock(storage.mutex);
    return storage.copy_count;
}

extern "C" LamboPakIoResult lambo_pak_storage_flush(void) {
    StorageState& storage = state();
    std::unique_lock lock(storage.mutex);
    if (storage.dirty || storage.writing) {
        start_worker(storage);
        storage.flush_requested = true;
        storage.changed.notify_all();
        storage.changed.wait(lock, [&storage] {
            return !storage.dirty && !storage.writing;
        });
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
        storage.flush_requested = false;
    }
}

#if defined(LAMBO_PAK_STORAGE_TESTING)
extern "C" void lambo_pak_storage_set_write_hook(LamboPakStorageWriteHook hook) {
    StorageState& storage = state();
    std::lock_guard lock(storage.mutex);
    storage.write = hook != nullptr ? hook : lambo_pak_write_file;
}
#endif
