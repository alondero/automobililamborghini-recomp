// Lock-scope regression for the SDL audio sink (issue #401). SDL fakes park a
// thread inside a slow call; the test then proves the other threads still get
// through. A lock held across the parked call shows up as a timeout.
#include "lambo_audio.h"

#include <SDL.h>
#include <ultramodern/ultramodern.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr auto k_deadline = std::chrono::seconds(5);

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::fflush(stderr);
    // Other threads may be parked on audio locks; skip static destructors.
    std::_Exit(1);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

// Parks the first caller after arm() until release().
class Gate {
public:
    void arm() {
        std::lock_guard lock(mutex_);
        armed_ = true;
        entered_ = false;
        released_ = false;
    }
    void pass() {
        std::unique_lock lock(mutex_);
        if (!armed_) return;
        armed_ = false;
        entered_ = true;
        cv_.notify_all();
        cv_.wait(lock, [this] { return released_; });
    }
    bool wait_entered() {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, k_deadline, [this] { return entered_; });
    }
    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool armed_ = false;
    bool entered_ = false;
    bool released_ = false;
};

Gate put_gate;
Gate close_gate;
std::mutex fake_mutex;
SDL_AudioDeviceID next_device = 2;
SDL_AudioStatus device_status = SDL_AUDIO_PLAYING;
std::map<SDL_AudioDeviceID, int> queued_per_device;
int streams_created = 0;
int streams_freed = 0;
int closed_devices = 0;
std::atomic<int> stream_calls_in_flight{0};
std::atomic<bool> stream_freed_while_in_use{false};
std::vector<uint8_t> stream_pending;
int stream_storage[8];

// Runs fn on a helper thread and reports whether it finished in time. A helper
// that stays blocked is detached; the test fails before it could be joined.
bool finishes(const std::function<void()>& fn) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([fn, done] {
        fn();
        done->store(true);
    });
    const auto until = std::chrono::steady_clock::now() + k_deadline;
    while (!done->load() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!done->load()) {
        worker.detach();
        return false;
    }
    worker.join();
    return true;
}

int queued_to(SDL_AudioDeviceID device) {
    std::lock_guard lock(fake_mutex);
    const auto it = queued_per_device.find(device);
    return it == queued_per_device.end() ? 0 : it->second;
}

} // namespace

extern "C" int SDLCALL SDL_InitSubSystem(Uint32) { return 0; }
extern "C" int SDLCALL SDL_setenv(const char*, const char*, int) { return 0; }
extern "C" const char* SDLCALL SDL_GetError(void) { return "fake"; }

// The device always runs at 48 kHz; the game asks for 22050 Hz, so every submit
// goes through the persistent resampler stream.
extern "C" SDL_AudioDeviceID SDLCALL SDL_OpenAudioDevice(
    const char*, int, const SDL_AudioSpec* desired, SDL_AudioSpec* obtained, int) {
    std::lock_guard lock(fake_mutex);
    *obtained = *desired;
    obtained->freq = 48000;
    device_status = SDL_AUDIO_PLAYING;
    return next_device++;
}

extern "C" void SDLCALL SDL_PauseAudioDevice(SDL_AudioDeviceID, int) {}
extern "C" void SDLCALL SDL_ClearQueuedAudio(SDL_AudioDeviceID) {}

extern "C" int SDLCALL SDL_QueueAudio(SDL_AudioDeviceID device, const void*, Uint32) {
    std::lock_guard lock(fake_mutex);
    ++queued_per_device[device];
    return 0;
}

extern "C" SDL_AudioStatus SDLCALL SDL_GetAudioDeviceStatus(SDL_AudioDeviceID) {
    std::lock_guard lock(fake_mutex);
    return device_status;
}

extern "C" Uint32 SDLCALL SDL_GetQueuedAudioSize(SDL_AudioDeviceID) { return 0; }

extern "C" void SDLCALL SDL_CloseAudioDevice(SDL_AudioDeviceID) {
    close_gate.pass();
    std::lock_guard lock(fake_mutex);
    ++closed_devices;
}

extern "C" SDL_AudioStream* SDLCALL SDL_NewAudioStream(
    SDL_AudioFormat, Uint8, int, SDL_AudioFormat, Uint8, int) {
    std::lock_guard lock(fake_mutex);
    ++streams_created;
    return reinterpret_cast<SDL_AudioStream*>(&stream_storage[streams_created % 8]);
}

extern "C" int SDLCALL SDL_AudioStreamPut(SDL_AudioStream*, const void* data, int len) {
    ++stream_calls_in_flight;
    put_gate.pass();
    {
        std::lock_guard lock(fake_mutex);
        const auto* bytes = static_cast<const uint8_t*>(data);
        stream_pending.insert(stream_pending.end(), bytes, bytes + len);
    }
    --stream_calls_in_flight;
    return 0;
}

extern "C" int SDLCALL SDL_AudioStreamAvailable(SDL_AudioStream*) {
    std::lock_guard lock(fake_mutex);
    return int(stream_pending.size());
}

extern "C" int SDLCALL SDL_AudioStreamGet(SDL_AudioStream*, void* out, int len) {
    std::lock_guard lock(fake_mutex);
    const int got = std::min<int>(len, int(stream_pending.size()));
    std::memcpy(out, stream_pending.data(), size_t(got));
    stream_pending.erase(stream_pending.begin(), stream_pending.begin() + got);
    return got;
}

extern "C" void SDLCALL SDL_FreeAudioStream(SDL_AudioStream*) {
    if (stream_calls_in_flight.load() != 0) stream_freed_while_in_use.store(true);
    std::lock_guard lock(fake_mutex);
    ++streams_freed;
    stream_pending.clear();
}

int main() {
    ultramodern::audio_callbacks_t callbacks{};
    lambo::audio::get_callbacks(&callbacks);
    lambo::audio::init(48000);
    callbacks.set_frequency(22050);
    expect(next_device == 3, "init should open the first device");

    static int16_t samples[64] = {1, 2, 3, 4};
    auto submit = [&callbacks] { callbacks.queue_samples(samples, 64); };

    callbacks.queue_samples(samples, 64);
    expect(queued_to(2) == 1, "a converted buffer should reach the open device");

    // 1. Resampling must not hold the lock that the main-thread pump and the
    //    guest backpressure query take every frame.
    put_gate.arm();
    std::thread converting(submit);
    expect(put_gate.wait_entered(), "submit should reach SDL_AudioStreamPut");
    expect(finishes([] { lambo::audio::pump(); }),
           "pump() blocked behind SDL_AudioStreamPut on the audio thread");
    expect(finishes([&callbacks] { (void)callbacks.get_frames_remaining(); }),
           "get_frames_remaining() blocked behind SDL_AudioStreamPut");
    put_gate.release();
    converting.join();
    expect(queued_to(2) == 2, "the converted buffer is queued once conversion finishes");

    // 2. A device recovered while a buffer is converting must not have the
    //    converter freed under it, and the stale buffer must not reach the
    //    replacement device.
    put_gate.arm();
    std::thread stale(submit);
    expect(put_gate.wait_entered(), "submit should reach SDL_AudioStreamPut");
    {
        std::lock_guard lock(fake_mutex);
        device_status = SDL_AUDIO_STOPPED;
    }
    expect(finishes([] { lambo::audio::pump(); }),
           "device recovery blocked behind SDL_AudioStreamPut");
    expect(next_device == 4, "recovery should open a replacement device");
    expect(!stream_freed_while_in_use.load(),
           "recovery freed the resampler stream while the audio thread was using it");
    put_gate.release();
    stale.join();
    expect(!stream_freed_while_in_use.load(),
           "the resampler stream was freed while the audio thread was using it");
    expect(queued_to(3) == 0, "a buffer converted for the old device reached the new one");
    expect(queued_to(2) == 2, "a closed device must not receive more audio");
    callbacks.queue_samples(samples, 64);
    expect(queued_to(3) == 1, "the replacement device receives the next buffer");

    // 3. A slow SDL_CloseAudioDevice (it joins SDL's device thread) must not
    //    stall the guest audio callbacks.
    close_gate.arm();
    {
        std::lock_guard lock(fake_mutex);
        device_status = SDL_AUDIO_STOPPED;
    }
    std::thread recovering([] { lambo::audio::pump(); });
    expect(close_gate.wait_entered(), "pump() should close the stopped device");
    expect(finishes([&callbacks] { (void)callbacks.get_frames_remaining(); }),
           "get_frames_remaining() blocked behind SDL_CloseAudioDevice");
    expect(finishes(submit), "queue_samples() blocked behind SDL_CloseAudioDevice");
    close_gate.release();
    recovering.join();
    expect(next_device == 5, "recovery should open a replacement device after the close");
    callbacks.queue_samples(samples, 64);
    expect(queued_to(4) == 1, "the device opened after a slow close receives audio");

    lambo::audio::shutdown();
    expect(streams_created == streams_freed, "every resampler stream is freed");
    std::puts("audio lock-scope tests passed");
    return 0;
}
