#include "lambo_analog_override.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace lambo::analog_override {

namespace {

std::optional<float> parse_value(const char* text) {
    if (text == nullptr || text[0] == '\0') return std::nullopt;
    errno = 0;
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (errno == ERANGE || end == text || end == nullptr || *end != '\0' ||
        !std::isfinite(value)) {
        return std::nullopt;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

Values parse(const char* throttle, const char* brake) {
    return {parse_value(throttle), parse_value(brake)};
}

} // namespace lambo::analog_override
