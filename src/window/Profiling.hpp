#pragma once

#include "config_settings_generated.hpp"
#include "perf/LiveProfiler.hpp"

namespace hyprimate::animation {
    // Setup has exact open/close attribution. Mapped update/draw is shared: opening,
    // movement and residual dynamics can overlap; screen effects also use shared.
    // Resource bytes are owned inline state plus explicitly sampled storage only.
    inline constexpr perf::Key profile_key(config::Style style,
        std::string_view operation = "shared") noexcept {
        using enum config::Style;
        switch (style) {
            case Liquid: return {perf::Domain::Window, "liquid", operation};
            case Paper: return {perf::Domain::Window, "paper", operation};
            case Cloth: return {perf::Domain::Window, "cloth", operation};
            case Swarm: return {perf::Domain::Window, "swarm", operation};
            case Bubble: return {perf::Domain::Window, "bubble", operation};
            case Smoke: return {perf::Domain::Window, "smoke", operation};
            case Burn: return {perf::Domain::Window, "burn", operation};
            case Construction: return {perf::Domain::Window, "construction", operation};
        }
        return {perf::Domain::Window, "unknown", operation};
    }
}  // namespace hyprimate::animation
