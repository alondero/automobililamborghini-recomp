// Host-side spec for issue #80: the Win32 vectored exception handler must not
// kill the process on a benign first-chance exception (a C++ throw is normal
// control flow -- ultramodern ends guest threads by throwing thread_terminated
// through recompiled frames), while a genuine CPU fault in recompiled code must
// still produce the crash banner and exit(1).
//
// Windows-only (the VEH is Win32-only). Compile and run from the repo root with
// the host compiler -- no ROM build needed:
//   g++ -O0 -Ilib/N64ModernRuntime/ultramodern/include \
//       -Ilib/N64ModernRuntime/librecomp/include \
//       -Ilib/N64ModernRuntime/N64Recomp/include \
//       tests/test_crash_whitelist.cpp src/lambo_crash.cpp \
//       -o test_crash_whitelist && ./test_crash_whitelist
//
// The handler _Exit(1)s on a (perceived) crash, so each scenario runs in a
// child process: the no-arg driver spawns itself with a mode argument and
// asserts on the child's exit code.

#if !defined(_WIN32)
#include <cstdio>
int main(void) {
    std::fprintf(stderr, "SKIP: Win32-only test (VEH whitelist)\n");
    return 0;
}
#else

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <process.h>

#include "../src/lambo_crash.h"
#include "librecomp/sections.h"

extern "C" void lambo_crash_register_code_ptrs(
    uint32_t ram_addr, uint32_t size, FuncEntry* funcs, size_t num_funcs);

// Marked noinline so the registered code pointer really is the frame the
// exception is raised from (native_pc_to_vram matches PCs at/past it).
__attribute__((noinline)) static void guest_frame_throw_catch(uint8_t*, recomp_context*) {
    try {
        throw std::runtime_error("benign first-chance throw");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[test] caught: %s\n", e.what());
    }
}

__attribute__((noinline)) static void guest_frame_access_violation(uint8_t*, recomp_context*) {
    volatile uint32_t* guest_ptr = (volatile uint32_t*)(uintptr_t)0x80001000u;
    *guest_ptr = 0xDEADBEEFu;  // unmapped on the host: real EXCEPTION_ACCESS_VIOLATION
}

static void register_as_recomp(recomp_func_t* fn) {
    static FuncEntry fe;
    fe.func = fn;
    fe.offset = 0;
    fe.rom_size = 0;
    lambo_crash_register_code_ptrs(0x80001000u, 0x1000u, &fe, 1);
}

static int run_child(const char* self, const char* mode) {
    intptr_t rc = _spawnl(_P_WAIT, self, self, mode, nullptr);
    if (rc == -1) { std::perror("_spawnl"); return -1; }
    return (int)rc;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "benign-throw") == 0) {
        register_as_recomp((recomp_func_t*)guest_frame_throw_catch);
        lambo::crash::install();
        guest_frame_throw_catch(nullptr, nullptr);
        std::fprintf(stderr, "[test] survived benign throw\n");
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "fatal-av") == 0) {
        register_as_recomp((recomp_func_t*)guest_frame_access_violation);
        lambo::crash::install();
        guest_frame_access_violation(nullptr, nullptr);
        std::fprintf(stderr, "[test] ERROR: survived an access violation\n");
        return 42;  // must be unreachable: the handler _Exit(1)s
    }

    int failures = 0;

    // A C++ throw caught normally, with a registered recomp PC on the stack,
    // must NOT be treated as a crash (issue #80: this killed boot in the field).
    int rc = run_child(argv[0], "benign-throw");
    std::fprintf(stderr, "[driver] benign-throw child exit=%d (want 0)\n", rc);
    if (rc != 0) failures++;

    // A genuine access violation at a guest KSEG0 pointer inside recomp code
    // must still produce the crash banner and exit(1).
    rc = run_child(argv[0], "fatal-av");
    std::fprintf(stderr, "[driver] fatal-av child exit=%d (want 1)\n", rc);
    if (rc != 1) failures++;

    std::fprintf(stderr, failures ? "FAIL (%d)\n" : "PASS\n", failures);
    return failures ? 1 : 0;
}

#endif // _WIN32
