#pragma once

#include <cstdint>

// Applies published phone-driving assists at the guest input merge point.
extern "C" void lambo_driving_assists_tick(std::uint8_t* rdram) noexcept;
