#include "animations/bubble/BubbleFoam.hpp"

#include <algorithm>
#include <cmath>

namespace hyprimate {
    namespace {
        constexpr double pi = 3.14159265358979323846;
        Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
        Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
        Vec2 mul(Vec2 a, double b) { return {a.x * b, a.y * b}; }
        double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
        double length(Vec2 a) { return std::hypot(a.x, a.y); }
        // Quadrupole direction of a contact normal: the outline mode it excites.
        Vec2 mode(Vec2 n) { return {n.x * n.x - n.y * n.y, 2 * n.x * n.y}; }

        // Liquid drag relaxes a film toward the local flow; contact forces act through it, so
        // overlaps resolve at a rate rather than by impulse.
        constexpr double kDrag = 9;
        // Contact stiffness and capillary attraction (floating bubbles cluster), per unit overlap.
        constexpr double kRepel = 190, kAttract = 90;
        // Deformation beyond this fraction of the smaller radius is not elastic: it is resolved
        // positionally, so dense rafts pack instead of stacking.
        constexpr double kHardOverlap = .35;
        // Rates per unit time: neck flow during coalescence and a ruptured film's retraction.
        constexpr double kNeckRate = 45, kBurstRate = 20;
        // n = 2 shape oscillator of a small film: frequency and damping ratio.
        constexpr double kShapeFrequency = 55, kShapeDamping = .35;
    }

    double bubbleBoxReach(Vec2 half, double corner, Vec2 n) {
        // Ray/rounded-box intersection, including the circular corner.
        const double ax = std::abs(n.x), ay = std::abs(n.y);
        double d = std::min(half.x / std::max(ax, 1e-12), half.y / std::max(ay, 1e-12));
        const double cx = half.x - corner, cy = half.y - corner;
        if (ax * d > cx && ay * d > cy) {
            const double b = ax * cx + ay * cy;
            d = b + std::sqrt(std::max(0.0, b * b - cx * cx - cy * cy + corner * corner));
        }
        return d;
    }

    Vec2 BubbleFoamFlow::velocity(Vec2 p) const {
        Vec2 v = drift;
        const Vec2 d = sub(p, source);
        // A 2D source: outflow spreads over circles around it; the core keeps it finite.
        const double r2 = dot(d, d) + .04 * .04;
        v = add(v, mul(d, outflow / (2 * pi * r2)));
        for (const auto &e : eddies) {
            if (e.circulation == 0) continue;
            const Vec2 q = sub(p, e.centre);
            const double s = e.circulation / (2 * pi * (dot(q, q) + e.core * e.core));
            v = add(v, Vec2{-q.y * s, q.x * s});
        }
        return v;
    }

    void BubbleFoam::reset(std::uint64_t seed, const BubbleFoamParams &params, std::size_t budget) {
        *this = BubbleFoam{};
        params_ = params;
        budget_ = std::min(budget, films_.size());
        random_ = seed ^ 0xA0761D6478BD642FULL;
    }

    int BubbleFoam::spawn(Vec2 position, Vec2 velocity, double radius, double thickness,
        double surfacing, double life) {
        if (!(radius > 0) || !std::isfinite(position.x) || !std::isfinite(position.y)) return -1;
        for (std::size_t i = 0; i < budget_; ++i) {
            auto &f = films_[i];
            if (f.live) continue;
            f = {};
            f.live = true;
            f.position = position;
            f.velocity = velocity;
            f.target = radius;
            f.surfacing = std::clamp(surfacing, 0.0, 1.0);
            f.radius = radius * (.3 + .7 * smoothstep(0, 1, f.surfacing));
            f.surfaceRate = 1 / (.035 + .05 * unit_draw(random_));
            f.thickness = thickness;
            f.seed = unit_draw(random_);
            f.phase = 6 * unit_draw(random_);
            f.life = std::max(.02, life);
            f.drainage = .25 + .75 * unit_draw(random_);
            f.adhesion = .6 + .8 * unit_draw(random_);
            return static_cast<int>(i);
        }
        return -1;
    }

    void BubbleFoam::inflate(int slot, double volume) {
        if (slot < 0 || static_cast<std::size_t>(slot) >= budget_ || !(volume > 0)) return;
        auto &f = films_[static_cast<std::size_t>(slot)];
        if (!f.live) return;
        f.radius = std::cbrt(f.radius * f.radius * f.radius + volume);
        f.target = std::max(f.target, f.radius);
    }

    void BubbleFoam::setFed(int slot, bool fed) {
        if (slot < 0 || static_cast<std::size_t>(slot) >= budget_) return;
        films_[static_cast<std::size_t>(slot)].fed = fed;
    }

    void BubbleFoam::expire(double within) {
        for (auto &f : films_) {
            if (!f.live || f.fed || f.burst > 0) continue;
            f.life = std::min(f.life, f.age + std::max(0.0, within) * (.25 + .7 * f.seed));
        }
    }

    BubbleFoam::Film BubbleFoam::take(int slot) {
        if (slot < 0 || static_cast<std::size_t>(slot) >= budget_) return {};
        auto &f = films_[static_cast<std::size_t>(slot)];
        const Film taken = f;
        f = {};
        // Coalescences into it now have nowhere to go; their films stay intact.
        for (auto &other : films_)
            if (other.into == slot) other.into = -1;
        return taken;
    }

    void BubbleFoam::scale(Vec2 axis) {
        for (auto &f : films_) {
            f.position = {f.position.x * axis.x, f.position.y * axis.y};
            f.velocity = {f.velocity.x * axis.x, f.velocity.y * axis.y};
        }
    }

    int BubbleFoam::nearest(Vec2 point, double reach) const {
        int best = -1;
        double bestGap = reach;
        for (std::size_t i = 0; i < budget_; ++i) {
            const auto &f = films_[i];
            if (!f.live || f.burst > 0 || f.into != -1) continue;
            const double gap = length(sub(f.position, point)) - f.radius;
            if (gap <= bestGap) {
                bestGap = gap;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    int BubbleFoam::largest() const {
        int best = -1;
        for (std::size_t i = 0; i < budget_; ++i) {
            const auto &f = films_[i];
            if (!f.live || f.burst > 0 || f.into != -1) continue;
            if (best < 0 || f.radius > films_[static_cast<std::size_t>(best)].radius)
                best = static_cast<int>(i);
        }
        return best;
    }

    std::size_t BubbleFoam::liveCount() const {
        return static_cast<std::size_t>(
            std::count_if(films_.begin(), films_.end(), [](const Film &f) { return f.live; }));
    }

    void BubbleFoam::interact(double dt) {
        const double k = params_.interaction;
        for (std::size_t i = 0; i < budget_; ++i) {
            auto &a = films_[i];
            if (!a.live || a.burst > 0) continue;
            for (std::size_t j = i + 1; j < budget_; ++j) {
                auto &b = films_[j];
                if (!b.live || b.burst > 0) continue;
                // A coalescing pair is one film already: the neck, not contact, governs it.
                if (a.into == static_cast<int>(j) || b.into == static_cast<int>(i)) continue;
                const Vec2 d = sub(b.position, a.position);
                const double distance = length(d);
                const double small = std::min(a.radius, b.radius);
                const double overlap = a.radius + b.radius - distance;
                if (overlap <= -.6 * small || distance < 1e-9) continue;
                const Vec2 n = mul(d, 1 / distance);
                // Wetted area sets the share of a contact each film takes.
                const double ma = a.radius * a.radius, mb = b.radius * b.radius;
                const double wa = mb / (ma + mb), wb = ma / (ma + mb);
                const double adhesion = std::min(a.adhesion, b.adhesion);
                const double accel = overlap > 0 ? k * kRepel * overlap
                                                 : -k * kAttract * adhesion * (overlap + .6 * small) *
                                                       std::min(1.0, -overlap / (.15 * small));
                a.velocity = sub(a.velocity, mul(n, accel * wa * dt));
                b.velocity = add(b.velocity, mul(n, accel * wb * dt));
                // Adhesion holds touching films at a shared wall; that counts as contact.
                const double touch = overlap + .08 * small;
                if (touch <= 0) continue;
                if (overlap > 0) {
                    const double hard = std::min(overlap - kHardOverlap * small, .1 * small);
                    if (hard > 0) {
                        a.position = sub(a.position, mul(n, hard * wa));
                        b.position = add(b.position, mul(n, hard * wb));
                    }
                    // Both outlines flatten along the shared wall.
                    const Vec2 m = mode(n);
                    a.squashRate = sub(a.squashRate,
                        mul(m, kShapeFrequency * kShapeFrequency * std::min(.25, overlap / (2 * a.radius)) * dt));
                    b.squashRate = sub(b.squashRate,
                        mul(m, kShapeFrequency * kShapeFrequency * std::min(.25, overlap / (2 * b.radius)) * dt));
                }
                if (a.into != -1 || b.into != -1) continue;
                // The film between them drains while they touch, then ruptures; pressing speeds it.
                // A smaller film's higher Laplace pressure drives it into the larger one sooner.
                const double ratio = small / std::max(a.radius, b.radius);
                const double pressed = std::min(1.0, touch / (.25 * small));
                const double drainage = std::min(a.drainage, b.drainage);
                // Air flows into the fed film from every bubble it presses: it coarsens fastest.
                const double hazard = dt * pressed * (1 + 2.5 * (1 - ratio)) / drainage *
                                      (a.fed || b.fed ? 4 : 1);
                if (unit_draw(random_) < hazard) {
                    // Air flows into a fed film; otherwise the smaller film empties.
                    Film &donor = a.fed ? b : b.fed ? a : (a.radius < b.radius ? a : b);
                    donor.into = static_cast<int>(&donor == &a ? j : i);
                }
            }
        }
    }

    void BubbleFoam::meet(double dt, BubbleFoamBody &body) {
        const double k = params_.interaction;
        const double bodyMass = std::max(body.mass, 1e-6);
        for (std::size_t i = 0; i < budget_; ++i) {
            auto &f = films_[i];
            if (!f.live || f.burst > 0 || f.into != -1) continue;
            const Vec2 delta = sub(f.position, body.centre);
            const double dist = length(delta);
            const Vec2 n = dist > 1e-9 ? mul(delta, 1 / dist) : Vec2{1, 0};
            const double edge = bubbleBoxReach(body.half, body.corner, n);
            const double overlap = edge + f.radius - dist;
            if (overlap <= -.6 * f.radius) continue;
            // Contact is with an inflating membrane, not a stationary obstacle: its outward speed
            // must push neighbours before its expansion engulfs them.
            const double surfaceSpeed =
                (edge - bubbleBoxReach(body.previousHalf, body.previousCorner, n)) / dt;
            const double compression = std::max(0.0, overlap);
            const double relative = dot(sub(f.velocity, body.velocity), n) - surfaceSpeed;
            double accel = overlap > 0 ? k * kRepel * compression
                                       : -k * kAttract * f.adhesion * (overlap + .6 * f.radius) *
                                             std::min(1.0, -overlap / (.15 * f.radius));
            const double mass = f.radius * f.radius;
            f.velocity = add(f.velocity, mul(n, accel * dt));
            // An advancing wall carries a pressed film with it (an inelastic push), so the film is
            // shoved aside continuously instead of being caught and expelled.
            if (overlap > 0 && relative < 0) f.velocity = sub(f.velocity, mul(n, relative));
            body.velocity = sub(body.velocity, mul(n, accel * dt * std::min(1.0, mass / bodyMass)));
            if (overlap <= 0) continue;
            if (overlap > f.radius * 1.2 && body.room > 0) {
                // Overrun by the wall: it is already inside, so it joins the body.
                f.into = kIntoBody;
                continue;
            }
            const double hard = compression - kHardOverlap * f.radius;
            if (hard > 0) f.position = add(f.position, mul(n, std::min(hard, .1 * f.radius)));
            const Vec2 m = mode(n);
            f.squashRate = sub(f.squashRate,
                mul(m, kShapeFrequency * kShapeFrequency * std::min(.3, compression / (2 * f.radius)) * dt));
            const double drive = compression * 12 + std::abs(relative) * .15;
            body.stir += drive * dt;
            body.quadrupole = add(body.quadrupole, mul(m, drive * dt));
            const double pressed = std::min(1.0, compression / (.2 * f.radius));
            if (body.room > 0 && unit_draw(random_) < dt * pressed * 1.2 / f.drainage) f.into = kIntoBody;
        }
    }

    void BubbleFoam::coalesce(double dt, BubbleFoamBody *body) {
        const double flowRate = 1 - std::exp(-kNeckRate * dt);
        for (std::size_t i = 0; i < budget_; ++i) {
            auto &f = films_[i];
            if (!f.live || f.into == -1) continue;
            const double volume = f.radius * f.radius * f.radius;
            // The neck widens as it drains; the last droplet-sized remnant goes all at once.
            double moved = std::cbrt(volume * (1 - flowRate)) < .004 ? volume : volume * flowRate;
            if (f.into == kIntoBody) {
                if (!body) {
                    f.into = -1;
                    continue;
                }
                // The body is a flattened film: it receives projected area. A body that has filled
                // its space cannot take more; the film stays a separate bubble.
                const double area = pi * f.radius * f.radius * (moved / volume);
                const double accepted = std::min(area, std::max(0.0, body->room - body->absorbed));
                if (accepted <= 0) {
                    f.into = -1;
                    continue;
                }
                moved *= accepted / area;
                body->absorbed += accepted;
                body->velocity = add(body->velocity,
                    mul(sub(f.velocity, body->velocity), std::min(1.0, accepted / std::max(body->mass, 1e-6))));
                body->stir += accepted * 30;
                const Vec2 pull = sub(body->centre, f.position);
                f.velocity = add(f.velocity, mul(pull, 14 * dt));
            } else {
                auto &r = films_[static_cast<std::size_t>(f.into)];
                if (!r.live || r.burst > 0) {
                    f.into = -1;
                    continue;
                }
                const double rv = r.radius * r.radius * r.radius;
                // Momentum and air both pass through the neck; the drained film is drawn in.
                r.velocity = mul(add(mul(r.velocity, rv), mul(f.velocity, moved)), 1 / (rv + moved));
                r.radius = std::cbrt(rv + moved);
                r.target = std::max(r.target, r.radius);
                const Vec2 axis = sub(f.position, r.position);
                const double axisLength = length(axis);
                if (axisLength > 1e-9) {
                    // The merged film rings: stretched along the neck, then oscillating back.
                    r.squashRate = add(r.squashRate, mul(mode(mul(axis, 1 / axisLength)), 4 * moved / (rv + moved)));
                    f.velocity = add(f.velocity, mul(axis, -14 * dt));
                }
            }
            const double remaining = std::max(0.0, volume - moved);
            f.radius = std::cbrt(remaining);
            if (f.radius < .0015) f = {};
        }
    }

    void BubbleFoam::step(double dt, const BubbleFoamFlow &flow, BubbleFoamBody *body) {
        if (!(dt > 0)) return;
        if (body) body->absorbed = body->stir = 0, body->quadrupole = {};
        interact(dt);
        if (body) meet(dt, *body);
        coalesce(dt, body);
        const double drag = 1 - std::exp(-kDrag * params_.damping * dt);
        const Vec2 rise{0, -.05 * params_.buoyancy};
        for (auto &f : films_) {
            if (!f.live) continue;
            f.age += dt;
            if (f.surfacing < 1) {
                f.surfacing = std::min(1.0, f.surfacing + f.surfaceRate * dt);
                f.radius = std::max(f.radius, f.target * (.3 + .7 * smoothstep(0, 1, f.surfacing)));
            }
            if (f.fed) f.life = std::max(f.life, f.age + .05);
            if (f.burst > 0 || (f.age >= f.life && f.into == -1)) {
                f.burst += dt * kBurstRate;
                if (f.burst >= 1) {
                    f = {};
                    continue;
                }
            }
            // The liquid carries it; larger films sit deeper in the flow and lag it slightly.
            const Vec2 carried = add(flow.velocity(f.position), rise);
            f.velocity = add(f.velocity, mul(sub(carried, f.velocity), drag));
            if (flow.confine.x > 0 && flow.confine.y > 0) {
                // Soft confinement: films may protrude past the region but are drawn back.
                const double wallX = std::abs(f.position.x) - flow.confine.x - .5 * f.radius;
                const double wallY = std::abs(f.position.y) - flow.confine.y - .5 * f.radius;
                if (wallX > 0) f.velocity.x -= std::copysign(60 * wallX * dt, f.position.x);
                if (wallY > 0) f.velocity.y -= std::copysign(60 * wallY * dt, f.position.y);
            }
            f.position = add(f.position, mul(f.velocity, dt));
            const double w = kShapeFrequency;
            f.squashRate = add(f.squashRate, mul(add(mul(f.squash, -w * w), mul(f.squashRate, -2 * kShapeDamping * w)), dt));
            f.squash = add(f.squash, mul(f.squashRate, dt));
            f.flow += dt * (1 + 6 * length(f.squash));
        }
    }

    std::size_t BubbleFoam::write(std::span<BubbleMicro> out, double unit, Vec2 axis, double flowPhase) const {
        std::size_t count = 0;
        for (std::size_t i = 0; i < budget_ && i < out.size(); ++i) {
            auto &m = out[i];
            m = {};
            m.visibility = 0;
            const auto &f = films_[i];
            if (!f.live) continue;
            count = i + 1;
            m.centre = {f.position.x * axis.x * unit, f.position.y * axis.y * unit};
            m.anchor = m.centre;
            m.radius = f.radius * unit;
            m.growth = f.surfacing;
            m.collapse = f.burst;
            // Drainage thins the wall toward rupture; its colour follows.
            m.thickness = f.thickness * (1 - .45 * smoothstep(0, 1, f.age / f.life));
            m.seed = f.seed;
            m.flow = flowPhase + f.phase + f.flow;
            m.visibility = 1;
            m.squash = {std::clamp(f.squash.x, -.14, .14), std::clamp(f.squash.y, -.14, .14)};
        }
        return count;
    }
}  // namespace hyprimate
