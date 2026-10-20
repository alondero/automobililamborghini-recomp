// Exercise real F7/F8 file I/O and the producer hook; the Python driver restarts
// this process for every cycle. Synthetic RAM only, with no ROM-derived bytes.
#ifdef NDEBUG
#undef NDEBUG
#endif
#undef NDEBUG
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "lambo_f3dex.h"
#include "lambo_log.h"
#include "lambo_race_field.h"
#include "recomp.h"

extern "C" {
void lambo_graphics_task_prepare(uint8_t*, uint32_t);
void lambo_savestate_request_save(void);
void lambo_savestate_request_load(void);
void lambo_savestate_tick(uint8_t*, recomp_context*);
}
namespace {
double density = 0.5;
unsigned relinks = 0;
std::string log_output;
void environment(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}
}
namespace lambo::config {
bool widescreen_fog_match() { return false; }
double fog_scale(int) { return density; }
}
volatile int g_log_threshold = LAMBO_LEVEL_DEBUG;
extern "C" void lambo_log_write(LamboLogLevel, const char*, const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log_output += line;
}
extern "C" void ultramodern_relink_thread_contexts(uint8_t*) { ++relinks; }
extern "C" void lambo_replay_state_loaded(void) {}
extern "C" void lambo_track_patch_on_savestate_loaded(uint8_t*) {}
extern "C" void lambo_rt_probe_invalidate(void) {}

int main(int argc, char** argv) {
    assert(argc >= 3);
    using lambo::f3dex::read;
    using lambo::f3dex::write;
    std::vector<uint8_t> memory(0x924000);
    auto* ram = memory.data();
    constexpr uint32_t task = 0xbf1e8, root = 0xbf400;
    write(ram, 0xce6ac, 8u << 16);
    write(ram, 0xce6a4, 1u << 16);
    write(ram, task, 1);
    write(ram, task + 0x10, 0x80086b60);
    write(ram, task + 0x18, 0x8008f210);
    write(ram, task + 0x1c, 0x800);
    write(ram, task + 0x30, root);
    write(ram, root, 0xbc000008);
    write(ram, root + 4, 0x64009d00);
    write(ram, root + 8, 0xb8000000);
    environment("LAMBO_STATE_LOAD", "");
    environment("LAMBO_STATE_SAVE", "");
    const std::string mode = argv[1];
    if (mode == "write" && argc > 3) write(ram, 0xce6a4, uint32_t(std::atoi(argv[3])) << 16);
    if (mode == "default" || mode == "legacy-default") density = 1.0;
    const auto prepare = [&] { lambo_graphics_task_prepare(ram, task | 0x80000000); };
    const auto load = [&] {
        log_output.clear();
        environment("LAMBO_STATE_FILE", argv[2]);
        lambo_savestate_request_load();
        lambo_savestate_tick(ram, nullptr);
    };
    if (mode == "reject") {
        write(ram, root + 4, 0x4000c000);
        const auto before = memory;
        load();
        assert(memory == before && relinks == 0);
        const size_t payload_size = size_t(0x800000) + LAMBO_FIELD_SIZE;
        if (std::filesystem::file_size(argv[2]) == size_t(32) + payload_size - 1) {
            assert(log_output.find("truncated (8536063/8536064 bytes)") != std::string::npos);
        }
        return 0;
    }
    if (mode == "write") write(ram, 0x902084, 0x12345678u);
    if (mode != "write") {
        load();
        assert(log_output.find("loaded 8536064 bytes") != std::string::npos);
        assert(relinks == 1);
        assert(read(ram, 0x902084) == 0x12345678u);
    }
    prepare();
    const uint32_t expected = mode == "legacy-default" ? 0x3200ce80u :
        density == 1.0 ? 0x64009d00u : 0x3200ce80u;
    assert(read(ram, root + 4) == expected);
    const auto once = memory;
    prepare(); assert(memory == once);
    if (mode == "default" || mode == "legacy-default") return 0;
    if (mode == "cycle") {
        // F8 must also work after the same process changes its cache history.
        density = 0.25;
        prepare(); assert(read(ram, root + 4) == 0x1900e740);
        load();
        assert(log_output.find("loaded 8536064 bytes") != std::string::npos);
        assert(relinks == 2);
        density = 0.5;
        prepare(); assert(read(ram, root + 4) == 0x3200ce80);
    }
    const char* destination = mode == "write" ? argv[2] : argv[3];
    environment("LAMBO_STATE_FILE", destination);
    const auto live = memory;
    lambo_savestate_request_save();
    lambo_savestate_tick(ram, nullptr);
    assert(memory == live); // Authored-word normalization must touch only the copy.
}
