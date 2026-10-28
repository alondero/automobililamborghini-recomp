// On-device check for the present stall report. One thread holds a
// priority-inheritance mutex, as Qualcomm gralloc's lock is, and another blocks
// on it. Frames then stop, and the real monitor must name the holder.
// With "undumpable", the probe starts as a release app does: zygote's switch from
// root to the app's user leaves the process non-dumpable.
// "held-probe" checks a probe that waits on the held lock instead, and
// "allocator" asks this device's gralloc as the game does around renderer setup.
#include "lambo_stall_report.h"

#include <pthread.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "undumpable" || mode == "held-probe") prctl(PR_SET_DUMPABLE, 0);

    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_setprotocol(&attributes, PTHREAD_PRIO_INHERIT);
    static pthread_mutex_t lock;
    pthread_mutex_init(&lock, &attributes);

    if (mode == "allocator") {
        lambo::stall::check_buffer_allocator("on device");
        return 0;
    }
    if (mode == "held-probe") {
        std::thread holder([] {
            pthread_setname_np(pthread_self(), "PI Holder");
            pthread_mutex_lock(&lock);
            std::printf("holder tid %d\n", gettid());
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            pthread_mutex_unlock(&lock);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const bool answered = lambo::stall::check_wait("Held lock check", [] {
            pthread_mutex_lock(&lock);
            pthread_mutex_unlock(&lock);
        }, std::chrono::milliseconds(500));
        holder.join();
        std::printf("answered %d dumpable after check %d\n", answered, prctl(PR_GET_DUMPABLE));
        return 0;
    }

    lambo::stall::start();
    lambo::stall::beat();

    std::thread holder([] {
        pthread_setname_np(pthread_self(), "PI Holder");
        pthread_mutex_lock(&lock);
        std::printf("holder tid %d\n", gettid());
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(6));
        pthread_mutex_unlock(&lock);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::thread waiter([] {
        pthread_setname_np(pthread_self(), "PI Waiter");
        std::printf("waiter tid %d\n", gettid());
        std::fflush(stdout);
        pthread_mutex_lock(&lock);
        pthread_mutex_unlock(&lock);
    });

    holder.join();
    waiter.join();
    // The report has run by now (the holder slept past the threshold).
    std::printf("dumpable after report %d\n", prctl(PR_GET_DUMPABLE));
    return 0;
}
