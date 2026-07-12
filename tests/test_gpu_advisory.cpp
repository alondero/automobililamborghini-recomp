// Host-side spec for issue #109: on the reporter's machine (Intel Iris Xe,
// driver 30.0.101.1404) RT64's old-Intel-driver workaround forces Vulkan, and
// that Vulkan driver immediately loses the device (VK_ERROR_DEVICE_LOST) ->
// black screen with no explanation. The port must recognise the condition and
// tell the user what is wrong and how to fix it.
//
// Pure logic, no ROM or GPU needed. Compile and run from the repo root:
//   g++ -O0 tests/test_gpu_advisory.cpp src/lambo_gpu_advisory.cpp -o test_gpu_advisory
//   ./test_gpu_advisory

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/lambo_gpu_advisory.h"

// Driver versions pack four 16-bit words: AA.BB.CCC.DDDD.
static const uint64_t kReporterDriver = 0x001e00000065057cULL; // 30.0.101.1404 (issue #109)
static const uint64_t kRT64Threshold  = 0x001F000000650843ULL; // 31.0.101.2115 (rt64_application.cpp)
static const uint64_t kModernDriver   = 0x0020000000651c86ULL; // 32.0.101.7302

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static bool contains(const char* haystack, const char* needle) {
    return std::strstr(haystack, needle) != nullptr;
}

int main() {
    char buf[32];

    // --- driver version formatting (the packed-word decode) --------------------
    lambo_gpu_format_driver_version(kReporterDriver, buf, sizeof buf);
    CHECK(std::strcmp(buf, "30.0.101.1404") == 0);
    lambo_gpu_format_driver_version(kRT64Threshold, buf, sizeof buf);
    CHECK(std::strcmp(buf, "31.0.101.2115") == 0);

    // --- the predicate must mirror RT64's <= comparison ------------------------
    CHECK(lambo_gpu_intel_driver_predates_fix(kReporterDriver) == 1);
    CHECK(lambo_gpu_intel_driver_predates_fix(kRT64Threshold) == 1);      // <= is inclusive
    CHECK(lambo_gpu_intel_driver_predates_fix(kRT64Threshold + 1) == 0);
    CHECK(lambo_gpu_intel_driver_predates_fix(kModernDriver) == 0);

    // --- modern-Intel discriminator (Xe/Arc keep D3D12; #109 core fix) ----------
    CHECK(lambo_gpu_name_is_modern_intel("Intel(R) Iris(R) Xe Graphics") == 1);
    CHECK(lambo_gpu_name_is_modern_intel("Intel(R) Arc(TM) A770 Graphics") == 1);
    CHECK(lambo_gpu_name_is_modern_intel("Intel(R) HD Graphics 530") == 0);   // 6th-gen
    CHECK(lambo_gpu_name_is_modern_intel("Intel(R) Iris(R) Pro Graphics 580") == 0);
    CHECK(lambo_gpu_name_is_modern_intel(nullptr) == 0);
    CHECK(lambo_gpu_name_is_modern_intel("") == 0);

    // --- severity matrix --------------------------------------------------------
    const char* kXe   = "Intel(R) Iris(R) Xe Graphics";
    const char* k6thGen = "Intel(R) HD Graphics 530";
    // Modern Intel on Vulkan with an old driver = the exact #109 failure: severe.
    CHECK(lambo_gpu_advisory_severity(LAMBO_GPU_VENDOR_INTEL, kReporterDriver, /*vulkan*/1, kXe)
          == LAMBO_GPU_ADVISORY_SEVERE);
    // Modern Intel on D3D12 (now the default after the narrowing): works, just note it.
    CHECK(lambo_gpu_advisory_severity(LAMBO_GPU_VENDOR_INTEL, kReporterDriver, /*vulkan*/0, kXe)
          == LAMBO_GPU_ADVISORY_INFO);
    // 6th-gen on Vulkan is RT64 doing the RIGHT thing (D3D12 would device-remove):
    // do NOT scare the user with a black-screen popup -- just a note.
    CHECK(lambo_gpu_advisory_severity(LAMBO_GPU_VENDOR_INTEL, kReporterDriver, /*vulkan*/1, k6thGen)
          == LAMBO_GPU_ADVISORY_INFO);
    // Up-to-date Intel driver: silence, regardless of GPU/API.
    CHECK(lambo_gpu_advisory_severity(LAMBO_GPU_VENDOR_INTEL, kModernDriver, 1, kXe)
          == LAMBO_GPU_ADVISORY_NONE);
    // Other vendors: not our advisory, even on ancient drivers.
    CHECK(lambo_gpu_advisory_severity(0x10DEu /*NVIDIA*/, kReporterDriver, 1, kXe)
          == LAMBO_GPU_ADVISORY_NONE);

    // --- the user-facing message ------------------------------------------------
    char msg[1024];
    lambo_gpu_advisory_message(LAMBO_GPU_ADVISORY_SEVERE,
                               "Intel(R) Iris(R) Xe Graphics", kReporterDriver,
                               "C:\\Users\\cathe\\AppData\\Local\\LamborghiniRecomp\\graphics.json",
                               msg, sizeof msg);
    CHECK(contains(msg, "30.0.101.1404"));                 // their driver, decoded
    CHECK(contains(msg, "Intel(R) Iris(R) Xe Graphics"));  // their GPU
    CHECK(contains(msg, "black screen"));                  // what they'll see
    CHECK(contains(msg, "update"));                        // the real fix
    CHECK(contains(msg, "31.0.101.2115"));                 // how new is new enough
    CHECK(contains(msg, "api_option"));                    // the escape hatch key...
    CHECK(contains(msg, "D3D12"));                         // ...and value
    CHECK(contains(msg, "graphics.json"));                 // where to put it
    CHECK(contains(msg, "C:\\Users\\cathe\\AppData\\Local\\LamborghiniRecomp\\graphics.json"));

    // INFO variant (already on D3D12) must not threaten a black screen.
    lambo_gpu_advisory_message(LAMBO_GPU_ADVISORY_INFO,
                               "Intel(R) Iris(R) Xe Graphics", kReporterDriver,
                               "graphics.json", msg, sizeof msg);
    CHECK(contains(msg, "update"));
    CHECK(!contains(msg, "black screen"));

    // Truncation must stay NUL-terminated and not overrun.
    char tiny[16];
    lambo_gpu_advisory_message(LAMBO_GPU_ADVISORY_SEVERE, "GPU", kReporterDriver,
                               "path", tiny, sizeof tiny);
    CHECK(std::strlen(tiny) < sizeof tiny);

    // --- popup handoff: take-once semantics --------------------------------------
    CHECK(lambo_gpu_advisory_take_pending() == nullptr);   // nothing posted yet
    lambo_gpu_advisory_set_pending("first advisory");
    lambo_gpu_advisory_set_pending("second must not overwrite");
    const char* taken = lambo_gpu_advisory_take_pending();
    CHECK(taken != nullptr && std::strcmp(taken, "first advisory") == 0);
    CHECK(lambo_gpu_advisory_take_pending() == nullptr);   // consumed exactly once

    if (failures == 0) {
        std::fprintf(stderr, "PASS: all gpu-advisory checks\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) FAILED\n", failures);
    return 1;
}
