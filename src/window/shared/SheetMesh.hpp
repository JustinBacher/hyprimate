#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "window/WindowAnimationFacts.hpp"
#include "window/shared/SheetFrame.hpp"

namespace hyprimate {

    inline constexpr std::size_t SHEET_VERTEX_COUNT = SHEET_TRIANGLES * 3;

    static_assert(SHEET_GRID_VERTICES <= 0xFFFFU, "sheet indices are 16-bit");

    // Pure projection into reusable backend vertex storage. The returned vectors are borrowed.
    // An indexed target gets the SHEET_GRID_VERTICES unique projected vertices plus a
    // SHEET_VERTEX_COUNT triangle-list index buffer in back-to-front order; otherwise the window
    // mesh is the expanded triangle list. The shade mesh is always expanded: its texel differs per
    // triangle corner, so its vertices are not shared.
    template <animation::Vertex V>
    animation::Mesh<V> fill_sheet_mesh(const SheetFrame &frame,
        const animation::MeshTarget<V> &target,
        bool shaded,
        float opacity) {
        auto &window = target.buffers->window;
        auto &indices = target.buffers->window_indices;
        auto &shade = target.buffers->shade;
        const std::size_t WINDOW_COUNT = target.indexed ? SHEET_GRID_VERTICES : SHEET_VERTEX_COUNT;
        if (window.size() != WINDOW_COUNT) {
            window.resize(WINDOW_COUNT);
        }
        if (target.indexed && indices.size() != SHEET_VERTEX_COUNT) {
            indices.resize(SHEET_VERTEX_COUNT);
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
        if (target.indexed) {
            for (std::size_t i = 0; i < projected.size(); ++i) {
                window[i] = {
                    .x = projected[i].x, .y = projected[i].y, .u = projected[i].u, .v = projected[i].v};
            }
        }
        const auto fill = [&]<bool SHADED, bool INDEXED>() {
            auto window_vertex = window.begin();
            auto index = indices.begin();
            auto shade_vertex = shade.begin();
            for (const std::size_t TRI : frame.order) {
                const auto &corners = sheet_triangle(TRI);
                for (std::size_t corner = 0; corner < corners.size(); ++corner) {
                    const auto &vertex = projected[corners[corner]];
                    if constexpr (INDEXED) {
                        *index++ = static_cast<std::uint16_t>(corners[corner]);
                    } else {
                        *window_vertex++ = {
                            .x = vertex.x, .y = vertex.y, .u = vertex.u, .v = vertex.v};
                    }
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
        if (target.indexed) {
            if (shaded) {
                fill.template operator()<true, true>();
            } else {
                fill.template operator()<false, true>();
            }
        } else if (shaded) {
            fill.template operator()<true, false>();
        } else {
            fill.template operator()<false, false>();
        }
        animation::Mesh<V> result{};
        result.window = &window;
        result.window_indices = target.indexed ? &indices : nullptr;
        result.shade = shaded ? &shade : nullptr;
        result.opacity = opacity;
        return result;
    }

}  // namespace hyprimate
