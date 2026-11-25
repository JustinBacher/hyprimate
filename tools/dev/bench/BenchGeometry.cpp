// Geometry, edge-transition, liquid-mesh and paper workloads.
#include <array>
#include <cstdint>

#include "Bench.hpp"
#include "window/liquid/LiquidMesh.hpp"
#include "window/paper/PaperCrumple.hpp"
#include "window/liquid/EdgeTransition.hpp"
#include "window/shared/EdgeFrame.hpp"

namespace {
    using namespace hyprimate;
    constexpr double FRAME = 1.0 / 60.0;
    constexpr Box SCREEN{.x = 0, .y = 0, .w = 1920, .h = 1080};

    // Tiled layouts with varied neighbours: every call walks the contact and validity logic.
    double edge_resolve() {
        double sum = 0;
        std::array<Box, 3> neighbours{};
        for (int i = 0; i < 1000; ++i) {
            const double J = static_cast<double>(i % 37);
            const Box BODY{.x = 400 + J * 9, .y = 200 + J * 5, .w = 700 + J * 3, .h = 500 + J * 2};
            const Box CAPTURE{.x = BODY.x - 20, .y = BODY.y - 20, .w = BODY.w + 40, .h = BODY.h + 40};
            neighbours[0] = {.x = 0, .y = BODY.y, .w = BODY.x - 8, .h = BODY.h};
            neighbours[1] = {.x = BODY.x + BODY.w + 8, .y = 0, .w = 300, .h = 1080};
            neighbours[2] = {.x = BODY.x, .y = BODY.y + BODY.h + 8, .w = BODY.w, .h = 200};
            const auto FRAME_OUT = resolve_edge_frame(BODY,
                CAPTURE,
                SCREEN,
                std::span<const Box>(neighbours).first(static_cast<std::size_t>(i % 4)),
                Edge::Bottom,
                i % 5 == 0);
            if (FRAME_OUT) {
                sum += FRAME_OUT->edge_distance + static_cast<double>(FRAME_OUT->edge);
            }
            bench::keep(sum);
        }
        return sum;
    }

    // Open then close, sampling the 33x33 render grid each frame as the renderer does.
    double edge_transition() {
        const auto FRAME_IN = resolve_edge_frame(
            {.x = 400, .y = 200, .w = 900, .h = 600},
            {.x = 380, .y = 180, .w = 940, .h = 640},
            SCREEN, {}, Edge::Bottom, true);
        double sum = 0;
        const Vec2 SIZE{.x = 940, .y = 640};
        for (const Transition KIND : {Transition::Open, Transition::Close}) {
            EdgeTransition transition;
            transition.begin(KIND, *FRAME_IN, {}, 0.65);
            for (int i = 0; i < 60 && transition.advance(FRAME); ++i) {
                const auto REST = transition.rest_map(SIZE);
                for (int row = 0; row <= LIQUID_RENDER_CELLS; ++row) {
                    for (int column = 0; column <= LIQUID_RENDER_CELLS; ++column) {
                        const Vec2 POINT = REST.map({.x = (column / 32.0 - 0.5) * SIZE.x,
                            .y = (row / 32.0 - 0.5) * SIZE.y});
                        sum += POINT.x + POINT.y;
                    }
                }
            }
        }
        return sum;
    }

    // A scripted drag, a stop and the settle, sampling the displayed field per frame.
    double liquid_movement() {
        LiquidMesh mesh({}, 7);
        const Vec2 SIZE{.x = 900, .y = 600};
        double sum = 0;
        for (int i = 0; i < 180; ++i) {
            if (i < 60) {
                mesh.record_translation({.x = 14.0, .y = 3.0}, SIZE, 0.65);
            }
            mesh.advance(FRAME, SIZE);
            for (int row = 0; row <= LIQUID_RENDER_CELLS; ++row) {
                for (int column = 0; column <= LIQUID_RENDER_CELLS; ++column) {
                    const Vec2 D = mesh.sample_displacement(column / 32.0, row / 32.0);
                    sum += D.x + D.y;
                }
            }
        }
        return sum;
    }

    double paper_close() {
        PaperCrumple paper;
        paper.begin(Transition::Close, {}, 5, {.x = 940, .y = 640});
        PaperSurface out;
        double sum = 0;
        for (int i = 0; i < 30; ++i) {
            paper.advance(FRAME);
            paper.frame(out);
            if (!out.points.empty()) { sum += out.points[out.points.size() / 2].position.z; }
        }
        return sum;
    }
}  // namespace

namespace bench {
    void register_geometry(Registry &registry) {
        registry.push_back({"geometry.edge_resolve", "resolve_edge_frame, 1000 layouts", 1000, edge_resolve});
        registry.push_back({"edge.open_close", "EdgeTransition open+close, 33x33 map per frame", 120, edge_transition});
        registry.push_back({"liquid.move_settle", "LiquidMesh drag 1 s then settle, 33x33 samples", 180, liquid_movement});
        registry.push_back({"paper.close", "Procedural Paper close and pose publication", 30, paper_close});
    }
}  // namespace bench
