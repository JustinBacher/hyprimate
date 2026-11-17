#include "animations/bubble/BubbleFormation.hpp"

#include <algorithm>
#include <cmath>
#include <span>

#include "animations/bubble/Bubble.hpp"

namespace hyprimate {
    namespace {
        constexpr double pi = 3.14159265358979323846;
        constexpr double dt = 1.0 / 360.0;
        constexpr double cornerArea = 4.0 - pi;
        // Progress at which air starts going into the bubble over the inlet, the latest it may
        // take over as the window bubble, and when the window bubble must have filled the box.
        constexpr double kFeed = .3, kLatestEmergence = .68, kLand = .88;
        // The window bubble's inflation keeps the profile tuned on its original timeline, from
        // .05 to the deadline .87 of that timeline, compressed into what remains after it forms.
        constexpr double kTimelineStart = .05, kTimelineEnd = .87;
        struct Shape { double x, y, corner; };

        bool usable(Vec2 s) {
            return std::isfinite(s.x) && std::isfinite(s.y) && s.x > 0 && s.y > 0 &&
                   s.x <= 1e9 && s.y <= 1e9;
        }
        Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
        Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
        Vec2 mul(Vec2 a, double b) { return {a.x * b, a.y * b}; }
        double gaussian(std::uint64_t &random) {
            const double u = std::max(1e-12, unitDraw(random)), v = unitDraw(random);
            return std::sqrt(-2 * std::log(u)) * std::cos(2 * pi * v);
        }

        // The available half extents are measured from the actual centre to
        // the nearest wall. Once contact starts, excess inflation flattens the
        // rounded ends as well as extending the free axis. Projected area is
        // conserved; there is no capsule-only wait for the remaining walls.
        Shape shape(double area, double width, double height, Vec2 position) {
            const double hx = std::max(1e-12, width * .5 - std::abs(position.x));
            const double hy = std::max(1e-12, height * .5 - std::abs(position.y));
            const double r = std::sqrt(std::max(0.0, area) / pi);
            const double small = std::min(hx, hy), large = std::max(hx, hy);
            if (r <= small) return {r, r, r};
            const double contactArea = pi * small * small;
            const double contactCorner = small * contactArea / area;
            const double other = (area + cornerArea * contactCorner * contactCorner) / (4 * small);
            if (other < large)
                return hx < hy ? Shape{small, other, contactCorner} : Shape{other, small, contactCorner};
            const double corner = std::sqrt(std::max(0.0, (4 * hx * hy - area) / cornerArea));
            return {hx, hy, std::min(small, corner)};
        }
        // If both axes are exhausted, wall reaction must move the centre before
        // a rounded rectangle can contain the gas. Project only this infeasible
        // excess; ordinary first contact is handled by the area-derived shape.
        Vec2 feasible(Vec2 position, double area, double width, double height) {
            position.x = std::clamp(position.x, -width * .5, width * .5);
            position.y = std::clamp(position.y, -height * .5, height * .5);
            const auto available = [&](double fraction) {
                return (width - 2 * std::abs(position.x) * fraction) *
                       (height - 2 * std::abs(position.y) * fraction);
            };
            if (available(1) >= area) return position;
            double lo = 0, hi = 1;
            for (int i = 0; i < 36; ++i) {
                const double mid = (lo + hi) * .5;
                if (available(mid) >= area) lo = mid;
                else hi = mid;
            }
            return mul(position, lo);
        }
    }

    void BubbleFormation::begin(Vec2 size, std::uint64_t seed, const BubbleFormationParams &params) {
        const BubbleFormationParams requested = params;
        *this = BubbleFormation{};
        const auto bounded = [](double value, double fallback, double low, double high) {
            return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
        };
        params_.count = std::clamp(requested.count, 0, static_cast<int>(kBubbleFoamCapacity));
        params_.birthRate = bounded(requested.birthRate, 1, .25, 2);
        params_.sizeVariation = bounded(requested.sizeVariation, .75, 0, 1);
        params_.growthVariation = bounded(requested.growthVariation, .6, 0, 1);
        params_.interaction = bounded(requested.interaction, 1, .25, 2);
        params_.damping = bounded(requested.damping, 1, .5, 2);
        params_.buoyancy = bounded(requested.buoyancy, 1, 0, 2);
        params_.linger = bounded(requested.linger, .5, 0, 1);
        if (!usable(size)) return;
        size_ = size;
        random_ = seed;
        foam_.reset(seed, {params_.interaction, params_.damping, params_.buoyancy, params_.linger},
            static_cast<std::size_t>(params_.count));
        const double shortSide = std::min(size.x, size.y);
        const double width = size.x / shortSide, height = size.y / shortSide;
        // The inlet wanders through the window's middle on two slow periods.
        wander_[0] = {std::min(.12, width * .16), std::min(.12, height * .16)};
        wander_[1] = {2 * pi * unitDraw(random_), 2 * pi * unitDraw(random_)};
        for (int i = 0; i < 2; ++i) {
            eddyCentre_[i] = {(unitDraw(random_) - .5) * .6 * width, (unitDraw(random_) - .5) * .6 * height};
            eddySpin_[i] = (i == 0 ? 1 : -1) * (.1 + .08 * unitDraw(random_));
        }
        inlet_ = {wander_[0].x * std::sin(wander_[1].x), wander_[0].y * std::sin(wander_[1].y)};
        for (std::size_t i = 0; i < plumes_.size(); ++i) {
            // The first stream is the straw itself; the others surface around it.
            const double reach = i == 0 ? 0 : .08 + .18 * unitDraw(random_);
            const double angle = 2 * pi * unitDraw(random_);
            plumes_[i].offset = {reach * std::cos(angle), reach * std::sin(angle)};
            plumes_[i].spin = (unitDraw(random_) < .5 ? -1 : 1) * (.5 + unitDraw(random_));
        }
        birthThreshold_ = -std::log(1 - unitDraw(random_));
        if (params_.count == 0) {
            // No companions: the window bubble itself is the first film.
            exists_ = true;
            gas_ = .00045;
            corner_ = std::sqrt(gas_ / pi);
            position_ = {0, .025};
            pace_ = (kTimelineEnd - kTimelineStart) / kLand;
        }
    }

    void BubbleFormation::advance(double progress, Vec2 size) {
        if (!usable(size) || !std::isfinite(progress)) return;
        if (!usable(size_)) begin(size, random_, params_);
        if (size.x != size_.x || size.y != size_.y) {
            // Preserve fractional fill and normalized positions on resize.
            const double oldShort = std::min(size_.x, size_.y);
            const double newShort = std::min(size.x, size.y);
            const double sx = (size.x / newShort) / (size_.x / oldShort);
            const double sy = (size.y / newShort) / (size_.y / oldShort);
            gas_ *= sx * sy;
            reservoir_ *= sx * sy;
            position_ = {position_.x * sx, position_.y * sy};
            velocity_ = {velocity_.x * sx, velocity_.y * sy};
            inlet_ = {inlet_.x * sx, inlet_.y * sy};
            foam_.scale({sx, sy});
            size_ = size;
        }
        const int target = static_cast<int>(std::floor(std::clamp(progress, 0.0, 1.0) * 360 + 1e-9));
        while (ticks_ < target) {
            ++ticks_;
            progress_ = ticks_ * dt;
            step();
        }
    }

    void BubbleFormation::nucleate(double step, double t, double width, double height) {
        // Air arrives in puffs through a few streams around the inlet; each surfaces as a tight
        // clump of bubbles, so the raft forms in groups rather than as an even scatter.
        double total = 0;
        for (auto &plume : plumes_) {
            plume.clock -= step;
            if (plume.clock <= 0) {
                plume.target = unitDraw(random_) < .35 ? 0 : .5 + unitDraw(random_);
                plume.clock = .04 + .1 * unitDraw(random_);
            }
            plume.strength += (plume.target - plume.strength) * (1 - std::exp(-step / .02));
            // The streams wander with the liquid; a stream never moves far from the inlet.
            const double turn = plume.spin * step;
            plume.offset = {plume.offset.x * std::cos(turn) - plume.offset.y * std::sin(turn),
                plume.offset.x * std::sin(turn) + plume.offset.y * std::cos(turn)};
            total += plume.strength;
        }
        const double rate = 1100 * params_.birthRate * smoothstep(0, .03, t) *
                            (1 - smoothstep(.5, .8, t)) * (exists_ ? .55 : 1) *
                            total / plumes_.size();
        birthHazard_ += rate * step;
        const double shortReach = std::sqrt(gas_ / pi);
        while (birthHazard_ >= birthThreshold_ && total > 0) {
            birthHazard_ -= birthThreshold_;
            birthThreshold_ = -std::log(1 - unitDraw(random_));
            const double radius = std::clamp(
                .011 * std::exp((.15 + .6 * params_.sizeVariation) * gaussian(random_)), .004, .06);
            double pick = unitDraw(random_) * total;
            std::size_t chosen = 0;
            while (chosen + 1 < plumes_.size() && pick > plumes_[chosen].strength)
                pick -= plumes_[chosen++].strength;
            const Vec2 source = add(inlet_, Vec2{plumes_[chosen].offset.x * std::sqrt(width),
                                                plumes_[chosen].offset.y * std::sqrt(height)});
            const double clump = .02 + .03 * smoothstep(0, .45, t);
            Vec2 at = add(source, Vec2{gaussian(random_) * clump, gaussian(random_) * clump});
            // Air rising under the clump surfaces against a bubble already there.
            const int neighbour = unitDraw(random_) < .7 ? foam_.nearest(at, .04) : -1;
            if (neighbour >= 0) {
                const auto &n = foam_.film(neighbour);
                const double a = 2 * pi * unitDraw(random_);
                at = add(n.position, mul(Vec2{std::cos(a), std::sin(a)}, n.radius + radius * .9));
            }
            at.x = std::clamp(at.x, -width * .5 + radius, width * .5 - radius);
            at.y = std::clamp(at.y, -height * .5 + radius, height * .5 - radius);
            if (exists_) {
                // Never inside the window bubble: air reaching it joins it.
                const Vec2 offset = sub(at, position_);
                const double d = std::hypot(offset.x, offset.y);
                if (d < shortReach + radius) continue;
            }
            // Wall thickness is the film's own (formation, drainage), not its size.
            const double thickness = 500 + 380 * unitDraw(random_);
            const double life = (.22 + .45 * unitDraw(random_)) * (.7 + .6 * params_.linger);
            const int slot = foam_.spawn(at, {}, radius, thickness, 0, life);
            if (slot < 0) continue;
            foam_.film(slot).surfaceRate *=
                std::exp(1.4 * params_.growthVariation * (unitDraw(random_) - .5));
        }
    }

    void BubbleFormation::feed(double step, double t) {
        if (t < kFeed) return;
        const auto valid = [&](int slot) {
            if (slot < 0) return false;
            const auto &f = foam_.film(slot);
            return f.live && f.burst == 0 && f.into == -1;
        };
        if (!valid(fed_)) {
            if (fed_ >= 0 && foam_.film(fed_).live) foam_.setFed(fed_, false);
            fed_ = foam_.nearest(inlet_, .015);
            if (fed_ < 0)
                fed_ = foam_.spawn(inlet_, {}, .03, 500 + 380 * unitDraw(random_), .3, 1);
            if (fed_ < 0) return;
            foam_.setFed(fed_, true);
        }
        // The air stream follows the bubble it fills; the stream's own wander still steers it.
        auto &f = foam_.film(fed_);
        const double ramp = smoothstep(kFeed, kFeed + .1, t);
        // A gentle stream: the bubble over the inlet grows mostly by swallowing the bubbles that
        // press on it, so its growth is seen to come from the mass around it.
        foam_.inflate(fed_, .004 * ramp * pumpRate_ / 1.625 * step);
        const Vec2 toInlet = sub(inlet_, f.position);
        f.velocity = add(f.velocity, mul(toInlet, 4 * step));
    }

    void BubbleFormation::emerge(double t) {
        const auto film = foam_.take(fed_);
        fed_ = -1;
        exists_ = true;
        emerged_ = t;
        origin_ = film;
        gas_ = pi * film.radius * film.radius;
        corner_ = film.radius;
        position_ = film.position;
        velocity_ = film.velocity;
        // Resume the inflation profile where a bubble of this size would be on its timeline, and
        // compress what remains of it into the time left. Its pump swells from the stream that
        // filled it, so neither the size nor the growth rate jumps.
        const double shortSide = std::min(size_.x, size_.y);
        const double capacity = size_.x / shortSide * (size_.y / shortSide);
        tauStart_ = std::clamp(kTimelineStart + gas_ / (capacity * 1.625), kTimelineStart, .4);
        pace_ = (kTimelineEnd - tauStart_) / std::max(.1, kLand - t);
        inletRate_ = .2 * pumpRate_;
    }

    double BubbleFormation::inflate(double step, double t, double width, double height, BubbleFoamBody *body) {
        const double capacity = width * height;
        // Air supply follows its tuned profile on the bubble's own timeline (tau), however late
        // the bubble formed. Contact dynamics and settling keep the transition's pace.
        const double air = step * pace_;
        const double tau = tauStart_ + (t - emerged_) * pace_;
        // The new window bubble keeps the pace it grew at for a while, still among the
        // companions, before the full stream takes over.
        const double swell = origin_.live ? .15 + .85 * smoothstep(emerged_, emerged_ + .16, t) : 1;
        const double pump = pumpRate_ * swell;
        // Confinement redirects the incoming air into the remaining free axis.
        // Ramp the supply and inlet together so the reservoir cannot starve
        // the faster morph. Free growth and the settling oscillator keep their pace.
        const double contactRadius = std::max(1e-12, std::min(
            width * .5 - std::abs(position_.x), height * .5 - std::abs(position_.y)));
        const double contactArea = pi * contactRadius * contactRadius;
        const double contactSpeed = 1 + smoothstep(contactArea, contactArea * 1.2, gas_);
        // The reserve pump only engages as a late deficit develops; it ensures
        // even extreme settings have enough air without prescribing the shape.
        const double recovery = 1.5 * smoothstep(.74, .85, tau);
        const double reserveRate = std::max(recovery, smoothstep(.48, .70, tau) * 1.8 *
            (capacity - gas_) / (capacity * std::max(.06, .90 - tau)));
        reservoir_ += capacity * std::max(pump * contactSpeed, reserveRate) * air;
        const Shape before = shape(std::min(gas_, capacity), width, height, position_);
        // Reservoir pressure, compression and the companions' push all influence the inlet. The
        // late pressure regulator supplies a deficit, not a geometry interpolation, and allows
        // the native area to be reached before the deadline.
        const double pressure = reservoir_ / capacity;
        const double deficitRate =
            swell * (capacity - gas_) / (capacity * std::max(.04, kTimelineEnd - tau));
        const double target = std::max({deficitRate, recovery,
            (pump * contactSpeed + 2 * pressure) / (1 + std::abs(core_[0]) * 2)});
        inletRate_ += (target - inletRate_) * (1 - std::exp(-air / .035));
        const double inlet = std::min(reservoir_, capacity * air * std::max(0.0, inletRate_));
        // Finite membrane relaxation prevents the square-root area relation
        // from snapping the last rounded corner flat in one integration step.
        // Air waits in the reservoir while surface tension resists that change.
        // Surface tension flattens the last rounded corner at the transition's own pace, however
        // compressed the air supply's timeline is.
        const double relaxedCorner = std::max(0.0, corner_ - step * contactSpeed * (2 + 2 * pressure));
        const double availableGas = std::max(gas_, capacity - cornerArea * relaxedCorner * relaxedCorner);
        const double accepted = std::min(inlet, std::max(0.0, availableGas - gas_));
        gas_ += accepted;
        reservoir_ -= accepted;
        coreVelocity_[0] += 80 * accepted / capacity;
        const Shape after = shape(gas_, width, height,
            feasible(position_, gas_ + cornerArea * relaxedCorner * relaxedCorner, width, height));
        body->centre = position_;
        body->velocity = velocity_;
        body->previousHalf = {before.x, before.y};
        body->previousCorner = before.corner;
        body->half = {after.x, after.y};
        body->corner = after.corner;
        body->mass = std::max(.035, gas_);
        body->room = std::max(0.0, availableGas - gas_);
        return relaxedCorner;
    }

    void BubbleFormation::step() {
        const double shortSide = std::min(size_.x, size_.y);
        const double width = size_.x / shortSide, height = size_.y / shortSide;
        const double capacity = width * height;
        const double t = progress_;
        // Feed events change the pressure target, not positions. Both the pump
        // and inlet have inertia, so their changing flux remains continuous.
        feedClock_ -= dt * (exists_ ? pace_ : 1);
        if (feedClock_ <= 0) {
            feedTarget_ = 1.625 + .95 * params_.growthVariation * (2 * unitDraw(random_) - 1);
            feedClock_ += .045 + .09 * unitDraw(random_);
        }
        pumpRate_ += (feedTarget_ - pumpRate_) * (1 - std::exp(-dt * (exists_ ? pace_ : 1) / .045));

        const Vec2 wander{wander_[0].x * std::sin(2 * pi * .6 * t + wander_[1].x),
            wander_[0].y * std::sin(2 * pi * .45 * t + wander_[1].y)};
        if (exists_) inlet_ = position_;
        else if (fed_ >= 0 && foam_.film(fed_).live) inlet_ = foam_.film(fed_).position;
        else inlet_ = add(inlet_, mul(sub(wander, inlet_), 1 - std::exp(-6 * dt)));
        if (!exists_ && fed_ >= 0) {
            // The stream keeps wandering under the bubble it fills.
            auto &f = foam_.film(fed_);
            f.velocity = add(f.velocity, mul(sub(wander, f.position), 3 * dt));
        }

        // The liquid: outflow from where air enters, two slowly turning eddies, calming late.
        BubbleFoamFlow flow;
        flow.source = inlet_;
        flow.outflow = .012 * std::sqrt(params_.birthRate) * (1 - smoothstep(.55, .9, t));
        flow.confine = {width * .5, height * .5};
        const double calm = 1 - smoothstep(.7, 1, t);
        for (int i = 0; i < 2; ++i) {
            const double turn = 2 * pi * .25 * t * (i == 0 ? 1 : -1);
            const Vec2 c = eddyCentre_[i];
            flow.eddies[static_cast<std::size_t>(i)] = {
                {c.x * std::cos(turn) - c.y * std::sin(turn), c.x * std::sin(turn) + c.y * std::cos(turn)},
                eddySpin_[i] * calm,
                .18};
        }

        if (params_.count > 0) nucleate(dt, t, width, height);
        if (!exists_ && params_.count > 0) {
            feed(dt, t);
            if (fed_ >= 0) {
                const double r = foam_.film(fed_).radius;
                if ((r >= .1 && t >= .55) || t >= kLatestEmergence) emerge(t);
            } else if (t >= kLatestEmergence) {
                // Nothing to grow from (a tiny budget): air surfaces as a new bubble.
                fed_ = foam_.spawn(inlet_, {}, .03, 450, .5, 1);
                if (fed_ >= 0) emerge(t);
            }
        }
        if (!expired_ && t >= .72 + .12 * params_.linger) {
            // Drainage catches up with every remaining companion before the transition ends.
            foam_.expire(.975 - t);
            expired_ = true;
        }

        if (!exists_) {
            foam_.step(dt, flow, nullptr);
            return;
        }
        BubbleFoamBody body;
        const double relaxedCorner = inflate(dt, t, width, height, &body);
        foam_.step(dt, flow, &body);
        // The companions' push and merged air feed back into the window bubble.
        velocity_ = body.velocity;
        const double merged = std::min(body.absorbed, body.room);
        gas_ += merged;
        coreVelocity_[0] += body.stir;
        coreVelocity_[1] += merged / capacity * 12;
        waveVelocity_[0] += body.quadrupole.x;
        waveVelocity_[1] += body.quadrupole.y;
        // Wall pressure is a restoring force as soon as the unconstrained
        // circle reaches a wall. Only geometrically impossible excess needs
        // projection; before that the capsule deforms at its actual centre.
        const double freeRadius = std::sqrt(gas_ / pi);
        const double wallX = std::max(0.0, freeRadius + std::abs(position_.x) - width * .5);
        const double wallY = std::max(0.0, freeRadius + std::abs(position_.y) - height * .5);
        // Opposing walls cancel as the centre reaches equilibrium. A sign-only
        // restoring force would chatter across zero once both sides are pressed.
        velocity_.x += 22 * dt *
            (std::max(0.0, freeRadius - position_.x - width * .5) -
                std::max(0.0, freeRadius + position_.x - width * .5));
        velocity_.y += 22 * dt *
            (std::max(0.0, freeRadius - position_.y - height * .5) -
                std::max(0.0, freeRadius + position_.y - height * .5));
        const double wallPressure = wallX + wallY;
        coreVelocity_[0] += (wallPressure - wallPressure_) * 3;
        wallPressure_ = wallPressure;
        position_ = add(position_, mul(velocity_, dt));
        velocity_ = mul(velocity_, std::exp(-2.2 * params_.damping * dt));
        const Vec2 before = position_;
        position_ = feasible(position_, gas_ + cornerArea * relaxedCorner * relaxedCorner, width, height);
        corner_ = shape(gas_, width, height, position_).corner;
        const Vec2 correction = sub(position_, before);
        if (correction.x != 0) velocity_.x = correction.x / dt * .15;
        if (correction.y != 0) velocity_.y = correction.y / dt * .15;
        const double impact = std::hypot(correction.x, correction.y);
        coreVelocity_[0] += impact * 20;
        coreVelocity_[1] += impact * 12;
        waveVelocity_[0] += (correction.x - correction.y) * 6;
        for (int i = 0; i < 2; ++i) {
            const double frequency = i == 0 ? 26 : 44;
            coreVelocity_[i] += (-frequency * frequency * core_[i] - 11.2 * coreVelocity_[i]) * dt;
            core_[i] += coreVelocity_[i] * dt;
            waveVelocity_[i] += (-26 * 26 * wave_[i] - 11.2 * waveVelocity_[i]) * dt;
            wave_[i] += waveVelocity_[i] * dt;
        }
        landed_ = gas_ >= capacity;
    }

    bool BubbleFormation::landed() const { return landed_; }

    void BubbleFormation::writeFrame(BubbleFrame &frame) const {
        if (!usable(frame.size) || !usable(size_)) return;
        const double scale = std::min(frame.size.x, frame.size.y);
        const double width = frame.size.x / scale, height = frame.size.y / scale;
        const double capacity = width * height;
        const double oldShort = std::min(size_.x, size_.y);
        const double sx = width / (size_.x / oldShort);
        const double sy = height / (size_.y / oldShort);
        frame.formationCount = foam_.write(
            std::span(frame.formation).first(kBubbleFoamCapacity), scale, {sx, sy}, progress_ * 2);
        auto &w = frame.window;
        w = {};
        if (!exists_) {
            w.content = 0;
            w.motion.carry = {inlet_.x * sx * scale, inlet_.y * sy * scale};
            return;
        }
        const double projectedGas = gas_ * sx * sy;
        const Vec2 centre = feasible({position_.x * sx, position_.y * sy},
            std::min(projectedGas, capacity), width, height);
        const Shape primary = shape(std::min(projectedGas, capacity), width, height, centre);
        const double fill = std::clamp(projectedGas / capacity, 0.0, 1.0);
        const double residual = 1 - smoothstep(.92, 1, progress_);
        w.halfSize = {primary.x * scale, primary.y * scale};
        w.corner = primary.corner * scale;
        w.motion.carry = mul(centre, scale);
        w.depth = scale * std::min(primary.x, primary.y) * (1 - fill);
        w.core = {core_[0] * scale * residual, core_[1] * scale * residual};
        w.motion.waveCos[0] = wave_[0] * scale * residual;
        w.motion.waveSin[0] = wave_[1] * scale * residual;
        w.content = smoothstep(.02, .95, fill);
        w.film = 1 - smoothstep(.8, 1, progress_);
        // The membrane takes over from the film it grew from; both show the same bubble.
        const double takeover = params_.count > 0 ? smoothstep(emerged_, emerged_ + .1, progress_)
                                                  : smoothstep(0, .09, progress_);
        w.visibility = takeover;
        const double settled = 720 - 270 * fill;
        w.thickness = origin_.live ? origin_.thickness + (settled - origin_.thickness) * smoothstep(0, .35, fill)
                                   : settled;
        w.flow = progress_ * 2;
        w.time = progress_;
        if (origin_.live && takeover < 1) {
            for (std::size_t i = frame.formationCount; i < kBubbleFoamCapacity; ++i)
                frame.formation[i].visibility = 0;
            auto &g = frame.formation[kBubbleFoamCapacity];
            g = {};
            g.centre = g.anchor = w.motion.carry;
            g.radius = std::sqrt(std::min(projectedGas, capacity) / pi) * scale;
            g.growth = 1;
            g.thickness = origin_.thickness;
            g.seed = origin_.seed;
            g.flow = progress_ * 2 + origin_.phase + origin_.flow;
            g.visibility = 1 - takeover;
            frame.formationCount = kBubbleFormationCapacity;
        }
    }
}  // namespace hyprimate
