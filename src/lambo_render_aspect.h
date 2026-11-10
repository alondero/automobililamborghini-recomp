#pragma once

#include <optional>

namespace lambo::render {

struct Aspects {
    float sky{}; // Zero without an RT64 presenter.
    float hud{4.0f / 3.0f};
};

// Guest graphics producers share one lazy snapshot per task build. Keeping it
// thread-local avoids cross-thread publication and retains the configuration
// mutex only for the first read. The next build observes resizes and settings.
class FrameAspects {
public:
    void begin_frame() { snapshot.reset(); }

    template <typename Reader>
    const Aspects& get(Reader read) {
        if (!snapshot) snapshot = read();
        return *snapshot;
    }

private:
    std::optional<Aspects> snapshot;
};

inline thread_local FrameAspects frame_aspects;

} // namespace lambo::render
