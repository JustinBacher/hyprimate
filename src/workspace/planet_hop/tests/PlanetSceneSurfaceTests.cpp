#include <array>
#include <cmath>
#include <cstdint>

#include "Check.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

using namespace hyprimate::workspace::planet_hop;
using hyprimate::workspace::JourneyEndpoint;

namespace {
    constexpr std::array<float, 9> IDENTITY{1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool close(double first, double second, double tolerance = 1e-5) {
        return std::abs(first - second) < tolerance;
    }
    bool close(Vec3 first, Vec3 second, double tolerance = 1e-5) {
        return close(first.x, second.x, tolerance) && close(first.y, second.y, tolerance) &&
               close(first.z, second.z, tolerance);
    }
    double length(Vec3 value) {
        return std::hypot(value.x, value.y, value.z);
    }
    std::array<double, 4> transform(
        const std::array<float, 16> &matrix, std::array<double, 4> point) {
        std::array<double, 4> result{};
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                result[row] += matrix[(column * 4) + row] * point[column];
            }
        }
        return result;
    }
    JourneyEndpoint numbered(std::int64_t identity, std::uint32_t number, std::uint32_t rank) {
        return {.identity = identity,
            .numbered = number,
            .special = false,
            .logical_order = rank,
            .placement = static_cast<std::uint64_t>(number) * 2};
    }
}  // namespace

namespace planet_scene_surface_tests {
    void geometry_has_native_endpoints() {
        const auto JOURNEY = make_journey(numbered(1, 1, 0), numbered(9, 9000, 2));
        for (double const ASPECT : {.5, 1., 16. / 9, 3.}) {
            for (double const PROGRESS : {0., 1.}) {
                const auto FRAME = evaluate(JOURNEY, PROGRESS, ASPECT);
                const double CURVATURE =
                    PROGRESS == 0 ? FRAME.source_curvature : FRAME.destination_curvature;
                CHECK(CURVATURE == 0);
                CHECK((PROGRESS == 0 ? FRAME.destination_curvature : FRAME.source_curvature) == 1);
                CHECK(FRAME.endpoint_envelope == 0);
                CHECK(FRAME.source_rotation == IDENTITY);
                CHECK(FRAME.destination_rotation == IDENTITY);
                const auto CENTER = PROGRESS == 0 ? FRAME.source_center : FRAME.destination_center;
                for (double const COORD_U : {0., .3, 1.}) {
                    for (double const COORD_V : {0., .7, 1.}) {
                        const auto POINT = project_surface({.u = COORD_U,
                            .v = COORD_V,
                            .aspect = ASPECT,
                            .radius = FRAME.radius,
                            .coverage = FRAME.coverage,
                            .curvature = CURVATURE,
                            .rotation = IDENTITY,
                            .center = CENTER,
                            .elevation = 20});
                        CHECK(close(POINT.position,
                            {CENTER.x + (((2 * COORD_U) - 1) * ASPECT),
                                CENTER.y + 1 - (2 * COORD_V),
                                CENTER.z + FRAME.radius}));
                        CHECK(close(POINT.normal, {0, 0, 1}));
                        const auto CLIP = transform(FRAME.view_projection,
                            {POINT.position.x, POINT.position.y, POINT.position.z, 1});
                        CHECK(close(CLIP[0] / CLIP[3], (2 * COORD_U) - 1));
                        CHECK(close(CLIP[1] / CLIP[3], 1 - (2 * COORD_V)));
                    }
                }
            }
        }
    }

    void projection_is_spherical_and_aspect_correct() {
        for (double const ASPECT : {.5, 1., 16. / 9, 3.}) {
            for (double const COORD_U : {0., .25, .5, 1.}) {
                for (double const COORD_V : {0., .3, .5, 1.}) {
                    const auto VERTEX = make_surface_vertex(COORD_U, COORD_V, ASPECT, 2, .7);
                    CHECK(close(length({VERTEX.sphere_position[0],
                                    VERTEX.sphere_position[1],
                                    VERTEX.sphere_position[2]}),
                        2));
                    const auto POINT = project_surface({.u = COORD_U,
                        .v = COORD_V,
                        .aspect = ASPECT,
                        .radius = 2,
                        .coverage = .7,
                        .curvature = 1,
                        .rotation = IDENTITY});
                    CHECK(close(length(POINT.position), 2));
                    CHECK(close(length(POINT.normal), 1));
                    const auto ELEVATED = project_surface({.u = COORD_U,
                        .v = COORD_V,
                        .aspect = ASPECT,
                        .radius = 2,
                        .coverage = .7,
                        .curvature = 1,
                        .rotation = IDENTITY,
                        .elevation = .2});
                    CHECK(close(length(ELEVATED.position), 2.2));
                }
            }
            const auto RIGHT = make_surface_vertex(.5001, .5, ASPECT, 1);
            const auto VERTICAL = make_surface_vertex(.5, .4999, ASPECT, 1);
            CHECK(close(RIGHT.sphere_position[0] / VERTICAL.sphere_position[1], ASPECT, 1e-3));
        }
        // All four material edges meet the geometric hemisphere silhouette,
        // not an interior chart boundary on a larger backing sphere.
        for (double const COORD : {0., .2, .5, .8, 1.}) {
            for (const auto UV : {std::array{0., COORD},
                     std::array{1., COORD},
                     std::array{COORD, 0.},
                     std::array{COORD, 1.}}) {
                const auto EDGE = make_surface_vertex(UV[0], UV[1], 2, 1, .7);
                CHECK(close(EDGE.sphere_position[2], 0));
                CHECK(close(std::hypot(EDGE.sphere_position[0], EDGE.sphere_position[1]), 1));
            }
        }
    }

    void angular_bend_is_convex_and_stable() {
        constexpr double RADIUS = 1.3;
        constexpr double COVERAGE = .7;
        for (double const ASPECT : {.5, 1., 16. / 9, 3.}) {
            for (double const CURVATURE : {0., 1e-12, .001, .05, .4, .8, 1.}) {
                for (double const COORD_U : {0., .2, .5, .8, 1.}) {
                    for (double const COORD_V : {0., .3, .5, .9, 1.}) {
                        const auto POINT = project_surface({.u = COORD_U,
                            .v = COORD_V,
                            .aspect = ASPECT,
                            .radius = RADIUS,
                            .coverage = COVERAGE,
                            .curvature = CURVATURE,
                            .rotation = IDENTITY});
                        CHECK(std::isfinite(POINT.position.x) && std::isfinite(POINT.position.y) &&
                              std::isfinite(POINT.position.z));
                        CHECK(close(length(POINT.normal), 1, 1e-10));
                        if (CURVATURE == 0) {
                            CHECK(POINT.position.x == ((2 * COORD_U) - 1) * ASPECT);
                            CHECK(POINT.position.y == 1 - (2 * COORD_V));
                            CHECK(POINT.position.z == RADIUS);
                            CHECK(close(POINT.normal, {0, 0, 1}, 1e-12));
                        } else if (CURVATURE < 1e-10) {
                            CHECK(close(POINT.position,
                                {((2 * COORD_U) - 1) * ASPECT, 1 - (2 * COORD_V), RADIUS},
                                1e-9));
                        } else {
                            // The front is a graph with bounded XY; it has no
                            // extended exterior sheet or receding hidden body.
                            const double HALF_WIDTH = (1 - CURVATURE) * ASPECT + CURVATURE * RADIUS;
                            const double HALF_HEIGHT = 1 - CURVATURE + CURVATURE * RADIUS;
                            CHECK(std::abs(POINT.position.x) <= HALF_WIDTH + 1e-12);
                            CHECK(std::abs(POINT.position.y) <= HALF_HEIGHT + 1e-12);
                            CHECK(POINT.position.z >= RADIUS * (1 - CURVATURE) - 1e-12);
                            CHECK(POINT.position.z <= RADIUS + 1e-12);
                            if (COORD_U == 0 || COORD_U == 1 || COORD_V == 0 || COORD_V == 1)
                                CHECK(close(POINT.position.z, RADIUS * (1 - CURVATURE), 1e-12));
                            if (CURVATURE == 1) CHECK(close(length(POINT.position), RADIUS, 1e-10));
                        }
                        const auto ELEVATED = project_surface({.u = COORD_U,
                            .v = COORD_V,
                            .aspect = ASPECT,
                            .radius = RADIUS,
                            .coverage = COVERAGE,
                            .curvature = CURVATURE,
                            .rotation = IDENTITY,
                            .elevation = .2});
                        CHECK(close(ELEVATED.position,
                            {POINT.position.x + (.2 * CURVATURE * POINT.normal.x),
                                POINT.position.y + (.2 * CURVATURE * POINT.normal.y),
                                POINT.position.z + (.2 * CURVATURE * POINT.normal.z)},
                            1e-10));
                    }
                }
            }
        }
    }

    void angular_bend_normals_match_tangents_and_rotation() {
        constexpr double ANGLE = .7;
        const auto COSINE = static_cast<float>(std::cos(ANGLE));
        const auto SINE = static_cast<float>(std::sin(ANGLE));
        const std::array<float, 9> ROTATION{COSINE, 0, -SINE, 0, 1, 0, SINE, 0, COSINE};
        const double MATRIX_ANGLE = std::atan2(ROTATION[6], ROTATION[0]);
        for (double const CURVATURE : {0., .05, .4, .8, 1.}) {
            const auto CENTER = project_surface({.u = .5,
                .v = .5,
                .aspect = 1.7,
                .radius = 1.3,
                .coverage = .7,
                .curvature = CURVATURE,
                .rotation = ROTATION});
            CHECK(close(CENTER.position,
                {1.3 * std::sin(MATRIX_ANGLE * CURVATURE),
                    0,
                    1.3 * std::cos(MATRIX_ANGLE * CURVATURE)},
                1e-10));
            CHECK(close(CENTER.normal,
                {std::sin(MATRIX_ANGLE * CURVATURE), 0, std::cos(MATRIX_ANGLE * CURVATURE)},
                1e-10));
            for (double const COORD_U : {.2, .5, .8}) {
                for (double const COORD_V : {.2, .5, .8}) {
                    const auto POINT = project_surface({.u = COORD_U,
                        .v = COORD_V,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const double EPSILON = 1e-5;
                    const auto LEFT = project_surface({.u = COORD_U - EPSILON,
                        .v = COORD_V,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const auto RIGHT = project_surface({.u = COORD_U + EPSILON,
                        .v = COORD_V,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const auto ABOVE = project_surface({.u = COORD_U,
                        .v = COORD_V - EPSILON,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const auto DOWN = project_surface({.u = COORD_U,
                        .v = COORD_V + EPSILON,
                        .aspect = 1.7,
                        .radius = 1.3,
                        .coverage = .7,
                        .curvature = CURVATURE,
                        .rotation = ROTATION});
                    const Vec3 TANGENT_U{.x = RIGHT.position.x - LEFT.position.x,
                        .y = RIGHT.position.y - LEFT.position.y,
                        .z = RIGHT.position.z - LEFT.position.z};
                    const Vec3 TANGENT_V{.x = DOWN.position.x - ABOVE.position.x,
                        .y = DOWN.position.y - ABOVE.position.y,
                        .z = DOWN.position.z - ABOVE.position.z};
                    const Vec3 CROSS{.x = (TANGENT_V.y * TANGENT_U.z) - (TANGENT_V.z * TANGENT_U.y),
                        .y = (TANGENT_V.z * TANGENT_U.x) - (TANGENT_V.x * TANGENT_U.z),
                        .z = (TANGENT_V.x * TANGENT_U.y) - (TANGENT_V.y * TANGENT_U.x)};
                    const double LENGTH = length(CROSS);
                    CHECK(LENGTH > 0);
                    CHECK(close(POINT.normal,
                        {CROSS.x / LENGTH, CROSS.y / LENGTH, CROSS.z / LENGTH},
                        1e-5));
                }
            }
        }
    }
}  // namespace planet_scene_surface_tests
