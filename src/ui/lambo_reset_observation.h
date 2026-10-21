#pragma once

namespace lambo::ui {

// A title sample taken before a reset cannot prove that reset completed.
class ResetObservation {
    unsigned requested_generation_{};
    int consumed_vi_{-1};
public:
    void requested(unsigned generation) {
        requested_generation_ = generation;
        consumed_vi_ = -1;
    }

    bool completed(unsigned generation, int vi, int state) {
        if (generation == requested_generation_) return false;
        if (consumed_vi_ < 0) {
            consumed_vi_ = vi;
            return false;
        }
        return vi > consumed_vi_ && state == 4;
    }
};

} // namespace lambo::ui
