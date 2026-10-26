// The boot thread's dispatch loop polls two message queues and, on hardware, spins
// whenever both are empty. The idle hook replaces that spin with a wait for the next
// interrupt-style message, but must never wait while either queue still holds work.
#include "lambo_main_loop_idle.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "ultramodern/ultramodern.hpp"

namespace {

std::vector<std::string> calls;
uint32_t waited_millis = 0;

void set_valid_count(std::vector<uint8_t>& rdram, uint32_t queue, int32_t count) {
    std::memcpy(rdram.data() + (queue - 0x80000000u) + offsetof(OSMesgQueue, validCount), &count, sizeof count);
}

} // namespace

void ultramodern::wait_for_external_message_timed(uint8_t*, u32 millis) {
    calls.push_back("wait");
    waited_millis = millis;
}

void ultramodern::check_running_queue(uint8_t*) {
    calls.push_back("check");
}

int main() {
    int failures = 0;
    auto expect = [&](bool value, const char* message) {
        if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    using namespace lambo::main_loop;
    std::vector<uint8_t> rdram(0x800000);

    expect(!has_pending_message(rdram.data()), "empty queues have no pending message");

    set_valid_count(rdram, kRetraceQueue, 1);
    expect(has_pending_message(rdram.data()), "a retrace/done message is pending work");
    calls.clear();
    lambo_main_loop_idle(rdram.data());
    expect(calls.empty(), "a queued scheduler message must be dispatched, not waited on");

    set_valid_count(rdram, kRetraceQueue, 0);
    set_valid_count(rdram, kControllerQueue, 2);
    expect(has_pending_message(rdram.data()), "a controller message is pending work");
    calls.clear();
    lambo_main_loop_idle(rdram.data());
    expect(calls.empty(), "a queued controller message must be dispatched, not waited on");

    set_valid_count(rdram, kControllerQueue, 0);
    calls.clear();
    lambo_main_loop_idle(rdram.data());
    expect(calls == std::vector<std::string>{"wait", "check"},
           "an idle loop waits for an external message, then lets woken higher-priority threads run");
    expect(waited_millis == 1, "the wait is bounded so a missed wakeup costs at most 1 ms");

    if (failures == 0) std::cout << "main loop idle tests passed\n";
    return failures == 0 ? 0 : 1;
}
