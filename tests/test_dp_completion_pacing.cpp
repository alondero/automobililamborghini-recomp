// RDP completion pacing (patch 0001). The game starts a frame only on a retrace with no
// graphics task pending, so the retrace that observes DP completion sets its frame rate.
// Hardware never completes the RDP inside the retrace that submitted the task (30 fps), but
// it completes as soon as the RDP finishes after that. A renderer finishing just after the
// next retrace must not wait for another one: on slow Android devices that turned 30 fps
// into 20 fps. Only the VI thread signals completion, so it stays ordered after its own
// retrace message.
#include <atomic>
#include <cstdint>
#include <iostream>

#include "ultramodern/dp_pacing.hpp"

int main() {
    int failures = 0;
    auto expect = [&](bool value, const char* message) {
        if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };

    {
        std::atomic<uint64_t> ticks{10};
        ultramodern::DpCompletionPacer pacer{ticks};
        expect(!pacer.take_completion() && !pacer.late(), "nothing to complete before a submission");
        pacer.submitted();
        pacer.finished();
        expect(!pacer.take_completion(), "a fast renderer does not complete inside the submitting retrace");
        expect(!pacer.late(), "a finished task is never late");
        ticks = 11;
        expect(pacer.take_completion(), "the next retrace completes a task that already finished");
        expect(!pacer.take_completion(), "completion is signalled once");
    }
    {
        std::atomic<uint64_t> ticks{10};
        ultramodern::DpCompletionPacer pacer{ticks};
        pacer.submitted();
        expect(!pacer.late(), "a task is not late inside its submitting retrace");
        ticks = 11; // the renderer is still busy when the next retrace passes
        expect(!pacer.take_completion(), "an unfinished task is not completed by a retrace");
        expect(pacer.late(), "the VI thread watches for a renderer that missed the next retrace");
        pacer.finished();
        expect(pacer.take_completion(), "a renderer finishing after the next retrace completes without another retrace");
        expect(!pacer.late() && !pacer.take_completion(), "no second completion");
        ticks = 12;
        expect(!pacer.take_completion(), "no second completion on a later retrace");
    }
    {
        std::atomic<uint64_t> ticks{20};
        ultramodern::DpCompletionPacer pacer{ticks};
        pacer.submitted();
        ticks = 21;
        expect(pacer.late() && !pacer.take_completion(), "each new submission starts unfinished");
        pacer.finished();
        expect(pacer.take_completion(), "each new submission is paced from its own retrace");
    }

    if (failures == 0) std::cout << "dp completion pacing tests passed\n";
    return failures == 0 ? 0 : 1;
}
