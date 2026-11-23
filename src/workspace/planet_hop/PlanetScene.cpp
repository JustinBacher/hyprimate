#include <algorithm>
#include <cmath>
#include <numbers>

#include "PlanetScene.hpp"
#include "workspace/WorkspaceDirection.hpp"

namespace hyprimate::workspace::planet_hop {
    namespace {
        constexpr double CIRCLE_PI = std::numbers::pi;
        constexpr double FOCAL = 2.414213562373095;  // 45 degree vertical field of view.

        double finite(double value, double fallback) {
            return std::isfinite(value) ? value : fallback;
        }
        double unit(double value) {
            return std::clamp(finite(value, 0), 0.0, 1.0);
        }
        double positive(double value, double fallback) {
            return std::max(0.001, finite(value, fallback));
        }
        double smooth(double value) {
            return value * value * value * ((value * ((value * 6) - 15)) + 10);
        }
        double travel_progress(double position, bool solar) {
            // Solar travel waits for full clearance and ends before descent.
            const double END = solar ? .68 : .90;
            const double START = solar ? .32 : .10;
            const double SPAN = solar ? .36 : .80;
            return position >= END ? 1 : smooth(unit((position - START) / SPAN));
        }
        double endpoint_envelope(double position) {
            return 16 * position * position * (1 - position) * (1 - position);
        }
        Vec3 add(Vec3 first, Vec3 second) {
            return {.x = first.x + second.x, .y = first.y + second.y, .z = first.z + second.z};
        }
        Vec3 scale(Vec3 value, double factor) {
            return {.x = value.x * factor, .y = value.y * factor, .z = value.z * factor};
        }
        Vec3 mix(Vec3 first, Vec3 second, double progress) {
            return add(scale(first, 1 - progress), scale(second, progress));
        }
        double length(Vec3 value) {
            return std::hypot(value.x, value.y, value.z);
        }
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
            constexpr std::array<std::array<int, 2>, 6> DIRECTIONS{
                {{0, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 0}, {1, -1}}};
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
        Vec3 solar_position(const JourneyEndpoint &endpoint, const SceneOptions &options) {
            // Fermat spiral: durable slots, not inventory rank/population, choose orbits.
            // The radial scale leaves clearance between adjacent golden-angle samples.
            const auto SLOT = static_cast<double>(endpoint.placement);
            const double ANGLE = std::fmod(SLOT * 2.399963229728653, 2 * CIRCLE_PI);
            const double CELL = spacing(options) * .8;
            const double RADIUS = CELL * std::sqrt(SLOT + 1);
            const double HEIGHT = positive(endpoint.monitor_height, 1);
            const double OWNER_DEPTH = std::tanh(finite(endpoint.monitor_x, 0) / HEIGHT) -
                                       std::tanh(finite(endpoint.monitor_y, 0) / HEIGHT);
            return {.x = RADIUS * std::cos(ANGLE) *
                         std::max(1.0, positive(endpoint.monitor_width, 1) / HEIGHT),
                .y = RADIUS * std::sin(ANGLE) * (1 + options.solar_vertical_spread),
                .z = CELL * ((.7 * variation(endpoint.placement)) + (.3 * OWNER_DEPTH))};
        }
        std::array<float, 9> rotation(double angle) {
            const auto COSINE = static_cast<float>(std::cos(angle));
            const auto SINE = static_cast<float>(std::sin(angle));
            return {COSINE, 0, -SINE, 0, 1, 0, SINE, 0, COSINE};
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
    }  // namespace

    Vec3 layout_position(
        const JourneyEndpoint &endpoint, const SceneOptions &raw_options, double /*aspect*/) {
        const auto OPTIONS = sanitized(raw_options);
        const double SPACING = spacing(OPTIONS);
        if (OPTIONS.planet_layout == Layout::SolarSystem) {
            return solar_position(endpoint, OPTIONS);
        }
        auto position = hex_position(endpoint.placement, SPACING);
        const double OWNER_ASPECT =
            positive(endpoint.monitor_width, 1) / positive(endpoint.monitor_height, 1);
        position.x *= std::max(1.0, OWNER_ASPECT);
        // Bounded owner offsets preserve physical orientation without allowing a
        // monitor translation to alias another durable workspace's hex cell.
        position.x += SPACING * .1 * std::max(1.0, OWNER_ASPECT) *
                      std::tanh(finite(endpoint.monitor_x, 0) / 1080);
        position.y -= SPACING * .1 * std::tanh(finite(endpoint.monitor_y, 0) / 1080);
        if (OPTIONS.planet_layout == Layout::All) {
            // Compact tilted constellations retain identity in the image plane.
            // Never put an owner's entire inventory on the camera's landing ray.
            position.x += SPACING * .05 * std::max(1.0, OWNER_ASPECT) *
                          std::tanh(positive(endpoint.monitor_width, 1) / 1080);
            position.y -= SPACING * .05 * std::tanh(positive(endpoint.monitor_height, 1) / 1080);
            position.z = SPACING * .5 * variation(endpoint.placement);
        } else {
            position.z = SPACING * .25 * variation(endpoint.placement);
        }
        return position;
    }

    bool scene_member(const JourneyEndpoint &candidate,
        const JourneyEndpoint &from,
        const JourneyEndpoint &destination,
        Layout layout) {
        if (candidate.identity == from.identity || candidate.identity == destination.identity) {
            return true;
        }
        if (layout == Layout::Focused ||
            (candidate.special && !from.special && !destination.special)) {
            return false;
        }
        return layout != Layout::Monitor || candidate.owner == from.owner ||
               candidate.owner == destination.owner;
    }

    bool neighbor_visible(const Frame &frame, Vec3 center, double radius) {
        const auto &matrix = frame.view_projection;
        const auto PROJECT = [&](Vec3 point, std::size_t row) {
            return matrix[row] * point.x + matrix[4 + row] * point.y + matrix[8 + row] * point.z +
                   matrix[12 + row];
        };
        // In flight, depth testing determines occlusion. During native handoff,
        // protect the viewport from inventory in front of the endpoint desktop.
        const Vec3 *native = frame.source_curvature < 1 || frame.travel_progress == 0
                                 ? &frame.source_center
                             : frame.destination_curvature < 1 || frame.travel_progress == 1
                                 ? &frame.destination_center
                                 : nullptr;
        if (!native) {
            return true;
        }
        const double BOUND = std::max(0.0, finite(radius, 0)) * std::max(1.0, frame.aspect);
        const double W = PROJECT(center, 3);
        if (W - BOUND >= PROJECT(*native, 3)) {
            return true;
        }
        for (std::size_t row = 0; row < 2; ++row) {
            const double EXTENT = BOUND * std::hypot(matrix[row], matrix[4 + row], matrix[8 + row]);
            if (std::abs(PROJECT(center, row)) > W + BOUND + EXTENT) {
                return true;
            }
        }
        return false;
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
        if (options.planet_layout == Layout::Focused) {
            // Endpoint-only flight follows the same navigation semantics as WorkspaceTurn.
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
        if (options.planet_layout == Layout::SolarSystem) {
            auto &path = result.solar_path;
            path.from_aspect =
                std::max(1.0, positive(from.monitor_width, 1) / positive(from.monitor_height, 1));
            path.to_aspect = std::max(1.0,
                positive(destination.monitor_width, 1) / positive(destination.monitor_height, 1));
            const Vec3 FIRST{result.source_center.x / path.from_aspect,
                result.source_center.y / (1 + options.solar_vertical_spread),
                0};
            const Vec3 LAST{result.destination_center.x / path.to_aspect,
                result.destination_center.y / (1 + options.solar_vertical_spread),
                0};
            path.from_angle = std::atan2(FIRST.y, FIRST.x);
            path.angle_delta =
                std::remainder(std::atan2(LAST.y, LAST.x) - path.from_angle, 2 * CIRCLE_PI);
            path.from_radius = length(FIRST);
            path.to_radius = length(LAST);
        }
        if (options.planet_layout == Layout::Monitor || options.planet_layout == Layout::All) {
            // A vertical leg still needs its neighboring horizontal hex column
            // in view, particularly on portrait outputs.
            result.neighborhood_width =
                FOCAL * spacing(options) * 1.65 *
                std::max({1.0,
                    positive(from.monitor_width, 1) / positive(from.monitor_height, 1),
                    positive(destination.monitor_width, 1) /
                        positive(destination.monitor_height, 1)});
        }
        return result;
    }

    double remaining_travel(double progress, bool destination, bool solar) {
        // Measure toward either endpoint directly; 1 minus a rounded cumulative
        // value would lose small but nonzero motion near the landing endpoint.
        const double POSITION = unit(destination ? 1.0 - progress : progress);
        const double ENVELOPE = endpoint_envelope(POSITION);
        // Half the budget measures cruise; the other half measures departure
        // and arrival, where camera translation alone can be stationary.
        return (.5 * travel_progress(POSITION, solar)) +
               (.25 * (POSITION <= .5 ? ENVELOPE : 2 - ENVELOPE));
    }

    Frame evaluate(const Journey &journey, double progress, double aspect) {
        const double POSITION = unit(progress);
        const auto &options = journey.options;
        const bool SOLAR = options.planet_layout == Layout::SolarSystem;
        const double TRAVEL = travel_progress(POSITION, SOLAR);
        // Polynomial bump has zero position/rotation/curvature velocity at both native endpoints.
        const double ENVELOPE = endpoint_envelope(POSITION);
        const double ASPECT = positive(aspect, 1);
        const double RADIUS = options.planet_size;
        auto center = mix(journey.source_center, journey.destination_center, TRAVEL);
        if (SOLAR && TRAVEL > 0 && TRAVEL < 1) {
            const auto &path = journey.solar_path;
            const double ANGLE = path.from_angle + path.angle_delta * TRAVEL;
            const double RADIAL = std::lerp(path.from_radius, path.to_radius, TRAVEL);
            center.x =
                RADIAL * std::cos(ANGLE) * std::lerp(path.from_aspect, path.to_aspect, TRAVEL);
            center.y = RADIAL * std::sin(ANGLE) * (1 + options.solar_vertical_spread);
        }
        const Vec3 CENTER = center;
        const Vec3 DELTA = add(journey.destination_center, scale(journey.source_center, -1));
        const double REVEAL = smooth(unit((POSITION - .09) / .23));
        // Evaluate from the landing endpoint rather than subtracting a nearly-one
        // polynomial: tiny cancellation errors become visible on very long world legs.
        const double APPROACH = smooth(unit((1 - POSITION - .09) / .23));
        // Hold flight altitude while crossing the direct leg (.32-.68), so the outgoing
        // sphere recedes while the incoming sphere grows. Only then descend onto the endpoint.
        const double FIT = std::max(journey.neighborhood_width / ASPECT,
            FOCAL * .65 *
                std::max({std::abs(DELTA.x) / ASPECT, std::abs(DELTA.y), std::abs(DELTA.z)}));
        // Cruise clearance is local and translation-invariant, never proportional
        // to an absolute orbital radius or a distant workspace number.
        const double LOCAL_ALTITUDE =
            ((6 * RADIUS) + (2 * std::abs(options.elevation)) + (3 * spacing(options))) *
            std::max(1.0, 1 / ASPECT);
        const double PULLBACK = REVEAL * APPROACH * (SOLAR ? LOCAL_ALTITUDE : (2 * RADIUS) + FIT);
        // A bounded sideways orbital arc makes small solar hops genuinely spatial,
        // while preserving exact native endpoint projection and reversible evaluation.
        const Vec3 LATERAL = normalized({.x = -DELTA.y, .y = DELTA.x, .z = 0});
        const double ARC = SOLAR ? spacing(options) * .65 * REVEAL * APPROACH : 0;
        const Vec3 CAMERA{
            .x = LATERAL.x * ARC, .y = LATERAL.y * ARC, .z = RADIUS + FOCAL + PULLBACK};
        // The near-camera phase reserves real time for bending/unbending, while
        // the coverage floor prevents any small distant body becoming a plate.
        // Both motions overlap and both endpoint ages use the same quintic law.
        const auto CURVATURE = [&](Vec3 anchor, double age) {
            const Vec3 RELATIVE = add(anchor, scale(CENTER, -1));
            const double DISTANCE = length(add(CAMERA, scale(RELATIVE, -1)));
            const double COVERAGE = FOCAL / std::max(.001, DISTANCE - RADIUS);
            return std::max(smooth(unit((1 - COVERAGE) / .18)), smooth(unit(age / .15)));
        };
        const double SOURCE_CURVATURE = CURVATURE(journey.source_center, POSITION);
        const double DESTINATION_CURVATURE = CURVATURE(journey.destination_center, 1 - POSITION);
        const double AIM = .30 + (.70 * smooth(unit((POSITION - .32) / .36)));
        const Vec3 REMAINING = add(journey.destination_center, scale(CENTER, -1));
        const double LEAD = SOLAR ? std::min(1.0,
                                        .4 * spacing(options) * std::min(1.0, ASPECT) /
                                            std::max(length(REMAINING), .001))
                                  : 1;
        const Vec3 TARGET = SOLAR ? scale(REMAINING, AIM * REVEAL * LEAD)
                                  : scale(DELTA, AIM * (1 - TRAVEL) * REVEAL);
        const double SIGN = direction_between(journey.from.numbered,
                                journey.to.numbered,
                                journey.from.special,
                                journey.to.special) == Direction::Forward
                                ? 1.0
                                : -1.0;
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
        const auto SPHERE = project_surface({.u = coord_u,
            .v = coord_v,
            .aspect = aspect,
            .radius = radius,
            .coverage = coverage,
            .curvature = 1,
            .rotation = {1, 0, 0, 0, 1, 0, 0, 0, 1}});
        return {.flat_position = {static_cast<float>(((2 * coord_u) - 1) * aspect),
                    static_cast<float>(1 - (2 * coord_v)),
                    static_cast<float>(radius)},
            .sphere_position = {static_cast<float>(SPHERE.position.x),
                static_cast<float>(SPHERE.position.y),
                static_cast<float>(SPHERE.position.z)},
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
        const double NATIVE_X = (2 * COORD_U) - 1;
        const double NATIVE_Y = 1 - (2 * COORD_V);
        // Monotone cubic redistribution preserves coverage and the desktop's
        // centre aspect without introducing an interior material boundary.
        const double CX = COVERAGE * std::min(1.0, ASPECT);
        const double CY = COVERAGE * std::min(1.0, 1 / ASPECT);
        const double X = NATIVE_X * (CX + (1 - CX) * NATIVE_X * NATIVE_X);
        const double Y = NATIVE_Y * (CY + (1 - CY) * NATIVE_Y * NATIVE_Y);
        const double ROOT_X = std::sqrt(std::max(0.0, 1 - (.5 * Y * Y)));
        const double ROOT_Y = std::sqrt(std::max(0.0, 1 - (.5 * X * X)));
        const double Z = std::sqrt(std::max(0.0, (1 - X * X) * (1 - Y * Y)));
        const Vec3 SPHERE{.x = RADIUS * X * ROOT_X, .y = RADIUS * Y * ROOT_Y, .z = RADIUS * Z};
        const Vec3 SURFACE{.x = ((1 - CURVATURE) * ASPECT * NATIVE_X) + CURVATURE * SPHERE.x,
            .y = ((1 - CURVATURE) * NATIVE_Y) + CURVATURE * SPHERE.y,
            .z = RADIUS * (1 - CURVATURE + CURVATURE * Z)};
        const double DX = CX + (3 * (1 - CX) * NATIVE_X * NATIVE_X);
        const double DY = CY + (3 * (1 - CY) * NATIVE_Y * NATIVE_Y);
        const double XX = ((1 - CURVATURE) * ASPECT) + (CURVATURE * RADIUS * DX * ROOT_X);
        const double XY = -CURVATURE * RADIUS * X * Y * DY / (2 * ROOT_X);
        const double YX = -CURVATURE * RADIUS * Y * X * DX / (2 * ROOT_Y);
        const double YY = (1 - CURVATURE) + (CURVATURE * RADIUS * DY * ROOT_Y);
        const double ZX = -CURVATURE * RADIUS * X * (1 - Y * Y) * DX;
        const double ZY = -CURVATURE * RADIUS * Y * (1 - X * X) * DY;
        const Vec3 CROSSED{.x = (YX * ZY) - (ZX * YY),
            .y = (ZX * XY) - (XX * ZY),
            .z = ((XX * YY) - (YX * XY)) * Z};
        const Vec3 NORMAL =
            CURVATURE == 0 ? Vec3{.z = 1}
                           : (length(CROSSED) > 1e-6 ? normalized(CROSSED) : normalized(SPHERE));
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
}  // namespace hyprimate::workspace::planet_hop
