// Run RT64's producer barrier waits against a held barrier. The queue methods
// come from the patched source; the fixture supplies only their members.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

namespace RT64 {
    struct Slot {};

    struct PresentQueue {
        std::vector<Slot> presents = std::vector<Slot>(4);
        int threadCursor = 0;
        int writeCursor = 3;
        int barrierCursor = 0;
        std::atomic<bool> presentThreadRunning = true;
        std::mutex cursorMutex;
        std::condition_variable cursorCondition;
        std::condition_variable barrierCondition;
        void advanceToNextPresent();
        void threadAdvanceBarrier();
    };

    struct WorkloadQueue {
        std::vector<Slot> workloads = std::vector<Slot>(4);
        int threadCursor = 0;
        int writeCursor = 2;
        int barrierCursor = 3;
        std::atomic<bool> threadsRunning = true;
        std::mutex cursorMutex;
        std::condition_variable cursorCondition;
        std::condition_variable barrierCondition;
        void advanceToNextWorkload();
        void threadAdvanceBarrier();
    };

// PRODUCTION_QUEUE_METHODS
}

static double threadCpuMilliseconds() {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return now.tv_sec * 1000.0 + now.tv_nsec / 1'000'000.0;
}

// A stalled consumer used to leave the producer spinning on cursorMutex; on
// Android that burned a core while the present thread sat inside gralloc.
template <typename Queue, typename Advance, typename Release>
static int blockedProducer(const char *name, Queue &queue, Advance advance, Release release, int expectedCursor) {
    std::atomic<bool> done = false;
    double producerCpu = 0.0;
    std::thread producer([&]() {
        const double start = threadCpuMilliseconds();
        advance(queue);
        producerCpu = threadCpuMilliseconds() - start;
        done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    if (done) {
        std::fprintf(stderr, "%s: producer wrote over the slot behind the barrier\n", name);
        producer.join();
        return 1;
    }

    release(queue);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done && (std::chrono::steady_clock::now() < deadline)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!done) {
        std::fprintf(stderr, "%s: producer was not woken\n", name);
        producer.detach();
        return 1;
    }

    producer.join();
    if (queue.writeCursor != expectedCursor) {
        std::fprintf(stderr, "%s: write cursor %d, expected %d\n", name, queue.writeCursor, expectedCursor);
        return 1;
    }

    if (producerCpu > 100.0) {
        std::fprintf(stderr, "%s: producer used %.0f ms of CPU while waiting 400 ms\n", name, producerCpu);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if ((argc == 2) && (std::strcmp(argv[1], "present") == 0)) {
        RT64::PresentQueue queue;
        return blockedProducer("present", queue,
            [](RT64::PresentQueue &q) { q.advanceToNextPresent(); },
            [](RT64::PresentQueue &q) { q.threadAdvanceBarrier(); }, 0);
    }

    if ((argc == 2) && (std::strcmp(argv[1], "workload") == 0)) {
        RT64::WorkloadQueue queue;
        return blockedProducer("workload", queue,
            [](RT64::WorkloadQueue &q) { q.advanceToNextWorkload(); },
            [](RT64::WorkloadQueue &q) { q.threadAdvanceBarrier(); }, 3);
    }

    // Mirrors the queue destructors: clear the running flag, then wake waiters.
    if ((argc == 2) && (std::strcmp(argv[1], "present-shutdown") == 0)) {
        RT64::PresentQueue queue;
        return blockedProducer("present-shutdown", queue,
            [](RT64::PresentQueue &q) { q.advanceToNextPresent(); },
            [](RT64::PresentQueue &q) {
                {
                    const std::scoped_lock lock(q.cursorMutex);
                    q.presentThreadRunning = false;
                }
                q.barrierCondition.notify_all();
            }, 0);
    }

    if ((argc == 2) && (std::strcmp(argv[1], "workload-shutdown") == 0)) {
        RT64::WorkloadQueue queue;
        return blockedProducer("workload-shutdown", queue,
            [](RT64::WorkloadQueue &q) { q.advanceToNextWorkload(); },
            [](RT64::WorkloadQueue &q) {
                {
                    const std::scoped_lock lock(q.cursorMutex);
                    q.threadsRunning = false;
                }
                q.barrierCondition.notify_all();
            }, 3);
    }

    std::fprintf(stderr, "usage: %s present|workload|present-shutdown|workload-shutdown\n", argv[0]);
    return 2;
}
