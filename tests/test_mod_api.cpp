#include "recomp.h"
#include "lambo_log.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" void lambo_log_v1(uint8_t*, recomp_context*);
volatile int g_log_threshold = LAMBO_LEVEL_INFO;
static std::string captured;
extern "C" void lambo_log_write(LamboLogLevel, const char*, const char* format, ...) {
    char message[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    captured = message;
}

int main() {
    std::vector<uint8_t> memory(2048, 'x');
    uint8_t* rdram = memory.data();
    recomp_context ctx{};
    const char text[] = "mod 100% ready";
    for (size_t i = 0; i < sizeof(text); ++i) memory[i ^ 3] = text[i];
    ctx.r4 = static_cast<int32_t>(0x80000000u);
    lambo_log_v1(rdram, &ctx);
    if (ctx.r2 != sizeof(text) - 1 || captured != "mod 100% ready\n") return 1;
    captured.clear();
    for (uint32_t invalid : {0u, 0x7FFFFFFFu, 0xA0000000u, 0xFFFFFFFFu}) {
        ctx.r4 = invalid;
        lambo_log_v1(rdram, &ctx);
        if (ctx.r2 != static_cast<gpr>(-1) || !captured.empty()) return 2;
    }
    std::memset(rdram, 'x', memory.size());
    ctx.r4 = static_cast<int32_t>(0x80000000u);
    lambo_log_v1(rdram, &ctx);
    if (ctx.r2 != static_cast<gpr>(-1) || !captured.empty()) return 3;
    memory[1023 ^ 3] = 0;
    lambo_log_v1(rdram, &ctx);
    return ctx.r2 == 1023 && captured.size() == 1024 ? 0 : 4;
}
