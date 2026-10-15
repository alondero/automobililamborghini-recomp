#pragma once
#include <cstdint>

namespace lambo {
struct FogPolicy {
    bool match_1p;
    double scale;
    void command(uint8_t* ram, uint32_t offset, uint32_t w0, uint32_t w1) const;
};
FogPolicy fog_policy(const uint8_t* ram);
}
