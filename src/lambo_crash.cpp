// Issue #13 / A14. See lambo_crash.h.

// _GNU_SOURCE: makes glibc expose SIGSTKSZ as a compile-time constant
// (without it, the POSIX install_posix alt-stack array is rejected with
// "storage size isn't constant"). Must precede any system header.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lambo_crash.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "librecomp/sections.h"
#include "recomp.h"

#if defined(_WIN32)
    #define LAMBO_CRASH_WIN32 1
    #include <windows.h>
    #include <dbghelp.h>
    #include <malloc.h>
    #pragma comment(lib, "dbghelp.lib")
#else
    #define LAMBO_CRASH_POSIX 1
    #include <csignal>
    #include <execinfo.h>
    #include <unistd.h>
#endif

// ----- Symbol table (parsed from lamborghini.syms.toml) -----
namespace lambo::crash {
namespace {

struct SymbolPool {
    std::vector<std::string> names;
    std::vector<SymbolInfo>  table;
} g_pool;

const SymbolInfo* lookup_in_pool(uint32_t vram, uint32_t* out_offset_bytes) {
    if (g_pool.table.empty()) return nullptr;
    auto it = std::upper_bound(g_pool.table.begin(), g_pool.table.end(), vram,
        [](uint32_t v, const SymbolInfo& s) { return v < s.vram; });
    if (it == g_pool.table.begin()) {
        if (out_offset_bytes) *out_offset_bytes = vram;
        return nullptr;
    }
    --it;
    if (vram - it->vram < it->size) {
        if (out_offset_bytes) *out_offset_bytes = vram - it->vram;
        return &*it;
    }
    return nullptr;  // gap or past-end -- don't lie about an out-of-range PC
}

bool load_symbol_table(const std::filesystem::path& syms_path) {
    std::ifstream f(syms_path);
    if (!f.good()) {
        std::fprintf(stderr, "[crash] syms file %s not found; raw N64 PCs only\n",
                     syms_path.string().c_str());
        return false;
    }

    auto trim = [](std::string& s) {
        auto issp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        size_t a = 0; while (a < s.size() && issp(s[a])) a++;
        size_t b = s.size(); while (b > a && issp(s[b - 1])) b--;
        s = s.substr(a, b - a);
    };

    // Two passes so we can reserve() capacity: nb.c_str() pointers are
    // stored in the table and would dangle across a vector reallocation.
    size_t reserve_n = 0;
    {
        std::ifstream fc(syms_path);
        std::string lc;
        bool in_fn = false;
        while (std::getline(fc, lc)) {
            trim(lc);
            if (lc.empty() || lc[0] == '#') continue;
            if (!in_fn) {
                if (lc.find("functions") != std::string::npos && lc.find('[') != std::string::npos)
                    in_fn = true;
                continue;
            }
            if (lc.find(']') != std::string::npos) { in_fn = false; continue; }
            if (lc.find("name") != std::string::npos && lc.find("vram") != std::string::npos)
                reserve_n++;
        }
    }
    g_pool.names.clear();
    g_pool.table.clear();
    g_pool.names.reserve(reserve_n);
    g_pool.table.reserve(reserve_n);

    std::string line;
    bool in_functions = false;
    while (std::getline(f, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (!in_functions) {
            if (line.find("functions") != std::string::npos && line.find('[') != std::string::npos)
                in_functions = true;
            continue;
        }
        if (line.find(']') != std::string::npos) { in_functions = false; continue; }
        std::string name; uint32_t vram = 0; uint32_t size = 0;
        size_t i = 0;
        bool got_all = true;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == ',' || line[i] == '\t' ||
                                       line[i] == '{' || line[i] == '}')) i++;
            if (i >= line.size()) break;
            size_t eq = line.find('=', i);
            if (eq == std::string::npos) break;
            std::string key = line.substr(i, eq - i);
            trim(key);
            i = eq + 1;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
            if (i >= line.size()) { got_all = false; break; }
            if (line[i] == '"') {
                size_t end = line.find('"', i + 1);
                if (end == std::string::npos) { got_all = false; break; }
                std::string val = line.substr(i + 1, end - (i + 1));
                if (key == "name") name = val;
                i = end + 1;
            } else {
                size_t end = i;
                while (end < line.size() && line[end] != ',' && line[end] != ' ' &&
                       line[end] != '\t' && line[end] != '}') end++;
                std::string val = line.substr(i, end - i);
                trim(val);
                if (val.empty()) { got_all = false; break; }
                // try/catch: std::stoul throws on bad input -- a malformed
                // toml must NOT std::terminate before this handler installs.
                try {
                    if (key == "vram") vram = (uint32_t)std::stoul(val, nullptr, 16);
                    else if (key == "size") size = (uint32_t)std::stoul(val, nullptr, 16);
                } catch (std::exception const&) { got_all = false; break; }
                i = end;
            }
        }
        if (!name.empty() && got_all && vram != 0) {
            g_pool.names.emplace_back(std::move(name));
            const std::string& nb = g_pool.names.back();
            g_pool.table.push_back({ nb.c_str(), vram, size });
        }
    }

    // stable_sort: aliases at the same vram stay in source order so the
    // attributed name is identical run-to-run (matters for crash-digest CI).
    std::stable_sort(g_pool.table.begin(), g_pool.table.end(),
        [](const SymbolInfo& a, const SymbolInfo& b) { return a.vram < b.vram; });
    std::fprintf(stderr, "[crash] loaded %zu symbols from %s\n",
                 g_pool.table.size(), syms_path.string().c_str());
    return !g_pool.table.empty();
}

void find_and_load_symbol_table() {
    const char* env = std::getenv("LAMBO_CRASH_SYMBOLS");
    std::vector<std::filesystem::path> candidates;
    if (env && env[0] != '\0') candidates.emplace_back(env);
    candidates.emplace_back(std::filesystem::current_path() / "lamborghini.syms.toml");
#if defined(_WIN32)
    char exe_buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe_buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::filesystem::path exedir = std::filesystem::path(std::string(exe_buf, n)).parent_path();
        candidates.emplace_back(exedir / "lamborghini.syms.toml");
        candidates.emplace_back(exedir.parent_path() / "lamborghini.syms.toml");
    }
#else
    {
        std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe");
        candidates.emplace_back(self.parent_path() / "lamborghini.syms.toml");
        candidates.emplace_back(self.parent_path().parent_path() / "lamborghini.syms.toml");
    }
#endif
    for (const auto& p : candidates) {
        if (load_symbol_table(p)) return;
    }
}

} // namespace

const SymbolInfo* lookup(uint32_t vram, uint32_t* out_offset_bytes) {
    return lookup_in_pool(vram, out_offset_bytes);
}

// ----- Native-PC -> N64-vram map (populated by register_overlays) -----
namespace {
std::unordered_map<const void*, uint32_t> g_native_to_vram;
} // namespace

extern "C" void lambo_crash_register_code_ptrs(
    uint32_t ram_addr, uint32_t /*size*/, FuncEntry* funcs, size_t num_funcs) {
    for (size_t k = 0; k < num_funcs; k++) {
        g_native_to_vram.emplace((const void*)funcs[k].func, ram_addr + funcs[k].offset);
    }
}

// ----- Trace ring (32-entry function-lookup) -----
namespace {
constexpr uint32_t kRingSize = 32;
struct RingEntry {
    uint32_t vram;
    uint32_t ra_vram;
    uint64_t tsc_us;
    std::thread::id tid;
};
RingEntry  g_ring[kRingSize] = {};
uint32_t   g_ring_cursor = 0;        // under g_ring_lock
std::mutex g_ring_lock;
uint64_t   g_tsc_base_us = 0;

uint64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

void record_recent(uint32_t vram, uint32_t ra_vram) {
    std::lock_guard<std::mutex> lk(g_ring_lock);
    if (g_tsc_base_us == 0) g_tsc_base_us = now_us();
    uint32_t i = g_ring_cursor++ % kRingSize;
    g_ring[i] = { vram, ra_vram, now_us() - g_tsc_base_us, std::this_thread::get_id() };
}

namespace {
void print_ring_impl(FILE* fp, const char* header) {
    // try_lock: the crashing thread may already hold g_ring_lock mid-
    // record_recent; a non-recursive mutex would deadlock the dump.
    if (!g_ring_lock.try_lock()) {
        std::fprintf(fp, "%s (skipped: ring lock held by crashing thread)\n", header);
        return;
    }
    std::lock_guard<std::mutex> lk(g_ring_lock, std::adopt_lock);
    uint32_t cur = g_ring_cursor;
    std::fprintf(fp, "%s (most recent first, %u entries max):\n", header, kRingSize);
    for (uint32_t off = 0; off < kRingSize; off++) {
        // 'cur' is the next slot to write (oldest); walk backwards so [0]
        // is the most recent push, like a stack trace.
        uint32_t idx = (cur + kRingSize - 1 - off) % kRingSize;
        const RingEntry& e = g_ring[idx];
        if (e.tsc_us == 0 && e.vram == 0 && e.ra_vram == 0) continue;
        uint32_t offb = 0;
        const SymbolInfo* si = lookup_in_pool(e.vram, &offb);
        char vname[160];
        if (si) std::snprintf(vname, sizeof(vname), "%s+0x%X", si->name, offb);
        else    std::snprintf(vname, sizeof(vname), "0x%08X", e.vram);
        std::fprintf(fp, "  [%2u] t=%llu.%03llums vram=%s ra=0x%08X tid=%zu\n",
                     off,
                     (unsigned long long)(e.tsc_us / 1000),
                     (unsigned long long)(e.tsc_us % 1000),
                     vname, e.ra_vram,
                     std::hash<std::thread::id>{}(e.tid));
    }
}
} // namespace

// ----- KSEG0/KSEG1 -> RDRAM byte offset -----
// KSEG0 (0x80000000-) / KSEG1 (0xA0000000-) are unmapped on the recomp's
// gpr address space; the low 29 bits are the RDRAM byte offset (the
// recomp MEM_W macro uses `rdram + (reg + 0x80000000)`).
constexpr uint32_t kRdramMask = 0x1FFFFFFFu;

bool rdram_offset_for_vram(uint32_t vram, uint32_t& off_out) {
    uint32_t hi = vram & 0xE0000000u;
    if (hi == 0x80000000u || hi == 0xA0000000u) { off_out = vram & kRdramMask; return true; }
    return false;
}

// ----- Native PC -> nearest recompiled function -----
namespace {
// Sanity ceiling: a real PC inside a recomp function is within a few KB
// of its FuncEntry.func; further out is almost certainly a system DLL
// and should not be misattributed to a random recomp body.
constexpr uintptr_t kMaxRecompDistance = 64 * 1024;

uint32_t native_pc_to_vram(const void* pc) {
    if (g_native_to_vram.empty()) return 0;
    auto it = g_native_to_vram.find(pc);
    if (it != g_native_to_vram.end()) return it->second;
    const void* best = nullptr; uint32_t best_v = 0;
    for (const auto& kv : g_native_to_vram) {
        if (kv.first > pc) continue;
        if ((uintptr_t)pc - (uintptr_t)kv.first > kMaxRecompDistance) continue;
        if (best == nullptr || kv.first > best) { best = kv.first; best_v = kv.second; }
    }
    return best_v;
}
} // namespace

// ----- Crash handlers + dump plumbing -----
namespace {

#if defined(_WIN32)
// Only Win32 calls this (POSIX uses signal names directly).
const char* win32_exception_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:  return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW:    return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_PRIV_INSTRUCTION:  return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_BREAKPOINT:        return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_SINGLE_STEP:       return "EXCEPTION_SINGLE_STEP";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_FLT_DENORMAL_OPERAND:  return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:    return "EXCEPTION_FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:          return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:       return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:         return "EXCEPTION_FLT_UNDERFLOW";
        default:                              return "EXCEPTION_UNKNOWN";
    }
}
#endif

void print_native_backtrace(FILE* fp, const void* const* pcs, int n, const char* kind) {
    std::fprintf(fp, "  native backtrace (%s, %d frames):\n", kind, n);
    for (int i = 0; i < n; i++) {
        const void* pc = pcs[i];
        uint32_t vram = native_pc_to_vram(pc);
        char vbuf[160] = "";
        if (vram != 0) {
            uint32_t offb = 0;
            const SymbolInfo* si = lookup_in_pool(vram, &offb);
            if (si) std::snprintf(vbuf, sizeof(vbuf), "  --> n64 %s+0x%X", si->name, offb);
            else    std::snprintf(vbuf, sizeof(vbuf), "  --> n64 0x%08X", vram);
        }
        std::fprintf(fp, "    [%2d] native=%p%s\n", i, pc, vbuf);
    }
}

void dump_crash_state(const char* reason, uint32_t vram_guess) {
    FILE* fp = stderr;
    std::fprintf(fp, "\n========================= NATIVE CRASH =========================\n");
    if (reason) std::fprintf(fp, "Reason: %s\n", reason);
    if (vram_guess != 0) {
        uint32_t off = 0;
        std::fprintf(fp, "Guest PC at crash: 0x%08X", vram_guess);
        if (rdram_offset_for_vram(vram_guess, off))
            std::fprintf(fp, "  (RDRAM offset 0x%07X)\n", off);
        else
            std::fprintf(fp, "\n");
        uint32_t offb = 0;
        if (const SymbolInfo* si = lookup_in_pool(vram_guess, &offb))
            std::fprintf(fp, "  function: %s+0x%X (size 0x%X)\n", si->name, offb, si->size);
    }
    print_ring_impl(fp, "Trace ring");
}

void final_dump_and_die(const char* reason, uint32_t vram_guess,
                        const void* const* native_pcs, int native_pcs_n,
                        const char* kind) {
    dump_crash_state(reason, vram_guess);
    if (native_pcs && native_pcs_n > 0)
        print_native_backtrace(stderr, native_pcs, native_pcs_n, kind);
    std::fflush(stderr);
    // _Exit (not abort): we finished the dump; do not re-enter via abort->SIGABRT.
    std::_Exit(EXIT_FAILURE);
}

#if defined(LAMBO_CRASH_WIN32)

LONG WINAPI win32_vectored_handler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Skip debugger-only codes; let gdb handle those.
    if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    // Whitelist: only treat as a real crash if recompiled code is on the
    // call chain, OR the fault address is a guest KSEG0/KSEG1 pointer. Windows
    // DLLs (D3D12, NVIDIA driver, XInput) raise benign structured exceptions
    // during init/teardown (EXCEPTION_GUARD_PAGE on heap guard pages,
    // EXCEPTION_INVALID_HANDLE on CloseHandle, etc.) -- blanket _Exit on those
    // kills the process during boot. CaptureStackBackTrace is VEH-safe.
    void* native_pcs[64];
    USHORT n = CaptureStackBackTrace(0, 64, (PVOID*)native_pcs, nullptr);
    bool has_recomp_pc = false;
    for (USHORT i = 0; i < n; i++) {
        if (native_pc_to_vram(native_pcs[i]) != 0) { has_recomp_pc = true; break; }
    }
    // ACCESS_VIOLATION: ExceptionInformation[1] is the faulting address.
    // Treat as a guest KSEG0/KSEG1 pointer iff the low 32 bits match the
    // segment mask; otherwise it's a native host pointer.
    uint32_t vram_guess = 0;
    bool guest_fault = false;
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        uint32_t lo = (uint32_t)(uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
        uint32_t hi = lo & 0xE0000000u;
        if (hi == 0x80000000u || hi == 0xA0000000u) {
            vram_guess = lo;
            guest_fault = true;
        }
    }
    if (!has_recomp_pc && !guest_fault)
        return EXCEPTION_CONTINUE_SEARCH;

    if (code == EXCEPTION_STACK_OVERFLOW) {
        // STACK_OVERFLOW: a recursive recomp trips the guard page; the
        // dump path's own fprintf would re-fault it. _resetstkoflw bumps
        // RSP past the guard, then we print a minimal banner and exit.
        _resetstkoflw();
        std::fprintf(stderr,
            "\n========================= NATIVE CRASH =========================\n"
            "Reason: %s\n"
            "(dump truncated: stack overflow -- no native backtrace recoverable)\n",
            win32_exception_name(code));
        std::_Exit(EXIT_FAILURE);
    }
    final_dump_and_die(win32_exception_name(code), vram_guess,
                       (const void* const*)native_pcs, n, "Win32 CaptureStackBackTrace");
    return EXCEPTION_CONTINUE_SEARCH;  // unreachable; final_dump_and_die is [[noreturn]]
}

#else // POSIX

void posix_signal_handler(int sig, siginfo_t* info, void* /*ucontext*/) {
    const char* reason = nullptr;
    switch (sig) {
        case SIGSEGV: reason = "SIGSEGV (segmentation fault)"; break;
        case SIGBUS:  reason = "SIGBUS (bus error)";          break;
        case SIGFPE:  reason = "SIGFPE (floating-point/div0)"; break;
        case SIGILL:  reason = "SIGILL (illegal instruction)"; break;
        default:      reason = "<unknown signal>";             break;
    }
    // si_addr is the faulting address; it's a guest pointer iff it parses
    // as KSEG0/KSEG1.
    uint32_t vram_guess = 0;
    if (sig == SIGSEGV && info && info->si_addr) {
        uint32_t v = (uint32_t)(uintptr_t)info->si_addr;
        uint32_t hi = v & 0xE0000000u;
        if (hi == 0x80000000u || hi == 0xA0000000u) vram_guess = v;
    }
    void* native_pcs[64];
    int n = backtrace(native_pcs, 64);
    final_dump_and_die(reason, vram_guess,
                       (const void* const*)native_pcs, n, "POSIX backtrace(3)");
    std::_Exit(EXIT_FAILURE);  // unreachable
}

void install_posix() {
    // Alt-stack so SIGSEGV from stack-overflow delivers the handler frame
    // off the just-overflowed stack. SIGSTKSZ is process-bound on glibc
    // but defined as a non-constant runtime expression in modern glibc,
    // so we use a hardcoded 64 KiB here (matches glibc's x86_64 default
    // and is comfortably larger than what the backtrace+fprintf path
    // needs).
    constexpr std::size_t kAltStackSize = 64 * 1024;
    static char alt_stack_buf[kAltStackSize];
    static stack_t alt_stack{};
    alt_stack.ss_sp    = alt_stack_buf;
    alt_stack.ss_size  = sizeof(alt_stack_buf);
    alt_stack.ss_flags = 0;
    if (sigaltstack(&alt_stack, nullptr) != 0)
        std::fprintf(stderr, "[crash] sigaltstack failed; stack-overflow dumps may not land\n");

    struct sigaction sa{};
    sa.sa_sigaction = posix_signal_handler;
    // SA_ONSTACK: use the alt stack above. SA_RESETHAND: chain to OS
    // default on the second crash (avoid infinite-loop dumps). SA_SIGINFO:
    // get si_addr. SIGABRT is deliberately absent -- recomp'd do_break /
    // switch_error use abort() for controlled terminations; treating
    // those as crashes would emit spurious dumps on every debug branch.
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    int fatal[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL };
    for (int s : fatal) {
        if (sigaction(s, &sa, nullptr) != 0)
            std::fprintf(stderr, "[crash] sigaction(%d) failed; crash dumps disabled for this signal\n", s);
    }
}

#endif
} // namespace

void install() {
    static std::atomic<bool> once{false};
    bool expected = false;
    if (!once.compare_exchange_strong(expected, true)) return;
#if defined(LAMBO_CRASH_POSIX)
    // Install handlers BEFORE the syms file IO: a slow disk read of the
    // .toml must not race a fault on another thread that just started.
    install_posix();
#endif
    find_and_load_symbol_table();
#if defined(LAMBO_CRASH_WIN32)
    void* veh = AddVectoredExceptionHandler(0, win32_vectored_handler);
    if (veh == nullptr)
        std::fprintf(stderr, "[crash] AddVectoredExceptionHandler returned NULL; dumps UNRELIABLE\n");
#endif
    std::fprintf(stderr, "[crash] native crash handler installed (%s)\n",
#if defined(LAMBO_CRASH_WIN32)
                 "Win32 AddVectoredExceptionHandler"
#else
                 "POSIX sigaction (with sigaltstack)"
#endif
    );
}

[[noreturn]] void crash_dump_and_die(const char* reason) {
    // Walk the stack ourselves rather than re-entering via the OS signal
    // path -- breakpoints / SIGABRT are filtered by some handlers / traps.
    void* native_pcs[64];
    int n = 0;
#if defined(LAMBO_CRASH_WIN32)
    n = (int)CaptureStackBackTrace(0, 64, (PVOID*)native_pcs, nullptr);
#else
    n = backtrace(native_pcs, 64);
#endif
    final_dump_and_die(reason ? reason : "deliberate crash", 0,
                       (const void* const*)native_pcs, n,
#if defined(LAMBO_CRASH_WIN32)
                       "Win32 CaptureStackBackTrace (deliberate)");
#else
                       "POSIX backtrace(3) (deliberate)");
#endif
    std::_Exit(EXIT_FAILURE);
}

} // namespace lambo::crash
