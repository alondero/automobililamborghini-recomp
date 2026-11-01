#ifndef LAMBO_MOD_GUEST_H
#define LAMBO_MOD_GUEST_H
#include "librecomp/addresses.hpp"

namespace lambo::mods {
inline bool guest_words(uint32_t address, uint32_t size) {
    return address >= 0x80000000u && (address & 3u) == 0 &&
        uint64_t(address) + size <= 0x80000000ull + recomp::mem_size;
}
inline gpr guest_result(uint32_t value) {
    return static_cast<gpr>(static_cast<int32_t>(value));
}
}
#endif
