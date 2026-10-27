// On-device check for the present stall report. One thread holds a
// priority-inheritance mutex, as Qualcomm gralloc's lock is, and another blocks
// on it. Frames then stop, and the real monitor must name the holder.
#include "lambo_stall_report.h"

#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_setprotocol(&attributes, PTHREAD_PRIO_INHERIT);
    static pthread_mutex_t lock;
    pthread_mutex_init(&lock, &attributes);

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
    return 0;
}
