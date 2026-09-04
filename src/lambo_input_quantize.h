#ifndef LAMBO_INPUT_QUANTIZE_H
#define LAMBO_INPUT_QUANTIZE_H

#include <cmath>
#include <cstdint>

namespace lambo::input {

inline std::uint16_t quantize_normalized(float value) noexcept {
    if (!std::isfinite(value) || value <= 0.0f) return 0;
    if (value >= 1.0f) return UINT16_MAX;
    return static_cast<std::uint16_t>(std::lround(value * static_cast<float>(UINT16_MAX)));
}

} // namespace lambo::input

#endif // LAMBO_INPUT_QUANTIZE_H
