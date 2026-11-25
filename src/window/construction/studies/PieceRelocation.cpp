#include <algorithm>
#include <cmath>
#include <numbers>

#include "PieceRelocation.hpp"

namespace hyprimate::construction::studies {
    namespace {

        // The workers-only-03 take uses four overlapping reservations, a brief bare-hand
        // release, shallow heel tilt, a capped critically damped haul, and hand-seating.
        constexpr double TICK = 1.0 / 120, HAUL_SPEED = 1088, FOLLOW_RATE = 23;
        constexpr double LONG_HAUL_SECONDS = 6;
        constexpr double STAGGER = .065, LATCH = .16, LIFT = .14, FIT = .095, FASTEN = .16,
                         DEPART = .36;
        constexpr double THIGH = .232, SHIN = .247;

        Vec2 operator+(Vec2 a, Vec2 b) noexcept {
            return {a.x + b.x, a.y + b.y};
        }
        Vec2 operator-(Vec2 a, Vec2 b) noexcept {
            return {a.x - b.x, a.y - b.y};
        }
        Vec2 operator*(Vec2 a, double s) noexcept {
            return {a.x * s, a.y * s};
        }
        Vec3 operator+(Vec3 a, Vec3 b) noexcept {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }
        Vec3 operator-(Vec3 a, Vec3 b) noexcept {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        Vec3 operator*(Vec3 a, double s) noexcept {
            return {a.x * s, a.y * s, a.z * s};
        }
        Vec3 mix(Vec3 a, Vec3 b, double t) noexcept {
            return a + (b - a) * t;
        }
        double length(Vec2 a) noexcept {
            return std::hypot(a.x, a.y);
        }

        void follow(Vec2 &position,
            Vec2 &velocity,
            Vec2 target,
            double dt,
            double decay,
            double reach) noexcept {
            // Clamp distance before the closed form spring update. A new target changes
            // acceleration, not the displayed position or the incoming velocity.
            Vec2 delta = position - target;
            const double distance = length(delta);
            if (distance > reach) {
                delta = delta * (reach / distance);
                target = position - delta;
            }
            const Vec2 tangent = velocity + delta * FOLLOW_RATE;
            position = target + (delta + tangent * dt) * decay;
            velocity = (velocity - tangent * (FOLLOW_RATE * dt)) * decay;
        }

        bool usable(Box box) noexcept {
            return is_usable(box) && std::isfinite(box.x + box.w) && std::isfinite(box.y + box.h);
        }

        Vec3 point(const auto &basis, double x, double y) noexcept {
            return basis.origin + basis.u * x + basis.v * y;
        }

        Vec3 pelvis_at(Vec3 stance, double size, double facing, double grip) noexcept {
            return stance + Vec3{.025 * facing * size, -std::lerp(.445, .39, grip) * size, 0};
        }

        Vec3 hip_at(Vec3 pelvis, double size, double facing, double side) noexcept {
            return pelvis + Vec3{.045 * side * facing * size, 0, .05 * side * size};
        }

        PieceRelocation::Limb limb(
            Vec3 root, Vec3 tip, double a, double b, double bend, bool free_tip = false) noexcept {
            const Vec3 delta = tip - root;
            const double planar = std::hypot(delta.x, delta.y);
            const double requested = std::hypot(planar, delta.z);
            if (!free_tip && requested == 0) return {root, root + Vec3{a * bend, 0, 0}, tip};
            const double distance =
                free_tip ? std::clamp(requested, std::abs(a - b), a + b) : requested;
            if (distance != requested) {
                tip =
                    root + (requested > 0 ? delta * (distance / requested) : Vec3{0, distance, 0});
            }
            const double along =
                std::clamp((a * a - b * b + distance * distance) / (2 * distance), -a, a);
            const double side = bend * std::sqrt(std::max(0.0, a * a - along * along));
            const Vec3 normal =
                planar > 0 ? Vec3{delta.y / planar, -delta.x / planar, 0} : Vec3{1, 0, 0};
            const Vec3 axis = requested > 0 ? delta * (along / requested) : Vec3{0, along, 0};
            return {root, root + axis + normal * side, tip};
        }

    }  // namespace

    void PieceRelocation::clear() noexcept {
        m_count = m_seated = m_face_count = m_worker_count = 0;
        m_installed = 0;
        m_until_dispatch = 0;
    }

    void PieceRelocation::reset(Box body, std::uint64_t seed, double worker_size) noexcept {
        clear();
        m_body = body;
        m_seed = seed;
        m_worker_size = worker_size;
    }

    void PieceRelocation::add(Rect a, Rect b) noexcept {
        auto &pack = m_packs[m_count++];
        pack = {};
        pack.rects[0] = pack.hull = a;
        pack.rect_count = 1;
        pack.area = (a.right - a.left) * (a.bottom - a.top);
        if (b.right > b.left && b.bottom > b.top) {
            pack.rects[pack.rect_count++] = b;
            pack.hull = {std::min(a.left, b.left),
                std::min(a.top, b.top),
                std::max(a.right, b.right),
                std::max(a.bottom, b.bottom)};
            pack.area += (b.right - b.left) * (b.bottom - b.top);
        }
        pack.position = m_landing;
        pack.crew = std::max(pack.hull.right - pack.hull.left, pack.hull.bottom - pack.hull.top) >
                                5.6 * m_height ||
                            pack.area > 11 * m_height * m_height
                        ? 2
                        : 1;
        // The first slice is the bearing edge. The glove pair touches it, not the hull of a step.
        const double width = a.right - a.left;
        const double inset = std::min(17.0, .27 * width);
        const double pair = std::min({5.0, .12 * m_height, .10 * width});
        pack.carriers[0].socket_x = a.left + inset;
        pack.carriers[0].other_x = a.left + inset - pair;
        pack.carriers[1].socket_x = a.right - inset;
        pack.carriers[1].other_x = a.right - inset + pair;
        pack.carriers[1].facing = -1;
    }

    void PieceRelocation::cut(Box source) noexcept {
        m_origin = m_landing = {source.x, source.y};
        m_landing_velocity = {};
        m_haul_distance = 0;
        const double width = source.w, height = source.h;
        // Hidden workers retain a positive simulation scale, so transport still completes.
        m_height = .075 * std::min(width, height) * (m_worker_size == 0 ? 1 : m_worker_size);
        auto rng = m_seed;
        const auto draw = [&](double low, double high) {
            return std::lerp(low, high, unit_draw(rng));
        };
        const auto columns = width < 96 || height < 56 ? 1U
                             : width < 380             ? 2U
                             : width < 720             ? 2U + unsigned(splitmix64(rng) % 2)
                                                       : 3U + unsigned(splitmix64(rng) % 2);
        std::array<double, MAX_COLUMNS> weights;
        std::array<double, MAX_COLUMNS + 1> edges;
        double total = 0;
        for (unsigned i = 0; i < columns; ++i) total += weights[i] = draw(.55, 1.85);
        edges[0] = 0;
        for (unsigned i = 0; i < columns; ++i)
            edges[i + 1] = edges[i] + width * (.60 / columns + .40 * weights[i] / total);
        edges[columns] = width;

        // Two independent volumes on larger cuts. Crowns bridge uneven lower bays;
        // a column shoe and its mating upper panel are each one rigid stepped pack.
        const auto split = columns >= 3 ? 1U + unsigned(splitmix64(rng) % (columns - 1)) : columns;
        for (unsigned first = 0; first < columns;) {
            const unsigned last = first == 0 ? split : columns;
            const double head = height >= 125 && last - first > 1 ? height * draw(.20, .39) : 0;
            for (unsigned i = first; i < last; ++i) {
                const double l = edges[i], r = edges[i + 1];
                const double sill = height - (height - head) * draw(.08, .29);
                if (columns > 1 && unit_draw(rng) < .55) {
                    const bool left = unit_draw(rng) < .5;
                    const double seam = std::lerp(l, r, draw(.22, .78));
                    const double shoulder = std::lerp(sill, head, draw(.14, .36));
                    add({l, sill, r, height}, {left ? l : seam, shoulder, left ? seam : r, sill});
                    add({left ? seam : l, shoulder, left ? r : seam, sill}, {l, head, r, shoulder});
                } else {
                    add({l, sill, r, height});
                    add({l, head, r, sill});
                }
            }
            if (head > 0) add({edges[first], 0, edges[last], head});
            first = last;
        }

        // Bearings come from actual positive-length shared edges. Hull overlap would
        // incorrectly let a stepped pack bear on the ledge it only surrounds.
        unsigned bearings = 0;
        for (std::size_t i = 0; i < m_count; ++i) {
            auto &pack = m_packs[i];
            for (std::size_t j = 0; j < i; ++j) {
                const auto &lower = m_packs[j];
                for (std::size_t a = 0; a < pack.rect_count; ++a) {
                    for (std::size_t b = 0; b < lower.rect_count; ++b) {
                        const auto &p = pack.rects[a], &q = lower.rects[b];
                        if (p.bottom == q.top && p.left < q.right && q.left < p.right)
                            pack.supports |= 1U << j;
                    }
                }
            }
            bearings |= pack.supports;
        }
        m_closure = m_count - 1;
        for (std::size_t i = 0; i < m_count; ++i)
            if (!(bearings & (1U << i)) && m_packs[i].area > m_packs[m_closure].area) m_closure = i;
    }

    PieceRelocation::Basis PieceRelocation::basis(const Pack &pack) const noexcept {
        const Vec3 origin{pack.position.x, pack.position.y, 0};
        if (pack.tilt == 0 && pack.roll == 0) return {origin, {1, 0, 0}, {0, 1, 0}};
        const double c = std::cos(pack.roll), s = std::sin(pack.roll);
        const double pinch = std::cos(pack.tilt), pitch = std::sin(pack.tilt);
        const Vec3 u{c, s, -.085 * pitch}, v{-s * pinch, c * pinch, -.32 * pitch};
        const double heel = (pack.hull.left + pack.hull.right) * .5, bottom = pack.hull.bottom;
        return {origin + Vec3{heel, bottom, 4 * pitch} - u * heel - v * bottom, u, v};
    }

    Vec3 PieceRelocation::station(
        const Pack &pack, const Carrier &carrier, const Basis &pose) const noexcept {
        const auto &rail = pack.rects[0];
        const double grip_y = rail.bottom - std::min(6.0, .27 * (rail.bottom - rail.top));
        const Vec3 grip = point(pose, carrier.socket_x, grip_y);
        Vec3 foot =
            grip + Vec3{-.366 * carrier.facing * m_height, .565 * m_height, .095 * m_height};
        if (pack.job == Job::Approach) {
            foot = foot + Vec3{-carrier.facing * (.20 * m_height + 20), 9, 0} *
                              (1 - smoothstep(0, .115, pack.age));
        } else if (pack.job == Job::Latch || pack.job == Job::Fasten) {
            const double reach =
                smoothstep(0, .055, pack.age) * (1 - smoothstep(.09, FASTEN, pack.age));
            foot.x -= carrier.facing * .16 * m_height * reach;
        } else if (pack.job == Job::Depart) {
            foot = foot + Vec3{-carrier.facing * 1.14 * m_height, .23 * m_height, 0} *
                              smoothstep(0, DEPART, pack.age);
        }
        return foot;
    }

    void PieceRelocation::reserve() noexcept {
        unsigned travelling = m_installed;
        std::size_t busy = 0;
        for (std::size_t i = 0; i < m_count; ++i) {
            const auto &pack = m_packs[i];
            busy += pack.job >= Job::Approach && pack.job <= Job::Depart;
            if (pack.job >= Job::Carry) travelling |= 1U << i;
        }
        if (busy >= MAX_TRIPS) return;
        for (std::size_t i = 0; i < m_count; ++i) {
            auto &pack = m_packs[i];
            if (pack.job != Job::Waiting || (pack.supports & travelling) != pack.supports) continue;
            pack.job = Job::Approach;
            pack.age = 0;
            const auto pose = basis(pack);
            for (std::size_t k = 0; k < pack.crew; ++k) {
                auto &carrier = pack.carriers[k];
                carrier.stance = station(pack, carrier, pose);
                for (std::size_t j = 0; j < carrier.boots.size(); ++j) {
                    const double side = j == 0 ? 1 : -1;
                    carrier.boots[j] = {
                        .sole =
                            carrier.stance +
                            Vec3{side * carrier.facing * .15 * m_height, 0, side * .05 * m_height},
                        .start = {},
                        .goal = {}};
                }
            }
            m_until_dispatch = STAGGER;
            return;
        }
    }

    double PieceRelocation::grip_amount(const Pack &pack) noexcept {
        if (pack.job == Job::Approach) return smoothstep(.051, .115, pack.age);
        if (pack.job == Job::Depart) return 1 - smoothstep(0, .065, pack.age);
        return 1;
    }

    void PieceRelocation::update_carriers(Pack &pack, double dt) noexcept {
        const auto pose = basis(pack);
        const double s = m_height;
        const double grip = grip_amount(pack);
        const double shortest = (SHIN - THIGH) * s, longest = (SHIN + THIGH) * s;
        for (std::size_t k = 0; k < pack.crew; ++k) {
            auto &carrier = pack.carriers[k];
            const Vec3 foot = station(pack, carrier, pose);
            const Vec3 pelvis = pelvis_at(foot, s, carrier.facing, grip);
            const Vec3 velocity = (foot - carrier.stance) * (1 / dt);
            carrier.stance = foot;
            const double speed = std::hypot(velocity.x, velocity.y);
            const double stride =
                std::clamp((.39 + .025 * double(k)) * s / std::max(speed, 1.0), .024, .12);
            const double lead = std::min(stride * .75, .23 * s / std::max(speed, 1.0));
            for (std::size_t j = 0; j < carrier.boots.size(); ++j) {
                auto &boot = carrier.boots[j];
                const double side = j == 0 ? 1 : -1;
                const Vec3 want = foot + Vec3{side * carrier.facing * .15 * s, 0, side * .05 * s};
                const Vec3 goal = want + velocity * lead;
                if (boot.duration > 0) {
                    boot.age = std::min(boot.duration, boot.age + dt);
                    const double progress = boot.age / boot.duration;
                    boot.goal = mix(boot.goal, goal, std::min(1.0, dt * (1 + 34 * progress)));
                    boot.sole = mix(boot.start, boot.goal, smoothstep(0, 1, progress));
                    boot.sole.y -= .13 * s * std::sin(std::numbers::pi * progress);
                    if (progress >= 1) boot.duration = 0;
                }
                const Vec3 hip = hip_at(pelvis, s, carrier.facing, side);
                const Vec3 leg = boot.sole - hip;
                const double length2 = leg.x * leg.x + leg.y * leg.y + leg.z * leg.z;
                const bool outside = length2 < shortest * shortest || length2 > longest * longest;
                if (outside) {
                    // An unreachable plant has to peel. A swing can reach toward its
                    // goal, but the published sole never extends the leg.
                    if (length2 > 0) {
                        const double distance = std::sqrt(length2);
                        boot.sole =
                            hip + leg * (std::clamp(distance, shortest, longest) / distance);
                    } else {
                        boot.sole = hip + Vec3{0, shortest, 0};
                    }
                }
                if (boot.duration > 0) continue;
                const double drift = std::hypot(boot.sole.x - want.x, boot.sole.y - want.y);
                const double peel = carrier.boots[1 - j].duration > 0 ? .28 : .15;
                if (outside || drift > peel * s) {
                    boot.start = boot.sole;
                    boot.goal = goal;
                    boot.age = 0;
                    boot.duration = stride;
                }
            }
        }
    }

    void PieceRelocation::advance(double dt, bool held) noexcept {
        const Vec2 destination{m_body.x, m_body.y};
        const double decay = std::exp(-FOLLOW_RATE * dt);
        const double reach = std::max(
            HAUL_SPEED / FOLLOW_RATE, 2 * m_haul_distance / (FOLLOW_RATE * LONG_HAUL_SECONDS));
        follow(m_landing, m_landing_velocity, destination, dt, decay, reach);
        if (length(m_landing - destination) <= .01 && length(m_landing_velocity) <= .25) {
            m_landing = destination;
            m_landing_velocity = {};
        }

        for (std::size_t i = 0; i < m_count; ++i) {
            auto &p = m_packs[i];
            if (p.job == Job::Waiting) continue;
            if (p.job >= Job::Fasten) p.position = m_landing;
            if (p.job == Job::Done) continue;
            p.age += dt;
            switch (p.job) {
                case Job::Approach:
                    if (p.age >= .115) {
                        p.job = Job::Latch;
                        p.age = 0;
                    }
                    break;
                case Job::Latch:
                    if (p.age >= LATCH) {
                        p.job = Job::Carry;
                        p.age = 0;
                    }
                    break;
                case Job::Carry: {
                    const bool supported =
                        (p.supports & m_installed) == p.supports && !(i == m_closure && held);
                    const double travel = length(m_landing - p.position);
                    const double margin =
                        supported ? 0 : std::min(1.0, length(m_landing - m_origin) / 24);
                    const auto lane = static_cast<double>(i % MAX_TRIPS);
                    // No staging props or unowned floating packs: wait below the bearing
                    // line with this same crew, and take the latest destination on every tick.
                    const Vec2 aim =
                        m_landing +
                        (supported ? m_landing_velocity * (2 / FOLLOW_RATE)
                                   : Vec2{(i % 2 == 0 ? -1 : 1) * (15 + 5 * lane), 23 + 6 * lane} *
                                         margin);
                    const double rise = smoothstep(0, LIFT, p.age);
                    const double desired = rise * ((.34 + .072 * std::sin(double(i) * 2.39)) *
                                                          smoothstep(0, 70, travel) +
                                                      (supported ? 0 : .17 * margin));
                    p.tilt = std::lerp(p.tilt, desired, std::min(1.0, 18 * dt));
                    const double sway = p.velocity.x / (210 + std::abs(p.velocity.x));
                    p.roll = std::lerp(p.roll, .24 * p.tilt * sway, std::min(1.0, 18 * dt));
                    follow(p.position, p.velocity, aim, dt, decay, reach);
                    if (supported && p.age >= LIFT && length(p.position - m_landing) < 2.2 &&
                        length(p.velocity - m_landing_velocity) < 112) {
                        p.job = Job::Fit;
                        p.age = 0;
                    }
                    break;
                }
                case Job::Fit: {
                    follow(p.position, p.velocity, m_landing, dt, decay, reach);
                    const double gap = length(p.position - m_landing);
                    if (gap > 9 || (i == m_closure && held)) {
                        p.job = Job::Carry;
                        p.age = LIFT;
                        break;
                    }
                    const double flatten = std::max(0.0, 1 - 28 * dt);
                    p.tilt *= flatten;
                    p.roll *= flatten;
                    const double diagonal =
                        std::hypot(p.hull.right - p.hull.left, p.hull.bottom - p.hull.top);
                    // Bound the visible final correction even on a very wide client.
                    const double correction =
                        gap + 4 * p.tilt + diagonal * (p.tilt + std::abs(p.roll));
                    if (p.age >= FIT && correction < .65) {
                        p.position = m_landing;
                        p.velocity = {};
                        p.tilt = p.roll = 0;
                        p.job = Job::Fasten;
                        p.age = 0;
                        m_installed |= 1U << i;
                        ++m_seated;
                    }
                    break;
                }
                case Job::Fasten:
                    if (p.age >= FASTEN) {
                        p.job = Job::Depart;
                        p.age = 0;
                    }
                    break;
                case Job::Depart:
                    if (p.age >= DEPART) {
                        p.job = Job::Done;
                        continue;
                    }
                    break;
                case Job::Waiting:
                case Job::Done:
                    break;
            }
            update_carriers(p, dt);
        }
        m_until_dispatch -= dt;
        if (m_until_dispatch <= 0 && m_seated < m_count) reserve();
    }

    void PieceRelocation::publish() noexcept {
        m_face_count = m_worker_count = 0;
        std::array<std::size_t, MAX_TRIPS> moving;
        std::size_t moving_count = 0;
        const auto faces_for = [&](std::size_t i, Placement placement) {
            const auto &pack = m_packs[i];
            const auto pose = basis(pack);
            for (std::size_t j = 0; j < pack.rect_count; ++j) {
                const auto &r = pack.rects[j];
                const double w = r.right - r.left, h = r.bottom - r.top;
                m_faces[m_face_count++] = {{r.left, r.top, w, h},
                    point(pose, r.left, r.top),
                    pose.u * w,
                    pose.v * h,
                    i,
                    placement};
            }
        };
        // The stationary source is behind seated work and both are behind travelling faces.
        for (std::size_t i = 0; i < m_count; ++i) {
            const auto &p = m_packs[i];
            if (p.job <= Job::Latch) faces_for(i, Placement::Source);
        }
        for (std::size_t i = 0; i < m_count; ++i) {
            const auto &p = m_packs[i];
            if (p.job >= Job::Fasten) faces_for(i, Placement::Seated);
            if (p.job != Job::Carry && p.job != Job::Fit) continue;
            const double bottom = p.hull.bottom + p.position.y;
            std::size_t pos = moving_count++;
            while (pos > 0) {
                const auto &previous = m_packs[moving[pos - 1]];
                if (previous.hull.bottom + previous.position.y <= bottom) break;
                moving[pos] = moving[pos - 1];
                --pos;
            }
            moving[pos] = i;
        }
        for (std::size_t i = 0; i < moving_count; ++i) faces_for(moving[i], Placement::Carried);

        for (std::size_t i = 0; i < m_count; ++i) {
            const auto &p = m_packs[i];
            if (p.job < Job::Approach || p.job > Job::Depart) continue;
            const auto pose = basis(p);
            const double s = m_height;
            const auto &rail = p.rects[0];
            const double grip_y = rail.bottom - std::min(6.0, .27 * (rail.bottom - rail.top));
            const double reach = grip_amount(p);
            const double lean = std::lerp(.06, .165, reach);
            for (std::size_t j = 0; j < p.crew; ++j) {
                const auto &c = p.carriers[j];
                const double f = c.facing;
                auto &w = m_workers[m_worker_count++];
                w.piece = i;
                w.height = s;
                w.facing = f;
                w.visibility = p.job == Job::Approach ? smoothstep(0, .11, p.age)
                               : p.job == Job::Depart ? 1 - smoothstep(.13, DEPART, p.age)
                                                      : 1;
                w.gripping = p.job >= Job::Latch && p.job <= Job::Fasten;
                w.pelvis = pelvis_at(c.stance, s, f, reach);
                w.neck = w.pelvis + Vec3{(lean - .025) * f * s, -.31 * s, 0};
                w.head = w.neck + Vec3{.02 * f * s, -.10 * s, .024 * s};
                for (std::size_t k = 0; k < 2; ++k) {
                    const double side = k == 0 ? 1 : -1;
                    const auto grip = point(pose, k == 0 ? c.socket_x : c.other_x, grip_y);
                    const auto hand =
                        c.stance + Vec3{(k == 0 ? .19 : -.09) * f * s, -.45 * s, side * .06 * s};
                    const auto shoulder = w.neck + Vec3{(k == 0 ? 0 : -.075) * f * s,
                                                       (k == 0 ? .065 : .093) * s,
                                                       side * .05 * s};
                    // Free hands can reach toward a socket without stretching; a
                    // latched hand keeps the exact socket on the posed face.
                    const auto tip = w.gripping ? grip : mix(hand, grip, reach);
                    w.arms[k] = limb(shoulder, tip, .22 * s, .24 * s, -f, !w.gripping);
                    const auto hip = hip_at(w.pelvis, s, f, side);
                    w.legs[k] = limb(hip, c.boots[k].sole, THIGH * s, SHIN * s, f);
                    w.planted[k] = c.boots[k].duration == 0;
                }
            }
        }
    }

    void PieceRelocation::step(Box body, double elapsed, bool held) noexcept {
        const Box previous = m_body;
        if (!usable(body) || !usable(previous) || body.w != previous.w || body.h != previous.h ||
            !std::isfinite(elapsed) || elapsed < 0 || elapsed >= STALL_SECONDS) {
            m_body = body;
            clear();
            return;
        }
        const bool translated = body.x != previous.x || body.y != previous.y;
        double haul_distance = m_count == 0 ? 0 : m_haul_distance;
        if (translated) {
            const Vec2 destination{body.x, body.y};
            const Vec2 origin = m_count == 0 ? Vec2{previous.x, previous.y} : m_origin;
            const Vec2 landing = m_count == 0 ? origin : m_landing;
            const auto include_distance = [&](Vec2 point) {
                const Vec2 delta = destination - point;
                const double distance = length(delta);
                if (!std::isfinite(delta.x) || !std::isfinite(delta.y) ||
                    !std::isfinite(distance))
                    return false;
                haul_distance = std::max(haul_distance, distance);
                return true;
            };
            if (!include_distance(origin) || !include_distance(landing)) return;
            for (std::size_t i = 0; i < m_count; ++i) {
                const auto &p = m_packs[i];
                if ((p.job == Job::Carry || p.job == Job::Fit) &&
                    !include_distance(p.position))
                    return;
            }
            const double reach = std::max(HAUL_SPEED / FOLLOW_RATE,
                2 * haul_distance / (FOLLOW_RATE * LONG_HAUL_SECONDS));
            if (!std::isfinite(reach)) return;
        }
        // Keep the last publishable pose when this retarget cannot be represented. The
        // publication boundary sees the body mismatch and interrupts rather than retargeting natively.
        m_body = body;
        if (!active()) {
            if (!translated) return;
            cut(previous);
        }
        if (translated) m_haul_distance = haul_distance;
        // Corrections during crew exit keep the completed load seated and retarget
        // its shared landing pose. Only an idle move creates new reservations.
        if (elapsed > 0) {
            const double time = std::min(elapsed, 8 * TICK);
            const int steps = static_cast<int>(std::ceil(time / TICK));
            for (int i = 0; i < steps; ++i) advance(time / steps, held);
        }
        if (m_seated == m_count && m_landing.x == body.x && m_landing.y == body.y &&
            std::all_of(m_packs.begin(),
                m_packs.begin() + static_cast<std::ptrdiff_t>(m_count),
                [](const Pack &p) { return p.job == Job::Done; })) {
            clear();
            return;
        }
        publish();
    }

}  // namespace hyprimate::construction::studies
