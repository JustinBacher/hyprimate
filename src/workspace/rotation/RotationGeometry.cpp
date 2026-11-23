#include <algorithm>
#include <cmath>

#include "RotationGeometry.hpp"

namespace hyprimate::workspace::rotation {
    namespace {
        std::array<float, 9> inverse(double angle, double center_x, double center_y) {
            const double COSINE = std::cos(angle);
            const double SINE = std::sin(angle);
            return {static_cast<float>(COSINE),
                static_cast<float>(-SINE),
                0,
                static_cast<float>(SINE),
                static_cast<float>(COSINE),
                0,
                static_cast<float>(center_x - (COSINE * center_x) - (SINE * center_y)),
                static_cast<float>(center_y + (SINE * center_x) - (COSINE * center_y)),
                1};
        }
    }  // namespace
    Frame make_frame(Extent extent, double position, Direction direction) {
        const double PROGRESS = std::isfinite(position) ? std::clamp(position, 0.0, 1.0) : 0.0;
        const double WIDTH = std::max(extent.width, 1);
        const double RADIUS = 2.0 * WIDTH;
        const double STEP = 2.0 * std::atan(WIDTH / (2.0 * RADIUS));
        const double SIGN = direction == Direction::Forward ? 1.0 : -1.0;
        const double OUTGOING = -SIGN * STEP * PROGRESS;
        const double INCOMING = SIGN * STEP * (1.0 - PROGRESS);
        return {.source = inverse(OUTGOING, WIDTH / 2.0, extent.height + RADIUS),
            .destination = inverse(INCOMING, WIDTH / 2.0, extent.height + RADIUS),
            .source_angle = OUTGOING,
            .destination_angle = INCOMING};
    }
}  // namespace hyprimate::workspace::rotation
