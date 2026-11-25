#include <numbers>
#include <utility>

#include "IntactTransportPrivate.hpp"

namespace hyprimate::construction::studies {
    using namespace intact_transport_detail;
    namespace {
        using intact_transport_detail::approach;

        constexpr double WIRE_WIDTH = 1.55;
        constexpr Vec2 STOWED_PIN{63, -83};
        constexpr double BOOM_FRINGE = 12, RASTER_FRINGE = 2;
        constexpr Box TRACK_SHADOW{-53, -5, 114, 14};
        constexpr Box CHASSIS{-53, -96, 110, 104};
        constexpr Box CAB{-15, -80, 60, 60};
        constexpr Box BALLAST{-46, -83, 60, 60};
        Vec2 approach(Vec2 from, Vec2 to, double amount) noexcept {
            const double distance = length(to - from);
            return distance <= amount ? to : from + (to - from) * (amount / distance);
        }

        Box join(Box a, Box b) noexcept {
            const double l = std::min(a.x, b.x), t = std::min(a.y, b.y);
            return {l, t, std::max(right(a), right(b)) - l, std::max(bottom(a), bottom(b)) - t};
        }

        Box swept_bounds(Vec2 a, Vec2 b, double fringe) noexcept {
            return {std::min(a.x, b.x) - fringe,
                std::min(a.y, b.y) - fringe,
                std::abs(a.x - b.x) + 2 * fringe,
                std::abs(a.y - b.y) + 2 * fringe};
        }
        struct Rail {
            double left, right, floor;
        };
        Rail rail_for(
            Box body, double home_bottom, Box apron, std::span<const Box> outputs) noexcept {
            // The crawler stays on the original staging rail; hoisting the load does not lift the
            // tracks.
            const double floor = home_bottom + 17 + std::min(0.0, bottom(body) - home_bottom) * .08;
            Rail rail{apron.x,
                right(apron),
                std::max(
                    apron.y + std::min(78.0, apron.h * .5), std::min(floor, bottom(apron) - 10))};
            // Touching spans at track height are one apron. Gaps only matter for the exit ray.
            bool grew;
            do {
                grew = false;
                for (const Box out : outputs) {
                    if (!usable(out) || out.y > rail.floor - 9 || bottom(out) < rail.floor + 9 ||
                        out.x > rail.right || right(out) < rail.left)
                        continue;
                    if (out.x < rail.left || right(out) > rail.right) {
                        rail.left = std::min(rail.left, out.x);
                        rail.right = std::max(rail.right, right(out));
                        grew = true;
                    }
                }
            } while (grew);
            return rail;
        }

        Vec2 crane_station(Rail rail, Box body, int side) noexcept {
            const double inset = std::min(61.0, (rail.right - rail.left) * .5);
            const double edge = side == 1 ? body.x : right(body);
            return {
                std::clamp(edge - side * 84, rail.left + inset, rail.right - inset), rail.floor};
        }

        Vec2 stowed_pin(Vec2 base, int side) noexcept {
            return base + Vec2{side * STOWED_PIN.x, STOWED_PIN.y};
        }

        void crane_geometry(CarryCranePose &pose, Vec2 base, Vec2 tip, int side) noexcept {
            const auto p = [=](double x, double y) { return base + Vec2{side * x, y}; };
            const auto box = [&](Box local) {
                const Vec2 o = p(side == 1 ? local.x : right(local), local.y);
                return Box{o.x, o.y, local.w, local.h};
            };
            pose.track = base;
            pose.facing = side;
            pose.track_shadow = box(TRACK_SHADOW);
            // Padded cutout bounds, including the larger foreground tread and the counterweight.
            pose.chassis = box(CHASSIS);
            pose.cab = box(CAB);
            pose.ballast = box(BALLAST);
            pose.heel = p(-11, -43);
            pose.ram_base = p(-28, -35);
            pose.sheave = tip;
            const Vec2 delta = tip - pose.heel;
            const double span = length(delta), heel_length = std::min(222.0, span * .88);
            const Vec2 unit = span > 0 ? delta * (1 / span) : Vec2{double(side), 0};
            pose.collar = pose.heel + unit * heel_length;
            pose.nose = pose.collar + (tip - pose.collar) * .57;
            pose.ram_tip = pose.heel + unit * std::min(67.0, span);
            for (std::size_t i = 0; i < pose.wheels.size(); ++i)
                pose.wheels[i] = p(-31 + 20 * static_cast<double>(i), -10);
            pose.bounds = join(join(pose.track_shadow, pose.chassis), join(pose.cab, pose.ballast));
            pose.bounds = join(pose.bounds, swept_bounds(pose.heel, pose.collar, BOOM_FRINGE));
            pose.bounds = join(pose.bounds, swept_bounds(pose.collar, pose.nose, 8));
            pose.bounds = join(pose.bounds, swept_bounds(pose.nose, tip, BOOM_FRINGE));
            pose.bounds = join(pose.bounds, swept_bounds(pose.ram_base, pose.ram_tip, 4));
        }

        std::optional<double> exit_base(
            double floor, int side, std::span<const Box> outputs) noexcept {
            const double top =
                floor + std::min(CHASSIS.y, STOWED_PIN.y - BOOM_FRINGE) - RASTER_FRINGE;
            const double last_y = floor + bottom(TRACK_SHADOW) + RASTER_FRINGE;
            std::optional<double> edge;
            for (const Box out : outputs) {
                if (!usable(out) || out.y >= last_y || bottom(out) <= top) continue;
                const double candidate = side == 1 ? out.x : right(out);
                edge = !edge       ? candidate
                       : side == 1 ? std::min(*edge, candidate)
                                   : std::max(*edge, candidate);
            }
            if (!edge) return std::nullopt;
            const double front = STOWED_PIN.x + BOOM_FRINGE + RASTER_FRINGE;
            return *edge - side * front;
        }

        bool exposed_exit(Box owner, double old_floor, double floor, int side,
            std::span<const Box> outputs) noexcept {
            const double top = std::min(old_floor, floor) +
                               std::min(CHASSIS.y, STOWED_PIN.y - BOOM_FRINGE) - RASTER_FRINGE;
            const double last_y = std::max(old_floor, floor) + bottom(TRACK_SHADOW) + RASTER_FRINGE;
            return std::ranges::none_of(outputs, [&](Box out) {
                return usable(out) && out.y < last_y && bottom(out) > top &&
                       (side == 1 ? out.x < owner.x : right(out) > right(owner));
            });
        }

        // Bounded critically damped pursuit. Evaluating the closed form avoids substep debt.
        void follow(double &value,
            double &speed,
            double target,
            double dt,
            double rate,
            double limit) noexcept {
            const double error = std::clamp(value - target, -2 * limit / rate, 2 * limit / rate);
            const double c = speed + rate * error, decay = std::exp(-rate * dt);
            value += (decay - 1) * error + decay * c * dt;
            speed = (speed - rate * c * dt) * decay;
        }
    }  // namespace
    bool IntactTransport::advance_crane(double dt,
        Box apron,
        std::span<const Box> outputs,
        bool mapped,
        Vec2 velocity,
        bool work,
        bool fresh,
        bool pickup_started,
        bool resizing) noexcept {
        const Box body = m_face.source;
        auto &crane = m_crane;
        if (fresh) crane.home_bottom = bottom(body);
        const auto rail_outputs = mapped ? std::span<const Box>{&apron, 1} : outputs;
        const Rail rail = rail_for(body, crane.home_bottom, apron, rail_outputs);
        const auto allowed = [&](int side) {
            return !mapped || exposed_exit(apron, fresh ? rail.floor : crane.base.y, rail.floor, side, outputs);
        };
        if (fresh) {
            const auto option = [&](int side, double room) {
                const double station = crane_station(rail, body, side).x;
                const double exit = exit_base(rail.floor, side, rail_outputs).value_or(station);
                return std::pair{std::min(room, 145.0), -std::abs(station - exit)};
            };
            const bool left = allowed(1), right_side = allowed(-1);
            if (!left && !right_side) return false;
            const auto left_score = option(1, body.x - rail.left);
            const auto right_score = option(-1, rail.right - right(body));
            const bool prefer_right = mapped && outputs.size() > 1 ? right_score.second > left_score.second :
                                      right_score > left_score;
            crane.side = right_side && (!left || prefer_right) ? -1 : 1;
            crane.base = {exit_base(rail.floor, crane.side, rail_outputs)
                              .value_or(crane_station(rail, body, crane.side).x),
                rail.floor};
            crane.pin = stowed_pin(crane.base, crane.side);
            crane.aim = {body.x + .5 * body.w, body.y - 160};
            crane.anchor = {{{body.x, body.y}, {right(body), body.y}}};
        } else if (!allowed(crane.side)) {
            // Never turn a retreating or regrabbed rig into an unsafe exit or teleport it.
            return false;
        }
        const int side = crane.side;
        Vec2 station = crane_station(rail, body, side);
        if (pickup_started)
            m_pickup_remaining += std::max(
                std::abs(crane.base.x - station.x) / 480, std::abs(crane.base.y - station.y) / 340);

        bool departing = false;
        const bool reeling =
            !work && crane.lift == 0 && crane.payout > 0 && (!crane.attached || crane.slack == 1);
        if (work) {
            if (crane.extend < 1) {
                const Vec2 gap = crane.base - station;
                if (crane.extend > 0 || (std::abs(gap.x) <= 145 && std::abs(gap.y) <= 36))
                    crane.extend = approach(crane.extend, 1, dt / .29);
            } else if (!crane.attached) {
                const std::array<Vec2, 2> targets{{{body.x, body.y}, {right(body), body.y}}};
                bool aligned = true;
                for (int i = 0; i < 2; ++i) {
                    crane.anchor[i] = approach(crane.anchor[i], targets[i], 1450 * dt);
                    aligned &= length(crane.anchor[i] - targets[i]) < 1e-8;
                }
                if (crane.payout > 0 ||
                    crane.pin.y <= std::min(crane.anchor[0].y, crane.anchor[1].y) - 38)
                    crane.payout = approach(crane.payout, 1, dt / .22);
                crane.attached = crane.payout == 1 && aligned;
            } else if (crane.slack > 0) {
                crane.slack = approach(crane.slack, 0, dt / .085);
            } else {
                crane.lift = approach(crane.lift, 1, dt / .17);
            }
        } else if (crane.lift > 0) {
            crane.lift = approach(crane.lift, 0, dt / .18);
        } else if (crane.attached && crane.slack < 1) {
            crane.slack = approach(crane.slack, 1, dt / .105);
        } else if (reeling) {
            if (crane.attached) {
                crane.anchor = {m_face.point({0, 0}), m_face.point({m_face.source.w, 0})};
                crane.attached = false;
            }
            crane.payout = approach(crane.payout, 0, dt / .24);
        } else if (crane.extend > 0) {
            crane.extend = approach(crane.extend, 0, dt / .25);
        } else {
            departing = true;
            crane.base_v.y = 0;
            const auto exit = exit_base(crane.base.y, side, rail_outputs);
            if (!exit || side * (crane.base.x - *exit) <= 0) {
                m_active = false;
                pose_face(0, 0);
                return true;
            }
            // The unconstrained study can drive across gaps; a mapped exit uses its owner only.
            station = {*exit - side * 12, crane.base.y};
        }

        // The sampled velocity changes the pendulum's impulse even on acquisition with elapsed ==
        // 0.
        const double omega = m_angular_velocity - .00038 * (velocity.x - m_velocity.x);
        m_velocity = velocity;
        const double decay = std::exp(-3.9 * dt), c = std::cos(7.67 * dt),
                     s = std::sin(7.67 * dt) / 7.67;
        const double angle = m_angle;
        m_angle = decay * (angle * c + (omega + 3.9 * angle) * s);
        m_angular_velocity = decay * (omega * c - (74 * angle + 3.9 * omega) * s);
        const double lift = resizing ? 0 : ease(crane.lift);
        const double gain = lift * std::tanh(m_angle / .0105);
        pose_face(
            2.8 * lift, gain * std::min(.0105, 8 / std::max(1.0, std::hypot(body.w, body.h))));
        const std::array corners{m_face.origin, m_face.origin + m_face.u};
        if (crane.attached) crane.anchor = corners;
        if (lift > 0)
            m_shadow = {body.x - 4 - body.w * .03,
                rail.floor - 6,
                body.w * 1.06,
                17 + std::max(0.0, crane.home_bottom - bottom(body)) * .19};

        follow(crane.base.x, crane.base_v.x, station.x, dt, 14, departing ? 540 : 640);
        if (!departing) follow(crane.base.y, crane.base_v.y, station.y, dt, 16, 420);
        if (reeling) {
            crane.aim_v = {};
        } else if (!departing) {
            const double sway = 32 * std::tanh(crane.base_v.x / 210) * lift + 4 * gain;
            follow(crane.aim.x,
                crane.aim_v.x,
                .5 * (corners[0].x + corners[1].x) + sway,
                dt,
                22,
                1700);
            follow(crane.aim.y,
                crane.aim_v.y,
                std::min(corners[0].y, corners[1].y) - 160 - 27 * lift,
                dt,
                22,
                1700);
        }
        if (crane.payout > 0) {
            const double clearance = std::min(crane.anchor[0].y, crane.anchor[1].y) - 38;
            if (crane.aim.y > clearance) {
                crane.aim.y = clearance;
                crane.aim_v.y = std::min(0.0, crane.aim_v.y);
            }
        }
        const Vec2 stowed = stowed_pin(crane.base, side);
        crane.pin = stowed + (crane.aim - stowed) * ease(crane.extend);
        if (!m_crane_pose) m_crane_pose.emplace();
        auto &pose = *m_crane_pose;
        crane_geometry(pose, crane.base, crane.pin, side);
        pose.wire_count = 0;
        pose.extension = crane.extend;
        pose.departing = departing;
        pose.lever = .5 + .47 * std::tanh(side * crane.base_v.x / 160);
        const double tread = side * crane.base.x / 6.8 +
                             static_cast<double>(m_seed % 17) * std::numbers::pi * 2 / 17;
        const Vec2 tick{side * 3.4 * std::cos(tread), 3.4 * std::sin(tread)};
        for (std::size_t i = 0; i < pose.wheels.size(); ++i)
            pose.tread_marks[i] = pose.wheels[i] + tick;
        if (crane.payout > 0) {
            pose.wire_count = pose.wires.size();
            const double payout = ease(crane.payout), slack = ease(crane.slack);
            for (std::size_t i = 0; i < pose.wires.size(); ++i) {
                const Vec2 end = payout == 1 ? crane.anchor[i]
                                             : crane.pin + (crane.anchor[i] - crane.pin) * payout;
                Vec2 mid = (crane.pin + end) * .5;
                mid.y += std::min(19.0, std::max(0.0, end.y - mid.y) * .65) * slack;
                pose.wires[i] = {
                    crane.pin, mid, end, WIRE_WIDTH, crane.attached, crane.attached && slack == 0};
                pose.bounds = join(
                    join(pose.bounds, swept_bounds(crane.pin, mid, 1)), swept_bounds(mid, end, 1));
            }
        }
        return true;
    }

}  // namespace hyprimate::construction::studies
