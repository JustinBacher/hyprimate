#include <numbers>

#include "IntactTransportPrivate.hpp"

namespace hyprimate::construction::studies {
    using namespace intact_transport_detail;
    namespace {
        using intact_transport_detail::operator+;
        using intact_transport_detail::operator-;
        using intact_transport_detail::operator*;

        constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }
        constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        constexpr Vec3 operator*(Vec3 a, double s) noexcept {
            return {a.x * s, a.y * s, a.z * s};
        }
        constexpr Vec2 xy(Vec3 v) noexcept {
            return {v.x, v.y};
        }
        constexpr Vec3 xyz(Vec2 v) noexcept {
            return {v.x, v.y, 0};
        }
        constexpr double sum_sq(Vec3 v) noexcept {
            return v.x * v.x + v.y * v.y + v.z * v.z;
        }
        struct Chain {
            Vec3 joint, end;
        };

        Chain two_link(Vec3 root, Vec3 wanted, double upper, double lower, int bend) noexcept {
            const Vec3 d = wanted - root;
            const double full = std::sqrt(sum_sq(d)), planar = length(xy(d));
            const Vec3 axis = full > 0 ? d * (1 / full) : Vec3{0, 1, 0};
            const Vec3 side = planar > 0 ? Vec3{bend * d.y / planar, -bend * d.x / planar, 0}
                                        : Vec3{double(bend), 0, 0};
            const double reach =
                std::clamp(full, std::abs(upper - lower) + .00001 * upper, upper + lower - .00001 * upper);
            const double along = (reach * reach + upper * upper - lower * lower) / (2 * reach);
            const double rise = std::sqrt(std::max(0.0, upper * upper - along * along));
            return {
                root + axis * along + side * rise, reach == full ? wanted : root + axis * reach};
        }

        struct ReachDisk {
            Vec2 center;
            double radius;
        };

        ReachDisk reach_disk(Vec3 wanted, Vec3 offset, double length) noexcept {
            const Vec3 d = wanted - offset;
            return {xy(d), std::sqrt(std::max(0.0, length * length - d.z * d.z))};
        }

        // Closest feasible pelvis in the intersection of the planted-boot and grip disks.
        // If the authored pose misses, the optimum is on one circle or at a pair of crossings.
        std::optional<Vec2> fit_hip(Vec2 hip, std::span<const ReachDisk> disks) noexcept {
            const auto fits = [disks](Vec2 p) {
                return std::ranges::all_of(disks, [p](ReachDisk d) {
                    const Vec2 delta = p - d.center;
                    return dot(delta, delta) <= d.radius * d.radius + 1e-8;
                });
            };
            if (fits(hip)) return hip;
            std::optional<Vec2> best;
            double error = 0;
            const auto consider = [&](Vec2 p) {
                const Vec2 delta = p - hip;
                const double d = dot(delta, delta);
                if ((!best || d < error) && fits(p)) {
                    best = p;
                    error = d;
                }
            };
            for (std::size_t i = 0; i < disks.size(); ++i) {
                const auto [at, r] = disks[i];
                const Vec2 towards = hip - at;
                const double dist = length(towards);
                if (dist > 0) consider(at + towards * (r / dist));
                for (std::size_t j = 0; j < i; ++j) {
                    const auto [other, q] = disks[j];
                    const Vec2 d = other - at;
                    const double n = length(d);
                    if (n == 0 || n < std::abs(r - q) || n > r + q) continue;
                    const double along = (r * r - q * q + n * n) / (2 * n);
                    const double across = std::sqrt(std::max(0.0, r * r - along * along));
                    const Vec2 mid = at + d * (along / n),
                               side{-d.y * across / n, d.x * across / n};
                    consider(mid + side);
                    consider(mid - side);
                }
            }
            return best;
        }
    }  // namespace
    Vec2 IntactTransport::Bearer::station(Box body) const noexcept {
        return Vec2{body.x + sockets[0].x * body.w, body.y + sockets[0].y * body.h} + stance_offset;
    }

    Vec3 IntactTransport::Bearer::neutral(int i) const noexcept {
        return {root.x + facing * height * (i == 0 ? .102 : -.115),
            root.y + height * (i == 0 ? .025 : -.035),
            height * (i == 0 ? .046 : -.055)};
    }

    void IntactTransport::Bearer::enter(Vec2 target, bool fresh) noexcept {
        if (fresh) {
            root = target + staging;
            feet[0].sole = neutral(0);
            feet[1].sole = neutral(1);
            swing = -1;
        }
        start = root - target;
        duration = std::min(.43, .125 + length(start) / 235);
        initial_alpha = alpha;
        initial_reach = reach;
        age = 0;
        route = Route::Approach;
    }

    void IntactTransport::Bearer::walk(double dt, Vec2 v) noexcept {
        const double speed = length(v);
        const Vec2 dir = speed > 1e-6 ? v * (1 / speed) : Vec2{};
        const std::array homes{neutral(0), neutral(1)};
        std::array debts{xy(homes[0] - feet[0].sole), xy(homes[1] - feet[1].sole)};
        if (swing >= 0) {
            const int i = swing;
            auto &foot = feet[i];
            // If reversal uses up the current stance room, land the airborne boot first.
            const bool transfer = length(debts[1 - i]) > height * .386;
            foot.elapsed = transfer ? foot.duration : std::min(foot.duration, foot.elapsed + dt);
            const double u = foot.elapsed / foot.duration;
            const double forward = std::min(height * .93,
                std::min(stride * .60, speed * .087) + speed * (foot.duration - foot.elapsed));
            const Vec3 end = homes[i] + xyz(dir * forward);
            const double arc = u < 1 ? std::sin(std::numbers::pi * u) : 0;
            foot.sole = foot.origin + (end - foot.origin) * ease(u) -
                        Vec3{0, height * (.154 - .045 * brace) * arc, 0};
            foot.pitch = -.55 * foot.strike_x * arc * std::cos(std::numbers::pi * u);
            if (u < 1) return;
            swing = -1;
            debts[i] = xy(homes[i] - foot.sole);
        }

        double priority = 0;
        for (int i = 0; i < 2; ++i) {
            const auto [dx, dy] = debts[i];
            const double debt = length(debts[i]);
            const double urgency = speed < 8 ? debt
                                             : std::max({dot(dir, debts[i]),
                                                   .73 * std::abs(dir.x * dy - dir.y * dx),
                                                   debt - .12 * height});
            if (urgency <= (speed < 8 ? height * .245 : trigger)) continue;
            const double ranked = urgency + (i == next_foot ? .023 * height : 0);
            if (ranked >= priority) {
                priority = ranked;
                swing = i;
            }
        }
        if (swing >= 0) {
            auto &foot = feet[swing];
            next_foot = 1 - swing;
            foot.origin = foot.sole;
            foot.elapsed = 0;
            foot.duration = std::clamp(stride / std::max(1.0, speed), .033, .145);
            foot.strike_x = speed >= 8 ? dir.x : debts[swing].x >= 0 ? 1 : -1;
        }
    }

    void IntactTransport::Bearer::advance(double dt,
        Box body,
        bool working,
        double episode_age,
        double settle,
        double exit) noexcept {
        const Vec2 target = station(body);
        if (route == Route::Waiting) {
            if (!working)
                route = Route::Gone;
            else if (episode_age >= delay)
                enter(target, true);
            return;
        }
        if (route == Route::Gone) return;
        if (!working && route != Route::Leave && (route != Route::Grip || episode_age >= settle)) {
            if (alpha == 0) {
                route = Route::Gone;
                return;
            }
            start = root;
            end = root + staging * 1.08;
            initial_alpha = alpha;
            initial_reach = reach;
            age = 0;
            duration = std::max(.08, exit - episode_age);
            route = Route::Leave;
        }
        if (dt == 0) return;
        age += dt;
        const Vec2 previous = root;
        double target_brace = 0;
        if (route == Route::Approach) {
            const double u = std::min(1.0, age / duration);
            root = target + start * (1 - ease(u));
            alpha = std::lerp(initial_alpha, 1.0, ease(u / .38));
            reach = std::lerp(initial_reach, 1.0, ease((u - .42) / .58));
            target_brace = ease((u - .33) / .62);
            if (u == 1) {
                age = 0;
                route = Route::Grip;
            }
        } else if (route == Route::Grip) {
            root = target;
            reach = 1;
            target_brace = working ? std::lerp(1.0, .61, ease(age / .21))
                                   : std::lerp(.61, 1.0, ease(episode_age / settle));
        } else {
            const double u = std::min(1.0, age / duration);
            root = start + (end - start) * ease(u);
            reach = initial_reach * (1 - ease(u / .36));
            alpha = initial_alpha * (1 - ease((u - .62) / .38));
            if (u == 1) {
                route = Route::Gone;
                return;
            }
        }
        brace = approach(brace, target_brace, 7.5 * dt);
        const Vec2 v = (root - previous) * (1 / dt);
        velocity = velocity + (v - velocity) * (-std::expm1(-19 * dt));
        shift = approach(shift, 2.1 * std::tanh((side ? velocity.y : velocity.x) / 180), 26 * dt);
        walk(dt, v);
    }

    void IntactTransport::Bearer::publish(
        CarryBearerPose &pose, const IntactTransportFace &face, double pressure) noexcept {
        const double s = height, f = facing, push = std::tanh(velocity.x / 190);
        Vec3 hips{root.x + s * (.025 * f + .034 * push),
            root.y + s * (-.469 + .101 * brace + .016 * pressure),
            0};
        const Vec3 neck_offset{s * (f * (.038 + .089 * brace) + push * (.037 + .012 * pressure)),
            -.308 * s,
            -.009 * s * push};
        const std::array shoulders{neck_offset + Vec3{f * s * .018, s * .067, s * .048},
            neck_offset + Vec3{-f * s * (.072 - .035 * reach), s * .092, -s * .077}};
        const Vec3 glove{
            f * s * std::lerp(.088, .101, reach), s * std::lerp(.077, -.099, reach), 0};
        std::array<Vec3, 2> sole_offset, ankle, leg_offset, socket;
        std::array<ReachDisk, 4> disks;
        std::size_t disk_count = 0;
        for (int i = 0; i < 2; ++i) {
            // The preview boot includes the painted .25 texel keyline beyond its sole socket.
            const double d = s * .0042 * (i == 0 ? 27.25 : 26.25);
            const double a = feet[i].pitch;
            sole_offset[i] = {-std::sin(a) * d, std::cos(a) * d, 0};
            ankle[i] = feet[i].sole - sole_offset[i];
            leg_offset[i] = {f * s * (i == 0 ? .049 : -.040), 0, s * (i == 0 ? .043 : -.054)};
            if (i != swing) disks[disk_count++] = reach_disk(ankle[i], leg_offset[i], .478 * s);
            const double slide = shift * (i == 0 ? 1 : -.34);
            Vec2 local{sockets[i].x * face.source.w, sockets[i].y * face.source.h};
            if (side && (i == 0 || side_pair))
                local.y = std::clamp(local.y + slide, 0.0, face.source.h);
            else
                local.x = std::clamp(local.x + slide, 0.0, face.source.w);
            socket[i] = xyz(face.point(local));
        }

        int latch = 0;
        if (route == Route::Grip) {
            const std::array palms{reach_disk(socket[0] - glove * .76, shoulders[0], .362 * s),
                reach_disk(socket[1] - glove * .76, shoulders[1], .362 * s)};
            for (const int mask : {3, 1, 2}) {
                std::size_t n = disk_count;
                for (int i = 0; i < 2; ++i)
                    if ((mask & (1 << i)) != 0) disks[n++] = palms[i];
                if (const auto fit = fit_hip(xy(hips), {disks.data(), n})) {
                    hips = xyz(*fit);
                    latch = mask;
                    break;
                }
            }
        }
        // Arms yield before a planted boot; never stretch a limb to preserve a contact.
        if (latch == 0) {
            if (const auto fit = fit_hip(xy(hips), {disks.data(), disk_count})) hips = xyz(*fit);
        }

        pose.station = root;
        pose.hips = hips;
        pose.neck = hips + neck_offset;
        pose.head = pose.neck + Vec3{s * (.023 * f + .054 * push * brace), -.013 * s, .024 * s};
        pose.height = s;
        pose.opacity = alpha;
        pose.facing = facing;
        pose.vest = vest;
        pose.behind_face = rear;
        contacts = 0;
        const double gait = std::clamp((feet[0].sole.x - feet[1].sole.x) / s - .217 * f, -.40, .40);
        for (int i = 0; i < 2; ++i) {
            const Vec3 hip = hips + leg_offset[i];
            const Chain leg = two_link(hip, ankle[i], .232 * s, .247 * s, facing);
            pose.legs[i] = {hip,
                leg.joint,
                leg.end,
                leg.end + sole_offset[i],
                feet[i].pitch,
                i != swing && sum_sq(leg.end - ankle[i]) < 1e-8};
            const double counter = gait * (i == 0 ? .31 : -.27);
            const Vec3 free = hips + Vec3{s * ((i == 0 ? .114 : .049) * f - counter),
                                         -.019 * s,
                                         s * (i == 0 ? .061 : -.080)};
            const bool touching = (latch & (1 << i)) != 0;
            const double weight = route != Route::Grip || touching ? reach : .30;
            const Vec3 shoulder = hips + shoulders[i];
            const Vec3 target = free + (socket[i] + glove * .24 - free) * weight;
            const Chain arm = two_link(shoulder, target - glove, .179 * s, .184 * s, -facing);
            const Vec3 palm = arm.end + glove * .76;
            pose.arms[i] = {shoulder, arm.joint, arm.end, arm.end + glove, palm, std::nullopt};
            if (touching && sum_sq(palm - socket[i]) < 1e-8) {
                pose.arms[i].grip = xy(socket[i]);
                ++contacts;
            }
        }
    }

    void IntactTransport::place_crew(Box apron) noexcept {
        const Box body = m_face.source;
        const double w = body.w, h = body.h, room_b = bottom(apron) - bottom(body);
        const double size = .075 * std::min(w, h) * (m_worker_size == 0 ? 1 : m_worker_size);
        std::uint64_t rng = m_seed ^ 0x57BAE2;
        const auto draw = [&](double lo, double hi) { return std::lerp(lo, hi, unit_draw(rng)); };
        const auto filled = [&](unsigned slot) { return (m_crew_roster & (1U << slot)) != 0; };
        // Append newly feasible stations; existing grips, gait, assignment and entrance timing
        // stand.
        const auto add = [&](unsigned slot,
                             Vec2 near,
                             Vec2 far,
                             Vec2 stance,
                             Vec2 staging,
                             int facing,
                             double s,
                             bool side,
                             bool rear) {
            m_crew_roster |= 1U << slot;
            auto &a = m_crew[m_crew_count];
            a = {};
            a.sockets = {{{near.x / w, near.y / h}, {far.x / w, far.y / h}}};
            a.stance_offset = stance - near;
            a.staging = staging;
            a.facing = facing;
            a.height = s;
            a.side = side;
            a.side_pair = side && near.x == far.x;
            a.rear = rear;
            a.vest = static_cast<unsigned>(m_crew_count % 4);
            a.delay = .014 + .027 * static_cast<double>(m_crew_count) + draw(0, .013);
            a.stride = s * draw(.465, .555);
            a.trigger = s * draw(.146, .185);
            a.next_foot = static_cast<int>(splitmix64(rng) & 1U);
            ++m_crew_count;
        };
        for (const int facing : {1, -1}) {
            const double room = facing == 1 ? body.x - apron.x : right(apron) - right(body);
            const unsigned slot = facing == 1 ? 0U : 1U;
            if (filled(slot) || room < .31 * size) continue;
            const double s = size * (1 + draw(-2.3, 2.8) / 42);
            const double edge = facing == 1 ? 0 : w;
            const double bottom_grip = h - std::min(8.0, h * .18);
            const double hp = std::clamp(bottom(apron) - body.y - s * .76, 0.0, bottom_grip);
            const Vec2 near{edge, hp};
            const Vec2 far = hp == bottom_grip
                                 ? Vec2{edge + facing * std::min(s * .049, w * .22), h}
                                 : Vec2{edge, std::max(0.0, hp - s * .15)};
            const Vec2 stance{edge - facing * std::min(.305 * s, room * .67), hp + .76 * s};
            add(slot,
                near,
                far,
                stance,
                {-facing * std::min(draw(20, 33), std::max(8.0, room - .54 * s)),
                    std::min(draw(3, 13), std::max(0.0, bottom(apron) - body.y - stance.y - 3))},
                facing,
                s,
                true,
                facing == 1);
        }
        const double lo = std::max(w * .10, apron.x + 25 - body.x),
                     hi = std::min(w * .90, right(apron) - 25 - body.x);
        if (room_b < size * .42 || hi <= lo) return;
        const int count = 1 + static_cast<int>(std::min(3.0, (hi - lo) / 162));
        for (int i = 0; i < count; ++i) {
            const unsigned slot = 2U + static_cast<unsigned>(i);
            if (filled(slot)) continue;
            const double sx = std::lerp(lo, hi, (i + .5) / count);
            const auto roster = std::span{m_crew.data(), m_crew_count};
            if (std::ranges::any_of(roster, [=](const Bearer &a) {
                    return !a.side && std::abs(a.sockets[0].x * w - sx) < .85 * size;
                }))
                continue;
            const double s = size * (1 + draw(-3.3, 3.1) / 42);
            const int facing = i % 2 == 0 ? 1 : -1;
            const double spread = std::min(w * .16, s * .17);
            add(slot,
                {sx, h},
                {sx - facing * spread, h},
                {sx - facing * s * .215, h + std::clamp(room_b - 3, s * .63, s * .79)},
                {-facing * draw(25, 43), std::min(draw(12, 24), std::max(0.0, room_b - s * .64))},
                facing,
                s,
                false,
                false);
        }
    }

    void IntactTransport::advance_crew(
        double dt, Vec2 velocity, bool work, bool regrab, bool resizing) noexcept {
        const Box body = m_face.source;
        const double age = work ? m_work_age : m_release_age;
        bool any = false;
        int supporting = 0;
        double imbalance = 0;
        for (std::size_t i = 0; i < m_crew_count; ++i) {
            auto &a = m_crew[i];
            if (regrab && (a.route == Route::Leave || a.route == Route::Gone))
                a.enter(a.station(body), a.route == Route::Gone && a.alpha == 0);
            const Vec2 target = a.station(body);
            // A jump beyond a stride cannot be carried in place. Let that bearer walk back to its
            // socket.
            if (work && a.route == Route::Grip && length(target - a.root) > a.height * .82)
                a.enter(target, false);
            a.advance(dt,
                body,
                work,
                age,
                m_settle + (i % 2 != 0 ? .008 : 0),
                m_exit - .011 * static_cast<double>(i % 3));
            any |= a.route != Route::Gone;
            if (a.route == Route::Grip && a.contacts > 0) {
                ++supporting;
                imbalance += (2 * a.sockets[0].x - 1) * (a.swing >= 0 ? 1 : .23);
            }
        }
        m_pressure = work ? approach(m_pressure, supporting >= 2 ? 1 : 0, 5.45 * dt)
                          : m_release_pressure * (1 - ease(age / m_settle));
        const double pressure = ease(m_pressure);
        if (dt > 0) {
            const Vec2 change = (velocity - m_velocity) * (-std::expm1(-19 * dt));
            m_velocity = m_velocity + change;
            const double drive = 2.95 * std::tanh(dot(change, {-.95, .33}) / (790 * dt)) +
                                 1.65 * imbalance / std::max(supporting, 1);
            // Closed form of the preview's damped 141/18.8 response, with the sampled drive held
            // constant.
            constexpr double DAMPING = 9.4, RATE_SQ = 141;
            static const double rate = std::sqrt(RATE_SQ - DAMPING * DAMPING);
            const double decay = std::exp(-DAMPING * dt);
            const double c = std::cos(rate * dt), s = std::sin(rate * dt) / rate;
            const double error = m_angle - drive, v = m_angular_velocity;
            m_angle =
                std::clamp(drive + decay * (error * c + (v + DAMPING * error) * s), -3.8, 3.8);
            m_angular_velocity = decay * (v * c - (RATE_SQ * error + DAMPING * v) * s);
        } else if (velocity.x != 0 || velocity.y != 0) {
            m_velocity = velocity;
        }
        const double radius = std::max(1.0, .5 * std::hypot(body.w, body.h));
        const double radians =
            std::clamp(m_angle / radius, -std::numbers::pi / 90, std::numbers::pi / 90);
        pose_face(resizing ? 0 : 2.78 * pressure, radians * pressure);
        const auto publish_bearers = [&](double load) {
            int attached = 0;
            m_bearer_count = 0;
            for (std::size_t i = 0; i < m_crew_count; ++i) {
                auto &a = m_crew[i];
                if (a.route == Route::Gone || a.alpha <= 0)
                    a.contacts = 0;
                else
                    a.publish(m_bearers[m_bearer_count++], m_face, load);
                attached += a.contacts > 0;
            }
            return attached;
        };
        const int current_support = publish_bearers(pressure);
        if (m_face.lift > 0 && current_support < 2) {
            // Reversals can put the load beyond today's palms even when yesterday's lift was full.
            // The displayed face and hands must agree on support this tick, not on the next sample.
            m_pressure = m_release_pressure = 0;
            pose_face(0, 0);
            publish_bearers(0);
        }
        if (m_face.lift > 0)
            m_shadow = {body.x - body.w * .02, bottom(body) - 2, body.w * 1.04, 14};
        m_active = work || any || m_pressure > 0;
    }

}  // namespace hyprimate::construction::studies
