// Sense-of-speed chase-camera + FOV knobs (see lambo_config.h for the user-facing
// story). This file holds the float-bit shims the recompiled hook text calls: hooks
// are emitted into ROM-derived C, which cannot include the C++ config header, so
// each knob is exposed as an extern "C" function returning IEEE-754 bits.
//
// The LIVE race camera lives in func_80032450's L_800324C8 path (verified live via
// LAMBO_CAM_TRACE probes -- the classic chase math at L_800320xx never runs in a
// race):
//   eye.x/z = car + viewdir * (per-player s16 distance, e.g. 900)
//             -- distance consumer mul.s at 0x80032708 (scale site 1)
//   eye.y   = track anchor table entry + 300.0
//             -- height adder add.s at 0x80032780 (height site 1)
// The demo/attract camera is produced by boot_pad_apply_calibration with an
// absolute eye = car - dir*900, carY+1000; its mul.s consumers take the same
// distance SCALE so both camera families respond consistently. func_80032450's
// unused-in-race chase paths and its follow-distance compare are scaled too, so
// any mode that does wake them stays self-consistent.
//
// With all knobs at their defaults (1.0 / 1.0 / +0) every shim returns exactly`r`n// what the ROM computed, so stock presentation is untouched.
#include <cstring>

#include "lambo_config.h"
#include "lambo_log.h"

namespace {

unsigned int float_bits(double v) {
    float f = static_cast<float>(v);
    unsigned int b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// LAMBO_CAM_TRACE=1 logs value TRANSITIONS per knob -- a menu change must show up
// as a "-> <new value>" line within a frame or two of the click, which makes the
// knob -> hook -> camera chain directly observable. Same pattern as
// libultra_stubs.c's LAMBO_PAK_TRACE.
bool cam_trace() {
    static const bool on = [] {
        const char* e = std::getenv("LAMBO_CAM_TRACE");
        return e && e[0] == '1';
    }();
    return on;
}

void trace(const char* what, double v) {
    static int remaining = 25;
    if (!cam_trace() || remaining <= 0) return;
    --remaining;
    LAMBO_LOG("camera", "%s -> %.1f\n", what, v);
}

// Shared rescale helper: authored float bits * scale, tracing only actual changes.
unsigned int scaled_bits(unsigned int authored_bits, double scale, const char* tag,
                         double& last_scale) {
    float authored;
    std::memcpy(&authored, &authored_bits, sizeof(authored));
    if (scale != last_scale) {
        last_scale = scale;
        trace(tag, scale);
    }
    return float_bits(static_cast<double>(authored) * scale);
}

double g_dist_last = -1.0;
double g_height_last = -1.0;

} // namespace

// Scale an authored length (float bits in, float bits out). Used at every site
// that multiplies an authored camera distance into an eye offset.
extern "C" unsigned int lambo_camera_scale_bits(unsigned int authored_bits) {
    return scaled_bits(authored_bits, lambo::config::camera_distance_scale(),
                       "dist scale", g_dist_last);
}

// Scale the authored eye-height offset (float bits of float(s16 table value) in).
extern "C" unsigned int lambo_camera_height_bits(unsigned int authored_bits) {
    return scaled_bits(authored_bits, lambo::config::camera_height_scale(),
                       "height scale", g_height_last);
}

extern "C" unsigned int lambo_camera_fov_bits(unsigned int authored_bits) {
    float authored;
    std::memcpy(&authored, &authored_bits, sizeof(authored));
    const double out = static_cast<double>(authored) + lambo::config::camera_fov_add();
    if (out != (double)authored) {
        static int remaining = 5;
        if (cam_trace() && remaining > 0) {
            --remaining;
            LAMBO_LOG("camera", "fov %.1f -> %.1f\n", (double)authored, out);
        }
    }
    return float_bits(out);
}

// DIAGNOSTIC (LAMBO_CAM_TRACE): called from the func_80032450 race-camera body
// right before it stores eye.y, with the player index and the computed eye.y.
extern "C" void lambo_camera_probe(unsigned int idx, float ey) {
    static int remaining = 40;
    static unsigned frame = 0;
    if (!cam_trace() || remaining <= 0) return;
    if (++frame % 60 != 1) return; // ~once a second at 60 VI/s
    --remaining;
    LAMBO_LOG("camera", "probe idx=%u eye.y=%.1f\n", idx, ey);
}

// DIAGNOSTIC (LAMBO_CAM_TRACE): route mapper -- logs the FIRST arrival at each
// hooked address id, revealing which bodies of func_80032450 a race executes.
extern "C" void lambo_camera_route(unsigned int id) {
    static bool seen[1024] = {};
    static int logged = 0;
    if (!cam_trace() || id >= 1024 || seen[id] || logged >= 64) return;
    seen[id] = true;
    ++logged;
    LAMBO_LOG("camera", "route %u\n", id);
}

// DIAGNOSTIC (LAMBO_CAM_TRACE): called at func_80032450 ENTRY with the camera-mode
// gate halfword (0x800CE6B0), the current player index (0x800CE6AA) and the current
// eye globals straight from RDRAM. Logs sparsely, to establish what actually drives
// the race camera: a moving eye triple means these globals ARE the camera.
extern "C" void lambo_camera_entry_probe(unsigned int gate, unsigned int idx,
                                         float ex, float ey, float ez) {
    static int remaining = 60;
    static unsigned n = 0;
    if (!cam_trace() || remaining <= 0) return;
    ++n;
    if (n > 4 && (n % 120) != 1) return; // then ~every 2 s
    --remaining;
    LAMBO_LOG("camera", "entry #%u gate=%u idx=%u eye=(%.1f, %.1f, %.1f)\n",
              n, gate, idx, ex, ey, ez);
}

