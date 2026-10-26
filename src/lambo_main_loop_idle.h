#pragma once

#include <cstdint>

namespace lambo::main_loop {

// The boot thread (BootLoadInitialAssets) polls these queues without blocking.
inline constexpr uint32_t kRetraceQueue = 0x80098788;    // osScheduler retrace/done messages
inline constexpr uint32_t kControllerQueue = 0x80098240; // controller read completion

bool has_pending_message(const uint8_t* rdram);

} // namespace lambo::main_loop

extern "C" void lambo_main_loop_idle(uint8_t* rdram);
