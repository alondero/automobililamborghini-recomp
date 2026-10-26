#include "lambo_main_loop_idle.h"

#include <cstddef>
#include <cstring>

#include "ultramodern/ultramodern.hpp"

namespace {

int32_t valid_count(const uint8_t* rdram, uint32_t queue) {
    int32_t count;
    std::memcpy(&count, rdram + (queue - 0x80000000u) + offsetof(OSMesgQueue, validCount), sizeof count);
    return count;
}

} // namespace

bool lambo::main_loop::has_pending_message(const uint8_t* rdram) {
    return valid_count(rdram, kRetraceQueue) > 0 || valid_count(rdram, kControllerQueue) > 0;
}

// On hardware the boot thread spins here because nothing lower-priority may run.
// Natively the spin occupies a whole host core. Only an external (interrupt-style)
// message can make either queue non-empty while this thread holds the guest CPU,
// so waiting for one is equivalent. The bound covers any wakeup we did not model.
extern "C" void lambo_main_loop_idle(uint8_t* rdram) {
    if (lambo::main_loop::has_pending_message(rdram)) return;
    ultramodern::wait_for_external_message_timed(rdram, 1);
    ultramodern::check_running_queue(rdram);
}
