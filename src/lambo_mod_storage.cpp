#include "lambo_mod_game.h"
#include "lambo_mod_guest.h"
#include "librecomp/overlays.hpp"
#include <algorithm>
#include <unordered_map>

namespace {
// All access is on the calling guest thread, like the runtime mod heap. Owner
// comes from the runtime import shim, never from an untrusted guest argument.
enum Kind : uint32_t { Map, Set, Slots };
struct Store {
    uintptr_t owner;
    uint32_t kind;
    std::unordered_map<uint32_t, uint32_t> values;
};
std::unordered_map<uint32_t, Store> stores;
// Monotonic IDs are never recycled, including across boots. Stale handles
// cannot name a newly created container or slot; exhaustion fails closed.
uint32_t next_store = 1;
uint32_t next_slot = 1;
constexpr size_t max_stores_per_mod = 64;
constexpr size_t max_values = 4096;

Store* owned(uint32_t id, uintptr_t owner) {
    auto it = stores.find(id);
    return it != stores.end() && it->second.owner == owner ? &it->second : nullptr;
}

void create(uint8_t*, recomp_context* ctx, uintptr_t owner) {
    uint32_t kind = ctx->r4;
    ctx->r2 = 0;
    if (kind > Slots || !next_store) return;
    const auto count = std::count_if(stores.begin(), stores.end(),
        [owner](const auto& item) { return item.second.owner == owner; });
    if (static_cast<size_t>(count) >= max_stores_per_mod) return;
    uint32_t id = next_store++;
    stores.emplace(id, Store{owner, kind, {}});
    ctx->r2 = lambo::mods::guest_result(id);
}

void put(uint8_t*, recomp_context* ctx, uintptr_t owner) {
    Store* store = owned(ctx->r4, owner);
    uint32_t key = ctx->r5, value = ctx->r6;
    ctx->r2 = -1;
    if (!store || store->kind == Slots) return;
    auto found = store->values.find(key);
    const bool adding = found == store->values.end();
    if (adding && store->values.size() >= max_values) return;
    store->values[key] = store->kind == Set ? 1 : value;
    ctx->r2 = adding ? 1 : 0;
}

void insert(uint8_t*, recomp_context* ctx, uintptr_t owner) {
    Store* store = owned(ctx->r4, owner);
    uint32_t value = ctx->r5;
    ctx->r2 = 0;
    if (!store || store->kind != Slots || store->values.size() >= max_values || !next_slot) return;
    uint32_t slot = next_slot++;
    store->values.emplace(slot, value);
    ctx->r2 = lambo::mods::guest_result(slot);
}

void get(uint8_t* rdram, recomp_context* ctx, uintptr_t owner) {
    Store* store = owned(ctx->r4, owner);
    uint32_t key = ctx->r5, output = ctx->r6;
    ctx->r2 = -1;
    if (!store || !lambo::mods::guest_words(output, 4)) return;
    auto found = store->values.find(key);
    ctx->r2 = found != store->values.end();
    // A missing key leaves output untouched; stored zero remains distinguishable.
    if (found != store->values.end()) MEM_W(0, static_cast<int32_t>(output)) = found->second;
}

void erase(uint8_t*, recomp_context* ctx, uintptr_t owner) {
    Store* store = owned(ctx->r4, owner);
    uint32_t key = ctx->r5;
    ctx->r2 = store ? static_cast<gpr>(store->values.erase(key)) : static_cast<gpr>(-1);
}

void destroy(uint8_t*, recomp_context* ctx, uintptr_t owner) {
    uint32_t id = ctx->r4;
    ctx->r2 = -1;
    if (!owned(id, owner)) return;
    stores.erase(id);
    ctx->r2 = 1;
}
}

void lambo::mods::reset_storage_api() { stores.clear(); }

void lambo::mods::register_storage_api() {
    recomp::overlays::register_ext_base_export("lambo_store_create_v1", create);
    recomp::overlays::register_ext_base_export("lambo_store_put_v1", put);
    recomp::overlays::register_ext_base_export("lambo_store_insert_v1", insert);
    recomp::overlays::register_ext_base_export("lambo_store_get_v1", get);
    recomp::overlays::register_ext_base_export("lambo_store_erase_v1", erase);
    recomp::overlays::register_ext_base_export("lambo_store_destroy_v1", destroy);
}
