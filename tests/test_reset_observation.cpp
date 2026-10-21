#include "ui/lambo_reset_observation.h"
#undef NDEBUG
#include <cassert>

int main() {
    lambo::ui::ResetObservation observation;
    observation.requested(0);
    assert(!observation.completed(0, 100, 4)); // Title before consumption.
    assert(!observation.completed(1, 101, 4)); // Cached title at consumption.
    assert(!observation.completed(1, 101, 4)); // Presentation can outpace VIs.
    assert(!observation.completed(1, 102, 3)); // Still initializing title.
    assert(observation.completed(1, 103, 4));
    observation.requested(1);
    assert(!observation.completed(1, 104, 4)); // Second request still pending.
    assert(!observation.completed(2, 105, 4)); // Old title cannot pass again.
    assert(!observation.completed(2, 105, 4)); // A frozen game cannot pass.
    assert(observation.completed(2, 106, 4));
}
