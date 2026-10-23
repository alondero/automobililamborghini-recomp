#pragma once
#include <cstdint>

namespace lambo {
// Per-task state; source/tile state follows calls exactly as the RDP does.
class HudTextureBounds {
    uint8_t* ram;
    uint32_t source = 0;
    uint32_t image = 0;
    uint32_t tile = 0;
public:
    unsigned dial = 0, maps = 0, suffix = 0, logo = 0;
    explicit HudTextureBounds(uint8_t* memory) : ram(memory) {}
    void command(uint32_t offset, uint32_t w0, uint32_t w1);
};
}
