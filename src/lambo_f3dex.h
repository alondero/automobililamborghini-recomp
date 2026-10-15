// Bounded traversal of the USA game's F3DEX v1 lists, shared by producer policies.
// Words are native-endian u32 in word-swapped guest RAM. No pointer is retained.
#pragma once
#include <cstdint>
#include <cstring>

namespace lambo::f3dex {
constexpr uint32_t ram_size = 0x800000;
inline uint32_t read(const uint8_t* ram, uint32_t offset) {
    uint32_t value;
    std::memcpy(&value, ram + offset, sizeof(value));
    return value;
}
inline void write(uint8_t* ram, uint32_t offset, uint32_t value) {
    std::memcpy(ram + offset, &value, sizeof(value));
}

struct WalkResult {
    uint32_t commands = 0;
    bool budget_limit = false;
    bool depth_limit = false;
};

template<class Visitor> class Walker {
    const uint8_t* ram;
    Visitor& visit;
    uint32_t segments[16]{};
    WalkResult result;

    bool resolve(uint32_t address, uint32_t& offset) const {
        const auto hi = address >> 24;
        if (hi >= 0x80 && hi <= 0xbf) offset = address & 0x1fffffff;
        else if (hi < 16) offset = (segments[hi] & 0x1fffffff) + (address & 0xffffff);
        else return false;
        // Extended heap sublists contain the port's panorama, not HUD or fog.
        // Skip those calls; never dereference outside the native 8 MiB RAM.
        return offset <= ram_size - 8 && (offset & 7) == 0;
    }

    void run(uint32_t address, unsigned depth) {
        if (depth > 16) { result.depth_limit = true; return; }
        uint32_t offset;
        if (!resolve(address, offset)) return;
        while (offset <= ram_size - 8) {
            if (result.commands == 200000) { result.budget_limit = true; return; }
            ++result.commands;
            const auto w0 = read(ram, offset), w1 = read(ram, offset + 4);
            visit(offset, w0, w1);
            switch (w0 >> 24) {
            case 0xbc:
                if ((w0 & 255) == 6) segments[((w0 >> 8) & 0xffff) / 4 & 15] = w1;
                break;
            case 0x06:
                // RT64's G_DL tests bit 16, not the entire parameter byte.
                if (w0 & 0x10000) {
                    if (!resolve(w1, offset)) return;
                    continue;
                }
                run(w1, depth + 1);
                if (result.budget_limit || result.depth_limit) return;
                break;
            case 0xb8: return;
            case 0x64:
                // Skip RT64 extended-command payloads, which are data rather
                // than F3DEX commands. Sizes follow rt64_gbi_extended.cpp.
                switch (w0 & 0xffffff) {
                case 0x02: case 0x08: offset += 16; break;
                case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
                case 0x0c: case 0x13: case 0x14: case 0x30: case 0x31:
                    offset += 8; break;
                default: break;
                }
                break;
            default: break;
            }
            offset += 8;
        }
    }
public:
    Walker(const uint8_t* memory, Visitor& visitor) : ram(memory), visit(visitor) {}
    WalkResult walk(uint32_t address) { run(address, 0); return result; }
};

template<class Visitor> WalkResult walk(const uint8_t* ram, uint32_t address, Visitor visit) {
    return Walker<Visitor>(ram, visit).walk(address);
}
}
