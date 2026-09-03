// Persistent graphics configuration (see lambo_config.h).
//
// Schema and behaviour mirror Zelda64Recomp's src/game/config.cpp graphics.json
// (same key names, same per-key fall-back-to-default on missing/corrupt values,
// and a "portable.txt in the LAUNCH directory -> keep config there" escape hatch),
// with a lightweight native menu for the common live-safe options.
#include "lambo_config.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>

#include "lambo_log.h"
#include "lambo_paths.h"

#include "json/json.hpp"

namespace {

constexpr const char* kGraphicsFile = "graphics.json";

// Window-size keys live in the same graphics.json (extra keys alongside the
// GraphicsConfig fields); 16:9 default so AspectRatio::Expand widens on first run.
constexpr int kDefaultWindowWidth = 1600;
constexpr int kDefaultWindowHeight = 900;

lambo::config::WindowSize g_window_size{kDefaultWindowWidth, kDefaultWindowHeight};

// RT64 texture-replacement paths (issue #9), persisted as extra graphics.json string
// keys alongside the GraphicsConfig fields (like the window size). Empty = feature off.
std::string g_texture_pack;
std::string g_texture_dump;
std::mutex g_texture_mutex;
std::mutex g_graphics_file_mutex;

// Widen the dense 3P/4P split-screen fog to the open 1P window/colour (issue #83).
// Enhancement default-on, consistent with the widescreen wave; 1P/2P are unaffected
// regardless (the rewrite self-gates on player count).
std::atomic_bool g_widescreen_fog_match{true};

// Draw the sky panorama in 3P/4P split screen like 1P/2P (issue #84). Same
// enhancement family as the fog match; 1P/2P take the sky path natively anyway.
std::atomic_bool g_widescreen_sky_match{true};

// Remove the ROM's per-mode LOD reductions (issues #87/#91): the scene builder
// func_8000A6C0 emits each track segment's scenery layer (record+0xC sub-DL: the
// distant canyon walls / roadside relief) only when players < 2, so 2P-4P races
// lose the far scenery entirely. Default-on; the emit still self-gates on the
// record pointer being non-null, so segments without a scenery DL are unaffected.
std::atomic_bool g_no_lod{true};
std::atomic_bool g_show_launcher{false};

// Per-circuit refinement of the global no_lod. Rationale + JSON key in
// lambo_config.h; see that header for the ship-safe default and the
// basic/pro split.
std::array<std::atomic_bool, 6> g_no_lod_circuit{{true, true, true, false, false, false}};

// Fog density multipliers (see lambo_config.h). Stored as double: the round-trip
// validity check in from_or_default would reject float (0.3 -> 0.3f -> 0.30000001...).
std::atomic<double> g_fog_scale{1.0};
std::array<double, 6> g_fog_scale_circuit{1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

// Draw-distance multipliers (see lambo_config.h). 1.5 covers the worst measured
// authored-radius pop (circuit 5 segment 31 at ~51k units vs its 35000 radius)
// without reaching the cross-track geometry an unlimited radius exposes.
std::atomic<double> g_draw_distance{1.5};
std::array<double, 6> g_draw_distance_circuit{1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

// Chase-camera + FOV sense-of-speed knobs (see lambo_config.h). Defaults are the
// ROM's authored constants: camera at its authored distance, 300 above the track
// anchor, no FOV delta.
std::atomic<double> g_camera_distance_scale{1.0};
std::atomic<double> g_camera_height_scale{1.0};
std::atomic<double> g_camera_fov_add{0.0};

// Main-thread-owned snapshot used by native menu actions. It avoids reading the
// runtime's reference-returning getter while another thread may be applying a change.
ultramodern::renderer::GraphicsConfig g_current_graphics{};

// The renderer's API is selected when it is constructed. Keep its last applied
// API separately so a later live-safe setting cannot smuggle a persisted,
// restart-only API choice into RT64's update queue.
ultramodern::renderer::GraphicsConfig g_live_graphics{};

// Read a key into `out`, keeping the existing (default) value when the key is
// missing or invalid. NLOHMANN_JSON_SERIALIZE_ENUM does NOT throw on an
// unrecognised string -- it silently maps it to the FIRST enumerator, which for
// several options (res/ar/rr/msaa) is not this port's default. Round-tripping the
// parsed value back to JSON detects that: a value that doesn't re-serialise to
// what we read was invalid, so the default is kept (and the user warned).
template <typename T>
void from_or_default(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    try {
        T parsed = it->get<T>();
        if (nlohmann::json(parsed) != *it) {
            LAMBO_LOG_WARN("config", "%s: invalid value %s -- keeping default\n",
                         key, it->dump().c_str());
            return;
        }
        out = parsed;
    } catch (const nlohmann::json::exception&) {
        LAMBO_LOG_WARN("config", "%s: wrong type -- keeping default\n", key);
    }
}

nlohmann::json graphics_config_json(const ultramodern::renderer::GraphicsConfig& c) {
    return nlohmann::json{
        {"res_option", c.res_option},
        {"wm_option", c.wm_option},
        {"hr_option", c.hr_option},
        {"api_option", c.api_option},
        {"ar_option", c.ar_option},
        {"msaa_option", c.msaa_option},
        {"rr_option", c.rr_option},
        {"hpfb_option", c.hpfb_option},
        {"rr_manual_value", c.rr_manual_value},
        {"ds_option", c.ds_option},
        {"developer_mode", c.developer_mode},
    };
}

nlohmann::json to_json(const ultramodern::renderer::GraphicsConfig& c) {
    std::lock_guard<std::mutex> lock(g_texture_mutex);
    nlohmann::json result = graphics_config_json(c);
    result.update({
        {"window_width", g_window_size.width},
        {"window_height", g_window_size.height},
        {"texture_pack", g_texture_pack},
        {"texture_dump", g_texture_dump},
        {"widescreen_fog_match", g_widescreen_fog_match.load()},
        {"widescreen_sky_match", g_widescreen_sky_match.load()},
        {"no_lod", g_no_lod.load()},
        {"no_lod_circuit", nlohmann::json::array({
            g_no_lod_circuit[0].load(), g_no_lod_circuit[1].load(),
            g_no_lod_circuit[2].load(), g_no_lod_circuit[3].load(),
            g_no_lod_circuit[4].load(), g_no_lod_circuit[5].load()})},
        {"fog_scale", g_fog_scale.load()},
        {"fog_scale_circuit", g_fog_scale_circuit},
        {"draw_distance", g_draw_distance.load()},
        {"draw_distance_circuit", g_draw_distance_circuit},
        {"camera_distance_scale", g_camera_distance_scale.load()},
        {"camera_height_scale", g_camera_height_scale.load()},
        {"camera_fov_add", g_camera_fov_add.load()},
        {"show_launcher", g_show_launcher.load()},
    });
    return result;
}

void from_json(const nlohmann::json& j, ultramodern::renderer::GraphicsConfig& c) {
    std::lock_guard<std::mutex> lock(g_texture_mutex);
    from_or_default(j, "res_option", c.res_option);
    from_or_default(j, "wm_option", c.wm_option);
    from_or_default(j, "hr_option", c.hr_option);
    from_or_default(j, "api_option", c.api_option);
    from_or_default(j, "ar_option", c.ar_option);
    from_or_default(j, "msaa_option", c.msaa_option);
    from_or_default(j, "rr_option", c.rr_option);
    from_or_default(j, "hpfb_option", c.hpfb_option);
    from_or_default(j, "rr_manual_value", c.rr_manual_value);
    from_or_default(j, "ds_option", c.ds_option);
    from_or_default(j, "developer_mode", c.developer_mode);
    from_or_default(j, "window_width", g_window_size.width);
    from_or_default(j, "window_height", g_window_size.height);
    from_or_default(j, "texture_pack", g_texture_pack);
    from_or_default(j, "texture_dump", g_texture_dump);
    bool widescreen_fog_match = g_widescreen_fog_match.load();
    bool widescreen_sky_match = g_widescreen_sky_match.load();
    bool no_lod = g_no_lod.load();
    std::array<bool, 6> no_lod_circuit{};
    for (size_t i = 0; i < no_lod_circuit.size(); ++i) {
        no_lod_circuit[i] = g_no_lod_circuit[i].load();
    }
    double fog_scale = g_fog_scale.load();
    double draw_distance = g_draw_distance.load();
    double camera_distance_scale = g_camera_distance_scale.load();
    double camera_height_scale = g_camera_height_scale.load();
    double camera_fov_add = g_camera_fov_add.load();
    bool show_launcher = g_show_launcher.load();
    from_or_default(j, "widescreen_fog_match", widescreen_fog_match);
    from_or_default(j, "widescreen_sky_match", widescreen_sky_match);
    from_or_default(j, "no_lod", no_lod);
    from_or_default(j, "no_lod_circuit", no_lod_circuit);
    from_or_default(j, "fog_scale", fog_scale);
    from_or_default(j, "fog_scale_circuit", g_fog_scale_circuit);
    from_or_default(j, "draw_distance", draw_distance);
    from_or_default(j, "draw_distance_circuit", g_draw_distance_circuit);
    from_or_default(j, "camera_distance_scale", camera_distance_scale);
    from_or_default(j, "camera_height_scale", camera_height_scale);
    from_or_default(j, "camera_fov_add", camera_fov_add);
    from_or_default(j, "show_launcher", show_launcher);
    g_widescreen_fog_match.store(widescreen_fog_match);
    g_widescreen_sky_match.store(widescreen_sky_match);
    g_no_lod.store(no_lod);
    for (size_t i = 0; i < no_lod_circuit.size(); ++i) {
        g_no_lod_circuit[i].store(no_lod_circuit[i]);
    }
    g_fog_scale.store(fog_scale);
    g_draw_distance.store(draw_distance);
    g_camera_distance_scale.store(camera_distance_scale);
    g_camera_height_scale.store(camera_height_scale);
    g_camera_fov_add.store(camera_fov_add);
    g_show_launcher.store(show_launcher);
    // Sanity-bound the window size: below the N64 framebuffer is useless, above 8K
    // is a typo -- either way SDL_CreateWindow would fail and the port would run
    // permanently headless, so reset to defaults instead.
    if (g_window_size.width < 320 || g_window_size.width > 7680 ||
        g_window_size.height < 240 || g_window_size.height > 4320) {
        LAMBO_LOG_WARN("config", "window %dx%d out of range -- using %dx%d\n",
                     g_window_size.width, g_window_size.height,
                     kDefaultWindowWidth, kDefaultWindowHeight);
        g_window_size = {kDefaultWindowWidth, kDefaultWindowHeight};
    }
}

// Read graphics.json into cfg (merging over whatever cfg already holds).
enum class ReadResult { Missing, Ok, Unparseable };

ReadResult read_graphics_file(const std::filesystem::path& path,
                              ultramodern::renderer::GraphicsConfig& cfg) {
    std::ifstream in{path};
    if (!in.good()) return ReadResult::Missing;
    try {
        nlohmann::json j;
        in >> j;
        from_json(j, cfg);
        return ReadResult::Ok;
    } catch (const nlohmann::json::exception& e) {
        LAMBO_LOG_WARN("config", "%s unparseable (%s); using defaults IN MEMORY"
                     " -- file left untouched, fix or delete it\n",
                     path.string().c_str(), e.what());
        return ReadResult::Unparseable;
    }
}

std::filesystem::path graphics_json_path() {
    // Test/harness override: point at (or isolate to) an explicit file.
    if (const char* p = std::getenv("LAMBO_GRAPHICS_CONFIG")) {
        return std::filesystem::path{p};
    }
    return lambo::config::app_config_dir() / kGraphicsFile;
}

bool write_graphics_json(const std::filesystem::path& path, const nlohmann::json& json) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out{path};
    if (!out.good()) {
        LAMBO_LOG_ERROR("config", "cannot write %s\n", path.string().c_str());
        return false;
    }
    out << json.dump(4) << "\n";
    out.flush();
    if (!out.good()) {
        LAMBO_LOG_ERROR("config", "write to %s FAILED (disk full / permissions?)"
                            " -- settings may not persist\n", path.string().c_str());
        return false;
    }
    return true;
}

// Merge a narrow update over the current file so menu interaction never removes
// hand-edited or forward-compatible graphics.json keys. A malformed file is left
// untouched so it remains recoverable by the user.
void save_graphics_updates_sync(const nlohmann::json& updates) {
    if (updates.empty()) return;
    std::lock_guard<std::mutex> file_lock(g_graphics_file_mutex);
    const std::filesystem::path path = graphics_json_path();
    nlohmann::json current;
    std::ifstream in{path};
    if (!in.good()) {
        // Startup creates a complete file synchronously. If it is removed while
        // the game is running, write the requested delta rather than reading the
        // main-thread snapshot from this worker thread.
        current = nlohmann::json::object();
    } else {
        try {
            in >> current;
            if (!current.is_object()) {
                LAMBO_LOG_WARN("config", "%s is not a JSON object; leaving it untouched\n",
                          path.string().c_str());
                return;
            }
        } catch (const nlohmann::json::exception& e) {
            LAMBO_LOG_WARN("config", "%s unparseable (%s); leaving it untouched\n",
                      path.string().c_str(), e.what());
            return;
        }
    }
    current.update(updates);
    write_graphics_json(path, current);
}

class GraphicsUpdateWriter {
public:
    GraphicsUpdateWriter() : worker_([this] { run(); }) {}

    ~GraphicsUpdateWriter() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_one();
        worker_.join();
    }

    void enqueue(const nlohmann::json& updates) {
        if (updates.empty()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.update(updates);
        }
        wake_.notify_one();
    }

    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.notify_one();
        drained_.wait(lock, [this] { return pending_.empty() && !writing_; });
    }

private:
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_ && pending_.empty()) break;
            nlohmann::json updates = std::move(pending_);
            pending_ = nlohmann::json::object();
            writing_ = true;
            lock.unlock();
            save_graphics_updates_sync(updates);
            lock.lock();
            writing_ = false;
            drained_.notify_all();
        }
        drained_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable drained_;
    nlohmann::json pending_ = nlohmann::json::object();
    bool writing_ = false;
    bool stopping_ = false;
    std::thread worker_;
};

GraphicsUpdateWriter& graphics_update_writer() {
    // Function-local lifetime ensures the worker is created only after startup has
    // configured the graphics snapshot and is joined during ordinary process exit.
    static GraphicsUpdateWriter writer;
    return writer;
}

void save_graphics_updates(const nlohmann::json& updates) {
    graphics_update_writer().enqueue(updates);
}

} // anonymous namespace

namespace lambo {
namespace config {

std::filesystem::path graphics_config_path() {
    return graphics_json_path();
}

std::filesystem::path app_config_dir() {
    return lambo::paths::app_config_dir();
}

ultramodern::renderer::GraphicsConfig default_graphics_config() {
    // Zelda64Recomp's shipped defaults (config.cpp:26-35), which are the
    // enhancement goals of issue wave 1: window-scaled internal resolution,
    // widescreen Expand with the HUD clamped to 16:9, and RT64 frame
    // interpolation up to the display refresh rate (game logic stays at its
    // native 30Hz tick either way -- ultramodern's VI clock is fixed).
    ultramodern::renderer::GraphicsConfig cfg{};
    cfg.res_option = ultramodern::renderer::Resolution::Auto;
    cfg.wm_option = ultramodern::renderer::WindowMode::Windowed;
    cfg.hr_option = ultramodern::renderer::HUDRatioMode::Clamp16x9;
    cfg.api_option = ultramodern::renderer::GraphicsApi::Auto;
    cfg.ar_option = ultramodern::renderer::AspectRatio::Expand;
    cfg.msaa_option = ultramodern::renderer::Antialiasing::MSAA2X;
    cfg.rr_option = ultramodern::renderer::RefreshRate::Display;
    cfg.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    cfg.rr_manual_value = 60;
    cfg.ds_option = 1;
    cfg.developer_mode = false;
    return cfg;
}

// (issue #67) The widescreen-HUD geometry shifts no longer need a config-time gate:
// src/lambo_hud_widescreen.c derives them from lambo_ws_get_hud_rect_aspect_bits(), which
// is 0-travel (4/3) for any config where the rect pins don't move (non-Expand, 4:3 output,
// or hr_option Original) AND tracks runtime window resizes and the hr_option clamp -- none
// of which a load-time bool could capture. The old lambo_ws_hud_widescreen_active()
// (Expand + Clamp16x9) has been removed.

ultramodern::renderer::GraphicsConfig load_and_apply_graphics() {
    ultramodern::renderer::GraphicsConfig cfg = default_graphics_config();
    const std::filesystem::path path = graphics_json_path();
    const ReadResult r = read_graphics_file(path, cfg);

    ultramodern::renderer::set_graphics_config(cfg);
    g_current_graphics = cfg;
    g_live_graphics = cfg;
    // Write the merged config back so the on-disk file is always complete and
    // editable (new keys appear with their defaults after an upgrade) -- but NEVER
    // overwrite a file that failed to parse: a hand-edit typo must stay recoverable,
    // not be replaced by defaults.
    if (r != ReadResult::Unparseable) {
        save_graphics(cfg);
    }
    LAMBO_LOG_INFO("config", "graphics config: %s\n", path.string().c_str());
    return cfg;
}

ultramodern::renderer::GraphicsConfig current_graphics() {
    return g_current_graphics;
}

void apply_graphics(const ultramodern::renderer::GraphicsConfig& cfg, bool apply_live) {
    const auto before = g_current_graphics;
    g_current_graphics = cfg;
    nlohmann::json updates = nlohmann::json::object();
    if (before.res_option != cfg.res_option) updates["res_option"] = cfg.res_option;
    if (before.wm_option != cfg.wm_option) updates["wm_option"] = cfg.wm_option;
    if (before.hr_option != cfg.hr_option) updates["hr_option"] = cfg.hr_option;
    if (before.api_option != cfg.api_option) updates["api_option"] = cfg.api_option;
    if (before.ar_option != cfg.ar_option) updates["ar_option"] = cfg.ar_option;
    if (before.msaa_option != cfg.msaa_option) updates["msaa_option"] = cfg.msaa_option;
    if (before.rr_option != cfg.rr_option) updates["rr_option"] = cfg.rr_option;
    if (before.hpfb_option != cfg.hpfb_option) updates["hpfb_option"] = cfg.hpfb_option;
    if (before.rr_manual_value != cfg.rr_manual_value) updates["rr_manual_value"] = cfg.rr_manual_value;
    if (before.ds_option != cfg.ds_option) updates["ds_option"] = cfg.ds_option;
    if (before.developer_mode != cfg.developer_mode) updates["developer_mode"] = cfg.developer_mode;
    if (apply_live) {
        auto live_cfg = cfg;
        live_cfg.api_option = g_live_graphics.api_option;
        ultramodern::renderer::set_graphics_config(live_cfg);
        g_live_graphics = live_cfg;
    }
    save_graphics_updates(updates);
}

void update_saved_window_mode(ultramodern::renderer::WindowMode wm) {
    // All runtime config values now have one main-thread snapshot. Do not re-run
    // from_json here: its enhancement fields are read by the game/render threads.
    // Mutating those arrays during an F11 press would introduce a data race.
    g_current_graphics.wm_option = wm;
    save_graphics_updates({{"wm_option", wm}});
}

void save_graphics(const ultramodern::renderer::GraphicsConfig& cfg) {
    save_graphics_updates_sync(to_json(cfg));
}

void flush_pending_graphics_updates() {
    graphics_update_writer().flush();
}

WindowSize window_size() {
    return g_window_size;
}

// Env var wins over the JSON key so a headless capture run can point at a scratch
// directory without editing (and re-saving) the user's graphics.json.
static std::string path_from_env_or(const char* env, const std::string& fallback) {
    if (const char* v = std::getenv(env)) {
        return v;
    }
    return fallback;
}

std::string texture_pack_path() {
    std::lock_guard<std::mutex> lock(g_texture_mutex);
    return path_from_env_or("LAMBO_TEXTURE_PACK", g_texture_pack);
}

std::string texture_dump_dir() {
    std::lock_guard<std::mutex> lock(g_texture_mutex);
    return path_from_env_or("LAMBO_TEXTURE_DUMP", g_texture_dump);
}

// LAMBO_FOG_MATCH_1P=1/0 overrides the JSON key for headless capture/testing.
bool widescreen_fog_match() {
    if (const char* v = std::getenv("LAMBO_FOG_MATCH_1P")) {
        return v[0] == '1';
    }
    return g_widescreen_fog_match.load();
}

void set_widescreen_fog_match(bool enabled) {
    g_widescreen_fog_match.store(enabled);
    save_graphics_updates({{"widescreen_fog_match", enabled}});
}

// LAMBO_SKY_MATCH_1P=1/0 overrides the JSON key for headless capture/testing.
bool widescreen_sky_match() {
    if (const char* v = std::getenv("LAMBO_SKY_MATCH_1P")) {
        return v[0] == '1';
    }
    return g_widescreen_sky_match.load();
}

void set_widescreen_sky_match(bool enabled) {
    g_widescreen_sky_match.store(enabled);
    save_graphics_updates({{"widescreen_sky_match", enabled}});
}

// LAMBO_NO_LOD=1/0 overrides the JSON key for headless capture/testing.
bool no_lod() {
    if (const char* v = std::getenv("LAMBO_NO_LOD")) {
        return v[0] == '1';
    }
    return g_no_lod.load();
}

void set_no_lod(bool enabled) {
    g_no_lod.store(enabled);
    save_graphics_updates({{"no_lod", enabled}});
}

// Per-circuit refinement of no_lod (see lambo_config.h). No env var: the JSON
// array is the only knob, since per-circuit toggling is a user-facing choice,
// not a headless test parameter. The function returns the global no_lod for an
// out-of-range circuit so a stale array length never disables PVS synth
// wholesale.
bool no_lod_circuit(int circuit) {
    if (circuit < 0 || circuit >= (int)g_no_lod_circuit.size()) return no_lod();
    return g_no_lod_circuit[(size_t)circuit].load();
}

void set_no_lod_circuit(int circuit, bool enabled) {
    if (circuit < 0 || circuit >= (int)g_no_lod_circuit.size()) return;
    g_no_lod_circuit[(size_t)circuit].store(enabled);
    save_graphics_updates({{"no_lod_circuit", nlohmann::json::array({
        g_no_lod_circuit[0].load(), g_no_lod_circuit[1].load(),
        g_no_lod_circuit[2].load(), g_no_lod_circuit[3].load(),
        g_no_lod_circuit[4].load(), g_no_lod_circuit[5].load()})}});
}

// LAMBO_FOG_SCALE=<float> overrides both JSON keys for headless capture/testing.
double fog_scale(int circuit) {
    double s;
    if (const char* v = std::getenv("LAMBO_FOG_SCALE")) {
        s = std::atof(v);
    } else {
        s = g_fog_scale.load();
        if (circuit >= 0 && circuit < (int)g_fog_scale_circuit.size()) {
            s *= g_fog_scale_circuit[(size_t)circuit];
        }
    }
    if (s < 0.0) s = 0.0;
    if (s > 8.0) s = 8.0;
    return s;
}

double global_fog_scale() {
    return g_fog_scale.load();
}

void set_global_fog_scale(double scale) {
    if (scale < 0.0) scale = 0.0;
    if (scale > 8.0) scale = 8.0;
    g_fog_scale.store(scale);
    save_graphics_updates({{"fog_scale", scale}});
}

// LAMBO_DRAW_DISTANCE=<float> overrides both JSON keys for capture/testing.
// Returns 0.0 for "unlimited"; positive values are clamped to [0.1, 100] (100x the
// shortest authored radius already exceeds any cross-track distance).
double draw_distance(int circuit) {
    double s;
    if (const char* v = std::getenv("LAMBO_DRAW_DISTANCE")) {
        s = std::atof(v);
    } else {
        s = g_draw_distance.load();
        if (circuit >= 0 && circuit < (int)g_draw_distance_circuit.size()) {
            s *= g_draw_distance_circuit[(size_t)circuit];
        }
    }
    if (s <= 0.0) return 0.0;
    if (s < 0.1) s = 0.1;
    if (s > 100.0) s = 100.0;
    return s;
}

double global_draw_distance() {
    return g_draw_distance.load();
}

void set_global_draw_distance(double scale) {
    if (scale > 0.0 && scale < 0.1) scale = 0.1;
    if (scale > 100.0) scale = 100.0;
    g_draw_distance.store(scale <= 0.0 ? 0.0 : scale);
    save_graphics_updates({{"draw_distance", g_draw_distance.load()}});
}

// LAMBO_CAMERA_DISTANCE_SCALE=<float> overrides the JSON key for capture/testing.
// Multiplier on the authored camera distance; 1.0 = stock, 0.5 = half as far.
double camera_distance_scale() {
    double v;
    if (const char* s = std::getenv("LAMBO_CAMERA_DISTANCE_SCALE")) {
        v = std::atof(s);
    } else {
        v = g_camera_distance_scale.load();
    }
    if (v < 0.2) v = 0.2;
    if (v > 3.0) v = 3.0;
    return v;
}

void set_camera_distance_scale(double v) {
    if (v < 0.2) v = 0.2;
    if (v > 3.0) v = 3.0;
    g_camera_distance_scale.store(v);
    save_graphics_updates({{"camera_distance_scale", v}});
}

// LAMBO_CAMERA_HEIGHT_SCALE=<float> overrides the JSON key for capture/testing.
// Multiplier on the authored eye-height offset; 1.0 = stock.
double camera_height_scale() {
    double v;
    if (const char* s = std::getenv("LAMBO_CAMERA_HEIGHT_SCALE")) {
        v = std::atof(s);
    } else {
        v = g_camera_height_scale.load();
    }
    if (v < 0.2) v = 0.2;
    if (v > 3.0) v = 3.0;
    return v;
}

void set_camera_height_scale(double v) {
    if (v < 0.2) v = 0.2;
    if (v > 3.0) v = 3.0;
    g_camera_height_scale.store(v);
    save_graphics_updates({{"camera_height_scale", v}});
}

// LAMBO_CAMERA_FOV_ADD=<float> overrides the JSON key for capture/testing.
// Delta degrees on top of each layout's authored FOV; bounded so a typo can't
// produce a degenerate projection (guPerspective cot(fovy/2) blows up near 0).
double camera_fov_add() {
    double v;
    if (const char* s = std::getenv("LAMBO_CAMERA_FOV_ADD")) {
        v = std::atof(s);
    } else {
        v = g_camera_fov_add.load();
    }
    if (v < -20.0) v = -20.0;
    if (v > 60.0) v = 60.0;
    return v;
}

void set_camera_fov_add(double v) {
    if (v < -20.0) v = -20.0;
    if (v > 60.0) v = 60.0;
    g_camera_fov_add.store(v);
    save_graphics_updates({{"camera_fov_add", v}});
}

bool show_launcher() {
    return g_show_launcher.load();
}

void set_show_launcher(bool enabled) {
    g_show_launcher.store(enabled);
    save_graphics_updates({{"show_launcher", enabled}});
}

} // namespace config
} // namespace lambo
