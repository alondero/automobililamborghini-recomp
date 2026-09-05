#include "lambo_audio.h"

#include <SDL.h>
#include <ultramodern/ultramodern.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

int open_attempts = 0;
int queued_buffers = 0;
int queue_attempts = 0;
int closed_devices = 0;
SDL_AudioStatus device_status = SDL_AUDIO_PLAYING;
Uint32 queued_bytes = 0;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

// SDL boundary fakes. The first default-device open fails and the next attempt
// succeeds. Keeping the rest of SDL fake makes the regression deterministic on
// headless CI runners.
extern "C" int SDLCALL SDL_InitSubSystem(Uint32) {
    return 0;
}

extern "C" int SDLCALL SDL_setenv(const char*, const char*, int) {
    return 0;
}

extern "C" const char* SDLCALL SDL_GetError(void) {
    return "injected transient audio-device failure";
}

extern "C" SDL_AudioDeviceID SDLCALL SDL_OpenAudioDevice(
    const char*, int, const SDL_AudioSpec* desired, SDL_AudioSpec* obtained, int) {
    ++open_attempts;
    if (open_attempts == 1) {
        return 0;
    }

    *obtained = *desired;
    device_status = SDL_AUDIO_PLAYING;
    queued_bytes = 0;
    return 2;
}

extern "C" void SDLCALL SDL_PauseAudioDevice(SDL_AudioDeviceID, int) {}

extern "C" int SDLCALL SDL_QueueAudio(SDL_AudioDeviceID, const void*, Uint32) {
    ++queue_attempts;
    ++queued_buffers;
    return 0;
}

extern "C" SDL_AudioStatus SDLCALL SDL_GetAudioDeviceStatus(SDL_AudioDeviceID) {
    return device_status;
}

extern "C" Uint32 SDLCALL SDL_GetQueuedAudioSize(SDL_AudioDeviceID) {
    return queued_bytes;
}

extern "C" void SDLCALL SDL_CloseAudioDevice(SDL_AudioDeviceID) {
    ++closed_devices;
}

extern "C" SDL_AudioStream* SDLCALL SDL_NewAudioStream(
    SDL_AudioFormat, Uint8, int, SDL_AudioFormat, Uint8, int) {
    return nullptr;
}

extern "C" int SDLCALL SDL_AudioStreamPut(SDL_AudioStream*, const void*, int) {
    return -1;
}

extern "C" int SDLCALL SDL_AudioStreamAvailable(SDL_AudioStream*) {
    return 0;
}

extern "C" int SDLCALL SDL_AudioStreamGet(SDL_AudioStream*, void*, int) {
    return -1;
}

extern "C" void SDLCALL SDL_FreeAudioStream(SDL_AudioStream*) {}

int main() {
    ultramodern::audio_callbacks_t callbacks{};
    lambo::audio::get_callbacks(&callbacks);

    int16_t samples[] = {100, -100, 200, -200};
    callbacks.queue_samples(samples, 4);
    expect(open_attempts == 0, "callbacks must not open SDL before explicit initialization");

    lambo::audio::init(48000);
    expect(open_attempts == 1, "startup should make one device-open attempt");

    callbacks.queue_samples(samples, 4);

    expect(open_attempts == 1, "submission must not retry or open SDL on the audio callback");
    lambo::audio::pump();
    expect(open_attempts == 2, "the main-thread pump should retry after a transient startup failure");
    callbacks.queue_samples(samples, 4);
    expect(queued_buffers == 1,
           "the first buffer after recovery should be queued");

    // SDL2 reports a removed output device as STOPPED even though its software
    // queue can continue accepting and counting buffers successfully. Queue-depth
    // backpressure must not prevent the guest from making the next submission.
    queued_bytes = 65536;
    device_status = SDL_AUDIO_STOPPED;
    lambo::audio::pump();
    expect(closed_devices == 1, "the main-thread pump should close a stopped SDL device");
    expect(open_attempts == 3, "a stopped SDL device should reopen the current default device");
    expect(callbacks.get_frames_remaining() == 0,
           "the replacement device should start with an empty queue");

    callbacks.queue_samples(samples, 4);
    expect(queued_buffers == 2, "the replacement device should receive the current buffer");

    lambo::audio::shutdown();
    std::puts("audio device recovery tests passed");
    return 0;
}
