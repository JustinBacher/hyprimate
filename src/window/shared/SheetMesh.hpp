#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "window/WindowAnimationFacts.hpp"
#include "window/shared/SheetFrame.hpp"

namespace hyprimate {

    inline constexpr std::size_t SHEET_VERTEX_COUNT = SHEET_TRIANGLES * 3;

    // Pure projection into reusable backend vertex storage. The returned vectors are borrowed.
    template <animation::Vertex V>
    animation::Mesh<V> fill_sheet_mesh(const SheetFrame &frame,
        const animation::MeshTarget<V> &target,
        bool shaded,
        float opacity) {
        auto &window = target.buffers->window;
        auto &shade = target.buffers->shade;
        if (window.size() != SHEET_VERTEX_COUNT) {
            window.resize(SHEET_VERTEX_COUNT);
        }
        if (shaded && shade.size() != SHEET_VERTEX_COUNT) {
            shade.resize(SHEET_VERTEX_COUNT);
        }
        const auto &source = target.source;
        const auto &output = target.output;
        const double CENTRE_X = source.x + (0.5 * source.w);
        const double CENTRE_Y = source.y + (0.5 * source.h);
        struct ProjectedVertex {
            float x, y, u, v;
        };
        std::array<ProjectedVertex, SHEET_GRID_VERTICES> projected;
        for (std::size_t i = 0; i < projected.size(); ++i) {
            const auto &point = frame.position[i];
            const auto TEX_COORD = sheet_uv(i);
            projected[i] = {
                .x = static_cast<float>((CENTRE_X + (point.x * source.w) - output.x) / output.w),
                .y = static_cast<float>((CENTRE_Y + (point.y * source.h) - output.y) / output.h),
                .u = static_cast<float>(
                    (source.x + (source.w * TEX_COORD.x) - target.image_origin.x) /
                    target.image_size.x),
                .v = static_cast<float>(
                    (source.y + (source.h * TEX_COORD.y) - target.image_origin.y) /
                    target.image_size.y),
            };
        }
        const auto fill = [&]<bool SHADED>() {
            auto window_vertex = window.begin();
            auto shade_vertex = shade.begin();
            for (const std::size_t TRI : frame.order) {
                const auto &corners = sheet_triangle(TRI);
                for (std::size_t corner = 0; corner < corners.size(); ++corner) {
                    const auto &vertex = projected[corners[corner]];
                    *window_vertex++ = {.x = vertex.x, .y = vertex.y, .u = vertex.u, .v = vertex.v};
                    if constexpr (SHADED) {
                        const double TEXEL =
                            128 +
                            (std::clamp(static_cast<double>(frame.shade[TRI][corner]), -1.0, 1.0) *
                                127.0);
                        *shade_vertex++ = {.x = vertex.x,
                            .y = vertex.y,
                            .u = static_cast<float>((TEXEL + 0.5) / 256),
                            .v = 0.5F};
                    }
                }
            }
        };
        if (shaded) {
            fill.template operator()<true>();
        } else {
            fill.template operator()<false>();
        }
        animation::Mesh<V> result{};
        result.window = &window;
        result.shade = shaded ? &shade : nullptr;
        result.opacity = opacity;
        return result;
    }

}  // namespace hyprimate
