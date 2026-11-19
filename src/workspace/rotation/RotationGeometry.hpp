#pragma once

#include <array>
#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace::rotation {
    // Column-major affine inverse transforms, in top-left-origin pixel coordinates.
    struct Frame {
        std::array<float, 9> source;
        std::array<float, 9> destination;
        double source_angle;
        double destination_angle;
    };
    struct Extent {
        int width;
        int height;
    };
    [[nodiscard]] Frame make_frame(Extent extent, double position, Direction direction);
}
