#include "PlanetScene.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "workspace/WorkspaceDirection.hpp"

namespace hyprimate::workspace::planet_hop {
    namespace {
        constexpr double PI = std::numbers::pi;
        constexpr double FOCAL = 2.414213562373095; // 45 degree vertical field of view.

        double finite(double value, double fallback) { return std::isfinite(value) ? value : fallback; }
        double unit(double value) { return std::clamp(finite(value, 0), 0.0, 1.0); }
        double positive(double value, double fallback) { return std::max(0.001, finite(value, fallback)); }
        double smooth(double value) { return value * value * value * (value * (value * 6 - 15) + 10); }
        Vec3 add(Vec3 first, Vec3 second) { return {first.x + second.x, first.y + second.y, first.z + second.z}; }
        Vec3 scale(Vec3 value, double factor) { return {value.x * factor, value.y * factor, value.z * factor}; }
        Vec3 mix(Vec3 first, Vec3 second, double progress) { return add(scale(first, 1 - progress), scale(second, progress)); }
        double length(Vec3 value) { return std::hypot(value.x, value.y, value.z); }
        Vec3 normalized(Vec3 value) {
            const double LENGTH = length(value);
            return LENGTH > 1e-12 ? scale(value, 1 / LENGTH) : Vec3{0, 0, 1};
        }
        double dot(Vec3 first, Vec3 second) {
            return first.x * second.x + first.y * second.y + first.z * second.z;
        }
        Vec3 cross(Vec3 first, Vec3 second) {
            return {first.y * second.z - first.z * second.y,
                first.z * second.x - first.x * second.z, first.x * second.y - first.y * second.x};
        }
        double spacing(const SceneOptions &options) {
            return std::max(options.planetSeparation * options.planetSize,
                2 * (options.planetSize + std::abs(options.elevation)));
        }
        Vec3 hex_position(std::uint64_t placement, double separation) {
            // A durable slot, never a compositor lifetime ID, selects one disjoint hex cell.
            const std::uint64_t INDEX = placement;
            if (INDEX == 0) {
                return {};
            }
            auto ring = static_cast<std::uint64_t>(std::ceil((std::sqrt(12.L * INDEX + 9) - 3) / 6));
            const auto last = [](std::uint64_t value) {
                return static_cast<__uint128_t>(3) * value * (value + 1);
            };
            // Correct possible sqrt rounding at an exact ring boundary with integer arithmetic.
            if (last(ring) < INDEX) {
                ++ring;
            } else if (ring > 1 && last(ring - 1) >= INDEX) {
                --ring;
            }
            const auto START = static_cast<std::uint64_t>(last(ring - 1) + 1);
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
            return {separation * (static_cast<double>(axial_x) + static_cast<double>(axial_y) * .5),
                separation * std::sqrt(3.) * .5 * static_cast<double>(axial_y), 0};
        }
        double variation(std::uint64_t value) {
            // SplitMix64: durable slot bits, never population or compositor lifetime IDs.
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
            value ^= value >> 31;
            return static_cast<double>(value >> 11) * 0x1p-52 - 1;
        }
        Vec3 solar_position(const JourneyEndpoint &endpoint, const SceneOptions &options, double aspect) {
            // X cell boundaries guarantee separation regardless of local Y/Z variation.
            // Number gaps are genuine empty space, not compressed inventory ranks.
            const double CELL = spacing(options) * 1.25;
            const double WIDTH = std::max(1.0, positive(aspect, 1));
            if (endpoint.numbered && !endpoint.special) {
                const auto NUMBER = *endpoint.numbered;
                return {CELL * WIDTH * (NUMBER + .1 * variation(static_cast<std::uint64_t>(NUMBER) + 0x200000000ULL)),
                    CELL * options.solarVerticalSpread * variation(NUMBER),
                    CELL * .35 * variation(static_cast<std::uint64_t>(NUMBER) + 0x100000000ULL)};
            }
            // Named/special slots occupy disjoint XY cells beside the numbered corridor.
            // Keep depth bounded so cruise clearance does not depend on population. Y
            // spread stretches cells rather than collapsing them when configured to zero.
            const auto SLOT = endpoint.placement / 2;
            std::array<std::uint64_t, 2> cell{};
            for (unsigned bit = 0; bit < 64; ++bit) {
                cell[bit % 2] |= ((SLOT >> bit) & 1ULL) << (bit / 2);
            }
            return {-CELL * WIDTH * (1 + static_cast<double>(cell[0]) + .1 * variation(SLOT)),
                CELL * (static_cast<double>(cell[1]) * (1 + options.solarVerticalSpread) +
                    .1 * options.solarVerticalSpread * variation(SLOT + 1)),
                CELL * .35 * variation(SLOT + 2)};
        }
        std::array<float, 9> rotation(double angle) {
            const auto COSINE = static_cast<float>(std::cos(angle));
            const auto SINE = static_cast<float>(std::sin(angle));
            return {COSINE, 0, -SINE, 0, 1, 0, SINE, 0, COSINE};
        }
        double sinc(double angle) {
            if (std::abs(angle) < .01) {
                const double SQUARED = angle * angle;
                return 1 - SQUARED / 6 + SQUARED * SQUARED / 120;
            }
            return std::sin(angle) / angle;
        }
        SceneOptions sanitized(SceneOptions options) {
            options.planetSize = positive(options.planetSize, 1);
            options.planetSeparation = positive(options.planetSeparation, 4);
            options.planetCoverage = std::clamp(finite(options.planetCoverage, .7), .001, 1.0);
            options.rotationLimit = std::clamp(finite(options.rotationLimit, .35), 0.0, PI);
            options.elevation = finite(options.elevation, .2);
            options.solarVerticalSpread = std::clamp(finite(options.solarVerticalSpread, .65), 0.0, 4.0);
            return options;
        }
    }

    std::uint32_t logical_distance(const JourneyEndpoint &from, const JourneyEndpoint &to) {
        if (from.identity == to.identity) {
            return 0;
        }
        if (!from.numbered || !to.numbered || from.special || to.special) {
            return 1;
        }
        return std::max<std::uint32_t>(1, from.logicalOrder > to.logicalOrder ?
            from.logicalOrder - to.logicalOrder : to.logicalOrder - from.logicalOrder);
    }

    double duration_seconds(const JourneyEndpoint &from, const JourneyEndpoint &to, const SceneOptions &options) {
        const double BASE = positive(options.duration, .8);
        const double STEPS = std::max<std::uint32_t>(logical_distance(from, to), 1);
        const double SCALE = std::max(0.0, finite(options.distanceScale, .25));
        // A configured maximum may cap skip growth, never shorten the base transition.
        const double MAXIMUM = std::max(BASE, finite(options.maxDuration, 2.2));
        switch (options.skipDurationMode) {
            case DurationMode::Constant: return BASE;
            case DurationMode::DistanceScaled: return std::min(BASE * (1 + SCALE * (STEPS - 1)), MAXIMUM);
            case DurationMode::Adaptive: {
                const double EXCESS = std::max(0.0, STEPS - std::max<std::uint32_t>(1, options.adaptiveBoundary));
                return std::min(BASE * (1 + SCALE * std::log2(1 + EXCESS)), MAXIMUM);
            }
        }
        return BASE;
    }

    Vec3 layout_position(const JourneyEndpoint &endpoint, const SceneOptions &raw_options, double aspect) {
        const auto OPTIONS = sanitized(raw_options);
        const double SPACING = spacing(OPTIONS);
        if (OPTIONS.planetLayout == Layout::SolarSystem) {
            return solar_position(endpoint, OPTIONS, aspect);
        }
        if (endpoint.numbered && !endpoint.special) {
            return {SPACING * endpoint.logicalOrder, 0, 0};
        }
        return hex_position(endpoint.placement, SPACING);
    }

    Journey make_journey(JourneyEndpoint from, JourneyEndpoint to, SceneOptions options, double aspect) {
        options = sanitized(options);
        aspect = positive(aspect, 1);
        Journey result{from, to, options, layout_position(from, options, aspect), layout_position(to, options, aspect), aspect};
        const bool NUMBERED = from.numbered && to.numbered && !from.special && !to.special;
        if (options.planetLayout == Layout::Dynamic || (options.planetLayout == Layout::Linear && !NUMBERED)) {
            // Named/special fallback follows the same navigation semantics as WorkspaceTurn.
            const double SIGN = direction_between(from.numbered, to.numbered, from.special, to.special) ==
                Direction::Forward ? 1.0 : -1.0;
            result.sourceCenter = {};
            result.destinationCenter = from.identity == to.identity ? Vec3{} : Vec3{SIGN * spacing(options), 0, 0};
        }
        if (from.identity == to.identity) {
            result.destinationCenter = result.sourceCenter;
        }
        return result;
    }

    Frame evaluate(const Journey &journey, double progress, double aspect) {
        const double POSITION = unit(progress);
        const auto &OPTIONS = journey.options;
        const bool SOLAR = OPTIONS.planetLayout == Layout::SolarSystem;
        // Solar travel waits until full clearance, then reaches the destination before
        // descent. This remains collision-safe even when sparse number gaps are enormous.
        const double TRAVEL = SOLAR ? (POSITION >= .68 ? 1 : smooth(unit((POSITION - .32) / .36))) :
            (POSITION >= .90 ? 1 : smooth(unit((POSITION - .10) / .80)));
        // Polynomial bump has zero position/rotation/curvature velocity at both native endpoints.
        const double ENVELOPE = 16 * POSITION * POSITION * (1 - POSITION) * (1 - POSITION);
        const double ASPECT = positive(aspect, 1);
        const double RADIUS = OPTIONS.planetSize;
        const Vec3 CENTER = mix(journey.sourceCenter, journey.destinationCenter, TRAVEL);
        const Vec3 DELTA = add(journey.destinationCenter, scale(journey.sourceCenter, -1));
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
        const double LOCAL_ALTITUDE = 18 * RADIUS + 2 * std::abs(OPTIONS.elevation) + .7 * spacing(OPTIONS) * 1.25;
        const double PULLBACK = REVEAL * APPROACH * (SOLAR ? LOCAL_ALTITUDE : 2 * RADIUS + FIT);
        const Vec3 CAMERA{0, 0, RADIUS + FOCAL + PULLBACK};
        const double AIM = .30 + .70 * smooth(unit((POSITION - .32) / .36));
        const Vec3 REMAINING = scale(DELTA, 1 - TRAVEL);
        const double LEAD = SOLAR ? std::min(1.0,
            .4 * spacing(OPTIONS) * std::min(1.0, ASPECT) / std::max(length(REMAINING), .001)) : 1;
        const Vec3 TARGET = SOLAR ? scale(REMAINING, AIM * REVEAL * LEAD) :
            scale(DELTA, AIM * (1 - TRAVEL) * REVEAL);
        const double SIGN = direction_between(journey.from.numbered, journey.to.numbered,
            journey.from.special, journey.to.special) == Direction::Forward ? 1.0 : -1.0;
        const double ANGLE = OPTIONS.rotationEnabled ? OPTIONS.rotationLimit * ENVELOPE * SIGN : 0;
        const double NEAR = .01 + PULLBACK * .01;
        const double FAR = std::max(100.0, 4 * (RADIUS + PULLBACK + FOCAL));
        const auto SOURCE_ROTATION = rotation(ANGLE);
        auto destination_rotation = SOURCE_ROTATION;
        destination_rotation[2] = -destination_rotation[2];
        destination_rotation[6] = -destination_rotation[6];
        const double DEPTH_A = -(FAR + NEAR) / (FAR - NEAR);
        const double DEPTH_B = -2 * FAR * NEAR / (FAR - NEAR);
        const auto scalar = [](double value) { return static_cast<float>(value); };
        const Vec3 BACK = normalized(add(CAMERA, scale(TARGET, -1)));
        const Vec3 RIGHT = normalized(cross({0, 1, 0}, BACK));
        const Vec3 UP = cross(BACK, RIGHT);
        const double TX = -dot(RIGHT, CAMERA);
        const double TY = -dot(UP, CAMERA);
        const double TZ = -dot(BACK, CAMERA);
        // Perspective * look-at; only the aim leads, never the direct translation.
        const std::array<float, 16> VIEW_PROJECTION{
            scalar(FOCAL / ASPECT * RIGHT.x), scalar(FOCAL * UP.x), scalar(DEPTH_A * BACK.x), scalar(-BACK.x),
            scalar(FOCAL / ASPECT * RIGHT.y), scalar(FOCAL * UP.y), scalar(DEPTH_A * BACK.y), scalar(-BACK.y),
            scalar(FOCAL / ASPECT * RIGHT.z), scalar(FOCAL * UP.z), scalar(DEPTH_A * BACK.z), scalar(-BACK.z),
            scalar(FOCAL / ASPECT * TX), scalar(FOCAL * TY), scalar(DEPTH_A * TZ + DEPTH_B), scalar(-TZ)};
        // camera-to-world * inverse(perspective), analytic even while the aim changes.
        const std::array<float, 16> INVERSE{
            scalar(ASPECT / FOCAL * RIGHT.x), scalar(ASPECT / FOCAL * RIGHT.y), scalar(ASPECT / FOCAL * RIGHT.z), 0,
            scalar(UP.x / FOCAL), scalar(UP.y / FOCAL), scalar(UP.z / FOCAL), 0,
            scalar(CAMERA.x / DEPTH_B), scalar(CAMERA.y / DEPTH_B), scalar(CAMERA.z / DEPTH_B), scalar(1 / DEPTH_B),
            scalar(-BACK.x + CAMERA.x * DEPTH_A / DEPTH_B), scalar(-BACK.y + CAMERA.y * DEPTH_A / DEPTH_B),
            scalar(-BACK.z + CAMERA.z * DEPTH_A / DEPTH_B), scalar(DEPTH_A / DEPTH_B)};
        return {.worldOrigin = CENTER, .cameraPosition = CAMERA, .cameraTarget = TARGET,
            .sourceCenter = add(journey.sourceCenter, scale(CENTER, -1)),
            .destinationCenter = add(journey.destinationCenter, scale(CENTER, -1)),
            .viewProjection = VIEW_PROJECTION, .inverseViewProjection = INVERSE,
            .sourceRotation = SOURCE_ROTATION, .destinationRotation = destination_rotation,
            .sourceCurvature = SOURCE_CURVATURE, .destinationCurvature = DESTINATION_CURVATURE,
            .endpointEnvelope = ENVELOPE, .travelProgress = TRAVEL,
            .radius = RADIUS, .coverage = OPTIONS.planetCoverage, .aspect = ASPECT};
    }

    SurfaceVertex make_surface_vertex(double u, double v, double aspect, double radius, double coverage) {
        u = unit(u);
        v = unit(v);
        aspect = positive(aspect, 1);
        radius = positive(radius, 1);
        coverage = std::clamp(finite(coverage, .7), .001, 1.0);
        // Limit both chart angles to a sphere while preserving the desktop's center aspect.
        const double LONGITUDE = (2 * u - 1) * PI * coverage * std::min(1.0, aspect / 2);
        const double LATITUDE = (.5 - v) * PI * coverage * std::min(1.0, 2 / aspect);
        return {{static_cast<float>((2 * u - 1) * aspect), static_cast<float>(1 - 2 * v), static_cast<float>(radius)},
            {static_cast<float>(radius * std::cos(LATITUDE) * std::sin(LONGITUDE)),
                static_cast<float>(radius * std::sin(LATITUDE)), static_cast<float>(radius * std::cos(LATITUDE) * std::cos(LONGITUDE))},
            {static_cast<float>(u), static_cast<float>(v)}};
    }

    SurfacePoint project_surface(double u, double v, double aspect, double radius, double coverage,
        double curvature, const std::array<float, 9> &rotation_matrix, Vec3 center, double elevation) {
        u = unit(u);
        v = unit(v);
        aspect = positive(aspect, 1);
        radius = positive(radius, 1);
        coverage = std::clamp(finite(coverage, .7), .001, 1.0);
        const double CURVATURE = unit(curvature);
        const double LON_LIMIT = PI * coverage * std::min(1.0, aspect / 2);
        const double LAT_LIMIT = PI * .5 * coverage * std::min(1.0, 2 / aspect);
        const double LONGITUDE = (2 * u - 1) * LON_LIMIT;
        const double LATITUDE = (1 - 2 * v) * LAT_LIMIT;
        const double BENT_LON = CURVATURE * LONGITUDE;
        const double BENT_LAT = CURVATURE * LATITUDE;
        const double COS_LON = std::cos(BENT_LON);
        const double COS_LAT = std::cos(BENT_LAT);
        const double SINC_HALF_LON = sinc(BENT_LON / 2);
        const double SINC_HALF_LAT = sinc(BENT_LAT / 2);
        const double DEPTH_LON = LONGITUDE * LONGITUDE * SINC_HALF_LON * SINC_HALF_LON;
        const double DEPTH_LAT = LATITUDE * LATITUDE * SINC_HALF_LAT * SINC_HALF_LAT;
        // Stable (1-cos(kL)cos(kB))/k, including k=0 without division or cancellation.
        const double DEPTH = CURVATURE * (.5 * DEPTH_LON + .5 * DEPTH_LAT -
            .25 * CURVATURE * CURVATURE * DEPTH_LON * DEPTH_LAT);
        // Factor the flat coordinates so k=0 preserves the exact native arithmetic.
        const double SCALE_X = (1 - CURVATURE) + CURVATURE * radius * LON_LIMIT / aspect;
        const double SCALE_Y = (1 - CURVATURE) + CURVATURE * radius * LAT_LIMIT;
        const Vec3 SURFACE{(2 * u - 1) * aspect * COS_LAT * sinc(BENT_LON) * SCALE_X,
            (1 - 2 * v) * sinc(BENT_LAT) * SCALE_Y, radius - radius * DEPTH};
        const double AXIS_X = (1 - CURVATURE) * aspect / LON_LIMIT + CURVATURE * radius;
        const double AXIS_Y = (1 - CURVATURE) / LAT_LIMIT + CURVATURE * radius;
        const Vec3 NORMAL = normalized({radius / AXIS_X * std::sin(BENT_LON) * COS_LAT,
            radius / AXIS_Y * std::sin(BENT_LAT), COS_LON * COS_LAT});
        // The material's orientation is a Y rotation; bend and orientation share one amount.
        const double ANGLE = std::atan2(rotation_matrix[6], rotation_matrix[0]) * CURVATURE;
        const double COSINE = std::cos(ANGLE);
        const double SINE = std::sin(ANGLE);
        const auto ROTATE = [COSINE, SINE](Vec3 point) {
            return Vec3{COSINE * point.x + SINE * point.z, point.y, -SINE * point.x + COSINE * point.z};
        };
        const Vec3 WORLD_NORMAL = ROTATE(NORMAL);
        return {add(center, add(ROTATE(SURFACE), scale(WORLD_NORMAL, finite(elevation, 0) * CURVATURE))), WORLD_NORMAL};
    }
}
