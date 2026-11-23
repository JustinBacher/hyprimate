#include <algorithm>
#include <cmath>
#include <numbers>

#include "EdgeStretch.hpp"

namespace hyprimate::construction::studies {
    namespace {
        // From the resize-stretch-02 preview: alternating steps, two independently
        // solved gloves, and different counterweight shapes for pull/push/hold.
        constexpr std::array EDGES{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};
        constexpr std::array WALK{.445, .016, .062};
        constexpr double RELEASE_IDLE = .085;
        constexpr double ACTION_SPEED = 7;
        constexpr double MAX_SAMPLE_SPEED = 2400;

        constexpr std::size_t index(Edge edge) noexcept {
            return static_cast<std::size_t>(edge);
        }
        constexpr bool vertical_edge(Edge edge) noexcept {
            return edge == Edge::Left || edge == Edge::Right;
        }
        constexpr bool near_edge(Edge edge) noexcept {
            return edge == Edge::Left || edge == Edge::Top;
        }
        constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }
        constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        constexpr Vec3 operator*(Vec3 a, double scale) noexcept {
            return {a.x * scale, a.y * scale, a.z * scale};
        }
        constexpr Vec3 operator/(Vec3 a, double scale) noexcept {
            return {a.x / scale, a.y / scale, a.z / scale};
        }
        double length(Vec3 v) noexcept {
            return std::hypot(v.x, v.y, v.z);
        }
        Vec3 toward(Vec3 from, Vec3 to, double distance) noexcept {
            const Vec3 delta = to - from;
            const double gap = length(delta);
            return gap <= distance ? to : from + delta * (distance / gap);
        }

        bool usable(Box b) noexcept {
            return is_usable(b) && std::isfinite(b.x + b.w) && std::isfinite(b.y + b.h);
        }

        bool contains(Box b, Vec3 p) noexcept {
            return p.x >= b.x && p.x <= b.x + b.w && p.y >= b.y && p.y <= b.y + b.h;
        }

        double edge_line(Box b, Edge edge) noexcept {
            return std::array{b.x, b.x + b.w, b.y, b.y + b.h}[index(edge)];
        }

        // Keep the half-pixel inset only where the worker can reach it.
        Vec3 socket(Box body, Edge edge, double fraction, double size) noexcept {
            const bool vertical = vertical_edge(edge);
            const double inset = std::min({.5, .25 * (vertical ? body.w : body.h), size * (.5 / 39)});
            const double line = edge_line(body, edge) + (near_edge(edge) ? inset : -inset);
            if (vertical) return {line, body.y + body.h * fraction, 0};
            return {body.x + body.w * fraction, line, 0};
        }

        Vec3 second_socket(Vec3 primary, Box body, Edge edge, double size, double facing) noexcept {
            if (vertical_edge(edge))
                primary.y += std::min(.085 * size, (body.y + body.h - primary.y) * .5);
            else if (facing > 0)
                primary.x -= std::min(.095 * size, (primary.x - body.x) * .5);
            else
                primary.x += std::min(.095 * size, (body.x + body.w - primary.x) * .5);
            return primary;
        }

        Vec3 clip_station(Vec3 p, Box body, Box area, Edge edge, double size) noexcept {
            const double line = edge_line(body, edge);
            if (vertical_edge(edge))
                p.x = std::clamp(p.x, line - 1.18 * size, line + 1.18 * size);
            else
                p.y = std::clamp(p.y, line - .71 * size, line + 1.59 * size);
            if (!usable(area)) return p;
            // Prefer screen room like the filmed apron, but don't pull a boot so
            // far away at a screen boundary that reaching the rim becomes impossible.
            const double margin_x = std::min(16., area.w * .5);
            const double top = area.y + std::min(1.08 * size + 5, area.h * .5);
            const double bottom = area.y + area.h - std::min(10., area.h * .5);
            const Vec3 inside{std::clamp(p.x, area.x + margin_x, area.x + area.w - margin_x),
                std::clamp(p.y, top, bottom),
                p.z};
            return toward(p, inside, .10 * size);
        }

        Vec3 station_for(Vec3 grip,
            Box body,
            Box area,
            Edge edge,
            double size,
            double facing,
            EdgeStretchAction action,
            unsigned variant) noexcept {
            double across = .29;
            if (action == EdgeStretchAction::Pull) across = .20;
            if (action == EdgeStretchAction::Push) across = .43;
            constexpr std::array DOWN{.64, .64, .34, .77};
            return clip_station(grip + Vec3{-facing * size * (across + .012 * (variant % 3)),
                                           size * DOWN[index(edge)],
                                           size * (1.8 / 39)},
                body,
                area,
                edge,
                size);
        }

        Vec3 apron(Vec3 foot,
            Box body,
            Box area,
            Edge edge,
            double lane,
            double size,
            double travel) noexcept {
            const double outward = near_edge(edge) ? -1 : 1;
            const Vec3 offset = vertical_edge(edge)
                                    ? Vec3{outward * travel * .24, lane * travel, 0}
                                    : Vec3{lane * travel, outward * travel * .18, 0};
            return clip_station(foot + offset, body, area, edge, size);
        }

        struct Joint {
            Vec3 bend, end;
            bool exact;
        };
        Joint solve(Vec3 root, Vec3 wanted, double upper, double lower, double bend) noexcept {
            const Vec3 delta = wanted - root;
            const double actual = length(delta);
            const double planar = std::hypot(delta.x, delta.y);
            const Vec3 axis = actual > 0 ? delta * (1 / actual) : Vec3{0, 1, 0};
            const Vec3 side = planar > 0
                                  ? Vec3{bend * delta.y / planar, -bend * delta.x / planar, 0}
                                  : Vec3{bend, 0, 0};
            const double reach =
                std::clamp(actual, std::abs(upper - lower) + .00001 * upper, upper + lower - .00001 * upper);
            const double along = (reach * reach + upper * upper - lower * lower) / (2 * reach);
            const double rise = std::sqrt(std::max(0., upper * upper - along * along));
            return {root + axis * along + side * rise,
                reach == actual ? wanted : root + axis * reach,
                reach == actual};
        }

        std::array<double, 3> work_shape(
            EdgeStretchAction action, unsigned variant, double swing) noexcept {
            const double step = std::abs(swing), bias = .012 * (variant % 3);
            switch (action) {
                case EdgeStretchAction::Pull:
                    return {.345 + .032 * step, .022, -.145 + bias - .028 * step};
                case EdgeStretchAction::Push:
                    return {.347 + .035 * step, .054, .28 - bias + .028 * step};
                case EdgeStretchAction::Hold:
                    return {.413, .008, .061};
            }
            return WALK;
        }

        // Remove common translation before measuring outward work. A fixed far edge
        // during left/top resize must not produce an artificial opposite-side job.
        std::array<double, 4> edge_motion(Box before, Box now) noexcept {
            if (!usable(before)) return {};
            const double dx = now.x - before.x, dy = now.y - before.y;
            const double dr = dx + (now.w - before.w), db = dy + (now.h - before.h);
            const auto common = [](double near, double far) noexcept {
                return std::clamp(0., std::min(near, far), std::max(near, far));
            };
            const double x = common(dx, dr), y = common(dy, db);
            return {x - dx, dr - x, y - dy, db - y};
        }

        bool toward_leading_corner(EdgeSet targets, Edge edge) noexcept {
            const Edge first = vertical_edge(edge) ? Edge::Top : Edge::Left;
            const Edge last = vertical_edge(edge) ? Edge::Bottom : Edge::Right;
            return targets.has(first) != targets.has(last) ? targets.has(first) : near_edge(edge);
        }
    }  // namespace

    void EdgeStretch::enlist(
        Job &job, Edge edge, unsigned slot, const Box &area, bool leading_corner) const noexcept {
        const auto i = index(edge);
        std::uint64_t seed = m_seed ^ ((i + 1) * 0x9E3779B97F4A7C15ULL) ^ (slot * 0x85EBCA77ULL);
        // Keep the two match-marked stations across a reversal, even if the other
        // edge of the grabbed corner is released independently.
        const double fraction =
            slot == 0 ? .20 + .09 * unit_draw(seed) : .63 + .10 * unit_draw(seed);
        const double anchored = (leading_corner ? 0 : .38) + .62 * fraction;
        const double size = .075 * std::min(m_body.w, m_body.h) *
            (m_worker_size == 0 ? 1 : m_worker_size) * ((37 + 4 * unit_draw(seed)) / 39);
        const Vec3 grip = socket(m_body, edge, anchored, size);
        if (usable(area) && !contains(area, grip)) return;

        job = {};
        job.live = true;
        job.fraction = anchored;
        job.variant = static_cast<unsigned>((slot + i + m_seed) % 4);
        job.size = size;
        job.speed = 206 + 40 * unit_draw(seed);
        job.delay = .007 + .044 * slot + .026 * unit_draw(seed);
        job.lane = unit_draw(seed) < .5 ? -1 : 1;
        job.approach = 18 + 10 * unit_draw(seed);
        job.exit_duration = .24 + .09 * unit_draw(seed);
        job.stride = unit_draw(seed);
        job.facing = vertical_edge(edge) ? (near_edge(edge) ? 1 : -1) : job.lane;
        if (usable(area) && vertical_edge(edge) &&
            (near_edge(edge) ? grip.x < area.x + 34 : grip.x > area.x + area.w - 34))
            job.facing = -job.facing;
        job.station = station_for(
            grip, m_body, area, edge, job.size, job.facing, EdgeStretchAction::Hold, job.variant);
        if (usable(area)) {
            const bool vertical = vertical_edge(edge);
            const double tangent = vertical ? job.station.y - area.y : job.station.x - area.x;
            const double extent = vertical ? area.h : area.w;
            if (tangent < std::min(67., extent * .5))
                job.lane = 1;
            else if (tangent > extent - std::min(67., extent * .5))
                job.lane = -1;
        }
        job.origin = job.foot =
            apron(job.station, m_body, area, edge, job.lane, job.size, job.approach);
        job.shape = WALK;
        for (std::size_t i_limb = 0; i_limb < 2; ++i_limb) {
            const double side = i_limb == 0 ? 1 : -1;
            job.boots[i_limb].sole =
                job.foot + Vec3{side * job.facing * .125 * job.size, 0, side * .05 * job.size};
            job.gloves[i_limb] = job.foot + Vec3{job.facing * job.size * (i_limb == 0 ? .18 : -.06),
                                                job.size * (i_limb == 0 ? -.46 : -.48),
                                                job.size * (i_limb == 0 ? .08 : -.065)};
        }
    }

    void EdgeStretch::pose(Job &job,
        Edge edge,
        unsigned slot,
        EdgeStretchAction action,
        bool assigned,
        const Box &area,
        const Timing &timing,
        EdgeWorkerPose &out) const noexcept {
        const double elapsed = timing.elapsed;
        const double s = job.size, f = job.facing;
        const Vec3 grip = socket(m_body, edge, job.fraction, s);
        Vec3 goal;
        bool seeking = false;
        if (!assigned) {
            if (!job.leaving) {
                job.leaving = true;
                job.age = 0;
                job.leave_presence = job.presence;
                job.origin = job.foot;
                job.exit_to = apron(job.foot, m_body, area, edge, job.lane, s, job.approach + 6);
            }
            job.age += elapsed;
            const double leave = job.age / job.exit_duration;
            job.presence = job.leave_presence * (1 - smoothstep(.28, 1., leave));
            if (leave >= 1 || job.presence == 0) {
                job.live = false;
                return;
            }
            goal = job.origin + (job.exit_to - job.origin) * smoothstep(0, 1, leave);
        } else {
            const Vec3 station = station_for(grip, m_body, area, edge, s, f, action, job.variant);
            if (job.leaving) {
                job.leaving = false;
                job.age = job.delay + (job.presence > 0 ? .10 : 0);
                job.origin = job.foot;
                job.station_velocity = {};
                job.station = station;
            }
            job.age += elapsed;
            const double age = job.age - job.delay;
            if (elapsed > 0) {
                const Vec3 displacement =
                    toward({}, station - job.station, elapsed * MAX_SAMPLE_SPEED);
                const Vec3 velocity = displacement / elapsed;
                job.station_velocity =
                    job.station_velocity + (velocity - job.station_velocity) * timing.lead;
            }
            job.station = station;
            // A pixel-sized lookahead can exceed a tiny fitter's entire arm reach.
            // Reserve reach for the planted stance and apron adjustment as well.
            const Vec3 lead = toward({}, job.station_velocity * .037, .12 * s);
            // station_for already applies the apron nudge. Applying it again here
            // would spend another tenth-height of the same arm's reach at screen edges.
            goal = age <= 0 ? job.origin : station + lead;
            if (elapsed > 0) job.presence = std::max(job.presence, smoothstep(0, .115, age));
            seeking = age > .06 && length(job.foot - goal) < .57 * s;
        }

        Vec3 velocity{};
        double ground_speed = 0;
        if (elapsed > 0) {
            const Vec3 old = job.foot;
            const double gap = length(goal - old);
            const double speed = job.speed + 96 * smoothstep(7, 25, gap);
            const double limit = elapsed * speed;
            if (gap <= std::min(.08, limit))
                job.foot = goal;
            else
                job.foot = old + ((goal - old) / gap) * std::min(limit, gap * timing.follow);
            velocity = (job.foot - old) / elapsed;
            const double travel = std::hypot(job.foot.x - old.x, job.foot.y - old.y);
            ground_speed = travel / elapsed;
            job.stride += travel / (.82 * s);
            job.swing =
                std::sin(2 * std::numbers::pi * job.stride) * smoothstep(0, 88, ground_speed);
            const auto target = seeking ? work_shape(action, job.variant, job.swing) : WALK;
            for (std::size_t i = 0; i < job.shape.size(); ++i)
                job.shape[i] = std::lerp(job.shape[i], target[i], timing.blend);
        }

        out.edge = edge;
        out.slot = slot;
        out.variant = job.variant;
        out.height = s;
        out.facing = f;
        out.action = action;
        const double across = vertical_edge(edge) ? job.foot.x : job.foot.y - .44 * s;
        out.opacity = job.presence *
            (1 - smoothstep(1.0 * s, 1.21 * s, std::abs(across - edge_line(m_body, edge))));
        out.hip = job.foot + Vec3{f * job.shape[1] * s, -job.shape[0] * s, 0};
        out.neck = out.hip + Vec3{f * job.shape[2] * s, -.31 * s, 0};
        const std::array targets{grip, second_socket(grip, m_body, edge, s, f)};
        bool gripping = false;
        for (std::size_t i = 0; i < 2; ++i) {
            const bool near = i == 0;
            const Vec3 shoulder =
                out.neck +
                Vec3{near ? 0 : -.075 * f * s, (near ? .065 : .093) * s, (near ? .05 : -.06) * s};
            const Vec3 free =
                job.foot + (near ? Vec3{f * s * (.18 + .18 * job.swing), -.46 * s, .08 * s}
                                 : Vec3{f * s * (-.06 - .16 * job.swing), -.48 * s, -.065 * s});
            const bool reaching = assigned && (seeking || job.latched[i]);
            const Vec3 tip = targets[i];
            const Vec3 glove_offset{f * s * (near ? .145 : .12), -(near ? .025 : .029) * s, 0};
            const double reach_speed = reaching ? (near ? 440 : 480) : (near ? 155 : 175);
            const auto approach = [&] {
                return toward(job.gloves[i], reaching ? tip : free, elapsed * reach_speed);
            };
            // A live lock tracks the current rim directly while still reachable.
            // Unreachable or released hands continue from the actually drawn tip.
            const bool following_grip = reaching && job.latched[i];
            Vec3 aim = following_grip ? tip : approach();
            Joint arm = solve(shoulder, aim - glove_offset, .179 * s, .184 * s, -f);
            if (following_grip && !arm.exact) {
                aim = approach();
                arm = solve(shoulder, aim - glove_offset, .179 * s, .184 * s, -f);
            }
            job.gloves[i] = arm.exact ? aim : arm.end + glove_offset;
            job.latched[i] = reaching && arm.exact && aim.x == tip.x && aim.y == tip.y &&
                             aim.z == tip.z && out.opacity >= .18;
            out.arms[i] = {shoulder, arm.bend, arm.end, job.gloves[i], job.latched[i]};
            gripping |= job.latched[i];
        }
        out.stage = !assigned  ? EdgeWorkerStage::Release
                    : gripping ? EdgeWorkerStage::Support
                               : EdgeWorkerStage::Approach;

        const double speed = std::max(1., ground_speed);
        const double duration = std::clamp(.39 * s / speed, .024, .12);
        const Vec3 lead = velocity * std::min(duration * .75, .23 * s / speed);
        for (std::size_t i = 0; i < 2; ++i) {
            const bool near = i == 0;
            const double side = near ? 1 : -1;
            const Vec3 hip =
                out.hip + Vec3{f * s * (near ? .049 : -.04), 0, s * (near ? .043 : -.054)};
            const Vec3 ankle_offset{0, -s * .0042 * (near ? 27.25 : 26.25), 0};
            const Vec3 want = job.foot + Vec3{side * f * s * .15, 0, side * s * .05};
            auto &boot = job.boots[i];
            if (elapsed > 0 && boot.duration > 0) {
                boot.elapsed += elapsed;
                const double k = std::min(1., boot.elapsed / boot.duration);
                boot.end =
                    boot.end + (want + lead - boot.end) * std::min(1., elapsed * (1 + 34 * k));
                boot.sole = boot.start + (boot.end - boot.start) * smoothstep(0, 1, k);
                if (k < 1)
                    boot.sole.y -= .13 * s * std::sin(std::numbers::pi * k);
                else
                    boot.duration = 0;
            }
            const Joint leg = solve(hip, boot.sole + ankle_offset, .232 * s, .247 * s, f);
            const Vec3 sole = leg.exact ? boot.sole : leg.end - ankle_offset;
            const double drift_limit = job.boots[1 - i].duration > 0 ? .24 : .15;
            if (elapsed > 0 && boot.duration == 0 &&
                (length(sole - want) > s * drift_limit ||
                    length(boot.sole + ankle_offset - hip) > .454 * s)) {
                boot.start = sole;
                boot.end = want + lead;
                boot.elapsed = 0;
                boot.duration = duration;
            }
            out.legs[i] = {hip, leg.bend, leg.end, sole, boot.duration == 0 && leg.exact};
        }
    }

    void EdgeStretch::step(const EdgeStretchStep &sample) noexcept {
        const Box previous = m_body;
        m_body = usable(sample.body) ? sample.body : Box{};
        const bool had_work = active();
        m_count = 0;
        const double dt = sample.elapsed;
        if (!usable(m_body) || sample.stalled || !std::isfinite(dt) || dt < 0 ||
            dt > STALL_SECONDS) {
            for (auto &pair : m_jobs)
                for (auto &job : pair) job.live = false;
            m_velocity.fill(0);
            m_recent.fill(0);
            m_was_held = false;
            return;
        }

        // Re-pose at the current window scale while retaining planted world-space boots.
        if (usable(previous)) {
            const double ratio = std::min(m_body.w, m_body.h) / std::min(previous.w, previous.h);
            for (auto &pair : m_jobs)
                for (auto &job : pair)
                    if (job.live) job.size *= ratio;
        }
        const auto delta = edge_motion(previous, m_body);
        EdgeSet moving_edges;
        for (const Edge e : EDGES) {
            const double change = delta[index(e)];
            if (std::isfinite(change) && std::abs(change) > 1e-9) moving_edges.add(e);
        }
        const double measured = sample.sample_seconds > 0 && sample.sample_seconds <= STALL_SECONDS
                                    ? sample.sample_seconds
                                    : dt;
        EdgeSet targets = sample.grab.value_or(EdgeSet{});
        if (sample.grab || (m_was_held && !sample.held)) m_recent.fill(0);
        m_was_held = !sample.grab && sample.held;
        for (const Edge e : EDGES) {
            const auto i = index(e);
            const bool moving = moving_edges.has(e);
            if (!sample.grab) {
                // An opposite edge supersedes the old assignment on this axis.
                // Briefly retain an unchanged orthogonal edge for quantized corner
                // samples, but keep the entire assignment through a true pause.
                if (moving)
                    m_recent[i] = RELEASE_IDLE;
                else if (sample.held && moving_edges.has(EDGES[i ^ 1U]))
                    m_recent[i] = 0;
                else if (!sample.held || !moving_edges.empty()) {
                    const double age = sample.held ? measured : dt;
                    m_recent[i] = std::max(0., m_recent[i] - age);
                }
                if (m_recent[i] > 0) targets.add(e);
            }
            if (!targets.has(e)) {
                m_velocity[i] = 0;
            } else {
                const double interval = moving ? measured : dt;
                if (interval > 0) {
                    const double speed =
                        moving
                            ? std::clamp(delta[i] / interval, -MAX_SAMPLE_SPEED, MAX_SAMPLE_SPEED)
                            : 0;
                    m_velocity[i] = std::lerp(m_velocity[i], speed, std::min(1., 20 * interval));
                }
            }
        }

        const bool compact = std::max(m_body.w, m_body.h) < 24;
        if (compact) {
            // A sub-boot window can only stage one active fitter. Prefer the
            // currently moving edge; use the same assignment through a pause.
            const auto candidate = [&](Edge e) {
                return targets.has(e) &&
                       (sample.grab || moving_edges.empty() || moving_edges.has(e));
            };
            const auto first = std::find_if(EDGES.begin(), EDGES.end(), candidate);
            const auto existing = std::find_if(EDGES.begin(), EDGES.end(), [&](Edge e) {
                return candidate(e) && m_jobs[index(e)][0].live && !m_jobs[index(e)][0].leaving;
            });
            targets = {};
            if (existing != EDGES.end())
                targets.add(*existing);
            else if (first != EDGES.end())
                targets.add(*first);
        }
        const auto assigned = [&](Edge e, unsigned slot) noexcept {
            const double extent = vertical_edge(e) ? m_body.h : m_body.w;
            return targets.has(e) && (slot == 0 || extent >= 100);
        };
        if (targets.empty() && !had_work) return;
        const Timing timing{
            dt, -std::expm1(-dt * 28), -std::expm1(-dt * 31), -std::expm1(-dt * 22)};
        std::size_t live = 0;
        for (const Edge e : EDGES) {
            for (unsigned slot = 0; slot < 2; ++slot) {
                auto &job = m_jobs[index(e)][slot];
                if (job.live && !assigned(e, slot)) {
                    pose(job,
                        e,
                        slot,
                        EdgeStretchAction::Hold,
                        false,
                        sample.work_area,
                        timing,
                        m_poses[m_count]);
                    m_count += job.live;
                }
                live += job.live;
            }
        }
        // Reserve one worker per newly assigned edge before admitting helpers.
        for (unsigned slot = 0; slot < 2; ++slot) {
            for (const Edge e : EDGES) {
                if (!assigned(e, slot)) continue;
                const auto i = index(e);
                auto &job = m_jobs[i][slot];
                if (!job.live) {
                    // A compact edge handoff may overlap the outgoing fitter's release.
                    if (live >= MAX_WORKERS) continue;
                    enlist(job, e, slot, sample.work_area, toward_leading_corner(targets, e));
                    live += job.live;
                }
                if (!job.live) continue;
                const auto action = m_velocity[i] > ACTION_SPEED    ? EdgeStretchAction::Pull
                                    : m_velocity[i] < -ACTION_SPEED ? EdgeStretchAction::Push
                                                                    : EdgeStretchAction::Hold;
                pose(job, e, slot, action, true, sample.work_area, timing, m_poses[m_count++]);
            }
        }
    }

}  // namespace hyprimate::construction::studies
