#pragma once
#include <cstdint>
#include <unordered_map>

namespace lambo {
struct FogPolicy {
    bool match_1p;
    double scale;
};
FogPolicy fog_policy(const uint8_t* ram);

// Guest-producer state shared by successive task walks, including asset lists.
// Only copied command words and offsets survive a walk; no command pointer does.
class FogRewriter {
    struct Word {
        uint32_t opcode, authored, rewritten;
    };
    uintptr_t memory_identity = 0;
    std::unordered_map<uint32_t, Word> words;
public:
    void begin(const uint8_t* ram);
    void command(uint8_t* ram, uint32_t offset, uint32_t w0, uint32_t w1, FogPolicy policy);
};
}
