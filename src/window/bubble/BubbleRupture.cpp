#include <cmath>

#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleHoleGeometry.hpp"

namespace hyprimate {


    double bubble_hole_azimuth(const BubbleHole &hole, Vec3 point, bool spherical) {
        if (!spherical) {
            return std::atan2(point.y - hole.centre.y, point.x - hole.centre.x);
        }
        Vec3 basis_first;
        Vec3 basis_second;
        detail::basis(hole.centre, basis_first, basis_second);
        return std::atan2(detail::dot(point, basis_second), detail::dot(point, basis_first));
    }

    double bubble_hole_boundary(const BubbleHole &hole, double phi) {
        double factor = 1;
        const auto *amplitude = hole.amplitude.begin();
        const auto *phase = hole.phase.begin();
        for (const auto MODE : K_BUBBLE_RIM_MODES) {
            factor += *amplitude++ * std::cos((MODE * phi) + *phase++);
        }
        return hole.radius * factor;
    }


}  // namespace hyprimate
