#pragma once

#include <algorithm>
#include <array>

#include "window/construction/ConstructionBatch.hpp"
#include "window/construction/studies/RegionalClose.hpp"

namespace hyprimate::construction {

// Geometry receipt for the most recent successful physical draw on the owning output.
// Never carries a framebuffer, texture id, or any client pixel.
struct Publication {
    std::array<ConstructionQuad, studies::RegionalClose::MAX_VISIBLE_FACES> faces;
    Box body{}, capture{};
    std::size_t count = 0;
    bool known = false;

    void remember(const ConstructionBatch &batch, Box drawn_body, Box drawn_capture, Box crop) noexcept {
        known = false;
        count = 0;
        if (batch.overflow || batch.count > batch.quads.size() || !is_usable(crop)) return;
        for (std::size_t i = 0; i < batch.count; ++i) {
            const auto &q = batch.quads[i];
            if (q.surface != ConstructionSurface::Client || q.tint[3] == 0) continue;
            const auto [x, y, w, h] = q.uv;
            if (!(w > 0 && h > 0)) return;
            const double left = std::max<double>(x, crop.x), top = std::max<double>(y, crop.y);
            const double right = std::min(double(x) + w, crop.x + crop.w);
            const double bottom = std::min(double(y) + h, crop.y + crop.h);
            if (right <= left || bottom <= top) continue;
            if (count == faces.size()) return;
            auto &face = faces[count++];
            face = q;
            if (left == x && top == y && right == double(x) + w && bottom == double(y) + h) continue;
            const double u0 = (left - x) / w, v0 = (top - y) / h;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                face.origin[axis] = static_cast<float>(q.origin[axis] + u0 * q.u[axis] + v0 * q.v[axis]);
                face.u[axis] = static_cast<float>(q.u[axis] * ((right - left) / w));
                face.v[axis] = static_cast<float>(q.v[axis] * ((bottom - top) / h));
            }
            face.uv = {static_cast<float>(left), static_cast<float>(top),
                static_cast<float>(right - left), static_cast<float>(bottom - top)};
        }
        body = drawn_body;
        capture = drawn_capture;
        known = true;
    }
};

}  // namespace hyprimate::construction
