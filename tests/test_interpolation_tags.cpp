#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "recomp.h"
#include "rt64_extended_gbi.h"

extern "C" void lambo_interpolation_object_begin(uint8_t*, uint32_t);
extern "C" void lambo_interpolation_object_end(uint8_t*);

static void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    auto word = [&](uint32_t address) -> int32_t& {
        return MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address)));
    };
    auto tag = [&](uint32_t object, bool car = false) {
        MEM_H(0, static_cast<gpr>(static_cast<int32_t>(0x800B69A8u + object * 0x10Cu))) = car ? 8 : 0;
        word(0x800A39CC) = static_cast<int32_t>(0x80400000);
        lambo_interpolation_object_begin(rdram, object);
        const uint32_t id = word(0x8040000C);
        const uint32_t flags = word(0x80400010);
        require(((flags >> 13) & 3) == G_EX_COMPONENT_SKIP,
            "Rigid meshes must not morph when draw topology changes");
        require(((flags >> 17) & 3) == (car ? G_EX_ORDER_LINEAR : G_EX_ORDER_AUTO),
            "Car wheels need ordinal identity; scenery needs automatic ordering");
        lambo_interpolation_object_end(rdram);
        require(static_cast<uint32_t>(word(0x800A39CC)) == 0x80400020,
            "Object group must balance its push and pop");
        return id;
    };
    const auto body = tag(1, true);
    const auto wheel = tag(2);
    require(body != wheel && body != G_EX_ID_AUTO && body != G_EX_ID_IGNORE,
        "Separate render objects need distinct explicit identities");
    require(tag(2) == wheel && tag(1, true) == body,
        "Culling and draw-order changes must preserve object identity");
    MEM_H(0, static_cast<gpr>(static_cast<int32_t>(0x80098732u))) = 1;
    require(tag(1, true) != body,
        "Split-screen views must not share a linear transform sequence");
    std::cout << "Interpolation object identities survive draw reordering\n";
}
