#include "lambo_stall_report.h"

#include <atomic>
#include <charconv>
#include <cstdio>

#if defined(__ANDROID__)
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdlib>
#include <mutex>
#include <thread>
#endif

namespace lambo::stall {
namespace {

std::atomic<uint64_t> g_beats{0};

constexpr unsigned kWait = 0;
constexpr unsigned kLockPi = 6;
constexpr unsigned kWaitBitset = 9;
constexpr unsigned kWaitRequeuePi = 11;
constexpr unsigned kLockPi2 = 13;
// FUTEX_PRIVATE_FLAG and FUTEX_CLOCK_REALTIME modify a command; they are not one.
constexpr unsigned kCommandMask = ~(128u | 256u);
constexpr uint32_t kTidMask = 0x3fffffffu;

const char* command_name(unsigned command) {
    switch (command) {
        case kWait: return "WAIT";
        case kLockPi: return "LOCK_PI";
        case kWaitBitset: return "WAIT_BITSET";
        case kWaitRequeuePi: return "WAIT_REQUEUE_PI";
        case kLockPi2: return "LOCK_PI2";
        default: return nullptr;
    }
}

std::optional<std::string_view> next_token(std::string_view& text) {
    const size_t start = text.find_first_not_of(" \n");
    if (start == std::string_view::npos) return std::nullopt;
    const size_t end = text.find_first_of(" \n", start);
    const std::string_view token = text.substr(start, end - start);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end);
    return token;
}

template <typename Number>
std::optional<Number> parse_token(std::string_view token) {
    int base = 10;
    if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        token.remove_prefix(2);
        base = 16;
    }
    Number value{};
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value, base);
    if (error != std::errc{} || end != token.data() + token.size()) return std::nullopt;
    return value;
}

} // namespace

void beat() {
    g_beats.fetch_add(1, std::memory_order_relaxed);
}

std::optional<Syscall> parse_syscall(std::string_view line) {
    auto token = next_token(line);
    if (!token) return std::nullopt;
    Syscall call;
    if (*token == "running") {
        call.running = true;
        return call;
    }
    const auto number = parse_token<long>(*token);
    if (!number) return std::nullopt;
    call.number = *number;
    // A thread outside a syscall reports only its stack and program counters.
    const int expected = call.number == -1 ? 0 : 6;
    for (int i = 0; i < expected; ++i) {
        const auto arg = next_token(line);
        const auto value = arg ? parse_token<uint64_t>(*arg) : std::nullopt;
        if (!value) return std::nullopt;
        call.args[i] = *value;
    }
    return call;
}

std::optional<FutexWait> futex_wait(const Syscall& call, long futex_number) {
    if (call.running || call.number != futex_number) return std::nullopt;
    const unsigned command = unsigned(call.args[1]) & kCommandMask;
    if (command_name(command) == nullptr) return std::nullopt;
    return FutexWait{call.args[0], command, command == kLockPi || command == kLockPi2};
}

uint32_t pi_owner(uint32_t word) {
    return word & kTidMask;
}

std::string format_futex(const FutexWait& wait, std::optional<uint32_t> word) {
    char text[128];
    if (!word) {
        std::snprintf(text, sizeof text, "futex %s 0x%llx word=unreadable", command_name(wait.command),
                      (unsigned long long)wait.address);
    } else if (wait.priority_inheritance) {
        std::snprintf(text, sizeof text, "futex %s 0x%llx word=0x%x owner=%u", command_name(wait.command),
                      (unsigned long long)wait.address, *word, pi_owner(*word));
    } else {
        std::snprintf(text, sizeof text, "futex %s 0x%llx word=0x%x", command_name(wait.command),
                      (unsigned long long)wait.address, *word);
    }
    return text;
}

std::string format_holder(uint32_t owner, const std::optional<std::string>& holder) {
    if (owner == 0) return " holder: none";
    return holder ? " holder: " + *holder : " holder: not a thread of this process";
}

std::optional<ThreadStat> parse_stat(std::string_view line) {
    // The name is the text between the first '(' and the last ')'.
    const size_t open = line.find('(');
    const size_t close = line.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) return std::nullopt;
    ThreadStat stat;
    stat.name = std::string(line.substr(open + 1, close - open - 1));
    std::string_view rest = line.substr(close + 1);
    const auto state = next_token(rest);
    if (!state || state->size() != 1) return std::nullopt;
    stat.state = (*state)[0];
    // utime and stime are fields 14 and 15; the state was field 3.
    for (int field = 4; field <= 15; ++field) {
        const auto token = next_token(rest);
        if (!token) return std::nullopt;
        if (field >= 14) {
            const auto ticks = parse_token<uint64_t>(*token);
            if (!ticks) return std::nullopt;
            stat.cpu_ticks += *ticks;
        }
    }
    return stat;
}

bool StallMonitor::observe(uint64_t beats, std::chrono::steady_clock::time_point now) {
    if (!started_ || beats != beats_) {
        started_ = true;
        beats_ = beats;
        changed_ = now;
        reported_ = false;
        return false;
    }
    if (reported_ || reports_left_ <= 0 || now - changed_ < threshold_) return false;
    reported_ = true;
    --reports_left_;
    return true;
}

#if defined(__ANDROID__)
namespace {

std::string read_proc(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "re");
    if (file == nullptr) return {};
    char buffer[1024];
    const size_t size = std::fread(buffer, 1, sizeof buffer - 1, file);
    std::fclose(file);
    return std::string(buffer, size);
}

// /proc/self/mem turns an unmapped address into a failed read, not a fault.
// Opened per read so a failed open (for example, out of descriptors) is retried next time.
std::optional<uint32_t> read_word(uint64_t address) {
    const int memory = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    if (memory < 0) return std::nullopt;
#if defined(__aarch64__)
    address &= 0x00ffffffffffffffull; // Drop the heap pointer tag (top-byte ignore).
#endif
    uint32_t word;
    const ssize_t got = pread64(memory, &word, sizeof word, off64_t(address));
    close(memory);
    if (got != ssize_t(sizeof word)) return std::nullopt;
    return word;
}

std::string task_path(long tid, const char* file) {
    return "/proc/self/task/" + std::to_string(tid) + "/" + file;
}

std::string describe_thread(long tid) {
    const auto stat = parse_stat(read_proc(task_path(tid, "stat")));
    std::string wchan = read_proc(task_path(tid, "wchan"));
    if (wchan.empty()) wchan = "?";
    char text[160];
    std::snprintf(text, sizeof text, "tid %ld '%s' %c cpu=%llu wchan=%s", tid,
                  stat ? stat->name.c_str() : "?", stat ? stat->state : '?',
                  stat ? (unsigned long long)stat->cpu_ticks : 0ull, wchan.c_str());
    return text;
}

void report_thread(long tid) {
    std::string line = describe_thread(tid);
    const std::string raw = read_proc(task_path(tid, "syscall"));
    const auto call = parse_syscall(raw);
    const auto wait = call ? futex_wait(*call, SYS_futex) : std::nullopt;
    if (wait) {
        const auto word = read_word(wait->address);
        line += " " + format_futex(*wait, word);
        if (word && wait->priority_inheritance) {
            const uint32_t owner = pi_owner(*word);
            std::optional<std::string> holder;
            if (owner != 0 && !read_proc(task_path(owner, "stat")).empty()) holder = describe_thread(owner);
            line += format_holder(owner, holder);
        }
    } else if (call && call->running) {
        line += " running";
    } else if (call) {
        line += " syscall=" + std::to_string(call->number);
    } else if (raw.empty()) {
        line += " syscall=unreadable";
    }
    std::fprintf(stderr, "  %s\n", line.c_str());
}

void report(uint64_t beats, std::chrono::milliseconds threshold) {
    // A release app is not dumpable, so the kernel gives root its owner-only
    // /proc files (syscall, mem) and every lock word reads as missing.
    const int dumpable = prctl(PR_GET_DUMPABLE);
    if (dumpable == 0) prctl(PR_SET_DUMPABLE, 1);
    std::fprintf(stderr, "Present stall: no frame for %lld ms after %llu frames. Thread waits:\n",
                 (long long)threshold.count(), (unsigned long long)beats);
    if (DIR* tasks = opendir("/proc/self/task")) {
        while (const dirent* entry = readdir(tasks)) {
            const long tid = std::strtol(entry->d_name, nullptr, 10);
            if (tid > 0) report_thread(tid);
        }
        closedir(tasks);
    }
    if (dumpable == 0) prctl(PR_SET_DUMPABLE, 0);
    std::fprintf(stderr, "End of present stall report.\n");
    std::fflush(stderr);
}

} // namespace

void start() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            pthread_setname_np(pthread_self(), "Lambo Stall");
            constexpr std::chrono::milliseconds threshold{3000};
            StallMonitor monitor(threshold, 3);
            for (;;) {
                const uint64_t beats = g_beats.load(std::memory_order_relaxed);
                if (monitor.observe(beats, std::chrono::steady_clock::now())) report(beats, threshold);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }).detach();
    });
}
#else
void start() {}
#endif

} // namespace lambo::stall
