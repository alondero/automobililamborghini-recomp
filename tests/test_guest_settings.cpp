// Manual-call o32 ABI regression: packed bit 31 must remain negative in a GPR.
#include "lambo_guest_calls.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace lambo::config { double menu_stick_sensitivity() { return 1.5; } }
static uint32_t sample;
static std::array<uint32_t, 3> observed;
extern "C" uint32_t lambo_vehicle_input_snapshot(uint32_t port, uint32_t kind) {
    if (port != 3 || kind != 1) std::abort();
    return sample;
}
extern "C" void lambo_vehicle_observe_native(uint8_t*, uint32_t kind, int32_t authored, uint32_t snapshot) {
    observed = {kind, static_cast<uint32_t>(authored), snapshot};
}
int main() {
    recomp_context ctx{};
    ctx.r4 = 3; ctx.r5 = 1;
    for (uint32_t value : {0u, 0x40000000u, 0x80000001u, 0xFFFFFFFFu}) {
        sample = value;
        lambo_vehicle_input(nullptr, &ctx);
        if (ctx.r2 != static_cast<gpr>(static_cast<int32_t>(value)) || ctx.r4 != 3 || ctx.r5 != 1)
            return EXIT_FAILURE;
    }
    lambo_get_menu_stick_sensitivity(nullptr, &ctx);
    if (ctx.f0.d != 1.5) return EXIT_FAILURE;
    ctx.r4 = 2; ctx.r5 = static_cast<int32_t>(-17); ctx.r6 = 0x80001234;
    lambo_vehicle_observe(nullptr, &ctx);
    if (observed != std::array<uint32_t, 3>{2, static_cast<uint32_t>(-17), 0x80001234})
        return EXIT_FAILURE;
    std::cout << "PASS guest setting/query/observer o32 ABI\n";
}
