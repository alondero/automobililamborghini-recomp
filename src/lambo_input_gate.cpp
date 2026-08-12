#include "lambo_input_gate.h"

#include <atomic>

namespace {

std::atomic<bool> g_ui_capture{false};
std::atomic<bool> g_release_barrier{false};
std::atomic<bool> g_release_barrier_sampled{false};
std::atomic<uint32_t> g_physical_snapshot{0};
std::atomic<uint32_t> g_guest_snapshot{0};

} // namespace

namespace lambo::input_gate {

void clear_before_release() {
    g_guest_snapshot.store(0, std::memory_order_release);
    g_release_barrier_sampled.store(false, std::memory_order_release);
    g_release_barrier.store(true, std::memory_order_release);
}

void set_ui_capture(bool capturing) {
    const bool was_capturing = g_ui_capture.exchange(capturing, std::memory_order_acq_rel);
    if (capturing) {
        g_release_barrier.store(false, std::memory_order_release);
        g_release_barrier_sampled.store(false, std::memory_order_release);
        g_guest_snapshot.store(0, std::memory_order_release);
        return;
    }
    if (was_capturing) clear_before_release();
}

bool guest_input_suppressed() {
    return g_ui_capture.load(std::memory_order_acquire) ||
           g_release_barrier.load(std::memory_order_acquire);
}

void publish_physical_snapshot(uint32_t snapshot) {
    g_physical_snapshot.store(snapshot, std::memory_order_release);
    if (g_ui_capture.load(std::memory_order_acquire)) {
        g_guest_snapshot.store(0, std::memory_order_release);
    } else if (g_release_barrier.load(std::memory_order_acquire)) {
        // The first physical sample after capture release defines the neutral
        // frame. Keep the barrier active until the following sample so multiple
        // guest reads during that frame cannot leak input through.
        if (g_release_barrier_sampled.exchange(true, std::memory_order_acq_rel)) {
            g_release_barrier.store(false, std::memory_order_release);
            g_guest_snapshot.store(snapshot, std::memory_order_release);
        } else {
            g_guest_snapshot.store(0, std::memory_order_release);
        }
    } else {
        g_guest_snapshot.store(snapshot, std::memory_order_release);
    }
}

uint32_t guest_snapshot() {
    if (g_ui_capture.load(std::memory_order_acquire)) return 0;
    if (g_release_barrier.load(std::memory_order_acquire)) return 0;
    // The physical sample may have been taken while capture was active, so the
    // guest snapshot is intentionally neutral. Once the one-frame barrier has
    // elapsed, resume from the latest physical sample immediately.
    return g_physical_snapshot.load(std::memory_order_acquire);
}

} // namespace lambo::input_gate
