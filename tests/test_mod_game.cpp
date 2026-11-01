#ifdef NDEBUG
#undef NDEBUG
#endif
#include "lambo_mod_game.h"
#include "lambo/mod_api.h"
#include "lambo_vehicle.h"
#include "lambo_race_field.h"
#include "librecomp/addresses.hpp"
#include "librecomp/overlays.hpp"
#include <cassert>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

extern "C" void lambo_test_change_state();

namespace {
std::map<std::string, recomp_func_t*> functions;
std::map<std::string, recomp_func_ext_t*> owned_functions;
std::vector<std::string> event_names;
std::vector<std::string> seen;
std::function<void(uint8_t*, recomp_context*, const std::string&)> on_event;
size_t heap = 0x1000000;
std::map<void*, size_t> allocations;
bool fail_alloc = false;
}

void recomp::overlays::register_base_export(const std::string& name, recomp_func_t* fn) {
    functions[name] = fn;
}
void recomp::overlays::register_ext_base_export(const std::string& name, recomp_func_ext_t* fn) {
    owned_functions[name] = fn;
}
void recomp::overlays::register_base_events(const char* const* names) {
    for (; *names; ++names) event_names.emplace_back(*names);
}
void* recomp::alloc(uint8_t* rdram, size_t size) {
    if (fail_alloc) return nullptr;
    void* ptr = rdram + heap;
    heap += (size + 15u) & ~15u;
    allocations[ptr] = size;
    return ptr;
}
void recomp::free(uint8_t*, void* ptr) { assert(allocations.erase(ptr) == 1); }
extern "C" void recomp_trigger_event(uint8_t* rdram, recomp_context* ctx, uint32_t index) {
    const auto& name = event_names.at(index);
    seen.push_back(name);
    if (on_event) on_event(rdram, ctx, name);
    std::memset(ctx, 0xCC, sizeof(*ctx)); // interrupted context must be preserved
}

int main() {
    // The header is a volatile read-only view of data changed by the game.
    // An optimized build with plain `const` incorrectly reuses the first read.
    const auto before = lambo_game_state;
    lambo_test_change_state();
    assert(lambo_game_state != before);
    std::vector<uint8_t> memory(0x1100000, 0xA5);
    uint8_t* rdram = memory.data();
    recomp_context ctx{};
    ctx.r29 = static_cast<int32_t>(0x80300000);
    ctx.f_odd = &ctx.f0.u32h;
    auto original = ctx;
    lambo::mods::register_game_api();
    lambo::mods::register_storage_api();
    auto call = [&](const char* name, uint32_t a = 0, uint32_t b = 0,
                    uint32_t c = 0, uintptr_t owner = 1) -> uint32_t {
        recomp_context args{};
        args.r4 = static_cast<int32_t>(a);
        args.r5 = static_cast<int32_t>(b);
        args.r6 = static_cast<int32_t>(c);
        if (functions.contains(name)) functions.at(name)(rdram, &args);
        else owned_functions.at(name)(rdram, &args, owner);
        return args.r2;
    };
    auto frame = [&] { lambo_mod_frame(rdram, &ctx); assert(std::memcmp(&ctx, &original, sizeof(ctx)) == 0); };
    auto half = [&](uint32_t address) -> int16_t& { return MEM_H(0, static_cast<int32_t>(address)); };
    half(LAMBO_GUEST_GAME_STATE_ADDR) = 4;
    frame();
    assert(seen.empty()); // cannot trigger events before successful boot
    uint32_t extension = 0;
    on_event = [&](uint8_t*, recomp_context*, const std::string& name) {
        if (name == "recomp_on_init") extension = call("lambo_register_racer_extension_v1", 5);
    };
    lambo_mod_boot(rdram, &ctx);
    on_event = {};
    assert(extension && seen == std::vector<std::string>{"recomp_on_init"});
    assert(std::memcmp(&ctx, &original, sizeof(ctx)) == 0);
    on_event = [](uint8_t*, recomp_context* callback, const std::string&) {
        assert(callback->f_odd == (callback->mips3_float_mode ? &callback->f1.u32l : &callback->f0.u32h));
        callback->f_odd[0] = 0x3F800000;
        callback->f_odd[28] = 0x40000000;
        set_cop1_cs(1);
    };
    set_cop1_cs(2);
    frame(); // FR=0 odd single writes must not touch the interrupted FPR bank
    assert(get_cop1_cs() == 2);
    ctx.mips3_float_mode = 1;
    ctx.f_odd = &ctx.f1.u32l;
    original = ctx;
    frame(); // FR=1 uses the other odd-register mapping
    assert(get_cop1_cs() == 2);
    set_cop1_cs(0);
    on_event = {};
    ctx.mips3_float_mode = 0;
    ctx.f_odd = &ctx.f0.u32h;
    original = ctx;
    assert(call("lambo_mod_api_version") == 1);
    assert(call("lambo_get_game_value_v1", 0) == 4);
    assert(call("lambo_get_game_value_v1", 99) == 0x80000000);
    assert(call("lambo_register_racer_extension_v1", 0) == 0);
    assert(call("lambo_register_racer_extension_v1", 4097) == 0);
    assert(call("lambo_get_racer_v1", 0) == 0);
    assert(call("lambo_get_racer_v1", 17) == 0);

    half(LAMBO_GUEST_GAME_STATE_ADDR) = 8;
    half(0x800CE7AC) = 1;
    half(LAMBO_FIELD_BODIES + 2) = 3;
    const uint32_t body = LAMBO_VEHICLE_BASE + 3 * sizeof(LamboVehicleRecord);
    half(body) = 8;
    seen.clear();
    frame();
    assert((seen == std::vector<std::string>{"lambo_on_racer_added_v1", "lambo_on_race_start_v1", "lambo_on_frame_v1"}));
    uint32_t racer = call("lambo_get_racer_v1", 1);
    assert(racer && call("lambo_get_racer_body_v1", racer) == body);
    assert(call("lambo_get_racer_extension_v1", racer, extension, 0, 2) == 0); // owner isolation
    fail_alloc = true;
    assert(call("lambo_get_racer_extension_v1", racer, extension) == 0);
    fail_alloc = false;
    uint32_t data = call("lambo_get_racer_extension_v1", racer, extension);
    assert(data);
    assert(MEM_W(0, static_cast<int32_t>(data)) == 0 && MEM_W(4, static_cast<int32_t>(data)) == 0);
    MEM_W(0, static_cast<int32_t>(data)) = 42;
    frame();
    assert(call("lambo_get_racer_extension_v1", racer, extension) == data);
    assert(MEM_W(0, static_cast<int32_t>(data)) == 42);
    bool removal_read = false;
    on_event = [&](uint8_t*, recomp_context* args, const std::string& name) {
        if (name == "lambo_on_racer_removed_v1") {
            assert(args->r4 == racer && args->r5 == 1);
            assert(call("lambo_get_racer_extension_v1", racer, extension) == data);
            removal_read = true;
            lambo_mod_frame(rdram, &ctx); // recursive notification is ignored
        }
    };
    lambo_mod_scene_reset(rdram, &ctx);
    on_event = {};
    assert(removal_read && allocations.empty());
    assert(call("lambo_get_racer_body_v1", racer) == 0);
    assert(call("lambo_get_racer_extension_v1", racer, extension) == 0);
    frame();
    assert(call("lambo_get_racer_v1", 1) != racer); // same slot, new lifetime
    lambo_mod_state_restored();
    seen.clear();
    frame();
    assert(seen.at(0) == "lambo_on_race_end_v1");
    assert(seen.at(2) == "lambo_on_state_restored_v1");
    half(LAMBO_GUEST_GAME_STATE_ADDR) = 4;
    frame();
    assert(call("lambo_get_racer_v1", 1) == 0);
    lambo_mod_pak_loaded();
    lambo_mod_pak_written();
    lambo_mod_pak_written();
    seen.clear();
    frame();
    assert((seen == std::vector<std::string>{"recomp_on_load", "recomp_on_save", "lambo_on_frame_v1"}));
    seen.clear();
    frame();
    assert(seen.size() == 1);

    const uint32_t output = 0x80000100;
    uint32_t map = call("lambo_store_create_v1", 0);
    assert(map && call("lambo_store_create_v1", 3) == 0);
    assert(call("lambo_store_put_v1", map, 0, 0) == 1);
    assert(call("lambo_store_get_v1", map, 0, output) == 1);
    assert(MEM_W(0, static_cast<int32_t>(output)) == 0);
    assert(call("lambo_store_put_v1", map, 0, 42) == 0);
    for (auto address : {0u, 0x80000001u, 0xA0000000u, 0x9FFFFFFFu})
        assert(call("lambo_store_get_v1", map, 0, address) == UINT32_MAX);
    assert(call("lambo_store_get_v1", map, 0, output, 2) == UINT32_MAX);
    assert(call("lambo_store_destroy_v1", map, 0, 0, 2) == UINT32_MAX);
    for (uint32_t i = 1; i < 4096; ++i) assert(call("lambo_store_put_v1", map, i, i) == 1);
    assert(call("lambo_store_put_v1", map, 4096, 0) == UINT32_MAX);
    assert(call("lambo_store_put_v1", map, 0, 7) == 0); // overwrite at capacity
    assert(call("lambo_store_erase_v1", map, 0) == 1);
    assert(call("lambo_store_erase_v1", map, 0) == 0);
    MEM_W(0, static_cast<int32_t>(output)) = 99;
    assert(call("lambo_store_get_v1", map, 0, output) == 0);
    assert(MEM_W(0, static_cast<int32_t>(output)) == 99);
    assert(call("lambo_store_destroy_v1", map) == 1);
    assert(call("lambo_store_destroy_v1", map) == UINT32_MAX);
    uint32_t set = call("lambo_store_create_v1", 1);
    assert(set != map && call("lambo_store_put_v1", set, 3, 99) == 1);
    assert(call("lambo_store_get_v1", set, 3, output) == 1);
    assert(MEM_W(0, static_cast<int32_t>(output)) == 1);
    assert(call("lambo_store_insert_v1", set, 42) == 0);
    uint32_t slots = call("lambo_store_create_v1", 2);
    uint32_t slot = call("lambo_store_insert_v1", slots, 42);
    assert(slot && call("lambo_store_get_v1", slots, slot, output) == 1);
    assert(MEM_W(0, static_cast<int32_t>(output)) == 42);
    assert(call("lambo_store_put_v1", slots, slot, 99) == UINT32_MAX);
    assert(call("lambo_store_erase_v1", slots, slot) == 1);
    assert(call("lambo_store_insert_v1", slots, 42) != slot);
    assert(call("lambo_store_get_v1", slots, slot, output) == 0);
    for (unsigned i = 2; i < 64; ++i) assert(call("lambo_store_create_v1", 0));
    assert(call("lambo_store_create_v1", 0) == 0);
    lambo_mod_boot(rdram, &ctx);
    assert(call("lambo_store_get_v1", set, 3, output) == UINT32_MAX);
    assert(call("lambo_store_create_v1", 0) != set);
}
