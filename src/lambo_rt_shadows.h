#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace lambo::rt {

// USA task arenas and light-art fields are documented in docs/rt-shadows.md.
// These are diagnostic inputs, not a validated physical sun direction.
struct SunArt {
    int16_t camera_slot = -1; // Native scratch index; not a viewport identifier.
    int16_t bearing = 0; // Authored planar bearing in degrees.
    int16_t camera_heading = 0; // Native planar camera heading in degrees.
    // Art scale: 256 * normalized camera-vector component; no world units.
    float camera_height_term = 0;
};

struct TaskSunProbe {
    uint64_t epoch = 0;
    uint64_t sequence = 0;
    uint32_t task_address = 0;
    int16_t phase = 0;
    int16_t circuit = -1;
    int16_t players = 0;
    std::array<std::optional<SunArt>, 4> cameras;
};

// Game producer writes a slot after the native reuse fence. The HLE consumer
// takes a value for that exact display list; it never reads live guest globals.
// This seam currently records provenance only and cannot enable shadows.
class TaskSunProbes {
public:
    bool begin(const uint8_t* rdram, size_t size);
    bool capture(const uint8_t* rdram, size_t size);
    // Exact physical/KSEG0/KSEG1 roots share a single consumable task record.
    std::optional<TaskSunProbe> take(uint32_t dl_address);
    void invalidate();

private:
    std::mutex mutex_;
    std::array<std::optional<TaskSunProbe>, 2> slots_;
    uint64_t epoch_ = 1;
    uint64_t sequence_ = 0;
    int16_t phase_ = -1;
    int16_t circuit_ = -1;
};

void consume_sun_probe(uint32_t dl_address);

} // namespace lambo::rt

extern "C" void lambo_rt_probe_task_begin(uint8_t* rdram);
extern "C" void lambo_rt_probe_sun_art(uint8_t* rdram);
extern "C" void lambo_rt_probe_invalidate();
