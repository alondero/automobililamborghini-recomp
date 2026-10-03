#include "lambo_rt_shadows.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "contrib/json/json.hpp"

#include "lambo_log.h"
#include "lambo_vehicle.h"

namespace lambo::rt {

bool presented_object_id(uint32_t matrix_id, uint32_t& object_id) {
    if ((matrix_id & 0xFFF00000u) != 0x10000000u) return false;
    object_id = matrix_id & 0xFFFFu;
    return true;
}

bool physical_car_object(const std::vector<TaskSunProbe::ObjectIdentity>& objects, uint32_t object_id) {
    // HLE-owned parent links identify child meshes such as car components.
    // The table-size bound also terminates malformed parent cycles.
    for (size_t depth = 0; depth < objects.size(); ++depth) {
        if (object_id >= objects.size()) return false;
        const auto& object = objects[object_id];
        if ((object.flags & 8u) != 0) return true;
        if (object.parent < 0) return false;
        object_id = uint32_t(object.parent);
    }
    return false;
}

bool procedural_world_object(const TaskSunProbe::ObjectIdentity& object) {
    // The native world builder's 0x800 branch emits tiled geometry directly,
    // so this measured kind has no child display-list pointer. Other kinds or
    // flag combinations remain unknown; material admission is a separate gate.
    // Constructor/dispatch and all-circuit observations: docs/rt-material-evidence.md.
    return object.flags == 0xC01u && object.list == 0 && object.parent == -1 && object.kind == 13;
}

namespace {
constexpr uint32_t first_task = 0x800BF240u;
constexpr uint32_t task_stride = 0x7A50u;
constexpr size_t guest_ram_size = 0x800000u;
// USA fields documented in docs/rt-shadows.md's guest-bridge table and
// docs/rt-provenance.md's emitter/object snapshot tables.
constexpr uint32_t current_task_address = 0x800A2BFCu;
constexpr uint32_t current_phase_address = 0x800CE6ACu;
constexpr uint32_t current_circuit_address = 0x800CE794u;
constexpr uint32_t current_players_address = 0x800CE6A4u;
// USA signed halfword mode, copied on the game producer. Model cursors use
// the shared vehicle layout; docs/rt-shadows.md records timing and failure.
constexpr uint32_t race_mode_address = 0x800CE6B4u;
constexpr uint32_t task_cursor_address = 0x800A39CCu;
constexpr uint32_t camera_slot_address = 0x800CE6AAu;
constexpr uint32_t native_bearing_address = 0x800A2FB8u;
constexpr uint32_t camera_heading_address = 0x800A2F10u;
constexpr uint32_t camera_height_term_address = 0x800A2F90u;
constexpr uint32_t native_object_table_address = 0x800B69A8u;
constexpr uint32_t native_object_stride = 0x10Cu;
constexpr uint32_t native_object_list_offset = 0x08u;
constexpr uint32_t native_object_kind_offset = 0x0Eu;
constexpr uint32_t native_object_parent_offset = 0x58u;

int slot_index(uint32_t task) {
    if (task == first_task) return 0;
    if (task == first_task + task_stride) return 1;
    return -1;
}

int emitter_index(uint32_t emitter) {
    switch (emitter) {
        case 0x80009AC0u: return 0; // Native track builder.
        case 0x8000F6D8u: return 1; // Native panorama emitter.
        case 0x800159FCu: return 2; // C8104A50 lead.
        case 0x8000E468u: return 3; // C8104B50 lead.
        default: return -1;
    }
}

bool valid_cursor(int slot, uint32_t cursor) {
    const uint32_t task = first_task + uint32_t(slot) * task_stride;
    // Scheduler fields begin at task + 0x79C0; the arena stride also includes
    // those reserved fields (docs/sky-panorama.md).
    return cursor >= task + 0x1C0u && cursor <= task + 0x79C0u && (cursor & 7u) == 0;
}

bool capture_sequence(uint64_t sequence) {
    return sequence == 60 || sequence == 300 || sequence == 420 || sequence == 540;
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
bool task_capture_enabled() {
    return probe_enabled();
}
}

bool TaskSunProbes::begin(const uint8_t* rdram, size_t size) {
    if (!rdram || size < guest_ram_size) return false;
    const uint32_t task = read<uint32_t>(rdram, current_task_address);
    const int slot = slot_index(task);
    const int16_t phase = read<int16_t>(rdram, current_phase_address);
    const int16_t circuit = read<int16_t>(rdram, current_circuit_address);
    const int16_t players = read<int16_t>(rdram, current_players_address);
    const int16_t race_mode = read<int16_t>(rdram, race_mode_address);
    std::array<int16_t, 4> model_cursors;
    for (size_t player = 0; player < model_cursors.size(); ++player) {
        model_cursors[player] = read<int16_t>(rdram, LAMBO_GUEST_PLAYER_MODEL_CURSOR_ADDR +
            uint32_t(player) * LAMBO_GUEST_PLAYER_MODEL_CURSOR_STRIDE);
    }
    std::lock_guard lock(mutex_);
    if (phase != phase_ || circuit != circuit_ || players != players_ || race_mode != race_mode_ ||
        model_cursors != model_cursors_) {
        slots_ = {};
        open_emitters_ = {};
        ++epoch_;
        phase_ = phase;
        circuit_ = circuit;
        players_ = players;
        race_mode_ = race_mode;
        model_cursors_ = model_cursors;
    }
    if (slot < 0) return false;
    open_emitters_[slot] = {};
    slots_[slot] = TaskSunProbe{epoch_, ++sequence_, task, phase, circuit, players, {}};
    slots_[slot]->race_mode = race_mode;
    slots_[slot]->model_cursors = model_cursors;
    return true;
}

bool TaskSunProbes::emitter_begin(const uint8_t* rdram, size_t size, uint32_t emitter) {
    if (!rdram || size < guest_ram_size) return false;
    const int slot = slot_index(read<uint32_t>(rdram, current_task_address));
    const int index = emitter_index(emitter);
    if (slot < 0 || index < 0) return false;
    const uint32_t cursor = read<uint32_t>(rdram, task_cursor_address);
    const int16_t camera = read<int16_t>(rdram, camera_slot_address);
    std::lock_guard lock(mutex_);
    if (!slots_[slot]) return false;
    if (!valid_cursor(slot, cursor) || camera < 0 || camera >= 4 || open_emitters_[slot][index]) {
        slots_[slot]->emitters_complete = false;
        return false;
    }
    open_emitters_[slot][index] = TaskSunProbe::EmitterSpan{emitter, cursor, cursor, camera};
    return true;
}

bool TaskSunProbes::emitter_end(const uint8_t* rdram, size_t size, uint32_t emitter) {
    if (!rdram || size < guest_ram_size) return false;
    const int slot = slot_index(read<uint32_t>(rdram, current_task_address));
    const int index = emitter_index(emitter);
    if (slot < 0 || index < 0) return false;
    const uint32_t cursor = read<uint32_t>(rdram, task_cursor_address);
    std::lock_guard lock(mutex_);
    if (!slots_[slot]) return false;
    auto& pending = open_emitters_[slot][index];
    if (!pending || !valid_cursor(slot, cursor) || cursor < pending->begin || slots_[slot]->emitters.size() >= 64) {
        pending.reset();
        slots_[slot]->emitters_complete = false;
        return false;
    }
    pending->end = cursor;
    slots_[slot]->emitters.push_back(*pending);
    pending.reset();
    return true;
}

bool TaskSunProbes::capture(const uint8_t* rdram, size_t size) {
    if (!rdram || size < guest_ram_size) return false;
    const uint32_t task = read<uint32_t>(rdram, current_task_address);
    const int slot = slot_index(task);
    const int16_t camera_slot = read<int16_t>(rdram, camera_slot_address);
    if (slot < 0 || camera_slot < 0 || camera_slot >= 4) return false;
    const SunArt art{camera_slot, read<int16_t>(rdram, native_bearing_address),
        read<int16_t>(rdram, camera_heading_address + uint32_t(camera_slot) * 2),
        read<float>(rdram, camera_height_term_address)};
    if (!std::isfinite(art.camera_height_term)) return false;
    std::lock_guard lock(mutex_);
    if (!slots_[slot] || slots_[slot]->epoch != epoch_) return false;
    slots_[slot]->cameras[camera_slot] = art;
    return true;
}

bool TaskSunProbes::snapshot(const uint8_t* rdram, size_t size) {
    if (!rdram || size < guest_ram_size) return false;
    const int slot = slot_index(read<uint32_t>(rdram, current_task_address));
    if (slot < 0) return false;
    std::lock_guard lock(mutex_);
    if (!slots_[slot]) return false;
    auto& record = *slots_[slot];
    // A scene selection must not change between begin and queue publication.
    // Reject its proof instead of pairing old identity with new RAM bytes.
    if (record.phase != read<int16_t>(rdram, current_phase_address) ||
        record.circuit != read<int16_t>(rdram, current_circuit_address) ||
        record.players != read<int16_t>(rdram, current_players_address) ||
        record.race_mode != read<int16_t>(rdram, race_mode_address)) {
        record.emitters_complete = false;
        return false;
    }
    for (size_t player = 0; player < record.model_cursors.size(); ++player) {
        if (record.model_cursors[player] != read<int16_t>(rdram, LAMBO_GUEST_PLAYER_MODEL_CURSOR_ADDR +
            uint32_t(player) * LAMBO_GUEST_PLAYER_MODEL_CURSOR_STRIDE)) {
            record.emitters_complete = false;
            return false;
        }
    }
    bool copied = false;
    if (!record.objects_complete && capture_sequence(record.sequence)) {
        // Object table fields are copied on the game producer immediately
        // before task publication. Runtime addresses/layout are documented in
        // docs/rt-shadows.md. The copy is small and remains task-owned.
        for (size_t object = 0; object < record.objects.size(); ++object) {
            const uint32_t at = native_object_table_address + uint32_t(object) * native_object_stride;
            record.objects[object] = {read<uint16_t>(rdram, at),
                read<uint32_t>(rdram, at + native_object_list_offset),
                read<int16_t>(rdram, at + native_object_parent_offset),
                read<int16_t>(rdram, at + native_object_kind_offset)};
        }
        record.objects_complete = true;
        copied = true;
    }
    const char* directory = std::getenv("LAMBO_RT_CAPTURE_DIR");
    if (!directory || !*directory || record.native_ram || !capture_sequence(record.sequence)) return copied;
    // Game-owned records/light banks may change while an older task is queued.
    // Copy on the game producer at 0x80005728, before osSendMesg publishes the
    // completed task. A task-arena reuse fence alone cannot protect globals.
    record.native_ram = std::make_shared<const std::vector<uint8_t>>(rdram, rdram + guest_ram_size);
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
    if (slots_[slot]) {
        for (const auto& pending : open_emitters_[slot]) {
            if (pending) slots_[slot]->emitters_complete = false;
        }
    }
    auto result = slots_[slot];
    slots_[slot].reset();
    open_emitters_[slot] = {};
    return result;
}

void TaskSunProbes::invalidate() {
    std::lock_guard lock(mutex_);
    slots_ = {};
    open_emitters_ = {};
    ++epoch_;
}

// Opt-in, bounded local evidence: four immutable producer snapshots consumed
// with the matching HLE task. Emitter spans travel in the same copied record;
// HLE/workers never read guest globals for this diagnostic. RAM contains copyrighted
// assets and must remain ignored. The Workload bridge is diagnostic only and
// cannot select a receiver or suppress native output.
void capture_task(const TaskSunProbe& task) {
    const char* directory = std::getenv("LAMBO_RT_CAPTURE_DIR");
    if (!directory || !*directory || !task.native_ram) return;
    try {
        const auto base = std::filesystem::path(directory) / ("task-" + std::to_string(task.sequence));
        std::filesystem::create_directories(base.parent_path());
        nlohmann::json metadata = {{"schema", 2}, {"snapshot_point", "producer-before-submit"},
            {"epoch", task.epoch}, {"sequence", task.sequence},
            {"task_address", task.task_address}, {"root", task.task_address + 0x1C0u},
            {"phase", task.phase}, {"circuit", task.circuit}, {"players", task.players},
            {"race_mode", task.race_mode}, {"model_cursors", task.model_cursors},
            {"emitters_complete", task.emitters_complete}, {"ram_layout", "word-swapped"},
            {"cameras", nlohmann::json::array()}, {"emitters", nlohmann::json::array()}};
        for (const auto& camera : task.cameras) {
            if (camera) metadata["cameras"].push_back({{"slot", camera->camera_slot}, {"art_bearing", camera->bearing},
                {"heading", camera->camera_heading}, {"height_term", camera->camera_height_term}});
        }
        for (const auto& span : task.emitters) {
            metadata["emitters"].push_back({{"emitter", span.emitter}, {"begin", span.begin}, {"end", span.end},
                {"camera_slot", span.camera_slot}});
        }
        auto ram_path = base;
        auto metadata_path = base;
        ram_path += ".bin";
        metadata_path += ".json";
        std::ofstream ram_file(ram_path, std::ios::binary);
        ram_file.write(reinterpret_cast<const char*>(task.native_ram->data()), task.native_ram->size());
        std::ofstream manifest(metadata_path);
        manifest << metadata.dump(2) << '\n';
        ram_file.flush();
        manifest.flush();
        if (!ram_file || !manifest) throw std::runtime_error("snapshot write failed");
        LAMBO_LOG("rt-capture", "task=%llu spans=%zu complete=%d local snapshot written\n",
            static_cast<unsigned long long>(task.sequence), task.emitters.size(), task.emitters_complete);
    }
    catch (const std::exception& error) {
        LAMBO_LOG_WARN("rt-capture", "snapshot failed: %s\n", error.what());
    }
}

std::optional<TaskSunProbe> consume_sun_probe(uint32_t dl_address) {
    if (!task_capture_enabled()) return std::nullopt;
    const auto task = probes.take(dl_address);
    if (task) capture_task(*task);
    // Evidence is sampled by task sequence, not frame/view. See rt-shadows.md.
    if (!probe_enabled() || !task || (task->sequence > 12 && task->sequence % 60 != 0)) return task;
    for (const auto& camera : task->cameras) {
        if (!camera) continue;
        LAMBO_LOG("rt-sun", "epoch=%llu task=%llu arena=0x%08X phase=%d circuit=%d players=%d camera_slot=%d art_bearing=%d camera_heading=%d camera_height_term=%.6f physical_sun=unproved\n",
            static_cast<unsigned long long>(task->epoch),
            static_cast<unsigned long long>(task->sequence), task->task_address,
            task->phase, task->circuit, task->players, camera->camera_slot,
            camera->bearing, camera->camera_heading, camera->camera_height_term);
    }
    return task;
}
} // namespace lambo::rt

extern "C" void lambo_rt_probe_task_begin(uint8_t* rdram) {
    if (lambo::rt::task_capture_enabled()) lambo::rt::probes.begin(rdram, lambo::rt::guest_ram_size);
}
extern "C" void lambo_rt_probe_sun_art(uint8_t* rdram) {
    if (lambo::rt::task_capture_enabled()) lambo::rt::probes.capture(rdram, lambo::rt::guest_ram_size);
}
extern "C" void lambo_rt_probe_invalidate() {
    if (lambo::rt::task_capture_enabled()) lambo::rt::probes.invalidate();
}
extern "C" void lambo_rt_probe_emitter_begin(uint8_t* rdram, uint32_t emitter) {
    if (lambo::rt::task_capture_enabled()) lambo::rt::probes.emitter_begin(rdram, lambo::rt::guest_ram_size, emitter);
}
extern "C" void lambo_rt_probe_emitter_end(uint8_t* rdram, uint32_t emitter) {
    if (lambo::rt::task_capture_enabled()) lambo::rt::probes.emitter_end(rdram, lambo::rt::guest_ram_size, emitter);
}
extern "C" void lambo_rt_probe_task_publish(uint8_t* rdram) {
    if (!lambo::rt::task_capture_enabled()) return;
    try {
        lambo::rt::probes.snapshot(rdram, lambo::rt::guest_ram_size);
    }
    catch (const std::exception& error) {
        LAMBO_LOG_WARN("rt-capture", "producer snapshot failed: %s\n", error.what());
    }
}
