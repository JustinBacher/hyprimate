#include "RotationGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace hyprimate::workspace::rotation {
    namespace {
        std::array<float, 9> inverse(double angle, double cx, double cy) {
            const double c = std::cos(angle), s = std::sin(angle);
            return {static_cast<float>(c), static_cast<float>(-s), 0,
                static_cast<float>(s), static_cast<float>(c), 0,
                static_cast<float>(cx - c * cx - s * cy),
                static_cast<float>(cy + s * cx - c * cy), 1};
        }
    }
    Frame make_frame(int width, int height, double position, Direction direction) {
        const double t = std::isfinite(position) ? std::clamp(position, 0.0, 1.0) : 0.0;
        const double w = std::max(width, 1);
        const double radius = 2.0 * w;
        const double step = 2.0 * std::atan(w / (2.0 * radius));
        const double sign = direction == Direction::Forward ? 1.0 : -1.0;
        const double outgoing = -sign * step * t;
        const double incoming = sign * step * (1.0 - t);
        return {inverse(outgoing, w / 2.0, height + radius),
            inverse(incoming, w / 2.0, height + radius), outgoing, incoming};
    }
}
