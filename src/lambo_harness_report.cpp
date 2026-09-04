#include "lambo_harness_report.h"

#include "json/json.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include "lambo_file.h"
#include "lambo_log.h"
#include "lambo_replay_runtime.h"

extern uint8_t* g_lambo_rdram;

extern "C" int lambo_warp_env_applied(void);
extern "C" int lambo_warp_env_failed(void);
extern "C" int lambo_warp_env_circuit(void);
extern "C" int lambo_warp_env_laps(void);
extern "C" int lambo_warp_env_car(void);
extern "C" int lambo_warp_env_players(void);
extern "C" int lambo_warp_env_mode(void);
extern "C" int lambo_savestate_env_load_applied(void);
extern "C" int lambo_savestate_env_load_failed(void);

namespace lambo::harness {
namespace {

using json = nlohmann::json;

struct Metrics {
    std::mutex mutex;
    Snapshot snapshot;
    std::uint32_t last_vi_buffer{};
    int last_state{-1};
    int last_menu_screen{-9999};
    int last_menu_sub{-9999};
    int swaps_at_last_log{};
};

Metrics g_metrics;

bool environment_requested(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

std::uint32_t guest_word(const std::uint8_t* rdram, std::uint32_t address) {
    return *reinterpret_cast<const std::uint32_t*>(rdram + (address - 0x80000000u));
}

std::uint32_t guest_halfword(const std::uint8_t* rdram, std::uint32_t address) {
    const std::uint32_t word = guest_word(rdram, address & ~3u);
    return (address & 2u) ? (word & 0xFFFFu) : (word >> 16);
}

void state_probe_locked(int vi) {
    const std::uint8_t* rdram = g_lambo_rdram;
    if (rdram == nullptr) return;
    const int state = static_cast<int>(guest_halfword(rdram, 0x800CE6ACu));
    if (state < 0 || state > 16) return;

    g_metrics.snapshot.current_state = state;
    if (state > g_metrics.snapshot.max_state) g_metrics.snapshot.max_state = state;
    if (state == 8) {
        const int circuit = static_cast<int16_t>(guest_halfword(rdram, 0x800CE794u));
        if (circuit >= 0 && circuit < 6) g_metrics.snapshot.loaded_circuit = circuit + 1;

        constexpr std::uint32_t kVehicleBase = 0x800B69A8u;
        constexpr std::uint32_t kVehicleStride = 0x10Cu;
        for (int vehicle = 0; vehicle < 6; ++vehicle) {
            const std::uint32_t base = kVehicleBase +
                static_cast<std::uint32_t>(vehicle) * kVehicleStride;
            if (static_cast<int16_t>(guest_halfword(rdram, base + 0x0Eu)) != 1) continue;
            const int speed = static_cast<int32_t>(guest_word(rdram, base + 0x90u));
            g_metrics.snapshot.player_vehicle = vehicle;
            g_metrics.snapshot.player_speed = speed;
            const int magnitude = speed == std::numeric_limits<int>::min()
                ? std::numeric_limits<int>::max() : std::abs(speed);
            if (magnitude > g_metrics.snapshot.max_abs_player_speed) {
                g_metrics.snapshot.max_abs_player_speed = magnitude;
            }
            break;
        }
    }
    if (state != g_metrics.last_state) {
        LAMBO_LOG("state", "vi=%d  state=%d (was %d)\n", vi, state, g_metrics.last_state);
        g_metrics.last_state = state;
    }
}

void menu_probe_locked(int vi) {
    const std::uint8_t* rdram = g_lambo_rdram;
    if (rdram == nullptr) return;
    const int screen = static_cast<int16_t>(guest_halfword(rdram, 0x80098562u));
    const int sub = static_cast<int16_t>(guest_halfword(rdram, 0x80098560u));
    g_metrics.snapshot.current_menu_screen = screen;
    if (screen != g_metrics.last_menu_screen || sub != g_metrics.last_menu_sub) {
        LAMBO_LOG("menu", "vi=%d  screen=%d sub=%d (was %d/%d)\n",
                  vi, screen, sub, g_metrics.last_menu_screen, g_metrics.last_menu_sub);
        g_metrics.last_menu_screen = screen;
        g_metrics.last_menu_sub = sub;
    }
}

void pace_probe_locked(int vi) {
    const std::uint8_t* rdram = g_lambo_rdram;
    if (rdram == nullptr) return;
    const std::uint32_t next = guest_word(rdram, 0x8008D1A4u);
    if (next < 0x80000000u || next >= 0x80800000u) return;
    const std::uint32_t buffer = guest_word(rdram, next + 0x4u);
    if (buffer != g_metrics.last_vi_buffer) {
        g_metrics.last_vi_buffer = buffer;
        ++g_metrics.snapshot.swaps;
    }
    if ((vi % 600) == 0) {
        LAMBO_LOG("pace", "vi=%d swaps_last_600vi=%d (~%.1f fps)\n",
                  vi, g_metrics.snapshot.swaps - g_metrics.swaps_at_last_log,
                  (g_metrics.snapshot.swaps - g_metrics.swaps_at_last_log) / 10.0);
        g_metrics.swaps_at_last_log = g_metrics.snapshot.swaps;
    }
}

} // namespace

void note_thread_created() {
    int count = 0;
    {
        std::lock_guard lock(g_metrics.mutex);
        count = ++g_metrics.snapshot.threads;
    }
    if (count <= 12) LAMBO_LOG("probe", "game thread #%d started (osCreateThread)\n", count);
}

int vis_count() {
    std::lock_guard lock(g_metrics.mutex);
    return g_metrics.snapshot.vis;
}

int sample_vi() {
    std::lock_guard lock(g_metrics.mutex);
    const int vi = ++g_metrics.snapshot.vis;
    state_probe_locked(vi);
    menu_probe_locked(vi);
    pace_probe_locked(vi);
    if (!g_metrics.snapshot.first_vi) {
        g_metrics.snapshot.first_vi = true;
        LAMBO_LOG("probe", "FIRST VI retrace\n");
    }
    return vi;
}

Snapshot snapshot() {
    std::lock_guard lock(g_metrics.mutex);
    return g_metrics.snapshot;
}

void log_boot_summary() {
    const Snapshot value = snapshot();
    LAMBO_LOG("probe", "boot summary; threads=%d vis=%d first_vi=%d max_state=%d swaps=%d\n",
              value.threads, value.vis, static_cast<int>(value.first_vi), value.max_state,
              value.swaps);
}

Outcome finalize_outcome(const char* reason, int exit_code) {
    lambo::replay_runtime::finalize();
    const lambo::replay_runtime::Status replay = lambo::replay_runtime::status();
    std::string effective_reason = reason == nullptr ? std::string{} : reason;
    if (replay.failed) {
        exit_code = 2;
        const std::string terminal = lambo::replay_runtime::terminal_reason();
        effective_reason = terminal.empty() ? "replay_failed" : terminal;
    }
    return {std::move(effective_reason), exit_code};
}

void write_result(const Outcome& outcome) {
    const char* value = std::getenv("LAMBO_HARNESS_RESULT");
    if (value == nullptr || value[0] == '\0') return;

    try {
        const Snapshot metrics = snapshot();
        const lambo::replay_runtime::Status replay = lambo::replay_runtime::status();
        const std::filesystem::path output(value);
        std::error_code error;
        if (!output.parent_path().empty()) {
            std::filesystem::create_directories(output.parent_path(), error);
            if (error) {
                LAMBO_LOG_ERROR("harness", "cannot create result directory for %s: %s\n",
                                value, error.message().c_str());
                return;
            }
        }

        const json result = {
            {"schema", 1},
            {"outcome", outcome.exit_code == 0 ? "passed" : "failed"},
            {"reason", outcome.reason},
            {"exit_code", outcome.exit_code},
            {"vis", metrics.vis},
            {"swaps", metrics.swaps},
            {"max_state", metrics.max_state},
            {"final_state", metrics.current_state},
            {"menu_screen", metrics.current_menu_screen},
            {"loaded_circuit", metrics.loaded_circuit},
            {"player_vehicle", metrics.player_vehicle},
            {"player_speed", metrics.player_speed},
            {"max_abs_player_speed", metrics.max_abs_player_speed},
            {"warp", {
                {"requested", environment_requested("LAMBO_WARP")},
                {"applied", lambo_warp_env_applied() != 0},
                {"failed", lambo_warp_env_failed() != 0},
                {"circuit", lambo_warp_env_circuit()},
                {"laps", lambo_warp_env_laps()},
                {"car", lambo_warp_env_car()},
                {"players", lambo_warp_env_players()},
                {"mode", lambo_warp_env_mode()}}},
            {"state_load", {
                {"requested", environment_requested("LAMBO_STATE_LOAD")},
                {"applied", lambo_savestate_env_load_applied() != 0},
                {"failed", lambo_savestate_env_load_failed() != 0}}},
            {"replay", {
                {"configured", replay.configured},
                {"recording", replay.recording},
                {"active", replay.active},
                {"complete", replay.complete},
                {"failed", replay.failed},
                {"total_frames", replay.total_frames},
                {"frames_consumed", replay.frames_consumed},
                {"guest_frames_verified", replay.guest_frames_verified},
                {"dispatcher_ticks", replay.dispatcher_ticks}}}
        };

        std::filesystem::path temporary = output;
        temporary += ".tmp";
        struct TemporaryCleanup {
            const std::filesystem::path& path;
            bool owned{};
            bool published{};
            ~TemporaryCleanup() {
                if (owned && !published) {
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                }
            }
        } cleanup{temporary};
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            LAMBO_LOG_ERROR("harness", "cannot write result file %s\n", value);
            return;
        }
        cleanup.owned = true;
        stream << result.dump(2) << '\n';
        stream.flush();
        const bool write_ok = stream.good();
        stream.close();
        const bool close_ok = !stream.fail();
        if (!write_ok || !close_ok) {
            LAMBO_LOG_ERROR("harness", "failed while writing result file %s\n", value);
            return;
        }

        std::string publish_error;
        if (!lambo::file::atomic_replace(temporary, output, publish_error)) {
            LAMBO_LOG_ERROR("harness", "%s\n", publish_error.c_str());
            return;
        }
        cleanup.published = true;
    } catch (const std::exception& exception) {
        LAMBO_LOG_ERROR("harness", "cannot serialize result %s: %s\n", value, exception.what());
    } catch (...) {
        LAMBO_LOG_ERROR("harness", "cannot serialize result %s: unknown exception\n", value);
    }
}

} // namespace lambo::harness
