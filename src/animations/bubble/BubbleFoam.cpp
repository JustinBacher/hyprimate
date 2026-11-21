#include "animations/bubble/BubbleFoam.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hyprimate {
    namespace {
        constexpr double PI_VALUE = std::numbers::pi;
        Vec2 add(Vec2 lhs, Vec2 rhs) { return {.x = lhs.x + rhs.x, .y = lhs.y + rhs.y}; }
        Vec2 sub(Vec2 lhs, Vec2 rhs) { return {.x = lhs.x - rhs.x, .y = lhs.y - rhs.y}; }
        Vec2 mul(Vec2 vector, double scale) { return {.x = vector.x * scale, .y = vector.y * scale}; }
        double dot(Vec2 lhs, Vec2 rhs) { return (lhs.x * rhs.x) + (lhs.y * rhs.y); }
        double length(Vec2 vector) { return std::hypot(vector.x, vector.y); }
        // Quadrupole direction of a contact normal: the outline mode it excites.
        Vec2 mode(Vec2 n) { return {.x = (n.x * n.x) - (n.y * n.y), .y = 2 * n.x * n.y}; }

        // Liquid drag relaxes a film toward the local flow; contact forces act through it, so
        // overlaps resolve at a rate rather than by impulse.
        constexpr double K_DRAG = 9;
        // Contact stiffness and capillary attraction (floating bubbles cluster), per unit overlap.
        constexpr double K_REPEL = 190, K_ATTRACT = 90;
        // Deformation beyond this fraction of the smaller radius is not elastic: it is resolved
        // positionally, so dense rafts pack instead of stacking.
        constexpr double K_HARD_OVERLAP = .35;
        // Rates per unit time: neck flow during coalescence and a ruptured film's retraction.
        constexpr double K_NECK_RATE = 45, K_BURST_RATE = 20;
        // n = 2 shape oscillator of a small film: frequency and damping ratio.
        constexpr double K_SHAPE_FREQUENCY = 55, K_SHAPE_DAMPING = .35;
    }

    double bubble_box_reach(Vec2 half, double corner, Vec2 n) {
        // Ray/rounded-box intersection, including the circular corner.
        const double ABS_X = std::abs(n.x);
        const double ABS_Y = std::abs(n.y);
        double distance = std::min(half.x / std::max(ABS_X, 1e-12), half.y / std::max(ABS_Y, 1e-12));
        const double CORNER_X = half.x - corner;
        const double CORNER_Y = half.y - corner;
        if (ABS_X * distance > CORNER_X && ABS_Y * distance > CORNER_Y) {
            const double PROJECTION = (ABS_X * CORNER_X) + (ABS_Y * CORNER_Y);
            distance = PROJECTION + std::sqrt(std::max(0.0, (PROJECTION * PROJECTION) - (CORNER_X * CORNER_X) - (CORNER_Y * CORNER_Y) + (corner * corner)));
        }
        return distance;
    }

    Vec2 BubbleFoamFlow::velocity(Vec2 point) const {
        Vec2 result = drift;
        const Vec2 DELTA = sub(point, source);
        // A 2D source: outflow spreads over circles around it; the core keeps it finite.
        const double RADIUS_SQUARED = dot(DELTA, DELTA) + (.04 * .04);
        result = add(result, mul(DELTA, outflow / (2 * PI_VALUE * RADIUS_SQUARED)));
        for (const auto &eddy : eddies) {
            if (eddy.circulation == 0) {
                continue;
            }
            const Vec2 OFFSET = sub(point, eddy.centre);
            const double STRENGTH = eddy.circulation / (2 * PI_VALUE * (dot(OFFSET, OFFSET) + (eddy.core * eddy.core)));
            result = add(result, Vec2{.x=-OFFSET.y * STRENGTH, .y=OFFSET.x * STRENGTH});
        }
        return result;
    }

    void BubbleFoam::reset(std::uint64_t seed, const BubbleFoamParams &params, std::size_t budget) {
        *this = BubbleFoam{};
        this->params = params;
        this->budget = std::min(budget, films.size());
        random = seed ^ 0xA0761D6478BD642FULL;
    }

    int BubbleFoam::spawn(Vec2 position, Vec2 velocity, double radius, double thickness,
        double surfacing, double life) {
        if (!(radius > 0) || !std::isfinite(position.x) || !std::isfinite(position.y)) {
            return -1;
        }
        for (std::size_t i = 0; i < budget; ++i) {
            auto &film = films[i];
            if (film.live) {
                continue;
            }
            film = {};
            film.live = true;
            film.position = position;
            film.velocity = velocity;
            film.target = radius;
            film.surfacing = std::clamp(surfacing, 0.0, 1.0);
            film.radius = radius * (.3 + (.7 * smoothstep(0, 1, film.surfacing)));
            film.surface_rate = 1 / (.035 + (.05 * unit_draw(random)));
            film.thickness = thickness;
            film.seed = unit_draw(random);
            film.phase = 6 * unit_draw(random);
            film.life = std::max(.02, life);
            film.drainage = .25 + (.75 * unit_draw(random));
            film.adhesion = .6 + (.8 * unit_draw(random));
            return static_cast<int>(i);
        }
        return -1;
    }

    void BubbleFoam::inflate(int slot, double volume) {
        if (slot < 0 || static_cast<std::size_t>(slot) >= budget || !(volume > 0)) {
            return;
        }
        auto &film = films[static_cast<std::size_t>(slot)];
        if (!film.live) {
            return;
        }
        film.radius = std::cbrt((film.radius * film.radius * film.radius) + volume);
        film.target = std::max(film.target, film.radius);
    }

    void BubbleFoam::set_fed(int slot, bool fed) {
        if (slot < 0 || static_cast<std::size_t>(slot) >= budget) {
            return;
        }
        films[static_cast<std::size_t>(slot)].fed = fed;
    }

    void BubbleFoam::expire(double within) {
        for (auto &film : films) {
            if (!film.live || film.fed || film.burst > 0) {
                continue;
            }
            film.life = std::min(film.life, film.age + (std::max(0.0, within) * (.25 + (.7 * film.seed))));
        }
    }

    BubbleFoam::Film BubbleFoam::take(int slot) {
        if (slot < 0 || static_cast<std::size_t>(slot) >= budget) {
            return {};
        }
        auto &film = films[static_cast<std::size_t>(slot)];
        const Film TAKEN = film;
        film = {};
        // Coalescences into it now have nowhere to go; their films stay intact.
        for (auto &other : films) {
            if (other.into == slot) {
                other.into = -1;
            }
        }
        return TAKEN;
    }

    void BubbleFoam::scale(Vec2 axis) {
        for (auto &film : films) {
            film.position = {.x=film.position.x * axis.x, .y=film.position.y * axis.y};
            film.velocity = {.x=film.velocity.x * axis.x, .y=film.velocity.y * axis.y};
        }
    }

    int BubbleFoam::nearest(Vec2 point, double reach) const {
        int best = -1;
        double best_gap = reach;
        for (std::size_t i = 0; i < budget; ++i) {
            const auto &film = films[i];
            if (!film.live || film.burst > 0 || film.into != -1) {
                continue;
            }
            const double GAP = length(sub(film.position, point)) - film.radius;
            if (GAP <= best_gap) {
                best_gap = GAP;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    int BubbleFoam::largest() const {
        int best = -1;
        for (std::size_t i = 0; i < budget; ++i) {
            const auto &film = films[i];
            if (!film.live || film.burst > 0 || film.into != -1) {
                continue;
            }
            if (best < 0 || film.radius > films[static_cast<std::size_t>(best)].radius) {
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    std::size_t BubbleFoam::live_count() const {
        return static_cast<std::size_t>(
            std::count_if(films.begin(), films.end(), [](const Film &film) { return film.live; }));
    }

    void BubbleFoam::interact(double elapsed) {
        const double INTERACTION = params.interaction;
        for (std::size_t i = 0; i < budget; ++i) {
            auto &first = films[i];
            if (!first.live || first.burst > 0) {
                continue;
            }
            for (std::size_t j = i + 1; j < budget; ++j) {
                auto &second = films[j];
                if (!second.live || second.burst > 0) {
                    continue;
                }
                // A coalescing pair is one film already: the neck, not contact, governs it.
                if (first.into == static_cast<int>(j) || second.into == static_cast<int>(i)) {
                    continue;
                }
                const Vec2 DELTA = sub(second.position, first.position);
                const double DISTANCE = length(DELTA);
                const double SMALL = std::min(first.radius, second.radius);
                const double OVERLAP = first.radius + second.radius - DISTANCE;
                if (OVERLAP <= -.6 * SMALL || DISTANCE < 1e-9) {
                    continue;
                }
                const Vec2 NORMAL = mul(DELTA, 1 / DISTANCE);
                // Wetted area sets the share of a contact each film takes.
                const double MASS_FIRST = first.radius * first.radius;
                const double MASS_SECOND = second.radius * second.radius;
                const double WEIGHT_FIRST = MASS_SECOND / (MASS_FIRST + MASS_SECOND);
                const double WEIGHT_SECOND = MASS_FIRST / (MASS_FIRST + MASS_SECOND);
                const double ADHESION = std::min(first.adhesion, second.adhesion);
                const double ACCEL = OVERLAP > 0 ? INTERACTION * K_REPEL * OVERLAP
                                                 : -INTERACTION * K_ATTRACT * ADHESION * (OVERLAP + (.6 * SMALL)) *
                                                       std::min(1.0, -OVERLAP / (.15 * SMALL));
                first.velocity = sub(first.velocity, mul(NORMAL, ACCEL * WEIGHT_FIRST * elapsed));
                second.velocity = add(second.velocity, mul(NORMAL, ACCEL * WEIGHT_SECOND * elapsed));
                // Adhesion holds touching films at a shared wall; that counts as contact.
                const double TOUCH = OVERLAP + (.08 * SMALL);
                if (TOUCH <= 0) {
                    continue;
                }
                if (OVERLAP > 0) {
                    const double HARD = std::min(OVERLAP - (K_HARD_OVERLAP * SMALL), .1 * SMALL);
                    if (HARD > 0) {
                        first.position = sub(first.position, mul(NORMAL, HARD * WEIGHT_FIRST));
                        second.position = add(second.position, mul(NORMAL, HARD * WEIGHT_SECOND));
                    }
                    // Both outlines flatten along the shared wall.
                    const Vec2 SHAPE_MODE = mode(NORMAL);
                    first.squash_rate = sub(first.squash_rate,
                        mul(SHAPE_MODE, K_SHAPE_FREQUENCY * K_SHAPE_FREQUENCY * std::min(.25, OVERLAP / (2 * first.radius)) * elapsed));
                    second.squash_rate = sub(second.squash_rate,
                        mul(SHAPE_MODE, K_SHAPE_FREQUENCY * K_SHAPE_FREQUENCY * std::min(.25, OVERLAP / (2 * second.radius)) * elapsed));
                }
                if (first.into != -1 || second.into != -1) {
                    continue;
                }
                // The film between them drains while they touch, then ruptures; pressing speeds it.
                // A smaller film's higher Laplace pressure drives it into the larger one sooner.
                const double RATIO = SMALL / std::max(first.radius, second.radius);
                const double PRESSED = std::min(1.0, TOUCH / (.25 * SMALL));
                const double DRAINAGE = std::min(first.drainage, second.drainage);
                // Air flows into the fed film from every bubble it presses: it coarsens fastest.
                const double HAZARD = elapsed * PRESSED * (1 + (2.5 * (1 - RATIO))) / DRAINAGE *
                                      (first.fed || second.fed ? 4 : 1);
                if (unit_draw(random) < HAZARD) {
                    // Air flows into a fed film; otherwise the smaller film empties.
                    Film &donor = first.fed ? second : second.fed ? first : (first.radius < second.radius ? first : second);
                    donor.into = static_cast<int>(&donor == &first ? j : i);
                }
            }
        }
    }

    void BubbleFoam::meet(double elapsed, BubbleFoamBody &body) {
        const double INTERACTION = params.interaction;
        const double BODY_MASS = std::max(body.mass, 1e-6);
        for (std::size_t i = 0; i < budget; ++i) {
            auto &film = films[i];
            if (!film.live || film.burst > 0 || film.into != -1) {
                continue;
            }
            const Vec2 DELTA = sub(film.position, body.centre);
            const double DIST = length(DELTA);
            const Vec2 NORMAL = DIST > 1e-9 ? mul(DELTA, 1 / DIST) : Vec2{.x=1, .y=0};
            const double EDGE = bubble_box_reach(body.half, body.corner, NORMAL);
            const double OVERLAP = EDGE + film.radius - DIST;
            if (OVERLAP <= -.6 * film.radius) {
                continue;
            }
            // Contact is with an inflating membrane, not a stationary obstacle: its outward speed
            // must push neighbours before its expansion engulfs them.
            const double SURFACE_SPEED =
                (EDGE - bubble_box_reach(body.previous_half, body.previous_corner, NORMAL)) / elapsed;
            const double COMPRESSION = std::max(0.0, OVERLAP);
            const double RELATIVE = dot(sub(film.velocity, body.velocity), NORMAL) - SURFACE_SPEED;
            const double ACCEL = OVERLAP > 0 ? INTERACTION * K_REPEL * COMPRESSION
                                             : -INTERACTION * K_ATTRACT * film.adhesion * (OVERLAP + (.6 * film.radius)) *
                                                   std::min(1.0, -OVERLAP / (.15 * film.radius));
            const double MASS = film.radius * film.radius;
            film.velocity = add(film.velocity, mul(NORMAL, ACCEL * elapsed));
            // An advancing wall carries a pressed film with it (an inelastic push), so the film is
            // shoved aside continuously instead of being caught and expelled.
            if (OVERLAP > 0 && RELATIVE < 0) {
                film.velocity = sub(film.velocity, mul(NORMAL, RELATIVE));
            }
            body.velocity = sub(body.velocity, mul(NORMAL, ACCEL * elapsed * std::min(1.0, MASS / BODY_MASS)));
            if (OVERLAP <= 0) {
                continue;
            }
            if (OVERLAP > film.radius * 1.2 && body.room > 0) {
                // Overrun by the wall: it is already inside, so it joins the body.
                film.into = INTO_BODY;
                continue;
            }
            const double HARD = COMPRESSION - (K_HARD_OVERLAP * film.radius);
            if (HARD > 0) {
                film.position = add(film.position, mul(NORMAL, std::min(HARD, .1 * film.radius)));
            }
            const Vec2 SHAPE_MODE = mode(NORMAL);
            film.squash_rate = sub(film.squash_rate,
                mul(SHAPE_MODE, K_SHAPE_FREQUENCY * K_SHAPE_FREQUENCY * std::min(.3, COMPRESSION / (2 * film.radius)) * elapsed));
            const double DRIVE = (COMPRESSION * 12) + (std::abs(RELATIVE) * .15);
            body.stir += DRIVE * elapsed;
            body.quadrupole = add(body.quadrupole, mul(SHAPE_MODE, DRIVE * elapsed));
            const double PRESSED = std::min(1.0, COMPRESSION / (.2 * film.radius));
            if (body.room > 0 && unit_draw(random) < elapsed * PRESSED * 1.2 / film.drainage) {
                film.into = INTO_BODY;
            }
        }
    }

    void BubbleFoam::coalesce(double elapsed, BubbleFoamBody *body) {
        const double FLOW_RATE = 1 - std::exp(-K_NECK_RATE * elapsed);
        for (std::size_t i = 0; i < budget; ++i) {
            auto &film = films[i];
            if (!film.live || film.into == -1) {
                continue;
            }
            const double VOLUME = film.radius * film.radius * film.radius;
            // The neck widens as it drains; the last droplet-sized remnant goes all at once.
            double moved = std::cbrt(VOLUME * (1 - FLOW_RATE)) < .004 ? VOLUME : VOLUME * FLOW_RATE;
            if (film.into == INTO_BODY) {
                if (body == nullptr) {
                    film.into = -1;
                    continue;
                }
                // The body is a flattened film: it receives projected area. A body that has filled
                // its space cannot take more; the film stays a separate bubble.
                const double AREA = PI_VALUE * film.radius * film.radius * (moved / VOLUME);
                const double ACCEPTED = std::min(AREA, std::max(0.0, body->room - body->absorbed));
                if (ACCEPTED <= 0) {
                    film.into = -1;
                    continue;
                }
                moved *= ACCEPTED / AREA;
                body->absorbed += ACCEPTED;
                body->velocity = add(body->velocity,
                    mul(sub(film.velocity, body->velocity), std::min(1.0, ACCEPTED / std::max(body->mass, 1e-6))));
                body->stir += ACCEPTED * 30;
                const Vec2 PULL = sub(body->centre, film.position);
                film.velocity = add(film.velocity, mul(PULL, 14 * elapsed));
            } else {
                if (film.into < 0 || static_cast<std::size_t>(film.into) >= budget) {
                    film.into = -1;
                    continue;
                }
                auto &recipient = films[static_cast<std::size_t>(film.into)];
                if (!recipient.live || recipient.burst > 0) {
                    film.into = -1;
                    continue;
                }
                const double RECIPIENT_VOLUME = recipient.radius * recipient.radius * recipient.radius;
                // Momentum and air both pass through the neck; the drained film is drawn in.
                recipient.velocity = mul(add(mul(recipient.velocity, RECIPIENT_VOLUME), mul(film.velocity, moved)), 1 / (RECIPIENT_VOLUME + moved));
                recipient.radius = std::cbrt(RECIPIENT_VOLUME + moved);
                recipient.target = std::max(recipient.target, recipient.radius);
                const Vec2 AXIS = sub(film.position, recipient.position);
                const double AXIS_LENGTH = length(AXIS);
                if (AXIS_LENGTH > 1e-9) {
                    // The merged film rings: stretched along the neck, then oscillating back.
                    recipient.squash_rate = add(recipient.squash_rate, mul(mode(mul(AXIS, 1 / AXIS_LENGTH)), 4 * moved / (RECIPIENT_VOLUME + moved)));
                    film.velocity = add(film.velocity, mul(AXIS, -14 * elapsed));
                }
            }
            const double REMAINING = std::max(0.0, VOLUME - moved);
            film.radius = std::cbrt(REMAINING);
            if (film.radius < .0015) {
                film = {};
            }
        }
    }

    void BubbleFoam::step(double elapsed, const BubbleFoamFlow &flow, BubbleFoamBody *body) {
        if (!(elapsed > 0)) {
            return;
        }
        if (body != nullptr) {
            body->absorbed = body->stir = 0, body->quadrupole = {};
        }
        interact(elapsed);
        if (body != nullptr) {
            meet(elapsed, *body);
        }
        coalesce(elapsed, body);
        const double DRAG = 1 - std::exp(-K_DRAG * params.damping * elapsed);
        const Vec2 RISE{.x=0, .y=-.05 * params.buoyancy};
        for (auto &film : films) {
            if (!film.live) {
                continue;
            }
            film.age += elapsed;
            if (film.surfacing < 1) {
                film.surfacing = std::min(1.0, film.surfacing + (film.surface_rate * elapsed));
                film.radius = std::max(film.radius, film.target * (.3 + (.7 * smoothstep(0, 1, film.surfacing))));
            }
            if (film.fed) {
                film.life = std::max(film.life, film.age + .05);
            }
            if (film.burst > 0 || (film.age >= film.life && film.into == -1)) {
                film.burst += elapsed * K_BURST_RATE;
                if (film.burst >= 1) {
                    film = {};
                    continue;
                }
            }
            // The liquid carries it; larger films sit deeper in the flow and lag it slightly.
            const Vec2 CARRIED = add(flow.velocity(film.position), RISE);
            film.velocity = add(film.velocity, mul(sub(CARRIED, film.velocity), DRAG));
            if (flow.confine.x > 0 && flow.confine.y > 0) {
                // Soft confinement: films may protrude past the region but are drawn back.
                const double WALL_X = std::abs(film.position.x) - flow.confine.x - (.5 * film.radius);
                const double WALL_Y = std::abs(film.position.y) - flow.confine.y - (.5 * film.radius);
                if (WALL_X > 0) {
                    film.velocity.x -= std::copysign(60 * WALL_X * elapsed, film.position.x);
                }
                if (WALL_Y > 0) {
                    film.velocity.y -= std::copysign(60 * WALL_Y * elapsed, film.position.y);
                }
            }
            film.position = add(film.position, mul(film.velocity, elapsed));
            const double FREQUENCY = K_SHAPE_FREQUENCY;
            film.squash_rate = add(film.squash_rate, mul(add(mul(film.squash, -FREQUENCY * FREQUENCY), mul(film.squash_rate, -2 * K_SHAPE_DAMPING * FREQUENCY)), elapsed));
            film.squash = add(film.squash, mul(film.squash_rate, elapsed));
            film.flow += elapsed * (1 + (6 * length(film.squash)));
        }
    }

    std::size_t BubbleFoam::write(std::span<BubbleMicro> out, double unit, Vec2 axis, double flow_phase) const {
        std::size_t count = 0;
        for (std::size_t i = 0; i < budget && i < out.size(); ++i) {
            auto &micro = out[i];
            micro = {};
            micro.visibility = 0;
            const auto &film = films[i];
            if (!film.live) {
                continue;
            }
            count = i + 1;
            micro.centre = {.x=film.position.x * axis.x * unit, .y=film.position.y * axis.y * unit};
            micro.anchor = micro.centre;
            micro.radius = film.radius * unit;
            micro.growth = film.surfacing;
            micro.collapse = film.burst;
            // Drainage thins the wall toward rupture; its colour follows.
            micro.thickness = film.thickness * (1 - (.45 * smoothstep(0, 1, film.age / film.life)));
            micro.seed = film.seed;
            micro.flow = flow_phase + film.phase + film.flow;
            micro.visibility = 1;
            micro.squash = {.x=std::clamp(film.squash.x, -.14, .14), .y=std::clamp(film.squash.y, -.14, .14)};
        }
        return count;
    }
}  // namespace hyprimate
