// Native crash backtrace (issue #13 / A14 -- peer-port cite: CV:LoD
// src/main/main.cpp:2870-2955 @ 180fd01).
//
// What this module does
// ---------------------
// On install, it:
//   1. Parses lamborghini.syms.toml at a list of candidate paths (caller set
//      via std::filesystem::current_path() or env LAMBO_CRASH_SYMBOLS) to
//      build a sorted N64-vram -> {name,size} table (the *function-lookup*
//      table the issue title refers to).
//   2. Walks the recompiled overlay section_table (the same tables
//      register_overlays.cpp uses) to build a recomp_func_t* -> N64-vram
//      map. This lets a Win32 EXCEPTION_POINTERS / POSIX backtrace(3) frame
//      be translated back into the guest function the recompiled C code
//      represents.
//   3. Installs the platform signal handlers:
//        - Win32:   AddVectoredExceptionHandler(0, ...) on
//                   EXCEPTION_ACCESS_VIOLATION, EXCEPTION_INT_DIVIDE_BY_ZERO,
//                   EXCEPTION_STACK_OVERFLOW, EXCEPTION_ILLEGAL_INSTRUCTION.
//        - POSIX:   sigaction on SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL,
//                   using backtrace(3) + backtrace_symbols(3) for the
//                   native stack.
// On either path, the handler dumps:
//   - the offending vram and its KSEG0 / RDRAM offset
//   - the N64 $ra (return address) recovered from the recompiled stack
//     when the OS is cooperative enough to let us reach rdram
//   - the recent 32-entry trace ring of (n64_vram, ra_vram, tsc_us, tid)
//     populated at install time + on demand via record_recent()
//   - the native backtrace translated to guest N64 functions, where
//     resolvable
//
// Scaffolding / removal
// ---------------------
// None. This module is the implementation the issue asked for. The single
// deferred piece is "instrument every recompiled function entry/exit" --
// that requires patching the N64Recomp cgenerator and was explicitly
// *not* in scope (would touch generated code; see CLAUDE.md ground rules).
// Instead the trace ring is filled from the boundaries the harness *does*
// own: vi_cb, update_gfx_stub, install() itself, and a deliberate crash
// can be induced with crash_dump_and_die() to smoke-test the handler.

#ifndef LAMBO_CRASH_H
#define LAMBO_CRASH_H

#include <cstdint>
#include <cstdio>

namespace lambo::crash {

struct SymbolInfo {
    const char* name;     // never null; falls back to "(no symbol)"
    uint32_t    vram;     // N64 runtime virtual address (KSEG0 form)
    uint32_t    size;     // size in bytes from lamborghini.syms.toml
};

// Install signal handlers + build the function-lookup tables. Idempotent.
// Safe to call from main() after register_overlays() has run. Calling
// before register_overlays() is fine -- the C-func -> vram map will just
// be empty and frames without a compiled body will show "(no recomp body)".
void install();

// Push (vram, ra_vram) into the 32-entry ring. Thread-safe (a global
// mutex covers the small write). Cheap enough to call from any harness
// checkpoint (vi_cb, audio submit, etc.). ra_vram may be 0 if unknown.
void record_recent(uint32_t vram, uint32_t ra_vram);

// Look up the symbol that contains a given N64 vram. Returns nullptr
// if the symbol table couldn't be populated. If out_offset_bytes is
// non-null, sets it to the byte offset of vram inside the returned
// symbol's range, so callers can show "+0x24" suffixes.
const SymbolInfo* lookup(uint32_t vram, uint32_t* out_offset_bytes = nullptr);

// Force a crash dump with `reason`. Exported so a deliberate abort() can
// exercise the handler without waiting for a real crash.
// [[noreturn]] so the compiler/optimizer know control never returns.
[[noreturn]] void crash_dump_and_die(const char* reason);

} // namespace lambo::crash

#endif // LAMBO_CRASH_H
