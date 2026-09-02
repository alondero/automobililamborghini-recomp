#include "lambo_pak_storage.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

namespace {

int failures;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

void make_valid_pak(std::array<uint8_t, LAMBO_PAK_SIZE>& image) {
    const size_t offset = 0x20;
    uint16_t sum = 0;
    for (size_t word = 0; word < 14; ++word) {
        const size_t pos = offset + word * 2;
        sum = static_cast<uint16_t>(sum + static_cast<uint16_t>((image[pos] << 8) | image[pos + 1]));
    }
    const uint16_t inverted = static_cast<uint16_t>(0xfff2u - sum);
    image[offset + 0x1c] = static_cast<uint8_t>(sum >> 8);
    image[offset + 0x1d] = static_cast<uint8_t>(sum);
    image[offset + 0x1e] = static_cast<uint8_t>(inverted >> 8);
    image[offset + 0x1f] = static_cast<uint8_t>(inverted);
}

} // namespace

int main() {
    const std::filesystem::path first = "pak_storage_first.mpk";
    const std::filesystem::path second = "pak_storage_second.mpk";
    std::filesystem::remove(first);
    std::filesystem::remove(second);

    std::array<uint8_t, LAMBO_PAK_SIZE> image{};
    for (size_t index = 0; index < image.size(); ++index)
        image[index] = static_cast<uint8_t>((index * 13u + 7u) & 0xffu);
    make_valid_pak(image);

    lambo_pak_storage_configure(first.string().c_str());
    expect(std::strcmp(lambo_pak_storage_path(), first.string().c_str()) == 0,
           "configured storage path is visible to the Joybus adapter");
    lambo_pak_storage_configure(second.string().c_str());
    expect(std::strcmp(lambo_pak_storage_path(), second.string().c_str()) == 0,
           "reconfiguration is not hidden by one-shot path caching");

    for (unsigned write = 0; write < 20; ++write) {
        image[0x500 + write] ^= 0xffu;
        lambo_pak_storage_schedule_save(image.data());
    }
    expect(!std::filesystem::exists(second),
           "Joybus block writes do not synchronously publish the whole container");
    for (unsigned attempt = 0; attempt < 200 && !std::filesystem::exists(second); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(std::filesystem::exists(second), "the debounced worker publishes a completed save burst");

    image[0x600] ^= 0xffu;
    lambo_pak_storage_schedule_save(image.data());
    const LamboPakIoResult flushed = lambo_pak_storage_flush();
    expect(flushed.ok, "explicit flush publishes the pending save batch");

    std::array<uint8_t, LAMBO_PAK_SIZE> actual{};
    const LamboPakIoResult loaded = lambo_pak_read_file(second.string().c_str(), actual.data());
    expect(loaded.ok && actual == image, "a save burst publishes the final Pak image");

    lambo_pak_storage_shutdown();
    std::filesystem::remove(first);
    std::filesystem::remove(second);
    return failures == 0 ? 0 : 1;
}
