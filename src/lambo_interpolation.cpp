#include <cstdint>

#include "recomp.h"
#include "rt64_extended_gbi.h"

namespace {

void emit(uint8_t* rdram, const GfxCommand* commands, unsigned count) {
    const gpr cursor_address = static_cast<int32_t>(0x800A39CCu);
    gpr cursor = MEM_W(0, cursor_address);
    for (unsigned i = 0; i < count; ++i, cursor += 8) {
        MEM_W(0, cursor) = commands[i].values.word0;
        MEM_W(4, cursor) = commands[i].values.word1;
    }
    MEM_W(0, cursor_address) = cursor;
}

}

extern "C" void lambo_interpolation_object_begin(uint8_t* rdram, uint32_t object) {
    GfxCommand enable{};
    gEXEnable(&enable);
    emit(rdram, &enable, 1);

    // Car records (flag 8) submit body, then four wheels in a fixed order. Anonymous
    // matching can swap even stationary wheels after a pan and sustain that swap.
    // Other scene records contain variable scenery draws and still need AUTO.
    const gpr record = static_cast<int32_t>(0x800B69A8u + object * 0x10Cu);
    const bool car = (MEM_HU(0, record) & 8u) != 0;
    const uint32_t viewport = MEM_HU(0, static_cast<gpr>(static_cast<int32_t>(0x80098732u)));
    const uint32_t id = 0x10000000u | ((viewport & 3u) << 16) | (object & 0xFFFFu);
    GfxCommand group[2]{};
    gEXMatrixGroupDecomposed(group, id, G_EX_PUSH, 0,
        G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
        G_EX_COMPONENT_AUTO, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
        G_EX_COMPONENT_AUTO, car ? G_EX_ORDER_LINEAR : G_EX_ORDER_AUTO, G_EX_EDIT_NONE,
        G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
    emit(rdram, group, 2);
}

extern "C" void lambo_interpolation_object_end(uint8_t* rdram) {
    GfxCommand pop{};
    gEXPopMatrixGroup(&pop, 0);
    emit(rdram, &pop, 1);
}
