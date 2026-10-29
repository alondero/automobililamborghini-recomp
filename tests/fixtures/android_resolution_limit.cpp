#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

enum class RenderDeviceVendor { UNKNOWN = 0, NVIDIA = 0x10de };
struct RenderDeviceDescription {
    std::string name;
    RenderDeviceVendor vendor;
    uint64_t driverVersion;
};
namespace UserConfiguration {
enum class Resolution { WindowIntegerScale, Manual, Original };
}
// PRODUCTION_HELPER
struct Config {
    UserConfiguration::Resolution resolution = UserConfiguration::Resolution::WindowIntegerScale;
    float resolutionMultiplier = 1.0f;
};
struct Shared {
    Config userConfig;
    uint32_t swapChainHeight = 1438;
    uint32_t viOriginalRate = 30;
};
struct WorkloadConfiguration {
    float aspectRatioTarget = 2560.0f / 1438.0f;
    uint32_t targetRate = 60;
};
struct Fixture {
    Shared shared;
    struct { Shared *sharedResources; } ext{&shared};
    WorkloadConfiguration workloadConfig;
    RenderDeviceDescription deviceDescription{"Adreno (TM) 615", RenderDeviceVendor(0x5143), 0x801f6000};
    float select(uint32_t referenceHeight = 240) {
        // PRODUCTION_SELECTION
    }
    // A 320x240 game shown with Expand on a landscape surface.
    void surface(uint32_t width, uint32_t height, uint32_t displayRate) {
        shared.swapChainHeight = height;
        workloadConfig.aspectRatioTarget = std::max(float(width) / float(height), 4.0f / 3.0f);
        workloadConfig.targetRate = displayRate;
    }
};
static int failures = 0;
static void expect(float actual, float expected, const char *label) {
    if (actual != expected) {
        std::fprintf(stderr, "%s: expected %.0f, got %.0f\n", label, expected, actual);
        failures++;
    }
}
#ifdef __ANDROID__
static constexpr bool android = true;
#else
static constexpr bool android = false;
#endif
int main() {
    Fixture f;
    expect(f.select(), android ? 2 : 6, "SM-T720 startup at 1438px");
    f.shared.swapChainHeight = 240;
    expect(f.select(), 1, "small surface must not be upscaled to the cap");
    f.shared.swapChainHeight = 0;
    expect(f.select(), 1, "surface not yet available");
    f.shared.swapChainHeight = 1438;
    f.shared.userConfig.resolution = UserConfiguration::Resolution::Manual;
    f.shared.userConfig.resolutionMultiplier = 4;
    expect(f.select(), 4, "explicit manual scale");
    f.shared.userConfig.resolution = UserConfiguration::Resolution::Original;
    expect(f.select(), 1, "native scale");
    f.shared.userConfig.resolution = UserConfiguration::Resolution::WindowIntegerScale;
    f.deviceDescription = {"Adreno (TM) 615", RenderDeviceVendor(0x5143), 0x801f6000};
    expect(f.select(720), android ? 1 : 2, "720-line VI: 2x exceeds the pixel budget");

    // Every other device keeps the window scale on desktop and is bounded by
    // the presented pixel rate on Android.
    f.deviceDescription = {"Turnip Adreno (TM) 615", RenderDeviceVendor(0x5143), 0x6402063};
    expect(f.select(), android ? 3 : 6, "Turnip on the SM-T720 at 60 Hz");
    f.deviceDescription = {"Adreno (TM) 650", RenderDeviceVendor(0x5143), 0x801f6000};
    expect(f.select(), android ? 3 : 6, "other Adreno models");
    f.deviceDescription = {"Adreno (TM) 615", RenderDeviceVendor(0x5143), 0x801f6001};
    expect(f.select(), android ? 3 : 6, "untested driver version");
    f.deviceDescription = {"Adreno (TM) 615", RenderDeviceVendor::NVIDIA, 0x801f6000};
    expect(f.select(), android ? 3 : 6, "vendor must match too");

    f.deviceDescription = {"Turnip Adreno (TM) 650", RenderDeviceVendor(0x5143), 0x6402063};
    f.surface(2400, 992, 60);
    expect(f.select(), android ? 3 : 5, "Redmi K30 Pro with status bar, 60 Hz");
    f.surface(2400, 1080, 60);
    expect(f.select(), android ? 3 : 5, "Redmi K30 Pro full screen, 60 Hz");
    f.surface(2400, 992, 0);
    expect(f.select(), android ? 4 : 5, "original refresh presents at the game's 30 Hz");
    f.shared.viOriginalRate = 0;
    expect(f.select(), android ? 4 : 5, "game rate not yet known");
    f.shared.viOriginalRate = 30;
    f.surface(2712, 1220, 120);
    expect(f.select(), android ? 2 : 6, "Xiaomi 12T Pro 20:9 at 120 Hz");
    f.surface(1920, 1080, 60);
    expect(f.select(), android ? 3 : 5, "1080p 16:9 at 60 Hz");
    f.surface(2560, 1600, 60);
    expect(f.select(), android ? 3 : 7, "16:10 tablet at 60 Hz");
    f.surface(1280, 720, 60);
    expect(f.select(), 3, "window scale already within budget");
    f.surface(800, 480, 120);
    expect(f.select(), 2, "small 120 Hz surface keeps its window scale");
    f.surface(2400, 992, 60);
    f.shared.userConfig.resolution = UserConfiguration::Resolution::Manual;
    f.shared.userConfig.resolutionMultiplier = 5;
    expect(f.select(), 5, "manual scales are not budgeted");
    return failures ? 1 : 0;
}
