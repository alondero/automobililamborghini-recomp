#include "lambo_mod_game.h"
#include "lambo_mod_guest.h"
#include "lambo_race_field.h"
#include "lambo_vehicle.h"
#include "librecomp/overlays.hpp"
#include <array>
#include <atomic>
#include <cfenv>
#include <cstring>
#include <vector>

extern "C" void recomp_trigger_event(uint8_t*, recomp_context*, uint32_t);

namespace {
// Game-thread owned. Events use a private context so callbacks cannot corrupt
// the interrupted instruction's registers. Their guest-memory writes persist.
enum Event { Init, Frame, RaceStart, RaceEnd, RacerAdded, RacerRemoved, Save, Load, Restored };
constexpr const char* events[] = {"recomp_on_init", "lambo_on_frame_v1",
    "lambo_on_race_start_v1", "lambo_on_race_end_v1", "lambo_on_racer_added_v1",
    "lambo_on_racer_removed_v1", "recomp_on_save", "recomp_on_load",
    "lambo_on_state_restored_v1", nullptr};
struct Racer {
    uint32_t handle = 0;
    uint32_t body = 0;
};
struct Extension {
    uintptr_t owner;
    uint32_t size;
    std::array<void*, 16> data{};
};
std::array<Racer, 16> racers{};
std::vector<Extension> extensions;
uint32_t next_racer = 1;
bool booted = false;
bool racing = false;
bool notifying = false;
std::atomic<unsigned> pak_notifications{0};
std::atomic<bool> state_restored{false};

void emit(uint8_t* rdram, recomp_context* ctx, Event event,
          uint32_t a0 = 0, uint32_t a1 = 0) {
    recomp_context callback = *ctx;
    // f_odd points into its owning context; copying it verbatim aliases the
    // interrupted game's odd FPRs. Match the runtime's COP0 FR-mode mapping.
    callback.f_odd = callback.mips3_float_mode ? &callback.f1.u32l : &callback.f0.u32h;
    callback.r4 = lambo::mods::guest_result(a0);
    callback.r5 = lambo::mods::guest_result(a1);
    callback.r6 = callback.r7 = 0;
    // COP1 rounding is thread-local host state, outside recomp_context.
    std::fenv_t environment{};
    const bool saved_environment = std::fegetenv(&environment) == 0;
    recomp_trigger_event(rdram, &callback, event);
    if (saved_environment) std::fesetenv(&environment);
}

void remove_racer(uint8_t* rdram, recomp_context* ctx, size_t index) {
    if (!racers[index].handle) return;
    // Removal callbacks may still read their extension. Invalidate immediately
    // afterwards; a future entrant at the same body slot receives a new handle.
    emit(rdram, ctx, RacerRemoved, racers[index].handle, index + 1);
    for (auto& extension : extensions) {
        if (extension.data[index]) recomp::free(rdram, extension.data[index]);
        extension.data[index] = nullptr;
    }
    racers[index] = {};
}

void end_race(uint8_t* rdram, recomp_context* ctx) {
    if (!racing) return;
    emit(rdram, ctx, RaceEnd);
    for (size_t i = 0; i < racers.size(); ++i) remove_racer(rdram, ctx, i);
    racing = false;
}

size_t racer_index(uint32_t handle) {
    if (handle) {
        for (size_t i = 0; i < racers.size(); ++i)
            if (racers[i].handle == handle) return i;
    }
    return racers.size();
}

void api_version(uint8_t*, recomp_context* ctx) { ctx->r2 = 1; }

void game_value(uint8_t* rdram, recomp_context* ctx) {
    // USA s16 fields, read on the game thread. Evidence: lambo_vehicle.h,
    // lambo_warp.c and docs/race-fields.md. Unknown keys return INT32_MIN.
    constexpr uint32_t addresses[] = {LAMBO_GUEST_GAME_STATE_ADDR, LAMBO_GUEST_RACE_MODE_ADDR,
        LAMBO_GUEST_PLAYER_COUNT_ADDR, LAMBO_GUEST_FIELD_COUNT_ADDR,
        LAMBO_GUEST_RACE_DIFFICULTY_ADDR, LAMBO_GUEST_MENU_TRACK_ADDR};
    uint32_t key = ctx->r4;
    ctx->r2 = key < std::size(addresses)
        ? static_cast<gpr>(MEM_H(0, static_cast<int32_t>(addresses[key])))
        : static_cast<gpr>(INT32_MIN);
}

void get_racer(uint8_t*, recomp_context* ctx) {
    uint32_t entrant = ctx->r4;
    ctx->r2 = entrant >= 1 && entrant <= racers.size()
        ? lambo::mods::guest_result(racers[entrant - 1].handle) : 0;
}

void racer_body(uint8_t*, recomp_context* ctx) {
    size_t index = racer_index(ctx->r4);
    ctx->r2 = index < racers.size() ? lambo::mods::guest_result(racers[index].body) : 0;
}

void register_extension(uint8_t*, recomp_context* ctx, uintptr_t owner) {
    uint32_t size = ctx->r4;
    ctx->r2 = 0;
    // A bounded registry keeps a faulty mod from allocating unbounded memory.
    // Registration during removal notification would invalidate iteration.
    if (!booted || notifying || !size || size > 4096 || extensions.size() >= 256) return;
    extensions.push_back({owner, size, {}});
    ctx->r2 = extensions.size();
}

void get_extension(uint8_t* rdram, recomp_context* ctx, uintptr_t owner) {
    size_t index = racer_index(ctx->r4);
    uint32_t id = ctx->r5;
    ctx->r2 = 0;
    if (index == racers.size() || !id || id > extensions.size()) return;
    auto& extension = extensions[id - 1];
    if (extension.owner != owner) return;
    if (!extension.data[index]) {
        extension.data[index] = recomp::alloc(rdram, (extension.size + 3u) & ~3u);
        if (!extension.data[index]) return;
        std::memset(extension.data[index], 0, (extension.size + 3u) & ~3u);
    }
    auto offset = static_cast<uint8_t*>(extension.data[index]) - rdram;
    ctx->r2 = lambo::mods::guest_result(0x80000000u + static_cast<uint32_t>(offset));
}
}

void lambo::mods::register_game_api() {
    recomp::overlays::register_base_events(events);
    recomp::overlays::register_base_export("lambo_mod_api_version", api_version);
    recomp::overlays::register_base_export("lambo_get_game_value_v1", game_value);
    recomp::overlays::register_base_export("lambo_get_racer_v1", get_racer);
    recomp::overlays::register_base_export("lambo_get_racer_body_v1", racer_body);
    recomp::overlays::register_ext_base_export("lambo_register_racer_extension_v1", register_extension);
    recomp::overlays::register_ext_base_export("lambo_get_racer_extension_v1", get_extension);
}

extern "C" void lambo_mod_boot(uint8_t* rdram, recomp_context* ctx) {
    // Runtime has successfully loaded code and initialized the mod heap before
    // BootLoadInitialAssets starts. A fresh boot discards metadata for the previous
    // heap without freeing pointers into a heap the runtime has reinitialized.
    extensions.clear();
    racers = {};
    racing = false;
    notifying = false;
    pak_notifications = 0;
    state_restored = false;
    lambo::mods::reset_storage_api();
    booted = true;
    emit(rdram, ctx, Init);
}

extern "C" void lambo_mod_scene_reset(uint8_t* rdram, recomp_context* ctx) {
    if (!booted || notifying) return;
    notifying = true;
    end_race(rdram, ctx);
    notifying = false;
}

extern "C" void lambo_mod_frame(uint8_t* rdram, recomp_context* ctx) {
    if (!booted || notifying) return;
    notifying = true;
    if (state_restored.exchange(false)) {
        end_race(rdram, ctx);
        emit(rdram, ctx, Restored);
    }
    const int state = MEM_H(0, static_cast<int32_t>(LAMBO_GUEST_GAME_STATE_ADDR));
    if (state != 8) end_race(rdram, ctx);
    else {
        const bool starting = !racing;
        racing = true;
        const int count = MEM_H(0, static_cast<int32_t>(LAMBO_GUEST_FIELD_COUNT_ADDR));
        for (size_t i = 0; i < racers.size(); ++i) {
            // One-based entrant -> shared body index, USA halfword map.
            // Pool reset invalidates all handles even if the same slot is reused
            // between observations. This API describes race entrants, not props.
            const int body = i < static_cast<size_t>(count > 0 && count <= 16 ? count : 0)
                ? MEM_H((i + 1) * 2, static_cast<int32_t>(LAMBO_FIELD_BODIES)) : -1;
            uint32_t address = body >= 0 && body < LAMBO_VEHICLE_CAPACITY
                ? LAMBO_VEHICLE_BASE + body * sizeof(LamboVehicleRecord) : 0;
            if (address && !(MEM_H(0, static_cast<int32_t>(address)) & 8)) address = 0;
            if (racers[i].body == address) continue;
            remove_racer(rdram, ctx, i);
            // Never reuse an exhausted handle; fail closed instead of aliasing.
            if (address && next_racer) {
                racers[i] = {next_racer++, address};
                emit(rdram, ctx, RacerAdded, racers[i].handle, i + 1);
            }
        }
        if (starting) emit(rdram, ctx, RaceStart);
    }
    const unsigned pak = pak_notifications.exchange(0);
    if (pak & 1) emit(rdram, ctx, Load);
    if (pak & 2) emit(rdram, ctx, Save);
    emit(rdram, ctx, Frame, state);
    notifying = false;
}

extern "C" void lambo_mod_pak_loaded() { pak_notifications.fetch_or(1); }
extern "C" void lambo_mod_pak_written() { pak_notifications.fetch_or(2); }
extern "C" void lambo_mod_state_restored() { state_restored = true; }
