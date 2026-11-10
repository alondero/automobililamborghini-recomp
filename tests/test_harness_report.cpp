#include "lambo_harness_report.h"
#include "lambo_replay_runtime.h"
#include "lambo_vehicle.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

uint8_t* g_lambo_rdram = nullptr;
extern "C" int lambo_warp_env_applied() { return 0; }
extern "C" int lambo_warp_env_failed() { return 0; }
extern "C" int lambo_warp_env_circuit() { return 0; }
extern "C" int lambo_warp_env_laps() { return 0; }
extern "C" int lambo_warp_env_car() { return 0; }
extern "C" int lambo_warp_env_players() { return 0; }
extern "C" int lambo_warp_env_mode() { return 0; }
extern "C" int lambo_savestate_env_load_applied() { return 0; }
extern "C" int lambo_savestate_env_load_failed() { return 0; }
namespace lambo::replay_runtime {
void finalize() {}
Status status() { return {}; }
std::string terminal_reason() { return {}; }
}

void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    using namespace lambo::harness;
    require(snapshot().player_vehicle == -1 && snapshot().current_menu_screen == -1,
            "initial diagnostic sentinels must be preserved");
    require(sample_vi() == 1 && snapshot().first_vi, "VI count advances without guest RAM");
    std::vector<std::uint8_t> ram(0x924000);
    g_lambo_rdram = ram.data();
    auto word = [&](std::uint32_t address, std::uint32_t value) {
        std::memcpy(ram.data() + address - 0x80000000u, &value, 4);
    };
    auto half = [&](std::uint32_t address, std::uint16_t value) {
        std::memcpy(ram.data() + ((address ^ 2u) - 0x80000000u), &value, 2);
    };
    half(0x800CE6AC, 8);
    half(0x800CE794, 2);
    half(0x80098562, 5);
    half(LAMBO_VEHICLE_BASE + 0x0E, 1);
    word(LAMBO_VEHICLE_BASE + 0x90, std::uint32_t(-37));
    word(0x8008D1A4, 0x80001000);
    word(0x80001004, 0x80200000);
    sample_vi();
    auto value = snapshot();
    require(value.vis == 2 && value.current_state == 8 && value.max_state == 8 &&
            value.loaded_circuit == 3 && value.current_menu_screen == 5 && value.swaps == 1 &&
            value.player_vehicle == 0 && value.player_speed == -37 && value.max_abs_player_speed == 37,
            "guest probes retain field values and units");

    std::atomic<bool> finished{false};
    std::thread observer([&] {
        int previous = 2;
        while (!finished.load(std::memory_order_relaxed)) {
            auto current = snapshot();
            require(current.vis >= previous && current.max_state == 8,
                    "concurrent snapshots retain monotonic counters");
            previous = current.vis;
        }
    });
    std::thread creator([] { for (int i = 0; i < 1000; ++i) note_thread_created(); });
    for (int i = 0; i < 1000; ++i) {
        note_thread_created();
        sample_vi(); // One VI writer; readers and thread notifications may race.
    }
    creator.join();
    finished.store(true, std::memory_order_relaxed);
    observer.join();
    require(snapshot().threads == 2000 && vis_count() == 1002 && snapshot().swaps == 1,
            "concurrent notifications never lose counts or invent framebuffer swaps");
    half(0x800CE6AC, 7);
    sample_vi();
    require(snapshot().current_state == 7 && snapshot().max_state == 8,
            "leaving a race retains the maximum state");
    half(0x800CE6AC, 8);
    word(LAMBO_VEHICLE_BASE + 0x90, std::uint32_t(std::numeric_limits<int>::min()));
    sample_vi();
    require(snapshot().max_abs_player_speed == std::numeric_limits<int>::max(),
            "diagnostic speed magnitude saturates for the minimum signed value");
}
