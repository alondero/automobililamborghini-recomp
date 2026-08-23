#ifndef LAMBO_CONTROLS_SDL_H
#define LAMBO_CONTROLS_SDL_H

#include <cstdint>
#include <memory>

#include "lambo_controls.h"

union SDL_Event;

namespace lambo::controls {

class SdlAdapter {
public:
    SdlAdapter();
    ~SdlAdapter();
    SdlAdapter(const SdlAdapter&) = delete;
    SdlAdapter& operator=(const SdlAdapter&) = delete;

    void open_existing();
    void device_added(int joystick_index);
    void device_removed(std::int32_t instance);
    // Capture handling precedes RmlUi. True means the event is consumed.
    bool handle_capture_event(const SDL_Event& event);
    void process_commands();
    EvaluatedState sample();
    void apply_rumble(bool on);
    bool selected_back_pressed(const SDL_Event& event) const;
    void cancel_capture();
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lambo::controls

#endif
