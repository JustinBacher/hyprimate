#pragma once

#include <cmath>
#include <type_traits>

namespace hyprimate {

    // Preserve every finite floating-point value (including subnormals); replace NaN and either
    // infinity with the caller's domain-appropriate fallback before arithmetic or clamping.
    template <typename T>
    [[nodiscard]] constexpr T finite_or(T value, T fallback) noexcept {
        static_assert(std::is_floating_point_v<T>);
        return std::isfinite(value) ? value : fallback;
    }

}  // namespace hyprimate
