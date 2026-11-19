#include "PlanetScene.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "workspace/WorkspaceDirection.hpp"

namespace hyprimate::workspace::planet_hop {
    namespace {
        constexpr double CIRCLE_PI = std::numbers::pi;
        constexpr double FOCAL = 2.414213562373095; // 45 degree vertical field of view.

        double finite(double value, double fallback) { return std::isfinite(value) ? value : fallback; }
        double unit(double value) { return std::clamp(finite(value, 0), 0.0, 1.0); }
        double positive(double value, double fallback) { return std::max(0.001, finite(value, fallback)); }
        double smooth(double value) {
            return value * value * value * ((value * ((value * 6) - 15)) + 10);
        }
        Vec3 add(Vec3 first, Vec3 second) {
            return {.x = first.x + second.x, .y = first.y + second.y, .z = first.z + second.z};
        }
        Vec3 scale(Vec3 value, double factor) {
            return {.x = value.x * factor, .y = value.y * factor, .z = value.z * factor};
        }
        Vec3 mix(Vec3 first, Vec3 second, double progress) { return add(scale(first, 1 - progress), scale(second, progress)); }
        double length(Vec3 value) { return std::hypot(value.x, value.y, value.z); }
        Vec3 normalized(Vec3 value) {
            const double LENGTH = length(value);
            return LENGTH > 1e-12 ? scale(value, 1 / LENGTH) : Vec3{.x = 0, .y = 0, .z = 1};
        }
        double dot(Vec3 first, Vec3 second) {
            return (first.x * second.x) + (first.y * second.y) + (first.z * second.z);
        }
        Vec3 cross(Vec3 first, Vec3 second) {
            return {.x = (first.y * second.z) - (first.z * second.y),
                .y = (first.z * second.x) - (first.x * second.z),
                .z = (first.x * second.y) - (first.y * second.x)};
        }
        double spacing(const SceneOptions &options) {
            return std::max(options.planet_separation * options.planet_size,
                2 * (options.planet_size + std::abs(options.elevation)));
        }
        Vec3 hex_position(std::uint64_t placement, double separation) {
            // A durable slot, never a compositor lifetime ID, selects one disjoint hex cell.
            const std::uint64_t INDEX = placement;
            if (INDEX == 0) {
                return {};
            }
            auto ring =
                static_cast<std::uint64_t>(std::ceil((std::sqrt((12.L * INDEX) + 9) - 3) / 6));
            const auto LAST = [](std::uint64_t value) {
                return static_cast<__uint128_t>(3) * value * (value + 1);
            };
            // Correct possible sqrt rounding at an exact ring boundary with integer arithmetic.
            if (LAST(ring) < INDEX) {
                ++ring;
            } else if (ring > 1 && LAST(ring - 1) >= INDEX) {
                --ring;
            }
            const auto START = static_cast<std::uint64_t>(LAST(ring - 1) + 1);
            auto remaining = INDEX - START;
            auto axial_x = static_cast<std::int64_t>(ring);
            std::int64_t axial_y = 0;
            constexpr std::array<std::array<int, 2>, 6> DIRECTIONS{{
                {0, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 0}, {1, -1}}};
            for (const auto &direction : DIRECTIONS) {
                const auto STEPS = std::min(remaining, ring);
                axial_x += direction[0] * static_cast<std::int64_t>(STEPS);
                axial_y += direction[1] * static_cast<std::int64_t>(STEPS);
                remaining -= STEPS;
            }
            return {.x = separation *
                         (static_cast<double>(axial_x) + (static_cast<double>(axial_y) * .5)),
                .y = separation * std::numbers::sqrt3 * .5 * static_cast<double>(axial_y),
                .z = 0};
        }
        double variation(std::uint64_t value) {
            // SplitMix64: durable slot bits, never population or compositor lifetime IDs.
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
            value ^= value >> 31U;
            return (static_cast<double>(value >> 11U) * 0x1p-52) - 1;
        }
        Vec3 solar_position(const JourneyEndpoint &endpoint, const SceneOptions &options, double aspect) {
            // X cell boundaries guarantee separation regardless of local Y/Z variation.
            // Number gaps are genuine empty space, not compressed inventory ranks.
            const double CELL = spacing(options) * 1.25;
            const double WIDTH = std::max(1.0, positive(aspect, 1));
            if (endpoint.numbered && !endpoint.special) {
                const auto NUMBER = *endpoint.numbered;
                return {.x = CELL * WIDTH *
                             (NUMBER + (.1 * variation(static_cast<std::uint64_t>(NUMBER) +
                                                       0x200000000ULL))),
                    .y = CELL * options.solar_vertical_spread * variation(NUMBER),
                    .z = CELL * .35 *
                         variation(static_cast<std::uint64_t>(NUMBER) + 0x100000000ULL)};
            }
            // Named/special slots occupy disjoint XY cells beside the numbered corridor.
            // Keep depth bounded so cruise clearance does not depend on population. Y
            // spread stretches cells rather than collapsing them when configured to zero.
            const auto SLOT = endpoint.placement / 2;
            std::array<std::uint64_t, 2> cell{};
            for (unsigned bit = 0; bit < 64; ++bit) {
                cell[bit % 2] |= ((SLOT >> bit) & 1ULL) << (bit / 2);
            }
            return {
                .x = -CELL * WIDTH * (1 + static_cast<double>(cell[0]) + (.1 * variation(SLOT))),
                .y = CELL * ((static_cast<double>(cell[1]) * (1 + options.solar_vertical_spread)) +
                                (.1 * options.solar_vertical_spread * variation(SLOT + 1))),
                .z = CELL * .35 * variation(SLOT + 2)};
        }
        std::array<float, 9> rotation(double angle) {
            const auto COSINE = static_cast<float>(std::cos(angle));
            const auto SINE = static_cast<float>(std::sin(angle));
            return {COSINE, 0, -SINE, 0, 1, 0, SINE, 0, COSINE};
        }
        double sinc(double angle) {
            if (std::abs(angle) < .01) {
                const double SQUARED = angle * angle;
                return 1 - (SQUARED / 6) + (SQUARED * SQUARED / 120);
            }
            return std::sin(angle) / angle;
        }
        SceneOptions sanitized(SceneOptions options) {
            options.planet_size = positive(options.planet_size, 1);
            options.planet_separation = positive(options.planet_separation, 4);
            options.planet_coverage = std::clamp(finite(options.planet_coverage, .7), .001, 1.0);
            options.rotation_limit =
                std::clamp(finite(options.rotation_limit, .35), 0.0, CIRCLE_PI);
            options.elevation = finite(options.elevation, .2);
            options.solar_vertical_spread =
                std::clamp(finite(options.solar_vertical_spread, .65), 0.0, 4.0);
            return options;
        }
    }

    std::uint32_t logical_distance(
        const JourneyEndpoint &from, const JourneyEndpoint &destination) {
        if (from.identity == destination.identity) {
            return 0;
        }
        if (!from.numbered || !destination.numbered || from.special || destination.special) {
            return 1;
        }
        return std::max<std::uint32_t>(1,
            from.logical_order > destination.logical_order
                ? from.logical_order - destination.logical_order
                : destination.logical_order - from.logical_order);
    }

    double duration_seconds(const JourneyEndpoint &from,
        const JourneyEndpoint &destination,
        const SceneOptions &options) {
        const double BASE = positive(options.duration, .8);
        const double STEPS = std::max<std::uint32_t>(logical_distance(from, destination), 1);
        const double SCALE = std::max(0.0, finite(options.distance_scale, .25));
        // A configured maximum may cap skip growth, never shorten the base transition.
        const double MAXIMUM = std::max(BASE, finite(options.max_duration, 2.2));
        switch (options.skip_duration_mode) {
            case DurationMode::Constant: return BASE;
            case DurationMode::DistanceScaled:
                return std::min(BASE * (1 + (SCALE * (STEPS - 1))), MAXIMUM);
            case DurationMode::Adaptive: {
                const double EXCESS =
                    std::max(0.0, STEPS - std::max<std::uint32_t>(1, options.adaptive_boundary));
                return std::min(BASE * (1 + (SCALE * std::log2(1 + EXCESS))), MAXIMUM);
            }
        }
        return BASE;
    }

    Vec3 layout_position(const JourneyEndpoint &endpoint, const SceneOptions &raw_options, double aspect) {
        const auto OPTIONS = sanitized(raw_options);
        const double SPACING = spacing(OPTIONS);
        if (OPTIONS.planet_layout == Layout::SolarSystem) {
            return solar_position(endpoint, OPTIONS, aspect);
        }
        if (endpoint.numbered && !endpoint.special) {
            return {.x = SPACING * endpoint.logical_order, .y = 0, .z = 0};
        }
        return hex_position(endpoint.placement, SPACING);
    }

    Journey make_journey(
        JourneyEndpoint from, JourneyEndpoint destination, SceneOptions options, double aspect) {
        options = sanitized(options);
        aspect = positive(aspect, 1);
        Journey result{.from = from,
            .to = destination,
            .options = options,
            .source_center = layout_position(from, options, aspect),
            .destination_center = layout_position(destination, options, aspect),
            .aspect = aspect};
        const bool NUMBERED =
            from.numbered && destination.numbered && !from.special && !destination.special;
        if (options.planet_layout == Layout::Dynamic ||
            (options.planet_layout == Layout::Linear && !NUMBERED)) {
            // Named/special fallback follows the same navigation semantics as WorkspaceTurn.
            const double SIGN =
                direction_between(
                    from.numbered, destination.numbered, from.special, destination.special) ==
                        Direction::Forward
                    ? 1.0
                    : -1.0;
            result.source_center = {};
            result.destination_center = from.identity == destination.identity
                                            ? Vec3{}
                                            : Vec3{.x = SIGN * spacing(options), .y = 0, .z = 0};
        }
        if (from.identity == destination.identity) {
            result.destination_center = result.source_center;
        }
        return result;
    }

    Frame evaluate(const Journey &journey, double progress, double aspect) {
        const double POSITION = unit(progress);
        const auto &options = journey.options;
        const bool SOLAR = options.planet_layout == Layout::SolarSystem;
        // Solar travel waits until full clearance, then reaches the destination before
        // descent. This remains collision-safe even when sparse number gaps are enormous.
        const double TRAVEL_END = SOLAR ? .68 : .90;
        const double TRAVEL_START = SOLAR ? .32 : .10;
        const double TRAVEL_SPAN = SOLAR ? .36 : .80;
        const double TRAVEL =
            POSITION >= TRAVEL_END ? 1 : smooth(unit((POSITION - TRAVEL_START) / TRAVEL_SPAN));
        // Polynomial bump has zero position/rotation/curvature velocity at both native endpoints.
        const double ENVELOPE = 16 * POSITION * POSITION * (1 - POSITION) * (1 - POSITION);
        const double ASPECT = positive(aspect, 1);
        const double RADIUS = options.planet_size;
        const Vec3 CENTER = mix(journey.source_center, journey.destination_center, TRAVEL);
        const Vec3 DELTA = add(journey.destination_center, scale(journey.source_center, -1));
        const double SOURCE_CURVATURE = smooth(unit(POSITION / .22));
        const double DESTINATION_CURVATURE = smooth(unit((1 - POSITION) / .22));
        const double REVEAL = smooth(unit((POSITION - .12) / .20));
        // Evaluate from the landing endpoint rather than subtracting a nearly-one
        // polynomial: tiny cancellation errors become visible on very long world legs.
        const double APPROACH = smooth(unit((1 - POSITION) / .32));
        // Hold flight altitude while crossing the direct leg (.32-.68), so the outgoing
        // sphere recedes while the incoming sphere grows. Only then descend onto the endpoint.
        const double FIT = FOCAL * .65 * std::max(std::abs(DELTA.x) / ASPECT, std::abs(DELTA.y));
        // Cruise frames the local neighborhood, never an entire sparse/long skip.
        // Bounded world depth plus its full range ensures clearance over every body.
        // A default .75-radius background body is roughly 50 pixels across at 720p.
        const double LOCAL_ALTITUDE =
            (18 * RADIUS) + (2 * std::abs(options.elevation)) + (.7 * spacing(options) * 1.25);
        const double PULLBACK = REVEAL * APPROACH * (SOLAR ? LOCAL_ALTITUDE : (2 * RADIUS) + FIT);
        const Vec3 CAMERA{.x = 0, .y = 0, .z = RADIUS + FOCAL + PULLBACK};
        const double AIM = .30 + (.70 * smooth(unit((POSITION - .32) / .36)));
        const Vec3 REMAINING = scale(DELTA, 1 - TRAVEL);
        const double LEAD = SOLAR ? std::min(1.0,
                                        .4 * spacing(options) * std::min(1.0, ASPECT) /
                                            std::max(length(REMAINING), .001))
                                  : 1;
        const Vec3 TARGET = SOLAR ? scale(REMAINING, AIM * REVEAL * LEAD) :
            scale(DELTA, AIM * (1 - TRAVEL) * REVEAL);
        const double SIGN = direction_between(journey.from.numbered, journey.to.numbered,
            journey.from.special, journey.to.special) == Direction::Forward ? 1.0 : -1.0;
        const double ANGLE =
            options.rotation_enabled ? options.rotation_limit * ENVELOPE * SIGN : 0;
        const double NEAR = .01 + (PULLBACK * .01);
        const double FAR = std::max(100.0, 4 * (RADIUS + PULLBACK + FOCAL));
        const auto SOURCE_ROTATION = rotation(ANGLE);
        auto destination_rotation = SOURCE_ROTATION;
        destination_rotation[2] = -destination_rotation[2];
        destination_rotation[6] = -destination_rotation[6];
        const double DEPTH_A = -(FAR + NEAR) / (FAR - NEAR);
        const double DEPTH_B = -2 * FAR * NEAR / (FAR - NEAR);
        const auto SCALAR = [](double value) { return static_cast<float>(value); };
        const Vec3 BACK = normalized(add(CAMERA, scale(TARGET, -1)));
        const Vec3 RIGHT = normalized(cross({.x = 0, .y = 1, .z = 0}, BACK));
        const Vec3 VERTICAL = cross(BACK, RIGHT);
        const double TRANSLATE_X = -dot(RIGHT, CAMERA);
        const double TRANSLATE_Y = -dot(VERTICAL, CAMERA);
        const double TRANSLATE_Z = -dot(BACK, CAMERA);
        // Perspective * look-at; only the aim leads, never the direct translation.
        const std::array<float, 16> VIEW_PROJECTION{SCALAR(FOCAL / ASPECT * RIGHT.x),
            SCALAR(FOCAL * VERTICAL.x),
            SCALAR(DEPTH_A * BACK.x),
            SCALAR(-BACK.x),
            SCALAR(FOCAL / ASPECT * RIGHT.y),
            SCALAR(FOCAL * VERTICAL.y),
            SCALAR(DEPTH_A * BACK.y),
            SCALAR(-BACK.y),
            SCALAR(FOCAL / ASPECT * RIGHT.z),
            SCALAR(FOCAL * VERTICAL.z),
            SCALAR(DEPTH_A * BACK.z),
            SCALAR(-BACK.z),
            SCALAR(FOCAL / ASPECT * TRANSLATE_X),
            SCALAR(FOCAL * TRANSLATE_Y),
            SCALAR((DEPTH_A * TRANSLATE_Z) + DEPTH_B),
            SCALAR(-TRANSLATE_Z)};
        // camera-to-world * inverse(perspective), analytic even while the aim changes.
        const std::array<float, 16> INVERSE{SCALAR(ASPECT / FOCAL * RIGHT.x),
            SCALAR(ASPECT / FOCAL * RIGHT.y),
            SCALAR(ASPECT / FOCAL * RIGHT.z),
            0,
            SCALAR(VERTICAL.x / FOCAL),
            SCALAR(VERTICAL.y / FOCAL),
            SCALAR(VERTICAL.z / FOCAL),
            0,
            SCALAR(CAMERA.x / DEPTH_B),
            SCALAR(CAMERA.y / DEPTH_B),
            SCALAR(CAMERA.z / DEPTH_B),
            SCALAR(1 / DEPTH_B),
            SCALAR(-BACK.x + (CAMERA.x * DEPTH_A / DEPTH_B)),
            SCALAR(-BACK.y + (CAMERA.y * DEPTH_A / DEPTH_B)),
            SCALAR(-BACK.z + (CAMERA.z * DEPTH_A / DEPTH_B)),
            SCALAR(DEPTH_A / DEPTH_B)};
        return {.world_origin = CENTER,
            .camera_position = CAMERA,
            .camera_target = TARGET,
            .source_center = add(journey.source_center, scale(CENTER, -1)),
            .destination_center = add(journey.destination_center, scale(CENTER, -1)),
            .view_projection = VIEW_PROJECTION,
            .inverse_view_projection = INVERSE,
            .source_rotation = SOURCE_ROTATION,
            .destination_rotation = destination_rotation,
            .source_curvature = SOURCE_CURVATURE,
            .destination_curvature = DESTINATION_CURVATURE,
            .endpoint_envelope = ENVELOPE,
            .travel_progress = TRAVEL,
            .radius = RADIUS,
            .coverage = options.planet_coverage,
            .aspect = ASPECT};
    }

    SurfaceVertex make_surface_vertex(
        double coord_u, double coord_v, double aspect, double radius, double coverage) {
        coord_u = unit(coord_u);
        coord_v = unit(coord_v);
        aspect = positive(aspect, 1);
        radius = positive(radius, 1);
        coverage = std::clamp(finite(coverage, .7), .001, 1.0);
        // Limit both chart angles to a sphere while preserving the desktop's center aspect.
        const double LONGITUDE =
            ((2 * coord_u) - 1) * CIRCLE_PI * coverage * std::min(1.0, aspect / 2);
        const double LATITUDE = (.5 - coord_v) * CIRCLE_PI * coverage * std::min(1.0, 2 / aspect);
        return {.flat_position = {static_cast<float>(((2 * coord_u) - 1) * aspect),
                    static_cast<float>(1 - (2 * coord_v)),
                    static_cast<float>(radius)},
            .sphere_position = {static_cast<float>(
                                    radius * std::cos(LATITUDE) * std::sin(LONGITUDE)),
                static_cast<float>(radius * std::sin(LATITUDE)),
                static_cast<float>(radius * std::cos(LATITUDE) * std::cos(LONGITUDE))},
            .uv = {static_cast<float>(coord_u), static_cast<float>(coord_v)}};
    }

    SurfacePoint project_surface(const SurfaceProjection &projection) {
        const double COORD_U = unit(projection.u);
        const double COORD_V = unit(projection.v);
        const double ASPECT = positive(projection.aspect, 1);
        const double RADIUS = positive(projection.radius, 1);
        const double COVERAGE = std::clamp(finite(projection.coverage, .7), .001, 1.0);
        const auto &rotation_matrix = projection.rotation;
        const double CURVATURE = unit(projection.curvature);
        const double LON_LIMIT = CIRCLE_PI * COVERAGE * std::min(1.0, ASPECT / 2);
        const double LAT_LIMIT = CIRCLE_PI * .5 * COVERAGE * std::min(1.0, 2 / ASPECT);
        const double LONGITUDE = ((2 * COORD_U) - 1) * LON_LIMIT;
        const double LATITUDE = (1 - (2 * COORD_V)) * LAT_LIMIT;
        const double BENT_LON = CURVATURE * LONGITUDE;
        const double BENT_LAT = CURVATURE * LATITUDE;
        const double COS_LON = std::cos(BENT_LON);
        const double COS_LAT = std::cos(BENT_LAT);
        const double SINC_HALF_LON = sinc(BENT_LON / 2);
        const double SINC_HALF_LAT = sinc(BENT_LAT / 2);
        const double DEPTH_LON = LONGITUDE * LONGITUDE * SINC_HALF_LON * SINC_HALF_LON;
        const double DEPTH_LAT = LATITUDE * LATITUDE * SINC_HALF_LAT * SINC_HALF_LAT;
        // Stable (1-cos(kL)cos(kB))/k, including k=0 without division or cancellation.
        const double DEPTH = CURVATURE * ((.5 * DEPTH_LON) + (.5 * DEPTH_LAT) -
                                             (.25 * CURVATURE * CURVATURE * DEPTH_LON * DEPTH_LAT));
        // Factor the flat coordinates so k=0 preserves the exact native arithmetic.
        const double SCALE_X = (1 - CURVATURE) + (CURVATURE * RADIUS * LON_LIMIT / ASPECT);
        const double SCALE_Y = (1 - CURVATURE) + (CURVATURE * RADIUS * LAT_LIMIT);
        const Vec3 SURFACE{.x = ((2 * COORD_U) - 1) * ASPECT * COS_LAT * sinc(BENT_LON) * SCALE_X,
            .y = (1 - (2 * COORD_V)) * sinc(BENT_LAT) * SCALE_Y,
            .z = RADIUS - (RADIUS * DEPTH)};
        const double AXIS_X = ((1 - CURVATURE) * ASPECT / LON_LIMIT) + (CURVATURE * RADIUS);
        const double AXIS_Y = ((1 - CURVATURE) / LAT_LIMIT) + (CURVATURE * RADIUS);
        const Vec3 NORMAL = normalized({.x = RADIUS / AXIS_X * std::sin(BENT_LON) * COS_LAT,
            .y = RADIUS / AXIS_Y * std::sin(BENT_LAT),
            .z = COS_LON * COS_LAT});
        // The material's orientation is a Y rotation; bend and orientation share one amount.
        const double ANGLE = std::atan2(rotation_matrix[6], rotation_matrix[0]) * CURVATURE;
        const double COSINE = std::cos(ANGLE);
        const double SINE = std::sin(ANGLE);
        const auto ROTATE = [COSINE, SINE](Vec3 point) {
            return Vec3{.x = (COSINE * point.x) + (SINE * point.z),
                .y = point.y,
                .z = (-SINE * point.x) + (COSINE * point.z)};
        };
        const Vec3 WORLD_NORMAL = ROTATE(NORMAL);
        return {.position = add(projection.center,
                    add(ROTATE(SURFACE),
                        scale(WORLD_NORMAL, finite(projection.elevation, 0) * CURVATURE))),
            .normal = WORLD_NORMAL};
    }
}
