#include "lambo_log.h"
#include "librecomp/addresses.hpp"
#include "recomp.h"
#include <algorithm>
#include <array>

// MIPS ABI: a0 is a KSEG0 pointer to a NUL-terminated byte string, v0 returns
// byte count or -1. Read at most 1024 bytes with the runtime's word-swapped
// byte layout, synchronously on the calling guest thread; retain no pointers.
// This versioned logging export is also the smoke package's execution oracle.
extern "C" void lambo_log_v1(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t address = static_cast<uint32_t>(ctx->r4);
    ctx->r2 = static_cast<gpr>(-1);
    if (address < 0x80000000u || uint64_t(address) >= 0x80000000ull + recomp::mem_size)
        return;
    std::array<char, 1024> message{};
    const size_t available = std::min(message.size(),
        size_t(0x80000000ull + recomp::mem_size - address));
    for (size_t i = 0; i < available; ++i) {
        message[i] = static_cast<char>(MEM_BU(i, static_cast<int32_t>(address)));
        if (message[i] == '\0') {
            LAMBO_LOG_INFO("mod", "%s\n", message.data());
            ctx->r2 = i;
            return;
        }
    }
}
