// Complete compiled guest selectors versus separately generated original ROM.
#include "recomp.h"
#include "guest_patch_data.inc"
#include "selector_categories.inc"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

extern "C" void func_8003F40C(uint8_t*, recomp_context*);
extern "C" void func_8003F56C(uint8_t*, recomp_context*);
extern "C" void reference_func_8003F40C(uint8_t*, recomp_context*);
extern "C" void reference_func_8003F56C(uint8_t*, recomp_context*);
static bool enabled;
static int query_count;
// Optionally toggle only after the first availability check in a call.
static bool enable_after_first;
extern "C" int selector_reference_enabled() { return enabled || (++query_count > 1 && enable_after_first); }
extern "C" void lambo_get_extra_vehicles(uint8_t*, recomp_context* ctx) {
    ctx->r2 = selector_reference_enabled();
}
static gpr address(uint32_t a) { return static_cast<int32_t>(a); }
static void half(uint8_t* rdram, uint32_t a, int value) { MEM_H(0, address(a)) = value; }

int main() {
    std::vector<uint8_t> initial(0x900000, 0xA5), reference, actual;
    auto* rdram = initial.data();
    std::copy(std::begin(guest_patch_data), std::end(guest_patch_data), initial.begin() + 0x801000);
    for (int i = 0; i < 24; ++i) half(rdram, 0x8013D490 + i * 24, categories[i]);
    const auto patch_funcs = {func_8003F40C, func_8003F56C};
    const auto original_funcs = {reference_func_8003F40C, reference_func_8003F56C};
    unsigned cases = 0;
    for (int mask = 0; mask < 256; ++mask) {
        for (int mode = 0; mode < 3; ++mode) {
            // Stock all-locked state does not terminate. Test it only enabled
            // or toggling on during the call; reachable menu states unlock 0/4.
            if (mask == 0 && mode == 0) continue;
            enabled = mode == 1;
            enable_after_first = mode == 2;
            for (int category = 0; category < 8; ++category)
                half(rdram, 0x800985C0 + category * 2,
                     (mask & (1 << category)) ? ((category & 1) ? -1 : 1) : 0);
            for (int player = 1; player <= 4; ++player) {
                half(rdram, 0x800CE6A6, player);
                for (int model = 0; model < 24; ++model) {
                    // Exercise every player/model/direction for single-category,
                    // stock-default, all-unlocked and all-locked states. Other
                    // masks rotate player/model coverage: the availability bits
                    // do not introduce a new indexing algorithm. This avoids
                    // several terabytes of redundant whole-RAM comparisons.
                    const bool full_matrix = mask == 0 || (mask & (mask - 1)) == 0 ||
                        mask == 0x11 || mask == 0xFF;
                    if (!full_matrix && (player != 1 + mask % 4 || model != mask % 24)) continue;
                    for (int other = 0; other < 4; ++other) half(rdram, 0x800CE7E8 + other * 2, (other + model) % 24);
                    half(rdram, 0x800CE7E8 + (player - 1) * 2, model);
                    for (int direction = 0; direction < 2; ++direction) {
                        reference = initial;
                        actual = initial;
                        recomp_context ref_ctx{}, patch_ctx{};
                        ref_ctx.r29 = patch_ctx.r29 = address(0x807FF000);
                        query_count = 0;
                        original_funcs.begin()[direction](reference.data(), &ref_ctx);
                        const int expected_queries = query_count;
                        query_count = 0;
                        patch_funcs.begin()[direction](actual.data(), &patch_ctx);
                        // Only compiler-owned stack scratch may differ. Every
                        // other byte, including all progression/unselected players,
                        // descriptors and availability values, must match.
                        if (expected_queries != query_count ||
                            std::memcmp(reference.data(), actual.data(), 0x7FE000) != 0 ||
                            std::memcmp(reference.data() + 0x7FF000, actual.data() + 0x7FF000,
                                        initial.size() - 0x7FF000) != 0) {
                            std::cerr << "selector parity failed mask=" << mask << " mode=" << mode
                                      << " player=" << player << " model=" << model
                                      << " direction=" << direction << '\n';
                            return EXIT_FAILURE;
                        }
                        ++cases;
                    }
                }
            }
        }
    }
    std::cout << "PASS " << cases << " complete selector ROM-parity cases\n";
}
