// Compile the production timer loop with recording clock, queue and sleep boundaries.
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <variant>
#include <vector>

using namespace std::chrono_literals;
struct FixtureClock {
    using duration = std::chrono::nanoseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<FixtureClock>;
    static constexpr bool is_steady = true;
    inline static duration elapsed{};
    static time_point now() { return time_point(elapsed); }
};
struct TimerFinished {};
struct OSMesgQueue {};
using OSTime = uint64_t;
using OSMesg = int;
#define PTR(type) type *
#define RDRAM_ARG1 uint8_t *rdram
#define PASS_RDRAM1 rdram
#define TO_PTR(type, address) (address)

namespace ultramodern {
    enum class ThreadPriority { VeryHigh };
    enum class EventMessageSource { Timer };
    void set_native_thread_name(const char *) {}
    void set_native_thread_priority(ThreadPriority) {}
    void set_app_suspended(bool);
    bool is_app_suspended();
    FixtureClock::time_point get_start();
    FixtureClock::duration time_since_start();
    void enqueue_external_message_src(OSMesgQueue *, OSMesg, bool, EventMessageSource) {
        assert(!is_app_suspended());
        assert(time_since_start() == 1s);
        throw TimerFinished{};
    }
}

int scenario = 0, sleeps = 0;
std::vector<FixtureClock::duration> waits;
void fixtureSleep(FixtureClock::duration duration) {
    assert(ultramodern::is_app_suspended()); // No new sleep belongs on the foreground path.
    ++sleeps;
    FixtureClock::elapsed += duration;
}

namespace moodycamel {
    template<class T> struct BlockingConcurrentQueue {
        std::deque<T> actions;
        bool try_dequeue(T &action) {
            if (actions.empty()) return false;
            action = actions.front();
            actions.pop_front();
            return true;
        }
        void wait_dequeue(T &) { assert(false && "unexpected timer wait without an action"); }
        bool wait_dequeue_timed(T &, FixtureClock::duration duration) {
            waits.push_back(duration);
            if (scenario && waits.size() == 1) {
                assert(duration == 1s);
                FixtureClock::elapsed += 250ms;
                ultramodern::set_app_suspended(true);
                FixtureClock::elapsed += 500ms;
                if (scenario == 1) ultramodern::set_app_suspended(false);
                FixtureClock::elapsed += 250ms;
            } else {
                if (scenario == 2) ultramodern::set_app_suspended(false);
                FixtureClock::elapsed += duration;
            }
            return false;
        }
    };
}

// PRODUCTION_TIMER

int main() {
    for (scenario = 0; scenario < 3; ++scenario) {
        FixtureClock::elapsed = {};
        suspended_duration = {};
        app_suspended = false;
        waits.clear();
        sleeps = 0;
        OSTimer timer{};
        timer.timestamp = duration_to_ticks(1s);
        timer_context.action_queue.actions.push_back(AddTimerAction{&timer});
        try { timer_thread(nullptr); assert(false); }
        catch (TimerFinished &) {}
        if (scenario == 0) {
            assert(waits.size() == 1 && waits[0] == 1s && sleeps == 0);
        } else if (scenario == 1) {
            assert(waits.size() == 2 && waits[1] == 500ms && sleeps == 0);
        } else {
            assert(waits.size() == 2 && waits[1] == 750ms && sleeps == 1);
        }
    }
    std::puts("foreground: 1 deadline wait, 0 sleeps; suspend/resume: deadlines preserved");
}
