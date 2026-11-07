#include "lambo_guest_calls.h"

namespace lambo::config { double menu_stick_sensitivity(); }
extern "C" uint32_t lambo_vehicle_input_snapshot(uint32_t port, uint32_t kind);
extern "C" void lambo_vehicle_observe_native(uint8_t*, uint32_t, int32_t, uint32_t);

// Called synchronously by guest menu code. No guest state crosses this seam.
extern "C" void lambo_get_menu_stick_sensitivity(uint8_t*, recomp_context* ctx) {
    ctx->f0.d = lambo::config::menu_stick_sensitivity();
}

extern "C" void lambo_vehicle_input(uint8_t*, recomp_context* ctx) {
    // o32 word results are sign-extended in the VR4300's 64-bit registers.
    // The compiled guest tests the analog flag using a signed branch.
    ctx->r2 = static_cast<int32_t>(lambo_vehicle_input_snapshot(
        static_cast<uint32_t>(ctx->r4), static_cast<uint32_t>(ctx->r5)));
}

extern "C" void lambo_vehicle_observe(uint8_t* rdram, recomp_context* ctx) {
    lambo_vehicle_observe_native(rdram, static_cast<uint32_t>(ctx->r4),
        static_cast<int32_t>(ctx->r5), static_cast<uint32_t>(ctx->r6));
}
