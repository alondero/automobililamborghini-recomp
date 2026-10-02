#include "lambo_rt_shadows.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "lambo_log.h"

namespace lambo::rt {
namespace {
constexpr uint32_t first_task = 0x800BF240u;
constexpr uint32_t task_stride = 0x7A50u;
constexpr size_t guest_ram_size = 0x800000u;

int slot_index(uint32_t task) {
    if (task == first_task) return 0;
    if (task == first_task + task_stride) return 1;
    return -1;
}

// Runtime RAM is word-swapped: native u32/float words, halfwords at offset^2.
// Fixed fields below are all inside the supported USA low 8 MiB layout.
template<class T> T read(const uint8_t* ram, uint32_t address) {
    const uint32_t offset = (address & 0x7FFFFFu) ^ (sizeof(T) == 2 ? 2u : 0u);
    T value;
    std::memcpy(&value, ram + offset, sizeof(value));
    return value;
}

TaskSunProbes probes;
bool probe_enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("LAMBO_RT_SUN_PROBE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
}

bool TaskSunProbes::begin(const uint8_t* rdram, size_t size) {
    if (!rdram || size < guest_ram_size) return false;
    const uint32_t task = read<uint32_t>(rdram, 0x800A2BFCu);
    const int slot = slot_index(task);
    const int16_t phase = read<int16_t>(rdram, 0x800CE6ACu);
    const int16_t circuit = read<int16_t>(rdram, 0x800CE794u);
    const int16_t players = read<int16_t>(rdram, 0x800CE6A4u);
    std::lock_guard lock(mutex_);
    if (phase != phase_ || circuit != circuit_) {
        slots_ = {};
        ++epoch_;
        phase_ = phase;
        circuit_ = circuit;
    }
    if (slot < 0) return false;
    slots_[slot] = TaskSunProbe{epoch_, ++sequence_, task, phase, circuit, players, {}};
    return true;
}

bool TaskSunProbes::capture(const uint8_t* rdram, size_t size) {
    if (!rdram || size < guest_ram_size) return false;
    const uint32_t task = read<uint32_t>(rdram, 0x800A2BFCu);
    const int slot = slot_index(task);
    const int16_t camera_slot = read<int16_t>(rdram, 0x800CE6AAu);
    if (slot < 0 || camera_slot < 0 || camera_slot >= 4) return false;
    const SunArt art{camera_slot, read<int16_t>(rdram, 0x800A2FB8u),
        read<int16_t>(rdram, 0x800A2F10u + uint32_t(camera_slot) * 2),
        read<float>(rdram, 0x800A2F90u)};
    if (!std::isfinite(art.camera_height_term)) return false;
    std::lock_guard lock(mutex_);
    if (!slots_[slot] || slots_[slot]->epoch != epoch_) return false;
    slots_[slot]->cameras[camera_slot] = art;
    return true;
}

std::optional<TaskSunProbe> TaskSunProbes::take(uint32_t dl_address) {
    // Accept the exact task + 0x1C0 root in physical, KSEG0 or KSEG1 form.
    // Restrict segments before stripping alias bits so unrelated addresses
    // cannot consume a pending task that happens to share the same low bits.
    const uint32_t segment = dl_address & 0xE0000000u;
    if (segment != 0 && segment != 0x80000000u && segment != 0xA0000000u) return {};
    const uint32_t task = ((dl_address & 0x1FFFFFFFu) | 0x80000000u) - 0x1C0u;
    const int slot = slot_index(task);
    if (slot < 0) return {};
    std::lock_guard lock(mutex_);
    auto result = slots_[slot];
    slots_[slot].reset();
    return result;
}

void TaskSunProbes::invalidate() {
    std::lock_guard lock(mutex_);
    slots_ = {};
    ++epoch_;
}

void consume_sun_probe(uint32_t dl_address) {
    if (!probe_enabled()) return;
    const auto task = probes.take(dl_address);
    // Evidence is sampled by task sequence, not frame/view. See rt-shadows.md.
    if (!task || (task->sequence > 12 && task->sequence % 60 != 0)) return;
    for (const auto& camera : task->cameras) {
        if (!camera) continue;
        LAMBO_LOG("rt-sun", "epoch=%llu task=%llu arena=0x%08X phase=%d circuit=%d players=%d camera_slot=%d art_bearing=%d camera_heading=%d camera_height_term=%.6f physical_sun=unproved\n",
            static_cast<unsigned long long>(task->epoch),
            static_cast<unsigned long long>(task->sequence), task->task_address,
            task->phase, task->circuit, task->players, camera->camera_slot,
            camera->bearing, camera->camera_heading, camera->camera_height_term);
    }
}
} // namespace lambo::rt

extern "C" void lambo_rt_probe_task_begin(uint8_t* rdram) {
    if (lambo::rt::probe_enabled()) lambo::rt::probes.begin(rdram, lambo::rt::guest_ram_size);
}
extern "C" void lambo_rt_probe_sun_art(uint8_t* rdram) {
    if (lambo::rt::probe_enabled()) lambo::rt::probes.capture(rdram, lambo::rt::guest_ram_size);
}
extern "C" void lambo_rt_probe_invalidate() {
    if (lambo::rt::probe_enabled()) lambo::rt::probes.invalidate();
}
