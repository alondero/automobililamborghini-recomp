#include "lambo_input_gate.h"

#include <atomic>

namespace {

std::atomic<bool> g_ui_capture{false};
std::atomic<bool> g_release_barrier{false};
std::atomic<bool> g_release_barrier_sampled{false};
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
    if (g_ui_capture.load(std::memory_order_acquire)) {
        g_guest_snapshot.store(0, std::memory_order_release);
    } else if (g_release_barrier.load(std::memory_order_acquire)) {
        // Held UI input must not become guest input when the overlay closes. Wait
        // for a genuinely neutral physical sample, publish that sample as neutral,
        // then remove the barrier on the next neutral sample. Input resumes only on
        // a later publish, so every guest read of the release frame remains neutral.
        if (snapshot != 0) {
            g_release_barrier_sampled.store(false, std::memory_order_release);
        } else if (g_release_barrier_sampled.exchange(true, std::memory_order_acq_rel)) {
            g_release_barrier.store(false, std::memory_order_release);
        }
        g_guest_snapshot.store(0, std::memory_order_release);
    } else {
        g_guest_snapshot.store(snapshot, std::memory_order_release);
    }
}

uint32_t guest_snapshot() {
    if (g_ui_capture.load(std::memory_order_acquire)) return 0;
    if (g_release_barrier.load(std::memory_order_acquire)) return 0;
    return g_guest_snapshot.load(std::memory_order_acquire);
}

} // namespace lambo::input_gate
