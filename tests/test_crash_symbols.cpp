// Crash symbols are parsed on a background thread. This host test waits for
// the published table, then checks that a known guest PC resolves to the name
// in lamborghini.syms.toml. It never raises a real crash.
#include "lambo_crash.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
void set_environment(const char* key, const char* value) {
#if defined(_WIN32)
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <lamborghini.syms.toml>\n", argv[0]);
        return 2;
    }
    set_environment("LAMBO_CRASH_SYMBOLS", argv[1]);

    lambo::crash::install();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const lambo::crash::SymbolInfo* symbol = nullptr;
    while (std::chrono::steady_clock::now() < deadline) {
        symbol = lambo::crash::lookup(0x80001CD0u);
        if (symbol != nullptr) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (symbol == nullptr) {
        std::fprintf(stderr, "FAIL: background symbol table was never published\n");
        return 1;
    }
    if (std::strcmp(symbol->name, "func_800028D0") != 0) {
        std::fprintf(stderr, "FAIL: 0x80001CD0 resolved to %s, want func_800028D0\n",
                     symbol->name);
        return 1;
    }
    uint32_t offset = 0;
    const lambo::crash::SymbolInfo* inner = lambo::crash::lookup(0x80001CD4u, &offset);
    if (inner == nullptr || offset != 4) {
        std::fprintf(stderr, "FAIL: in-function lookup returned an unexpected result\n");
        return 1;
    }
    std::fprintf(stderr, "PASS: background symbol table published and queryable\n");
    return 0;
}
