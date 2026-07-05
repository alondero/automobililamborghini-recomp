// Issue #13 / A14 — peer-port cite CV:LoD src/main/main.cpp:2870-2955 @ 180fd01.

#ifndef LAMBO_CRASH_H
#define LAMBO_CRASH_H

#include <cstdint>
#include <cstdio>

namespace lambo::crash {

struct SymbolInfo {
    const char* name;
    uint32_t    vram;
    uint32_t    size;
};

// Idempotent. Safe to call before register_overlays(); the native_pc -> vram
// map just stays empty until register_overlays() feeds it sections.
void install();

// Thread-safe; push sites are checked by handlers for fault context.
void record_recent(uint32_t vram, uint32_t ra_vram);

// nullptr when the vram is below the lowest symbol, in a gap, or past end.
const SymbolInfo* lookup(uint32_t vram, uint32_t* out_offset_bytes = nullptr);

// Smoke-test path. Use only with LAMBO_CRASH_TEST.
[[noreturn]] void crash_dump_and_die(const char* reason);

} // namespace lambo::crash

#endif // LAMBO_CRASH_H
