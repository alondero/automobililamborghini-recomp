#pragma once

#include <cstdint>
#include <mutex>

namespace lambo::rumble {
// Candidate USA vehicle vectors, deliberately not named as verified tire data.
struct SlideSample {
    float field_40, field_44, field_6c, field_74;
    int speed;
    int vehicle;
};
float slide_severity(const SlideSample& sample);

// Both threads use this mailbox. The lock makes pulse, ownership and timestamp
// one publication; the main thread never touches guest RAM. Times are monotonic
// milliseconds. Expiry also works when guest execution and VIs stop entirely.
class SlipRumble {
public:
    static constexpr std::uint64_t timeout_ms = 150;
    void publish(const SlideSample& sample, std::uint64_t now);
    void clear();
    void release_vehicle(int vehicle);
    bool output(bool original, bool enabled, bool connected, bool captured,
                std::uint64_t now);
private:
    void reset();
    std::mutex mutex_;
    std::uint64_t stamp_{};
    int vehicle_{-1};
    unsigned phase_{};
    bool on_{};
    bool accepting_{};
};
SlipRumble& slip();
std::uint64_t now_ms();
}

extern "C" void lambo_slip_rumble_reset();
