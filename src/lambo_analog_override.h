#ifndef LAMBO_ANALOG_OVERRIDE_H
#define LAMBO_ANALOG_OVERRIDE_H

#include <optional>

namespace lambo::analog_override {

struct Values {
    std::optional<float> throttle;
    std::optional<float> brake;
};

Values parse(const char* throttle, const char* brake);

} // namespace lambo::analog_override

#endif
