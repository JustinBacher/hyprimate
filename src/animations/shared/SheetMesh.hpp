#pragma once

#include <algorithm>
#include <cstddef>
#include <ranges>

#include "WindowAnimationFacts.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace hyprimate {

inline constexpr std::size_t SHEET_VERTEX_COUNT = SHEET_TRIANGLES * 3;

// Pure projection into reusable backend vertex storage. The returned vectors are borrowed.
template <animation::Vertex V>
animation::Mesh<V> fill_sheet_mesh(const SheetFrame &frame,
    const animation::MeshTarget<V> &target, bool shaded, float opacity) {
    auto &window = target.buffers->window;
    auto &shade = target.buffers->shade;
    if (window.size() != SHEET_VERTEX_COUNT) {
        window.resize(SHEET_VERTEX_COUNT);
    }
    if (shade.size() != SHEET_VERTEX_COUNT) {
        shade.resize(SHEET_VERTEX_COUNT);
    }
    const auto &source = target.source;
    const auto &output = target.output;
    const double CENTRE_X = source.x + (0.5 * source.w);
    const double CENTRE_Y = source.y + (0.5 * source.h);
    auto window_vertex = window.begin();
    auto shade_vertex = shade.begin();
    for (const std::size_t TRI : frame.order) {
        const auto &corners = sheet_triangle(TRI);
        for (auto &&[vertex_index, corner_shade] : std::views::zip(corners, frame.shade[TRI])) {
            const auto &point = frame.position[vertex_index];
            const auto TEX_COORD = sheet_uv(vertex_index);
            const auto POS_X = static_cast<float>((CENTRE_X + (point.x * source.w) - output.x) / output.w);
            const auto POS_Y = static_cast<float>((CENTRE_Y + (point.y * source.h) - output.y) / output.h);
            *window_vertex++ = {
                .x = POS_X, .y = POS_Y,
                .u = static_cast<float>((source.x + (source.w * TEX_COORD.x) - target.image_origin.x) / target.image_size.x),
                .v = static_cast<float>((source.y + (source.h * TEX_COORD.y) - target.image_origin.y) / target.image_size.y),
            };
            const double TEXEL = 128 + (std::clamp(static_cast<double>(corner_shade), -1.0, 1.0) * 127.0);
            *shade_vertex++ = {.x = POS_X, .y = POS_Y, .u = static_cast<float>((TEXEL + 0.5) / 256), .v = 0.5F};
        }
    }
    animation::Mesh<V> result{};
    result.window = &window;
    result.shade = shaded ? &shade : nullptr;
    result.opacity = opacity;
    return result;
}

}  // namespace hyprimate
