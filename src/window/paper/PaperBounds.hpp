#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "window/paper/PaperSurface.hpp"

namespace hyprimate {
    // Capture-local conservative bounds for the reconstructed sheet and desktop shadow.
    inline Box paper_bounds(const PaperSurface &surface, Box capture) {
        // A map with no published pose still needs a scheduled frame on this monitor.
        if (surface.points.empty()) {
            return {.x = capture.w * .5, .y = capture.h * .5, .w = 1, .h = 1};
        }
        const double LENGTH = std::sqrt(surface.size.x * surface.size.y);
        const double UNIT = std::sqrt(capture.w * capture.h);
        const Vec2 axes{LENGTH * capture.w / surface.size.x, LENGTH * capture.h / surface.size.y};
        const Vec2 center{.5 * capture.w, .5 * capture.h};
        double left = std::numeric_limits<double>::infinity(), top = left, right = -left,
               bottom = -left;
        double nearest = 3, lateral = 0;
        for (const auto &point : surface.points) {
            nearest = std::min(nearest, 3.0 - point.position.z);
            lateral = std::max({lateral, std::abs(point.position.x), std::abs(point.position.y)});
            const double SCALE = 3. / std::max(.25, 3. - point.position.z);
            const double dx = point.position.x * axes.x, dy = point.position.y * axes.y;
            const double X = center.x + dx * SCALE, Y = center.y + dy * SCALE;
            left = std::min(left, X);
            right = std::max(right, X);
            top = std::min(top, Y);
            bottom = std::max(bottom, Y);
            if (surface.shadow_strength > 0) {
                // The material projects the shadow onto the desktop, not through the camera.
                const double height = UNIT * std::max(0., point.position.z);
                const double shadow_x = center.x + dx + .32 * height;
                const double shadow_y = center.y + dy + .42 * height;
                left = std::min(left, shadow_x);
                right = std::max(right, shadow_x);
                top = std::min(top, shadow_y);
                bottom = std::max(bottom, shadow_y);
            }
        }
        // Include both lateral displacement and the perspective change from a curved
        // edge moving toward the eye. Use the same displacement limit as reconstruction.
        nearest = std::max(nearest - PAPER_PATCH_BULGE, .25);
        const double PATCH = surface.interaction_mesh ? 0. : PAPER_PATCH_BULGE;
        const double PAD =
            2.0 + PATCH * ((3 / nearest) * (1 + lateral / nearest) * std::max(axes.x, axes.y) +
                              (surface.shadow_strength > 0 ? .42 * UNIT : 0.));
        return {.x = left - PAD,
            .y = top - PAD,
            .w = right - left + 2 * PAD,
            .h = bottom - top + 2 * PAD};
    }
}  // namespace hyprimate
