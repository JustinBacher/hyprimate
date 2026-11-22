#include <algorithm>
#include <cassert>
#include <cmath>

#include "PaperInteraction.hpp"

namespace hyprimate {
    namespace {
        constexpr double PI = 3.14159265358979323846;
        double unit(double value) {
            return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0;
        }
        double bounded(double value) {
            return std::isfinite(value) ? std::clamp(value, -6.0, 6.0) : 0;
        }
        double compressed(double value) {
            return value / (1 + std::abs(value));
        }
        double frequency(double value) {
            return std::isfinite(value) ? std::clamp(value, 0.1, 4.0) : 1.2;
        }
        double square(double value) {
            return value * value;
        }
        double local(double distance, double width) {
            return square(std::max(0.0, 1 - distance / width));
        }
        double displacement(double value) {
            // Suppress final native-coordinate rounding, not slow sustained
            // movement: 30 px/s at 240 Hz is outside this dead band.
            return value * smoothstep(0.02, 0.10, std::abs(value));
        }
        double common_motion(double first, double second, bool resizing) {
            if (!resizing) return (first + second) * 0.5;
            if (first * second <= 0) return 0;
            return std::abs(first) < std::abs(second) ? first : second;
        }
        double constrain(PaperPoint &first,
            PaperPoint &second,
            double length,
            bool first_fixed,
            bool second_fixed) {
            if (first_fixed && second_fixed) return 0;
            const Vec3 delta{second.position.x - first.position.x,
                second.position.y - first.position.y,
                second.position.z - first.position.z};
            const double actual = std::sqrt(square(delta.x) + square(delta.y) + square(delta.z));
            if (actual <= 1e-12) return 0;
            const double error = std::abs(actual / length - 1);
            if (error < 0.0001) return error;
            const double correction = (1 - length / actual) / (first_fixed || second_fixed ? 1 : 2);
            if (!first_fixed) {
                first.position.x += delta.x * correction;
                first.position.y += delta.y * correction;
                first.position.z += delta.z * correction;
                first.position.z = std::max(0.0, first.position.z);
            }
            if (!second_fixed) {
                second.position.x -= delta.x * correction;
                second.position.y -= delta.y * correction;
                second.position.z -= delta.z * correction;
                second.position.z = std::max(0.0, second.position.z);
            }
            return error;
        }
        template <std::size_t Count>
        void integrate(const std::array<double, Count> &angles,
            std::array<Vec2, Count + 1> &curve,
            double span) {
            constexpr std::size_t centre = Count / 2;
            const double segment = span / Count;
            curve[centre] = {};
            for (std::size_t index = centre; index < Count; ++index)
                curve[index + 1] = {curve[index].x + segment * std::cos(angles[index]),
                    curve[index].y + segment * std::sin(angles[index])};
            for (std::size_t index = centre; index > 0; --index)
                curve[index - 1] = {curve[index].x - segment * std::cos(angles[index - 1]),
                    curve[index].y - segment * std::sin(angles[index - 1])};
        }
        template <std::size_t Count>
        void resize_angles(std::array<double, Count> &angles,
            double grow_left,
            double grow_right,
            double shrink_left,
            double shrink_right,
            double density,
            double segment,
            double thickness) {
            constexpr unsigned half = Count / 2;
            const unsigned cycles = 1 + static_cast<unsigned>(std::round(2 * density));
            const unsigned pleat_cells = cycles * 4;
            for (unsigned side = 0; side < 2; ++side) {
                const double grow = side == 0 ? grow_left : grow_right;
                const double shrink = side == 0 ? shrink_left : shrink_right;
                const double curl = 1.15 * grow / (0.25 + grow);
                const double peak = 2.8 * shrink / (0.12 + shrink);
                // Returning layers stop above the occluded material, not on it.
                const double descent = std::asin(std::max(0.0,
                    std::sin(peak) - std::min(std::sin(peak) * 0.15, thickness / (2 * segment))));
                for (unsigned outward = 0; outward < half; ++outward) {
                    const unsigned index = side == 0 ? half - 1 - outward : half + outward;
                    const unsigned distance = half - 1 - outward;
                    double angle = 0;
                    if (distance < 6)
                        angle += curl * square((6.0 - distance - 0.5) / 6) / (1 + 100 * shrink);
                    if (distance < pleat_cells) {
                        const unsigned phase = (pleat_cells - 1 - distance) % 4;
                        angle += phase < 2 ? peak : -descent;
                    }
                    angles[index] = (side == 0 ? -1 : 1) * std::clamp(angle, -2.8, 2.8);
                }
            }
        }
    }  // namespace

    void PaperInteraction::Response::advance(double target, double rate, double seconds) {
        // Exact two cascaded first-order responses. A released load approaches
        // zero without the overshoot of a spring or frame-dependent Euler steps.
        const double decay = std::exp(-rate * seconds);
        value = target + (value - target + (drive - target) * rate * seconds) * decay;
        drive = target + (drive - target) * decay;
        if (target == 0 && std::max(std::abs(drive), std::abs(value)) < 1e-6) drive = value = 0;
    }

    void PaperInteraction::mesh(PaperSurface &surface) {
        surface.interaction_mesh = true;
        surface.points.resize(COUNT);
        surface.faces.resize(CELLS * CELLS * 2);
        for (unsigned y = 0; y < SIDE; ++y)
            for (unsigned x = 0; x < SIDE; ++x)
                surface.points[y * SIDE + x].uv = {
                    static_cast<double>(x) / CELLS, static_cast<double>(y) / CELLS};
        for (unsigned y = 0; y < CELLS; ++y)
            for (unsigned x = 0; x < CELLS; ++x) {
                const auto a = y * SIDE + x, b = a + 1, c = a + SIDE, d = c + 1;
                const auto face = (y * CELLS + x) * 2;
                surface.faces[face] = {{a, b, d}, {}};
                surface.faces[face + 1] = {{a, d, c}, {}};
            }
    }

    bool PaperInteraction::advance(
        PaperSurface &surface, const PaperInteractionParams &params, const animation::Step &step) {
        const auto &motion = step.motion;
        Box current = is_usable(step.body) ? step.body : step.capture;
        if (motion) {
            if (is_usable(motion->body))
                current = motion->body;
            else if (is_usable(motion->capture))
                current = motion->capture;
        }
        if (!is_usable(current)) return false;
        if (!m_initialized || surface.points.size() != COUNT ||
            surface.faces.size() != CELLS * CELLS * 2) {
            mesh(surface);
            m_initialized = true;
        }
        const Box capture = is_usable(step.capture) ? step.capture : current;
        surface.size = {capture.w, capture.h};
        double measured =
            motion && std::isfinite(motion->sample_seconds) && motion->sample_seconds > 0
                ? motion->sample_seconds
                : step.elapsed;
        if (!std::isfinite(measured) || measured < 0) measured = 0;
        double seconds = std::isfinite(step.elapsed) && step.elapsed > 0 ? step.elapsed : measured;
        // An idle desktop may have a long first sample. Keep its conservative
        // measured velocity; bound only the integration interval. A true active
        // stall releases loads and dissipates continuously, never hard-resets.
        seconds = std::clamp(seconds, 0.0, 0.05);
        Vec2 velocity{}, acceleration{};
        std::array<double, 4> outward{};
        if (motion && !step.stalled && measured > 0) {
            const Box previous =
                is_usable(motion->from_body) ? motion->from_body : motion->from_capture;
            if (is_usable(previous)) {
                const double scale = std::sqrt(current.w) * std::sqrt(current.h);
                const double left = displacement(current.x - previous.x) / (scale * measured);
                const double right = displacement(current.x + current.w - previous.x - previous.w) /
                                     (scale * measured);
                const double top = displacement(current.y - previous.y) / (scale * measured);
                const double bottom =
                    displacement(current.y + current.h - previous.y - previous.h) /
                    (scale * measured);
                const double common_x = common_motion(left, right, current.w != previous.w);
                const double common_y = common_motion(top, bottom, current.h != previous.h);
                velocity = {bounded(common_x), bounded(common_y)};
                acceleration = {bounded((velocity.x - m_previous_velocity.x) * 0.025 / measured),
                    bounded((velocity.y - m_previous_velocity.y) * 0.025 / measured)};
                outward = {bounded(common_x - left),
                    bounded(right - common_x),
                    bounded(common_y - top),
                    bounded(bottom - common_y)};
            }
        }
        if (seconds > 0) m_previous_velocity = velocity;
        const double resize_rate =
            (12 + 24 * unit(params.settle_damping)) / (0.7 + 0.9 * unit(params.inertia));
        const bool moving = std::hypot(velocity.x, velocity.y) > 0;
        double move_rate = resize_rate;
        if (params.style == PaperMoveStyle::Glide)
            move_rate *= 0.55 + 1.5 * unit(params.glide.friction);
        double activity = 0;
        const auto respond = [&](Response &response, double target, double rate) {
            response.advance(target, rate, seconds);
            activity = std::max({activity, std::abs(response.drive), std::abs(response.value)});
        };
        for (unsigned edge = 0; edge < 4; ++edge) {
            respond(m_grow[edge], std::max(0.0, outward[edge]), resize_rate);
            respond(m_shrink[edge], std::max(0.0, -outward[edge]), resize_rate);
        }
        respond(m_velocity[0], velocity.x, move_rate);
        respond(m_velocity[1], velocity.y, move_rate);
        respond(m_acceleration[0], acceleration.x, move_rate);
        respond(m_acceleration[1], acceleration.y, move_rate);
        // Free decay does not keep exciting a periodic mode after release.
        if (moving && params.style == PaperMoveStyle::Flutter)
            m_phase =
                std::fmod(m_phase + seconds * 2 * PI * frequency(params.flutter.frequency), 2 * PI);
        const double length = std::sqrt(capture.w) * std::sqrt(capture.h);
        deform(surface, params, {capture.w / length, capture.h / length});
        return activity > 0;
    }

    PaperInteraction::Movement PaperInteraction::flutter(
        const PaperInteractionParams &params) const {
        Movement result;
        const double coupling =
            unit(params.flutter.air_resistance) * (1 - 0.6 * unit(params.stiffness));
        const double intensity = coupling * unit(params.flutter.intensity);
        const std::array<double, 2> velocity{
            compressed(m_velocity[0].value), compressed(m_velocity[1].value)};
        const std::array<double, 2> acceleration{
            compressed(m_acceleration[0].value), compressed(m_acceleration[1].value)};
        const double spacing = frequency(params.flutter.frequency);
        for (unsigned axis = 0; axis < 2; ++axis) {
            auto &angles = axis == 0 ? result.horizontal : result.vertical;
            const double speed = std::abs(velocity[axis]);
            const double direction = velocity[axis] < 0 ? -1 : 1;
            for (unsigned index = 0; index < CELLS; ++index) {
                const double coordinate = (index + 0.5) / CELLS;
                const double downstream = direction > 0 ? 1 - coordinate : coordinate;
                const double leading = local(downstream, 0.24);
                const double trailing = local(1 - downstream, 0.55);
                // Phase travels from the leading grip toward the trailing edge;
                // velocity chooses its direction, acceleration adds the impulse.
                const double ripple = std::sin(m_phase - downstream * PI * (1 + spacing));
                angles[index] = intensity * direction *
                                (0.25 * speed * leading + 0.36 * speed * trailing * ripple +
                                    0.12 * acceleration[axis] * direction * leading);
            }
        }
        const double forcing = std::hypot(velocity[0], velocity[1]);
        result.lift = 0.10 * coupling * unit(params.flutter.lift) * forcing;
        result.torsion = 0.14 * intensity * unit(params.flutter.torsion) *
                         (acceleration[0] - acceleration[1] + velocity[0] * acceleration[1] -
                             velocity[1] * acceleration[0]);
        return result;
    }

    PaperInteraction::Movement PaperInteraction::glide(const PaperInteractionParams &params) const {
        Movement result;
        result.shadow_separation = unit(params.glide.shadow_separation);
        const double flexibility = 1 - 0.6 * unit(params.stiffness);
        for (unsigned axis = 0; axis < 2; ++axis) {
            auto &angles = axis == 0 ? result.horizontal : result.vertical;
            const double velocity = compressed(m_velocity[axis].value);
            const double acceleration = compressed(m_acceleration[axis].value);
            const double direction = velocity < 0 ? -1 : 1;
            for (unsigned index = 0; index < CELLS; ++index) {
                const double coordinate = (index + 0.5) / CELLS;
                const double leading_distance = direction > 0 ? 1 - coordinate : coordinate;
                angles[index] =
                    flexibility * unit(params.glide.edge_curl) * direction *
                    (0.35 * std::abs(velocity) * local(leading_distance, 0.22) -
                        0.06 * std::abs(acceleration) * local(1 - leading_distance, 0.16));
            }
        }
        const double horizontal_acceleration = compressed(m_acceleration[0].value);
        const double vertical_acceleration = compressed(m_acceleration[1].value);
        result.vertical_corner =
            std::abs(vertical_acceleration) > std::abs(horizontal_acceleration);
        result.corner =
            0.15 * flexibility * unit(params.glide.corner_lift) *
            std::max(std::abs(horizontal_acceleration), std::abs(vertical_acceleration));
        const double across =
            result.vertical_corner ? horizontal_acceleration : vertical_acceleration;
        result.corner_side = across < 0 ? -1 : 1;
        return result;
    }

    void PaperInteraction::deform(
        PaperSurface &surface, const PaperInteractionParams &params, Vec2 material) {
        std::array<double, CELLS> horizontal{}, vertical{};
        std::array<double, 4> grow{}, shrink{};
        const double flexibility = 1 - 0.6 * unit(params.stiffness);
        double resizing = 0;
        for (unsigned edge = 0; edge < 4; ++edge) {
            grow[edge] = m_grow[edge].value * unit(params.resize.curl_intensity) * flexibility;
            shrink[edge] = m_shrink[edge].value * unit(params.resize.fold_intensity) * flexibility;
            resizing = std::max({resizing, grow[edge], shrink[edge]});
        }
        const double dx = material.x / CELLS, dy = material.y / CELLS;
        const double density = unit(params.resize.crease_density);
        resize_angles(
            horizontal, grow[0], grow[1], shrink[0], shrink[1], density, dx, surface.thickness);
        resize_angles(
            vertical, grow[2], grow[3], shrink[2], shrink[3], density, dy, surface.thickness);
        // Two perpendicular cylindrical folds cannot both lift through the
        // same patch without shearing it. Let the stronger crease take the
        // load; for equal loads the paper grain favours the horizontal ribbon.
        // A smooth transfer avoids flipping the preferred crease on reversal.
        // For unit tangents the squared diagonal strain has cross term
        // 2*dx*dy*sin(ax)*sin(ay). Bound it before projecting the live mesh.
        const auto peak_angle = [](const auto &angles) {
            double peak = 0;
            for (const double angle : angles) peak = std::max(peak, std::abs(angle));
            return peak;
        };
        const double ax = peak_angle(horizontal), ay = peak_angle(vertical);
        const double budget = 0.012 * (dx / dy + dy / dx);
        if (ax * ay > budget) {
            const double ratio = budget / (ax * ay);
            const double preference = smoothstep(-0.22, 0.02, (ax - ay) / (ax + ay));
            const double y_scale = std::pow(ratio, preference);
            for (double &angle : horizontal) angle *= ratio / y_scale;
            for (double &angle : vertical) angle *= y_scale;
        }
        std::array<Vec2, SIDE> resize_x{}, resize_y{};
        integrate(horizontal, resize_x, material.x);
        integrate(vertical, resize_y, material.y);
        double resize_height = 0;
        for (unsigned index = 0; index < SIDE; ++index)
            resize_height = std::max({resize_height, resize_x[index].y, resize_y[index].y});
        // Inactive style code is not evaluated. Neither Glide friction nor its
        // shadow setting may alter a resize-only interaction.
        bool movement = false;
        for (unsigned axis = 0; axis < 2; ++axis)
            movement |= m_velocity[axis].value != 0 || m_acceleration[axis].value != 0;
        Movement mode;
        if (movement)
            mode = params.style == PaperMoveStyle::Flutter ? flutter(params) : glide(params);
        double moving_angle = 0;
        for (unsigned index = 0; index < CELLS; ++index) {
            horizontal[index] += mode.horizontal[index];
            vertical[index] += mode.vertical[index];
            moving_angle = std::max(
                {moving_angle, std::abs(mode.horizontal[index]), std::abs(mode.vertical[index])});
        }
        std::array<Vec2, SIDE> curve_x{}, curve_y{};
        integrate(horizontal, curve_x, material.x);
        integrate(vertical, curve_y, material.y);
        double move_floor = 0;
        if (movement)
            for (unsigned index = 0; index < SIDE; ++index)
                move_floor = std::min({move_floor,
                    curve_x[index].y - resize_x[index].y,
                    curve_y[index].y - resize_y[index].y});
        const bool horizontal_twist =
            mode.torsion != 0 || (mode.corner != 0 && !mode.vertical_corner);
        const bool twist = horizontal_twist || mode.corner != 0;
        std::array<bool, COUNT> fixed{};
        for (unsigned row = 0; row < SIDE; ++row) {
            std::array<Vec2, SIDE> row_curve{};
            if (horizontal_twist) {
                auto row_angles = horizontal;
                const double across = 2.0 * row / CELLS - 1;
                const double corner = square(std::max(0.0, across * mode.corner_side));
                for (unsigned column = 0; column < CELLS; ++column) {
                    const double coordinate = (column + 0.5) / CELLS;
                    const double direction = m_velocity[0].value < 0 ? -1 : 1;
                    const double leading = direction > 0 ? 1 - coordinate : coordinate;
                    row_angles[column] +=
                        local(leading, 0.25) *
                        (mode.torsion * across +
                            (mode.vertical_corner ? 0 : direction * mode.corner * corner));
                }
                integrate(row_angles, row_curve, material.x);
            }
            for (unsigned column = 0; column < SIDE; ++column) {
                const auto &horizontal_curve =
                    horizontal_twist ? row_curve[column] : curve_x[column];
                auto &point = surface.points[row * SIDE + column];
                point.position = {horizontal_curve.x,
                    curve_y[row].x,
                    horizontal_curve.y + curve_y[row].y - 2 * move_floor + mode.lift};
            }
        }
        if (mode.vertical_corner && mode.corner != 0) {
            for (unsigned column = 0; column < SIDE; ++column) {
                auto column_angles = vertical;
                const double across = 2.0 * column / CELLS - 1;
                const double corner = square(std::max(0.0, across * mode.corner_side));
                const double direction = m_velocity[1].value < 0 ? -1 : 1;
                for (unsigned row = 0; row < CELLS; ++row) {
                    const double coordinate = (row + 0.5) / CELLS;
                    const double leading = direction > 0 ? 1 - coordinate : coordinate;
                    column_angles[row] += direction * mode.corner * corner * local(leading, 0.25);
                }
                std::array<Vec2, SIDE> column_curve{};
                integrate(column_angles, column_curve, material.y);
                for (unsigned row = 0; row < SIDE; ++row) {
                    auto &point = surface.points[row * SIDE + column];
                    point.position.y = column_curve[row].x;
                    point.position.z += column_curve[row].y - curve_y[row].y;
                }
            }
        }
        // Native support is immutable: a one-edge resize cannot drag its
        // stationary edge or lift the opposite half of the material.
        for (unsigned index = 0; index < COUNT; ++index) {
            const auto &point = surface.points[index];
            fixed[index] = mode.lift == 0 && mode.torsion == 0 &&
                           std::abs(point.position.z) < 1e-12 &&
                           std::abs(point.position.x - (point.uv.x - 0.5) * material.x) < 1e-12 &&
                           std::abs(point.position.y - (point.uv.y - 0.5) * material.y) < 1e-12;
        }
        // The resize ribbons already satisfy the strain bound. Only project
        // additional crossed or twisted movement loads.
        const bool crossing =
            twist || (movement && peak_angle(horizontal) > 0 && peak_angle(vertical) > 0);
        if (crossing) {
            const double diagonal = std::hypot(dx, dy);
            for (unsigned iteration = 0; iteration < 48; ++iteration) {
                double error = 0;
                for (unsigned index = 0; index < COUNT; ++index) {
                    const unsigned current = iteration % 2 == 0 ? index : COUNT - 1 - index;
                    const unsigned column = current % SIDE, row = current / SIDE;
                    const auto project = [&](unsigned other, double length) {
                        error = std::max(error,
                            constrain(surface.points[current],
                                surface.points[other],
                                length,
                                fixed[current],
                                fixed[other]));
                    };
                    if (column < CELLS) project(current + 1, dx);
                    if (row < CELLS) project(current + SIDE, dy);
                    if (column < CELLS && row < CELLS) project(current + SIDE + 1, diagonal);
                }
                if (error < 0.001) break;
            }
        }
        double highest = 0;
        for (auto &point : surface.points) {
            // Unilateral support is local, never a whole-sheet resize lift.
            point.position.z = std::max(0.0, point.position.z);
            point.contact = point.position.z < surface.thickness * 0.25;
            highest = std::max(highest, point.position.z);
        }
        const double movement_shadow = movement
                                           ? (mode.lift + moving_angle * 0.12 +
                                                 std::abs(mode.torsion) * 0.1 + mode.corner * 0.1) *
                                                 mode.shadow_separation
                                           : 0;
        surface.shadow_strength = unit(12 * std::min(highest, resize_height + movement_shadow));
        const float crease = static_cast<float>(std::min(0.9, resizing));
        for (auto &face : surface.faces) face.crease = {crease, crease, crease};
    }

    Vec3 PaperInteraction::sample(const PaperSurface &surface, Vec2 uv) {
        assert(surface.interaction_mesh && surface.points.size() == COUNT);
        assert(std::isfinite(uv.x) && std::isfinite(uv.y));
        const double horizontal = std::clamp(uv.x, 0.0, 1.0) * CELLS;
        const double vertical = std::clamp(uv.y, 0.0, 1.0) * CELLS;
        const auto column = std::min(static_cast<unsigned>(horizontal), CELLS - 1);
        const auto row = std::min(static_cast<unsigned>(vertical), CELLS - 1);
        const double fraction_x = horizontal - column, fraction_y = vertical - row;
        const unsigned index = row * SIDE + column;
        const auto &first = surface.points[index].position;
        const auto &diagonal = surface.points[index + SIDE + 1].position;
        const auto &other = surface.points[index + (fraction_y <= fraction_x ? 1 : SIDE)].position;
        const double first_weight = 1 - std::max(fraction_x, fraction_y);
        const double diagonal_weight = std::min(fraction_x, fraction_y);
        const double other_weight = std::abs(fraction_x - fraction_y);
        return {first.x * first_weight + diagonal.x * diagonal_weight + other.x * other_weight,
            first.y * first_weight + diagonal.y * diagonal_weight + other.y * other_weight,
            first.z * first_weight + diagonal.z * diagonal_weight + other.z * other_weight};
    }
}  // namespace hyprimate
