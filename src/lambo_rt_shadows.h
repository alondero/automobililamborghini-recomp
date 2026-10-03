#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <optional>
#include <vector>

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
    int16_t race_mode = -1; // USA menu/race mode; 0=time trial, 2=single race.
    // Native model-selection cursors, not physical object IDs or categories.
    std::array<int16_t, 4> model_cursors{};
    struct EmitterSpan {
        uint32_t emitter = 0; // Runtime entry PC, not a material classification.
        uint32_t begin = 0; // Guest display-list cursor, inclusive.
        uint32_t end = 0; // Exclusive; includes child-list call sites.
        int16_t camera_slot = -1;
    };
    std::vector<EmitterSpan> emitters;
    bool emitters_complete = true;
    struct ObjectIdentity {
        uint16_t flags = 0;
        uint32_t list = 0;
        int16_t parent = -1;
        int16_t kind = 0; // USA native constructor discriminator, not a material.
    };
    // Compact producer-owned object table authenticates the native car-child
    // overlay and its physical-car caster ancestry. It replaces an unsafe
    // worker-side guest-RAM lookup.
    std::array<ObjectIdentity, 128> objects{};
    bool objects_complete = false;
    // Immutable, opt-in low-RAM copy made by the game producer before queueing.
    std::shared_ptr<const std::vector<uint8_t>> native_ram;
};

// Game producer writes a slot after the native reuse fence. The HLE consumer
// takes a value for that exact display list. Optional low-RAM snapshots happen
// before native queue publication; renderer workers never read guest globals.
// This seam currently records provenance only and cannot enable shadows.
class TaskSunProbes {
public:
    bool begin(const uint8_t* rdram, size_t size);
    bool capture(const uint8_t* rdram, size_t size);
    bool emitter_begin(const uint8_t* rdram, size_t size, uint32_t emitter);
    bool emitter_end(const uint8_t* rdram, size_t size, uint32_t emitter);
    bool snapshot(const uint8_t* rdram, size_t size);
    // Exact physical/KSEG0/KSEG1 roots share a single consumable task record.
    std::optional<TaskSunProbe> take(uint32_t dl_address);
    void invalidate();
    // Production shadows copy object identity for every task; diagnostics
    // copy it only on their sampled sequences.
    void set_copy_all_objects(bool enabled) { copy_all_objects_ = enabled; }

private:
    std::mutex mutex_;
    std::array<std::optional<TaskSunProbe>, 2> slots_;
    std::array<std::array<std::optional<TaskSunProbe::EmitterSpan>, 4>, 2> open_emitters_;
    uint64_t epoch_ = 1;
    uint64_t sequence_ = 0;
    int16_t phase_ = -1;
    int16_t circuit_ = -1;
    int16_t players_ = -1;
    int16_t race_mode_ = -1;
    std::array<int16_t, 4> model_cursors_{};
    std::atomic_bool copy_all_objects_{false};
};

bool physical_car_object(const std::vector<TaskSunProbe::ObjectIdentity>& objects, uint32_t object_id);
bool procedural_world_object(const TaskSunProbe::ObjectIdentity& object);
bool presented_object_id(uint32_t matrix_id, uint32_t& object_id);
std::optional<TaskSunProbe> consume_sun_probe(uint32_t dl_address);
// Enables the producer hooks for production shadows on tasks begun afterwards.
// Only the compact identity is copied; no RAM snapshot or logging is added.
void set_production_task_values(bool enabled);

} // namespace lambo::rt

extern "C" void lambo_rt_probe_task_begin(uint8_t* rdram);
extern "C" void lambo_rt_probe_sun_art(uint8_t* rdram);
extern "C" void lambo_rt_probe_invalidate();
extern "C" void lambo_rt_probe_emitter_begin(uint8_t* rdram, uint32_t emitter);
extern "C" void lambo_rt_probe_emitter_end(uint8_t* rdram, uint32_t emitter);
extern "C" void lambo_rt_probe_task_publish(uint8_t* rdram);
