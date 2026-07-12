#include "lambo_gpu_advisory.h"

#include <atomic>
#include <cstdio>
#include <cstring>

// RT64's rt64_application.cpp forces Vulkan on Intel drivers <= this version
// (31.0.101.2115, an end-of-life driver where D3D12 device-removes). Keep in
// sync with BrokenIntelDriverD3D12 there; patch 0010 makes an explicit
// api_option=D3D12 win over that fallback.
static const uint64_t kBrokenIntelDriverD3D12 = 0x001F000000650843ULL;

void lambo_gpu_format_driver_version(uint64_t v, char* out, size_t out_size) {
    std::snprintf(out, out_size, "%u.%u.%u.%u",
                  (unsigned)((v >> 48) & 0xFFFF), (unsigned)((v >> 32) & 0xFFFF),
                  (unsigned)((v >> 16) & 0xFFFF), (unsigned)(v & 0xFFFF));
}

int lambo_gpu_intel_driver_predates_fix(uint64_t driver_version) {
    return driver_version <= kBrokenIntelDriverD3D12 ? 1 : 0;
}

int lambo_gpu_name_is_modern_intel(const char* device_name) {
    if (device_name == nullptr) {
        return 0;
    }
    // Gen12+ marketing names: "Intel(R) Iris(R) Xe Graphics", "Intel(R) Arc(TM) ...".
    // 6th-gen (D3D12-broken) names are "HD Graphics 5xx" / "Iris (Pro) Graphics 5xx"
    // and contain neither token, so they correctly fall through to RT64's fallback.
    return (std::strstr(device_name, "Xe") || std::strstr(device_name, "Arc")) ? 1 : 0;
}

int lambo_gpu_advisory_severity(uint32_t vendor, uint64_t driver_version,
                                int running_vulkan, const char* device_name) {
    if (vendor != LAMBO_GPU_VENDOR_INTEL || !lambo_gpu_intel_driver_predates_fix(driver_version)) {
        return LAMBO_GPU_ADVISORY_NONE;
    }
    if (!running_vulkan) {
        return LAMBO_GPU_ADVISORY_INFO; // on D3D12: works, driver merely old.
    }
    // On Vulkan with an old driver: only a MODERN Intel part is in trouble here
    // (device loss -> black screen). A 6th-gen part on Vulkan is RT64 doing the
    // right thing -- D3D12 would device-remove -- so that is INFO, not SEVERE.
    return lambo_gpu_name_is_modern_intel(device_name)
        ? LAMBO_GPU_ADVISORY_SEVERE
        : LAMBO_GPU_ADVISORY_INFO;
}

static char g_pending_text[1024];
static std::atomic<int> g_pending_state{0}; // 0 empty, 1 ready, 2 taken

void lambo_gpu_advisory_set_pending(const char* text) {
    int expected = 0;
    if (!g_pending_state.compare_exchange_strong(expected, -1, std::memory_order_acquire)) {
        return; // already posted or taken; first advisory wins
    }
    std::snprintf(g_pending_text, sizeof g_pending_text, "%s", text);
    g_pending_state.store(1, std::memory_order_release);
}

const char* lambo_gpu_advisory_take_pending(void) {
    int expected = 1;
    if (g_pending_state.compare_exchange_strong(expected, 2, std::memory_order_acquire)) {
        return g_pending_text;
    }
    return nullptr;
}

void lambo_gpu_advisory_message(int severity, const char* device_name,
                                uint64_t driver_version, const char* config_path,
                                char* out, size_t out_size) {
    char ver[32];
    lambo_gpu_format_driver_version(driver_version, ver, sizeof ver);

    if (severity == LAMBO_GPU_ADVISORY_SEVERE) {
        std::snprintf(out, out_size,
            "Your Intel graphics driver is outdated and known to break this game.\n"
            "\n"
            "GPU:    %s\n"
            "Driver: %s (needs 31.0.101.2115 or newer)\n"
            "\n"
            "The renderer fell back to Vulkan on this driver, which typically fails\n"
            "with a black screen (Vulkan device loss).\n"
            "\n"
            "To fix it, update the Intel graphics driver via Windows Update or\n"
            "intel.com/download-center.\n"
            "\n"
            "If you cannot update the driver, try Direct3D 12 instead by setting\n"
            "    \"api_option\": \"D3D12\"\n"
            "in %s",
            device_name, ver, config_path);
    }
    else {
        std::snprintf(out, out_size,
            "Note: your Intel graphics driver is outdated.\n"
            "GPU: %s, driver %s (31.0.101.2115 or newer recommended).\n"
            "If you see rendering problems, update the driver via Windows Update or\n"
            "intel.com/download-center. Config: %s",
            device_name, ver, config_path);
    }
}
