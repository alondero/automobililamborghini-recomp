// Original host-native smoke library, distributed beside the NRM, never inside
// it. Uses the runtime guest-context ABI and has no game or renderer linkage.
#include "recomp.h"
#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
extern "C" {
#ifndef LAMBO_SMOKE_API_VERSION
#define LAMBO_SMOKE_API_VERSION 1
#endif
EXPORT uint32_t recomp_api_version = LAMBO_SMOKE_API_VERSION;
EXPORT void lambo_native_smoke(uint8_t*, recomp_context* ctx) {
    ctx->r2 = static_cast<gpr>(static_cast<int32_t>(static_cast<uint32_t>(ctx->r4) ^ 0x4C414D42u));
}
}
