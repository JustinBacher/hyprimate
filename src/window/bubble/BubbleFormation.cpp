#include "window/bubble/BubbleFormation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

#include "window/bubble/Bubble.hpp"

namespace hyprimate {
    namespace {
        constexpr double PI_VALUE = std::numbers::pi;
        constexpr double TIME_STEP = 1.0 / 360.0;
        constexpr double CORNER_AREA = 4.0 - PI_VALUE;
        // Progress at which air starts going into the bubble over the inlet, the latest it may
        // take over as the window bubble, and when the window bubble must have filled the box.
        constexpr double FEED = .3, LATEST_EMERGENCE = .68, LAND = .88;
        // The window bubble's inflation keeps the profile tuned on its original timeline, from
        // .05 to the deadline .87 of that timeline, compressed into what remains after it forms.
        constexpr double TIMELINE_START = .05, TIMELINE_END = .87;
        struct Shape { double x, y, corner; };

        constexpr double MIN_SIZE = 1e-9;
        bool usable(Vec2 size) {
            return std::isfinite(size.x) && std::isfinite(size.y) && size.x >= MIN_SIZE &&
                   size.y >= MIN_SIZE && size.x <= 1e9 && size.y <= 1e9;
        }
        Vec2 add(Vec2 lhs, Vec2 rhs) { return {.x = lhs.x + rhs.x, .y = lhs.y + rhs.y}; }
        Vec2 sub(Vec2 lhs, Vec2 rhs) { return {.x = lhs.x - rhs.x, .y = lhs.y - rhs.y}; }
        Vec2 mul(Vec2 vector, double scale) { return {.x = vector.x * scale, .y = vector.y * scale}; }
        double gaussian(std::uint64_t &random) {
            const double DRAW_U = std::max(1e-12, unit_draw(random));
            const double DRAW_V = unit_draw(random);
            return std::sqrt(-2 * std::log(DRAW_U)) * std::cos(2 * PI_VALUE * DRAW_V);
        }

        // The available half extents are measured from the actual centre to
        // the nearest wall. Once contact starts, excess inflation flattens the
        // rounded ends as well as extending the free axis. Projected area is
        // conserved; there is no capsule-only wait for the remaining walls.
        Shape shape(double area, double width, double height, Vec2 position) {
            const double HALF_X = std::max(1e-12, (width * .5) - std::abs(position.x));
            const double HALF_Y = std::max(1e-12, (height * .5) - std::abs(position.y));
            const double RADIUS = std::sqrt(std::max(0.0, area) / PI_VALUE);
            const double SMALL = std::min(HALF_X, HALF_Y);
            const double LARGE = std::max(HALF_X, HALF_Y);
            if (RADIUS <= SMALL) { return {.x=RADIUS, .y=RADIUS, .corner=RADIUS}; }
            const double CONTACT_AREA = PI_VALUE * SMALL * SMALL;
            const double CONTACT_CORNER = SMALL * CONTACT_AREA / area;
            const double OTHER = (area + (CORNER_AREA * CONTACT_CORNER * CONTACT_CORNER)) / (4 * SMALL);
            if (OTHER < LARGE) {
                return HALF_X < HALF_Y ? Shape{.x=SMALL, .y=OTHER, .corner=CONTACT_CORNER} : Shape{.x=OTHER, .y=SMALL, .corner=CONTACT_CORNER};
            }
            const double CORNER = std::sqrt(std::max(0.0, ((4 * HALF_X * HALF_Y) - area) / CORNER_AREA));
            return {.x=HALF_X, .y=HALF_Y, .corner=std::min(SMALL, CORNER)};
        }
        // If both axes are exhausted, wall reaction must move the centre before
        // a rounded rectangle can contain the gas. Project only this infeasible
        // excess; ordinary first contact is handled by the area-derived shape.
        Vec2 feasible(Vec2 position, double area, double width, double height) {
            position.x = std::clamp(position.x, -width * .5, width * .5);
            position.y = std::clamp(position.y, -height * .5, height * .5);
            const auto AVAILABLE = [&](double fraction) {
                return (width - (2 * std::abs(position.x) * fraction)) *
                       (height - (2 * std::abs(position.y) * fraction));
            };
            if (AVAILABLE(1) >= area) { return position; }
            double lower = 0;
            double upper = 1;
            for (int i = 0; i < 36; ++i) {
                const double MID = (lower + upper) * .5;
                if (AVAILABLE(MID) >= area) { lower = MID;
                } else { upper = MID; }
            }
            return mul(position, lower);
        }
    }

    void BubbleFormation::begin(Vec2 size, std::uint64_t seed, const BubbleFormationParams &params) {
        const BubbleFormationParams REQUESTED = params;
        *this = BubbleFormation{};
        const auto BOUNDED = [](double value, double fallback, double low, double high) {
            return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
        };
        m_params.count = std::clamp(REQUESTED.count, 0, static_cast<int>(BUBBLE_FOAM_CAPACITY));
        m_params.birthRate = BOUNDED(REQUESTED.birthRate, 1, .25, 2);
        m_params.sizeVariation = BOUNDED(REQUESTED.sizeVariation, .75, 0, 1);
        m_params.growthVariation = BOUNDED(REQUESTED.growthVariation, .6, 0, 1);
        m_params.interaction = BOUNDED(REQUESTED.interaction, 1, .25, 2);
        m_params.damping = BOUNDED(REQUESTED.damping, 1, .5, 2);
        m_params.buoyancy = BOUNDED(REQUESTED.buoyancy, 1, 0, 2);
        m_params.linger = BOUNDED(REQUESTED.linger, .5, 0, 1);
        m_seed = seed;
        if (!usable(size)) { return; }
        m_size = size;
        m_random = seed;
        m_foam.reset(seed, {.interaction=m_params.interaction, .damping=m_params.damping, .buoyancy=m_params.buoyancy},
            static_cast<std::size_t>(m_params.count));
        const double SHORT_SIDE = std::min(size.x, size.y);
        const double WIDTH = size.x / SHORT_SIDE;
        const double HEIGHT = size.y / SHORT_SIDE;
        // The inlet wanders through the window's middle on two slow periods.
        m_wander[0] = {.x=std::min(.12, WIDTH * .16), .y=std::min(.12, HEIGHT * .16)};
        m_wander[1] = {.x=2 * PI_VALUE * unit_draw(m_random), .y=2 * PI_VALUE * unit_draw(m_random)};
        for (int i = 0; i < 2; ++i) {
            m_eddy_centre[i] = {.x=(unit_draw(m_random) - .5) * .6 * WIDTH, .y=(unit_draw(m_random) - .5) * .6 * HEIGHT};
            m_eddy_spin[i] = (i == 0 ? 1 : -1) * (.1 + (.08 * unit_draw(m_random)));
        }
        m_inlet = {.x=m_wander[0].x * std::sin(m_wander[1].x), .y=m_wander[0].y * std::sin(m_wander[1].y)};
        for (std::size_t i = 0; i < m_plumes.size(); ++i) {
            // The first stream is the straw itself; the others surface around it.
            const double REACH = i == 0 ? 0 : .08 + (.18 * unit_draw(m_random));
            const double ANGLE = 2 * PI_VALUE * unit_draw(m_random);
            m_plumes[i].offset = {.x=REACH * std::cos(ANGLE), .y=REACH * std::sin(ANGLE)};
            m_plumes[i].spin = (unit_draw(m_random) < .5 ? -1 : 1) * (.5 + unit_draw(m_random));
        }
        m_birth_threshold = -std::log(1 - unit_draw(m_random));
        if (m_params.count == 0) {
            // No companions: the window bubble itself is the first film.
            m_exists = true;
            m_gas = .00045;
            m_corner = std::sqrt(m_gas / PI_VALUE);
            m_position = {.x=0, .y=.025};
            m_pace = (TIMELINE_END - TIMELINE_START) / LAND;
        }
    }

    void BubbleFormation::advance(double progress, Vec2 size) {
        if (!usable(size) || !std::isfinite(progress)) { return; }
        if (!usable(m_size)) { begin(size, m_seed, m_params); }
        if (size.x != m_size.x || size.y != m_size.y) {
            // Preserve fractional fill and normalized positions on resize.
            const double OLD_SHORT = std::min(m_size.x, m_size.y);
            const double NEW_SHORT = std::min(size.x, size.y);
            const double SCALE_X = (size.x / NEW_SHORT) / (m_size.x / OLD_SHORT);
            const double SCALE_Y = (size.y / NEW_SHORT) / (m_size.y / OLD_SHORT);
            m_gas *= SCALE_X * SCALE_Y;
            m_reservoir *= SCALE_X * SCALE_Y;
            m_position = {.x=m_position.x * SCALE_X, .y=m_position.y * SCALE_Y};
            m_velocity = {.x=m_velocity.x * SCALE_X, .y=m_velocity.y * SCALE_Y};
            m_inlet = {.x=m_inlet.x * SCALE_X, .y=m_inlet.y * SCALE_Y};
            m_foam.scale({.x=SCALE_X, .y=SCALE_Y});
            m_size = size;
        }
        const int TARGET = static_cast<int>(std::floor((std::clamp(progress, 0.0, 1.0) * 360) + 1e-9));
        while (m_ticks < TARGET) {
            ++m_ticks;
            m_progress = m_ticks * TIME_STEP;
            step();
        }
    }

    void BubbleFormation::nucleate(double step, double progress, double width, double height) {
        // Air arrives in puffs through a few streams around the inlet; each surfaces as a tight
        // clump of bubbles, so the raft forms in groups rather than as an even scatter.
        double total = 0;
        for (auto &plume : m_plumes) {
            plume.clock -= step;
            if (plume.clock <= 0) {
                plume.target = unit_draw(m_random) < .35 ? 0 : .5 + unit_draw(m_random);
                plume.clock = .04 + (.1 * unit_draw(m_random));
            }
            plume.strength += (plume.target - plume.strength) * (1 - std::exp(-step / .02));
            // The streams wander with the liquid; a stream never moves far from the inlet.
            const double TURN = plume.spin * step;
            plume.offset = {.x=(plume.offset.x * std::cos(TURN)) - (plume.offset.y * std::sin(TURN)),
                .y=(plume.offset.x * std::sin(TURN)) + (plume.offset.y * std::cos(TURN))};
            total += plume.strength;
        }
        const double RATE = 1100 * m_params.birthRate * smoothstep(0, .03, progress) *
                            (1 - smoothstep(.5, .8, progress)) * (m_exists ? .55 : 1) *
                            total / static_cast<double>(m_plumes.size());
        m_birth_hazard += RATE * step;
        const double SHORT_REACH = std::sqrt(m_gas / PI_VALUE);
        // Keep one slot for the inlet film until it has been selected or created.
        std::size_t free_slots =
            !m_exists && m_fed < 0 && m_birth_hazard >= m_birth_threshold && total > 0
                ? m_foam.capacity() - m_foam.live_count()
                : 0;
        while (m_birth_hazard >= m_birth_threshold && total > 0) {
            m_birth_hazard -= m_birth_threshold;
            m_birth_threshold = -std::log(1 - unit_draw(m_random));
            const double RADIUS = std::clamp(
                .011 * std::exp((.15 + (.6 * m_params.sizeVariation)) * gaussian(m_random)), .004, .06);
            double pick = unit_draw(m_random) * total;
            std::size_t chosen = 0;
            while (chosen + 1 < m_plumes.size() && pick > m_plumes[chosen].strength) {
                pick -= m_plumes[chosen++].strength;
            }
            const Vec2 SOURCE = add(m_inlet, Vec2{.x=m_plumes[chosen].offset.x * std::sqrt(width),
                                                .y=m_plumes[chosen].offset.y * std::sqrt(height)});
            const double CLUMP = .02 + (.03 * smoothstep(0, .45, progress));
            Vec2 point = add(SOURCE, Vec2{.x=gaussian(m_random) * CLUMP, .y=gaussian(m_random) * CLUMP});
            // Air rising under the clump surfaces against a bubble already there.
            const int NEIGHBOUR = unit_draw(m_random) < .7 ? m_foam.nearest(point, .04) : -1;
            if (NEIGHBOUR >= 0) {
                const auto &neighbour = m_foam.film(NEIGHBOUR);
                const double ANGLE = 2 * PI_VALUE * unit_draw(m_random);
                point = add(neighbour.position, mul(Vec2{.x=std::cos(ANGLE), .y=std::sin(ANGLE)}, neighbour.radius + (RADIUS * .9)));
            }
            point.x = std::clamp(point.x, (-width * .5) + RADIUS, (width * .5) - RADIUS);
            point.y = std::clamp(point.y, (-height * .5) + RADIUS, (height * .5) - RADIUS);
            if (m_exists) {
                // Never inside the window bubble: air reaching it joins it.
                const Vec2 OFFSET = sub(point, m_position);
                const double DISTANCE = std::hypot(OFFSET.x, OFFSET.y);
                if (DISTANCE < SHORT_REACH + RADIUS) { continue; }
            }
            // Wall thickness is the film's own (formation, drainage), not its size.
            const double THICKNESS = 500 + (380 * unit_draw(m_random));
            const double LIFE = (.22 + (.45 * unit_draw(m_random))) * (.7 + (.6 * m_params.linger));
            if (!m_exists && m_fed < 0 && free_slots <= 1) {
                continue;
            }
            const int SLOT = m_foam.spawn(point, {}, RADIUS, THICKNESS, 0, LIFE);
            if (SLOT < 0) { continue; }
            if (!m_exists && m_fed < 0) {
                --free_slots;
            }
            m_foam.film(SLOT).surface_rate *=
                std::exp(1.4 * m_params.growthVariation * (unit_draw(m_random) - .5));
        }
    }

    void BubbleFormation::feed(double step, double progress) {
        if (progress < FEED) { return; }
        const auto VALID = [&](int slot) {
            if (slot < 0) { return false; }
            const auto &film = m_foam.film(slot);
            return film.live && film.burst == 0 && film.into == -1;
        };
        if (!VALID(m_fed)) {
            if (m_fed >= 0 && m_foam.film(m_fed).live) { m_foam.set_fed(m_fed, false); }
            m_fed = m_foam.nearest(m_inlet, .015);
            if (m_fed < 0) {
                m_fed = m_foam.spawn(m_inlet, {}, .03, 500 + (380 * unit_draw(m_random)), .3, 1);
            }
            if (m_fed < 0) { return; }
            m_foam.set_fed(m_fed, true);
        }
        // The air stream follows the bubble it fills; the stream's own wander still steers it.
        auto &film = m_foam.film(m_fed);
        const double RAMP = smoothstep(FEED, FEED + .1, progress);
        // A gentle stream: the bubble over the inlet grows mostly by swallowing the bubbles that
        // press on it, so its growth is seen to come from the mass around it.
        m_foam.inflate(m_fed, .004 * RAMP * m_pump_rate / 1.625 * step);
        const Vec2 TO_INLET = sub(m_inlet, film.position);
        film.velocity = add(film.velocity, mul(TO_INLET, 4 * step));
    }

    void BubbleFormation::emerge(double progress) {
        const auto FILM = m_foam.take(m_fed);
        m_fed = -1;
        m_exists = true;
        m_emerged = progress;
        m_origin = FILM;
        m_gas = PI_VALUE * FILM.radius * FILM.radius;
        m_corner = FILM.radius;
        m_position = FILM.position;
        m_velocity = FILM.velocity;
        // Resume the inflation profile where a bubble of this size would be on its timeline, and
        // compress what remains of it into the time left. Its pump swells from the stream that
        // filled it, so neither the size nor the growth rate jumps.
        const double SHORT_SIDE = std::min(m_size.x, m_size.y);
        const double CAPACITY = m_size.x / SHORT_SIDE * (m_size.y / SHORT_SIDE);
        m_tau_start = std::clamp(TIMELINE_START + (m_gas / (CAPACITY * 1.625)), TIMELINE_START, .4);
        m_pace = (TIMELINE_END - m_tau_start) / std::max(.1, LAND - progress);
        m_inlet_rate = .2 * m_pump_rate;
    }

    double BubbleFormation::inflate(double step, double progress, double width, double height, BubbleFoamBody *body) {
        const double CAPACITY = width * height;
        // Air supply follows its tuned profile on the bubble's own timeline (tau), however late
        // the bubble formed. Contact dynamics and settling keep the transition's pace.
        const double AIR = step * m_pace;
        const double TAU = m_tau_start + ((progress - m_emerged) * m_pace);
        // The new window bubble keeps the pace it grew at for a while, still among the
        // companions, before the full stream takes over.
        const double SWELL = m_origin.live ? .15 + (.85 * smoothstep(m_emerged, m_emerged + .16, progress)) : 1;
        const double PUMP = m_pump_rate * SWELL;
        // Confinement redirects the incoming air into the remaining free axis.
        // Ramp the supply and inlet together so the reservoir cannot starve
        // the faster morph. Free growth and the settling oscillator keep their pace.
        const double CONTACT_RADIUS = std::max(1e-12, std::min(
            (width * .5) - std::abs(m_position.x), (height * .5) - std::abs(m_position.y)));
        const double CONTACT_AREA = PI_VALUE * CONTACT_RADIUS * CONTACT_RADIUS;
        const double CONTACT_SPEED = 1 + smoothstep(CONTACT_AREA, CONTACT_AREA * 1.2, m_gas);
        // The reserve pump only engages as a late deficit develops; it ensures
        // even extreme settings have enough air without prescribing the shape.
        const double RECOVERY = 1.5 * smoothstep(.74, .85, TAU);
        const double RESERVE_RATE = std::max(RECOVERY, smoothstep(.48, .70, TAU) * 1.8 *
            (CAPACITY - m_gas) / (CAPACITY * std::max(.06, .90 - TAU)));
        m_reservoir += CAPACITY * std::max(PUMP * CONTACT_SPEED, RESERVE_RATE) * AIR;
        const Shape BEFORE = shape(std::min(m_gas, CAPACITY), width, height, m_position);
        // Reservoir pressure, compression and the companions' push all influence the inlet. The
        // late pressure regulator supplies a deficit, not a geometry interpolation, and allows
        // the native area to be reached before the deadline.
        const double PRESSURE = m_reservoir / CAPACITY;
        const double DEFICIT_RATE =
            SWELL * (CAPACITY - m_gas) / (CAPACITY * std::max(.04, TIMELINE_END - TAU));
        const double TARGET = std::max({DEFICIT_RATE, RECOVERY,
            ((PUMP * CONTACT_SPEED) + (2 * PRESSURE)) / (1 + (std::abs(m_core[0]) * 2))});
        m_inlet_rate += (TARGET - m_inlet_rate) * (1 - std::exp(-AIR / .035));
        const double INLET = std::min(m_reservoir, CAPACITY * AIR * std::max(0.0, m_inlet_rate));
        // Finite membrane relaxation prevents the square-root area relation
        // from snapping the last rounded corner flat in one integration step.
        // Air waits in the reservoir while surface tension resists that change.
        // Surface tension flattens the last rounded corner at the transition's own pace, however
        // compressed the air supply's timeline is.
        const double RELAXED_CORNER = std::max(0.0, m_corner - (step * CONTACT_SPEED * (2 + (2 * PRESSURE))));
        const double AVAILABLE_GAS = std::max(m_gas, CAPACITY - (CORNER_AREA * RELAXED_CORNER * RELAXED_CORNER));
        const double ACCEPTED = std::min(INLET, std::max(0.0, AVAILABLE_GAS - m_gas));
        m_gas += ACCEPTED;
        m_reservoir -= ACCEPTED;
        m_core_velocity[0] += 80 * ACCEPTED / CAPACITY;
        const Shape AFTER = shape(m_gas, width, height,
            feasible(m_position, m_gas + (CORNER_AREA * RELAXED_CORNER * RELAXED_CORNER), width, height));
        body->centre = m_position;
        body->velocity = m_velocity;
        body->previous_half = {.x=BEFORE.x, .y=BEFORE.y};
        body->previous_corner = BEFORE.corner;
        body->half = {.x=AFTER.x, .y=AFTER.y};
        body->corner = AFTER.corner;
        body->mass = std::max(.035, m_gas);
        body->room = std::max(0.0, AVAILABLE_GAS - m_gas);
        return RELAXED_CORNER;
    }

    void BubbleFormation::step() {
        const double SHORT_SIDE = std::min(m_size.x, m_size.y);
        const double WIDTH = m_size.x / SHORT_SIDE;
        const double HEIGHT = m_size.y / SHORT_SIDE;
        const double CAPACITY = WIDTH * HEIGHT;
        const double PROGRESS = m_progress;
        // Feed events change the pressure target, not positions. Both the pump
        // and inlet have inertia, so their changing flux remains continuous.
        m_feed_clock -= TIME_STEP * (m_exists ? m_pace : 1);
        if (m_feed_clock <= 0) {
            m_feed_target = 1.625 + (.95 * m_params.growthVariation * ((2 * unit_draw(m_random)) - 1));
            m_feed_clock += .045 + (.09 * unit_draw(m_random));
        }
        m_pump_rate += (m_feed_target - m_pump_rate) * (1 - std::exp(-TIME_STEP * (m_exists ? m_pace : 1) / .045));

        const Vec2 WANDER{.x=m_wander[0].x * std::sin((2 * PI_VALUE * .6 * PROGRESS) + m_wander[1].x),
            .y=m_wander[0].y * std::sin((2 * PI_VALUE * .45 * PROGRESS) + m_wander[1].y)};
        if (m_exists) { m_inlet = m_position;
        } else if (m_fed >= 0 && m_foam.film(m_fed).live) { m_inlet = m_foam.film(m_fed).position;
        } else { m_inlet = add(m_inlet, mul(sub(WANDER, m_inlet), 1 - std::exp(-6 * TIME_STEP))); }
        if (!m_exists && m_fed >= 0) {
            // The stream keeps wandering under the bubble it fills.
            auto &film = m_foam.film(m_fed);
            film.velocity = add(film.velocity, mul(sub(WANDER, film.position), 3 * TIME_STEP));
        }

        // The liquid: outflow from where air enters, two slowly turning eddies, calming late.
        BubbleFoamFlow flow;
        flow.source = m_inlet;
        flow.outflow = .012 * std::sqrt(m_params.birthRate) * (1 - smoothstep(.55, .9, PROGRESS));
        flow.confine = {.x=WIDTH * .5, .y=HEIGHT * .5};
        const double CALM = 1 - smoothstep(.7, 1, PROGRESS);
        for (int i = 0; i < 2; ++i) {
            const double TURN = 2 * PI_VALUE * .25 * PROGRESS * (i == 0 ? 1 : -1);
            const Vec2 EDDY_CENTRE = m_eddy_centre[i];
            flow.eddies[static_cast<std::size_t>(i)] = {
                .centre={.x=(EDDY_CENTRE.x * std::cos(TURN)) - (EDDY_CENTRE.y * std::sin(TURN)), .y=(EDDY_CENTRE.x * std::sin(TURN)) + (EDDY_CENTRE.y * std::cos(TURN))},
                .circulation=m_eddy_spin[i] * CALM,
                .core=.18};
        }

        if (m_params.count > 0) { nucleate(TIME_STEP, PROGRESS, WIDTH, HEIGHT); }
        if (!m_exists && m_params.count > 0) {
            feed(TIME_STEP, PROGRESS);
            if (m_fed >= 0) {
                const double RADIUS = m_foam.film(m_fed).radius;
                if ((RADIUS >= .1 && PROGRESS >= .55) || PROGRESS >= LATEST_EMERGENCE) { emerge(PROGRESS); }
            } else if (PROGRESS >= LATEST_EMERGENCE) {
                // Nothing to grow from (a tiny budget): air surfaces as a new bubble.
                m_fed = m_foam.spawn(m_inlet, {}, .03, 450, .5, 1);
                if (m_fed >= 0) { emerge(PROGRESS); }
            }
        }
        if (!m_expired && PROGRESS >= .72 + (.12 * m_params.linger)) {
            // Drainage catches up with every remaining companion before the transition ends.
            m_foam.expire(.975 - PROGRESS);
            m_expired = true;
        }

        if (!m_exists) {
            m_foam.step(TIME_STEP, flow, nullptr);
            return;
        }
        BubbleFoamBody body;
        const double RELAXED_CORNER = inflate(TIME_STEP, PROGRESS, WIDTH, HEIGHT, &body);
        m_foam.step(TIME_STEP, flow, &body);
        // The companions' push and merged air feed back into the window bubble.
        m_velocity = body.velocity;
        const double MERGED = std::min(body.absorbed, body.room);
        m_gas += MERGED;
        m_core_velocity[0] += body.stir;
        m_core_velocity[1] += MERGED / CAPACITY * 12;
        m_wave_velocity[0] += body.quadrupole.x;
        m_wave_velocity[1] += body.quadrupole.y;
        // Wall pressure is a restoring force as soon as the unconstrained
        // circle reaches a wall. Only geometrically impossible excess needs
        // projection; before that the capsule deforms at its actual centre.
        const double FREE_RADIUS = std::sqrt(m_gas / PI_VALUE);
        const double WALL_X = std::max(0.0, FREE_RADIUS + std::abs(m_position.x) - (WIDTH * .5));
        const double WALL_Y = std::max(0.0, FREE_RADIUS + std::abs(m_position.y) - (HEIGHT * .5));
        // Opposing walls cancel as the centre reaches equilibrium. A sign-only
        // restoring force would chatter across zero once both sides are pressed.
        m_velocity.x += 22 * TIME_STEP *
            (std::max(0.0, FREE_RADIUS - m_position.x - (WIDTH * .5)) -
                std::max(0.0, FREE_RADIUS + m_position.x - (WIDTH * .5)));
        m_velocity.y += 22 * TIME_STEP *
            (std::max(0.0, FREE_RADIUS - m_position.y - (HEIGHT * .5)) -
                std::max(0.0, FREE_RADIUS + m_position.y - (HEIGHT * .5)));
        const double WALL_PRESSURE = WALL_X + WALL_Y;
        m_core_velocity[0] += (WALL_PRESSURE - m_wall_pressure) * 3;
        m_wall_pressure = WALL_PRESSURE;
        m_position = add(m_position, mul(m_velocity, TIME_STEP));
        m_velocity = mul(m_velocity, std::exp(-2.2 * m_params.damping * TIME_STEP));
        const Vec2 BEFORE = m_position;
        m_position = feasible(m_position, m_gas + (CORNER_AREA * RELAXED_CORNER * RELAXED_CORNER), WIDTH, HEIGHT);
        m_corner = shape(m_gas, WIDTH, HEIGHT, m_position).corner;
        const Vec2 CORRECTION = sub(m_position, BEFORE);
        if (CORRECTION.x != 0) { m_velocity.x = CORRECTION.x / TIME_STEP * .15; }
        if (CORRECTION.y != 0) { m_velocity.y = CORRECTION.y / TIME_STEP * .15; }
        const double IMPACT = std::hypot(CORRECTION.x, CORRECTION.y);
        m_core_velocity[0] += IMPACT * 20;
        m_core_velocity[1] += IMPACT * 12;
        m_wave_velocity[0] += (CORRECTION.x - CORRECTION.y) * 6;
        for (int i = 0; i < 2; ++i) {
            const double FREQUENCY = i == 0 ? 26 : 44;
            m_core_velocity[i] += ((-FREQUENCY * FREQUENCY * m_core[i]) - (11.2 * m_core_velocity[i])) * TIME_STEP;
            m_core[i] += m_core_velocity[i] * TIME_STEP;
            m_wave_velocity[i] += ((-26 * 26 * m_wave[i]) - (11.2 * m_wave_velocity[i])) * TIME_STEP;
            m_wave[i] += m_wave_velocity[i] * TIME_STEP;
        }
        m_landed = m_gas >= CAPACITY;
    }

    bool BubbleFormation::landed() const { return m_landed; }

    void BubbleFormation::writeFrame(BubbleFrame &frame) const {
        if (!usable(frame.size) || !usable(m_size)) { return; }
        const double SCALE = std::min(frame.size.x, frame.size.y);
        const double WIDTH = frame.size.x / SCALE;
        const double HEIGHT = frame.size.y / SCALE;
        const double CAPACITY = WIDTH * HEIGHT;
        const double OLD_SHORT = std::min(m_size.x, m_size.y);
        const double SCALE_X = WIDTH / (m_size.x / OLD_SHORT);
        const double SCALE_Y = HEIGHT / (m_size.y / OLD_SHORT);
        frame.formation_count = m_foam.write(
            std::span(frame.formation).first(BUBBLE_FOAM_CAPACITY), SCALE, {.x=SCALE_X, .y=SCALE_Y}, m_progress * 2);
        auto &window = frame.window;
        window = {};
        if (!m_exists) {
            window.content = 0;
            window.motion.carry = {.x=m_inlet.x * SCALE_X * SCALE, .y=m_inlet.y * SCALE_Y * SCALE};
            return;
        }
        const double PROJECTED_GAS = m_gas * SCALE_X * SCALE_Y;
        const Vec2 CENTRE = feasible({.x=m_position.x * SCALE_X, .y=m_position.y * SCALE_Y},
            std::min(PROJECTED_GAS, CAPACITY), WIDTH, HEIGHT);
        const Shape PRIMARY = shape(std::min(PROJECTED_GAS, CAPACITY), WIDTH, HEIGHT, CENTRE);
        const double FILL = std::clamp(PROJECTED_GAS / CAPACITY, 0.0, 1.0);
        const double RESIDUAL = 1 - smoothstep(.92, 1, m_progress);
        window.half_size = {.x=PRIMARY.x * SCALE, .y=PRIMARY.y * SCALE};
        window.corner = PRIMARY.corner * SCALE;
        window.motion.carry = mul(CENTRE, SCALE);
        window.depth = SCALE * std::min(PRIMARY.x, PRIMARY.y) * (1 - FILL);
        window.core = {.x=m_core[0] * SCALE * RESIDUAL, .y=m_core[1] * SCALE * RESIDUAL};
        window.motion.wave_cos[0] = m_wave[0] * SCALE * RESIDUAL;
        window.motion.wave_sin[0] = m_wave[1] * SCALE * RESIDUAL;
        window.content = smoothstep(.02, .95, FILL);
        window.film = 1 - smoothstep(.8, 1, m_progress);
        // The membrane takes over from the film it grew from; both show the same bubble.
        const double TAKEOVER = m_params.count > 0 ? smoothstep(m_emerged, m_emerged + .1, m_progress)
                                                  : smoothstep(0, .09, m_progress);
        window.visibility = TAKEOVER;
        const double SETTLED = 720 - (270 * FILL);
        window.thickness = m_origin.live ? m_origin.thickness + ((SETTLED - m_origin.thickness) * smoothstep(0, .35, FILL))
                                   : SETTLED;
        window.flow = m_progress * 2;
        window.time = m_progress;
        if (m_origin.live && TAKEOVER < 1) {
            for (std::size_t i = frame.formation_count; i < BUBBLE_FOAM_CAPACITY; ++i) {
                frame.formation[i].visibility = 0;
            }
            auto &companion = frame.formation[BUBBLE_FOAM_CAPACITY];
            companion = {};
            companion.centre = companion.anchor = window.motion.carry;
            companion.radius = std::sqrt(std::min(PROJECTED_GAS, CAPACITY) / PI_VALUE) * SCALE;
            companion.growth = 1;
            companion.thickness = m_origin.thickness;
            companion.seed = m_origin.seed;
            companion.flow = (m_progress * 2) + m_origin.phase + m_origin.flow;
            companion.visibility = 1 - TAKEOVER;
            frame.formation_count = kBubbleFormationCapacity;
        }
    }
}  // namespace hyprimate
