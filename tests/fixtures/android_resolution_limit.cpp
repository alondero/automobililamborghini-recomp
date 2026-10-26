#include <algorithm>
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
};
struct Fixture {
    Shared shared;
    struct { Shared *sharedResources; } ext{&shared};
    RenderDeviceDescription deviceDescription{"Adreno (TM) 615", RenderDeviceVendor(0x5143), 0x801f6000};
    float select(uint32_t referenceHeight = 240) {
        // PRODUCTION_SELECTION
    }
};
static int failures = 0;
static void expect(float actual, float expected, const char *label) {
    if (actual != expected) {
        std::fprintf(stderr, "%s: expected %.0f, got %.0f\n", label, expected, actual);
        failures++;
    }
}
int main() {
    Fixture f;
#ifdef __ANDROID__
    const float affected = 2;
#else
    const float affected = 6;
#endif
    expect(f.select(), affected, "SM-T720 startup at 1438px");
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
    f.deviceDescription = {"Turnip Adreno (TM) 615", RenderDeviceVendor(0x5143), 0x6402063};
    expect(f.select(), 6, "Turnip keeps automatic scale");
    f.deviceDescription = {"Adreno (TM) 650", RenderDeviceVendor(0x5143), 0x801f6000};
    expect(f.select(), 6, "other Adreno models");
    f.deviceDescription = {"Adreno (TM) 615", RenderDeviceVendor(0x5143), 0x801f6001};
    expect(f.select(), 6, "untested driver version");
    f.deviceDescription = {"Adreno (TM) 615", RenderDeviceVendor::NVIDIA, 0x801f6000};
    expect(f.select(), 6, "vendor must match too");
    f.deviceDescription = {"Adreno (TM) 615", RenderDeviceVendor(0x5143), 0x801f6000};
    expect(f.select(720), 2, "automatic 2x already within limit");
    return failures ? 1 : 0;
}
