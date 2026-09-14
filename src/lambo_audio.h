// SPDX-License-Identifier: GPL-3.0-or-later
// Host-side audio boundary. Ownership and failure behavior are documented in
// docs/architecture.md under the audio and thread sections.
//
// Lamborghini's recompiled game pushes 16-bit signed stereo PCM into the AI
// buffer via osAiSetNextBuffer; the N64ModernRuntime HLE for that primitive
// (librecomp/src/ai.cpp) routes the buffer into ultramodern::queue_audio_buffer,
// which forwards to whatever the consuming project registered in
// audio_callbacks_t. We register a SDL2 push-audio backend here. The pattern
// The SDL device is a host sink. It must not become a second owner of guest
// audio state.
#ifndef LAMBO_AUDIO_H
#define LAMBO_AUDIO_H

#include <cstdint>

namespace ultramodern {
struct audio_callbacks_t;
}

namespace lambo::audio {

// Initialise the SDL2 audio backend. Safe to call once before recomp::start();
// idempotent if called more than once. Opens the default audio device at the
// requested sample rate (48 kHz is what ultramodern::init_audio asks for, per
// ultramodern/src/ultrainit.cpp:28). The device is started (SDL_PauseAudioDevice
// unpaused) so the runtime can immediately queue PCM via ultramodern's shim.
void init(uint32_t desired_sample_rate);

// Pump device discovery/recovery from the application's main-thread event loop.
// This keeps SDL device lifecycle calls out of queue_samples.
void pump();

// Populate the three ultramodern audio callbacks (queue_samples,
// get_frames_remaining, and set_frequency) into out. Callers must invoke
// `init(...)` first; the callback pointers are stable for the process lifetime
// of the SDL device.
void get_callbacks(ultramodern::audio_callbacks_t* out);

// Tear down the SDL device. Optional in the current boot path; provided for
// clean shutdown when the watchdog quits the process.
void shutdown();

} // namespace lambo::audio

#endif // LAMBO_AUDIO_H
