// The Android stall report must name the thread holding a priority-inheritance
// lock. Issue 329's ANR traces showed two waiters in Qualcomm gralloc and no
// visible holder, so these cases follow the kernel's /proc formats.
#include "lambo_stall_report.h"

#include <chrono>
#include <iostream>
#include <string>

int main() {
    using namespace lambo::stall;
    using namespace std::chrono_literals;
    int failures = 0;
    auto expect = [&](bool value, const char* message) {
        if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };

    // arm64 futex(addr, FUTEX_LOCK_PI | FUTEX_PRIVATE_FLAG, ...), then sp and pc.
    const auto blocked = parse_syscall("98 0x73ab10c0 0x86 0x0 0x0 0x0 0x0 0x7055fe1f50 0x73a4a865f0\n");
    expect(blocked && blocked->number == 98, "blocked syscall number");
    expect(blocked && blocked->args[0] == 0x73ab10c0u, "futex address");
    expect(blocked && blocked->args[1] == 0x86u, "futex operation");
    expect(parse_syscall("running\n") && parse_syscall("running\n")->running, "running thread");
    const auto user = parse_syscall("-1 0x7055fe1f50 0x73a4a865f0\n");
    expect(user && user->number == -1 && !user->running, "thread outside a syscall");
    expect(!parse_syscall(""), "empty syscall line");
    expect(!parse_syscall("98 0x1"), "truncated syscall line");

    const auto lock_pi = futex_wait(*blocked, 98);
    expect(lock_pi && lock_pi->priority_inheritance, "LOCK_PI is a priority-inheritance wait");
    expect(lock_pi && lock_pi->address == 0x73ab10c0u, "LOCK_PI address");
    // FUTEX_LOCK_PI2 | FUTEX_CLOCK_REALTIME
    const auto lock_pi2 = futex_wait(*parse_syscall("98 0x10 0x10d 0x0 0x0 0x0 0x0 0x1 0x2"), 98);
    expect(lock_pi2 && lock_pi2->priority_inheritance, "LOCK_PI2 is a priority-inheritance wait");
    // FUTEX_WAIT | FUTEX_PRIVATE_FLAG: the word is a value, not an owner.
    const auto plain = futex_wait(*parse_syscall("98 0x20 0x80 0x2 0x0 0x0 0x0 0x1 0x2"), 98);
    expect(plain && !plain->priority_inheritance, "FUTEX_WAIT has no owner");
    expect(!futex_wait(*parse_syscall("63 0x3 0x0 0x0 0x0 0x0 0x0 0x1 0x2"), 98), "read is not a futex wait");

    // Waiters bit plus the owner's thread id.
    expect(pi_owner(0x80006a3cu) == 0x6a3cu, "owner ignores FUTEX_WAITERS");
    expect(pi_owner(0xc0006a3cu) == 0x6a3cu, "owner ignores FUTEX_OWNER_DIED");
    expect(format_futex(*lock_pi, 0x80006a3cu) == "futex LOCK_PI 0x73ab10c0 word=0x80006a3c owner=27196",
           "PI wait names its owner");
    expect(format_futex(*plain, 2u) == "futex WAIT 0x20 word=0x2", "plain wait reports its value");
    expect(format_futex(*lock_pi, std::nullopt) == "futex LOCK_PI 0x73ab10c0 word=unreadable",
           "unreadable lock word");

    // The command name can contain spaces and parentheses.
    const auto stat = parse_stat("25582 (RT64 Present (2)) S 25122 1 0 0 -1 4194368 0 0 0 0 1750 12 0 0 13 -4");
    expect(stat && stat->name == "RT64 Present (2)", "thread name");
    expect(stat && stat->state == 'S', "thread state");
    expect(stat && stat->cpu_ticks == 1762, "user plus system ticks");
    expect(!parse_stat("25582 (truncated"), "unterminated thread name");

    const auto start = std::chrono::steady_clock::time_point{};
    StallMonitor monitor(3s, 2);
    expect(!monitor.observe(0, start), "no report at start");
    expect(!monitor.observe(0, start + 2900ms), "no report before the threshold");
    expect(monitor.observe(0, start + 3s), "report a startup stall");
    expect(!monitor.observe(0, start + 9s), "one report per stall");
    expect(!monitor.observe(5, start + 10s), "frames resume");
    expect(!monitor.observe(9, start + 12s), "steady frames");
    expect(monitor.observe(9, start + 15s), "report a second stall");
    expect(!monitor.observe(10, start + 16s), "frames resume again");
    expect(!monitor.observe(10, start + 30s), "report limit reached");

    return failures == 0 ? 0 : 1;
}
