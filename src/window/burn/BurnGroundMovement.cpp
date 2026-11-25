#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

#include "BurnGroundPrivate.hpp"

using namespace hyprimate;
using namespace burn_ground_detail;
namespace {
    constexpr std::size_t COVERAGE_SIDE = 4, COVERAGE_BITS = COVERAGE_SIDE * COVERAGE_SIDE;
    constexpr std::array SIDES{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};
    struct EdgeSweep {
        bool vertical, low_side;
        double from, to, low_from, low_to, high_from, high_to;

        [[nodiscard]] bool recedes() const noexcept {
            return low_side ? to > from : to < from;
        }
        [[nodiscard]] Vec2 point(double normal, double tangent) const noexcept {
            return vertical ? Vec2{normal, tangent} : Vec2{tangent, normal};
        }
        [[nodiscard]] Vec2 anchor(
            double edge, double low, double high, double fraction, double gap) const noexcept {
            return point(edge + (low_side ? -gap : gap), std::lerp(low, high, fraction));
        }
        [[nodiscard]] Vec2 source(double start, double end, double tangent) const noexcept {
            // The projected tangent interval intersects the strip. Clip its time
            // span to the two linear edge endpoints to find a point inside it.
            const auto CLIP = [&](double begin, double finish) {
                if (finish > begin) end = std::min(end, -begin / (finish - begin));
                if (finish < begin) start = std::max(start, -begin / (finish - begin));
            };
            CLIP(low_from - tangent, low_to - tangent);
            CLIP(tangent - high_from, tangent - high_to);
            return point(std::lerp(from, to, (start + end) * .5), tangent);
        }
    };
    EdgeSweep edge_sweep(Box from, Box to, Edge side) noexcept {
        const bool VERTICAL = side == Edge::Left || side == Edge::Right;
        const bool LOW = side == Edge::Left || side == Edge::Top;
        if (VERTICAL) {
            return {true,
                LOW,
                LOW ? from.x : from.x + from.w,
                LOW ? to.x : to.x + to.w,
                from.y,
                to.y,
                from.y + from.h,
                to.y + to.h};
        }
        return {false,
            LOW,
            LOW ? from.y : from.y + from.h,
            LOW ? to.y : to.y + to.h,
            from.x,
            to.x,
            from.x + from.w,
            to.x + to.w};
    }
    double separation_squared(Vec2 a, Vec2 b) noexcept {
        const double X = a.x - b.x, Y = a.y - b.y;
        return X * X + Y * Y;
    }
}  // namespace

float CBurnGround::Cell::movement_damage() const noexcept {
    const auto &movement = m_movement;
    return scorch_damage(movement.severity, movement.age, movement.lifetime, movement.healing);
}
float CBurnGround::Cell::movement_heat() const noexcept {
    const auto &movement = m_movement;
    return movement.severity > 0
               ? movement.severity * static_cast<float>(std::exp(-movement.age * 6.5))
               : 0;
}
float CBurnGround::Cell::kindle(
    std::uint16_t bit, std::size_t index, float stay, float recover) noexcept {
    const bool PREVIOUSLY_BURNED = (m_coverage & bit) != 0;
    auto &movement = m_movement;
    if (movement.age > 0) {
        if (PREVIOUSLY_BURNED && movement.age <= movement.lifetime) return 0;
        const float VISIBLE = movement_damage();
        const float RECOVERED = movement.severity > 0 ? 1 - VISIBLE / movement.severity : 0;
        // Keep the still-visible char and the available regrown fraction
        // separate. Each touched tile can consume its share only once.
        constexpr float MIN_REGROWTH = .04F;
        if (PREVIOUSLY_BURNED && RECOVERED < MIN_REGROWTH) return 0;
        m_regrown = RECOVERED >= MIN_REGROWTH ? m_coverage : 0;
        movement.regrowth = RECOVERED;
        movement.severity = VISIBLE;
    } else if (PREVIOUSLY_BURNED && (m_regrown & bit) == 0) {
        return 0;
    }

    const auto COVERED = std::popcount(m_coverage);
    std::uint64_t hash = (index + 1) * 0x9E3779B97F4A7C15ULL;
    const double TONE = .86 + .12 * unit_draw(hash);
    const auto CAPACITY = [TONE](int count) {
        const double FILL = static_cast<double>(count) / COVERAGE_BITS;
        return TONE * FILL * (1.35 - .35 * FILL);
    };
    const double PREVIOUS_CAPACITY = CAPACITY(COVERED);
    const float MATERIAL = PREVIOUSLY_BURNED ? movement.regrowth : 1.F;
    const double LIMIT = PREVIOUSLY_BURNED ? PREVIOUS_CAPACITY : CAPACITY(COVERED + 1);
    const double GAIN = PREVIOUSLY_BURNED ? LIMIT * MATERIAL / COVERED : LIMIT - PREVIOUS_CAPACITY;
    if (PREVIOUSLY_BURNED) {
        m_regrown &= static_cast<std::uint16_t>(~bit);
    } else {
        m_coverage |= bit;
    }
    const double VISIBLE = movement.severity;
    movement.severity = static_cast<float>(std::min(LIMIT, VISIBLE + GAIN));
    movement.age = 0;
    movement.lifetime = stay;
    movement.healing = recover;
    return MATERIAL;
}
void CBurnGround::stamp_movement(Area &area,
    std::uint64_t seed,
    animation::WindowId window,
    const BurnParams &params,
    const animation::Motion &motion) {
    const auto &from = motion.from_capture;
    const auto &to = motion.capture;
    const auto OWNER = window ? window : seed;
    const bool WINDOW_OWNED = window != 0;
    for (std::size_t index = 0; index < area.particle_count; ++index) {
        auto &particle = area.particles[index];
        if (particle.owner != OWNER || particle.window_owned != WINDOW_OWNED ||
            particle.age >= particle.held_until)
            continue;
        const auto EDGE = edge_sweep(from, to, particle.edge);
        const auto ANCHOR = EDGE.anchor(
            EDGE.to, EDGE.low_to, EDGE.high_to, particle.edge_fraction, particle.edge_gap);
        const double ANCHOR_CHANGE = separation_squared(ANCHOR, particle.position);
        if (EDGE.recedes() && ANCHOR_CHANGE == 0) continue;
        const auto PREVIOUS_ANCHOR = EDGE.anchor(
            EDGE.from, EDGE.low_from, EDGE.high_from, particle.edge_fraction, particle.edge_gap);
        if (!EDGE.recedes() || separation_squared(PREVIOUS_ANCHOR, particle.position) > 1 ||
            ANCHOR_CHANGE > 28 * 28 || !inside(ANCHOR, area.screen)) {
            particle.held_until = particle.age;
        } else {
            particle.position = ANCHOR;
        }
    }

    using Kind = Particle::Kind;
    struct Emission {
        Kind kind;
        double rate;
        unsigned budget;
    };
    std::array emission{
        Emission{Kind::Paper, .48 * std::clamp(params.fragment_density, 0., 2.), 24},
        Emission{Kind::Smoke, .12 * std::clamp(params.smoke_density, 0., 2.), 4},
        Emission{Kind::Ember, .075 * std::clamp(params.ember_density, 0., 2.), 6},
    };
    const double HEADING = params.wind_direction * std::numbers::pi / 180.;
    const double STRENGTH = std::max(0., params.wind_strength);
    const Vec2 WIND{std::cos(HEADING) * STRENGTH, std::sin(HEADING) * STRENGTH};
    const double GRAVITY = std::max(0., params.gravity_strength);
    const float LIFETIME = static_cast<float>(std::max(0., params.scorch_lifetime));
    const float HEALING = static_cast<float>(std::max(.001, params.healing_duration));
    const GridAxis X{area.screen.x, area.screen.w / (WIDTH * COVERAGE_SIDE), WIDTH * COVERAGE_SIDE};
    const GridAxis Y{
        area.screen.y, area.screen.h / (HEIGHT * COVERAGE_SIDE), HEIGHT * COVERAGE_SIDE};
    const auto DRAW = [&] { return unit_draw(m_rng); };

    // A receding edge sweeps just the wallpaper it reveals. At a given
    // normal coordinate, its tangent interval is the actual interpolated edge,
    // not the future client box or the entire sweep's bounding rectangle.
    for (const auto SIDE : SIDES) {
        const auto EDGE = edge_sweep(from, to, SIDE);
        if (!EDGE.recedes()) continue;
        const auto &normal = EDGE.vertical ? X : Y;
        const auto &tangent = EDGE.vertical ? Y : X;
        const double START = std::min(EDGE.from, EDGE.to), END = std::max(EDGE.from, EDGE.to);
        const double TRAVEL = EDGE.to - EDGE.from;
        const auto [FIRST, LAST] = normal.crossed(START, END);
        const Vec2 OUTWARD = EDGE.point(EDGE.low_side ? -1 : 1, 0);
        Vec2 carried;
        if (motion.sample_seconds > 0 && std::isfinite(motion.sample_seconds)) {
            const Vec2 DRAGGED = EDGE.point(
                TRAVEL, ((EDGE.low_to - EDGE.low_from) + (EDGE.high_to - EDGE.high_from)) * .5);
            // Inherit signed edge speed, smoothly capped independently of
            // wind and the fragment's outward peel impulse.
            const double SCALE =
                22. / (std::hypot(DRAGGED.x, DRAGGED.y) + 180. * motion.sample_seconds);
            carried = {DRAGGED.x * SCALE, DRAGGED.y * SCALE};
        }
        // Near-edge sources get first use of the bounded particle budget.
        for (std::size_t layer = FIRST; layer < LAST; ++layer) {
            const auto N = EDGE.low_side ? LAST - 1 - (layer - FIRST) : layer;
            const double CLIPPED_LOW = std::max(START, normal.boundary(N));
            const double CLIPPED_HIGH = std::min(END, normal.boundary(N + 1));
            if (!(CLIPPED_LOW < CLIPPED_HIGH)) continue;
            const double T0 = ((EDGE.low_side ? CLIPPED_LOW : CLIPPED_HIGH) - EDGE.from) / TRAVEL;
            const double T1 = ((EDGE.low_side ? CLIPPED_HIGH : CLIPPED_LOW) - EDGE.from) / TRAVEL;
            const double LOW = std::min(std::lerp(EDGE.low_from, EDGE.low_to, T0),
                std::lerp(EDGE.low_from, EDGE.low_to, T1));
            const double HIGH = std::max(std::lerp(EDGE.high_from, EDGE.high_to, T0),
                std::lerp(EDGE.high_from, EDGE.high_to, T1));
            const auto [BEGIN, END_TANGENT] = tangent.crossed(LOW, HIGH);
            for (std::size_t t = BEGIN; t < END_TANGENT; ++t) {
                const double TAN_LOW = std::max(LOW, tangent.boundary(t));
                const double TAN_HIGH = std::min(HIGH, tangent.boundary(t + 1));
                if (!(TAN_LOW < TAN_HIGH)) continue;
                const auto COLUMN = EDGE.vertical ? N : t;
                const auto ROW = EDGE.vertical ? t : N;
                const auto INDEX = (ROW / COVERAGE_SIDE) * WIDTH + COLUMN / COVERAGE_SIDE;
                const auto BIT = static_cast<std::uint16_t>(
                    1U << ((ROW % COVERAGE_SIDE) * COVERAGE_SIDE + COLUMN % COVERAGE_SIDE));
                auto &cell = area.cells[INDEX];
                const float MATERIAL = cell.kindle(BIT, INDEX, LIFETIME, HEALING);

                // A zero step.elapsed acquisition still deposits and sheds
                // debris; only fresh or regenerated material may emit.
                if (!(MATERIAL > 0) || area.particle_count == MAX_PARTICLES) continue;
                for (auto &candidate : emission) {
                    if (candidate.budget == 0 || !(candidate.rate > 0) ||
                        DRAW() >= candidate.rate * MATERIAL / COVERAGE_BITS)
                        continue;
                    --candidate.budget;
                    const Vec2 SOURCE = EDGE.source(T0, T1, std::lerp(TAN_LOW, TAN_HIGH, DRAW()));
                    const bool PAPER = candidate.kind == Kind::Paper;
                    const bool SMOKE = candidate.kind == Kind::Smoke;
                    const double SIZE_SAMPLE = DRAW();
                    const double SIZE = PAPER   ? 5 + 23 * SIZE_SAMPLE * SIZE_SAMPLE
                                        : SMOKE ? 7 + 12 * SIZE_SAMPLE
                                                : .8 + 1.8 * SIZE_SAMPLE;
                    const double PEEL =
                        PAPER ? 42 + DRAW() * 50 : (SMOKE ? 5 + DRAW() * 14 : 14 + DRAW() * 38);
                    const double LIFT =
                        PAPER ? 10 + DRAW() * 20 : (SMOKE ? 20 + DRAW() * 23 : 26 + DRAW() * 41);
                    const double WIND_FACTOR = PAPER ? .18 : SMOKE ? .42 : .32;
                    const double DRAG = PAPER   ? .75 + (28 - SIZE) * .037
                                        : SMOKE ? 2.6 - (SIZE - 7) * .08
                                                : 1.4 + (2.6 - SIZE) * .24;
                    auto &p = area.particles[area.particle_count++];
                    p = {.owner = OWNER,
                        .kind = candidate.kind,
                        .window_owned = WINDOW_OWNED,
                        .position = SOURCE,
                        .source = SOURCE,
                        .lifetime = PAPER   ? .8 + DRAW() * .85
                                    : SMOKE ? .55 + DRAW() * .65
                                            : .35 + DRAW() * .65,
                        .size = SIZE,
                        .angle = DRAW() * 2 * std::numbers::pi,
                        .fold = PAPER ? std::sin(1.24) : 1,
                        .char_depth =
                            PAPER ? .30 + DRAW() * .48 + .16 * cell.m_movement.severity : 0,
                        .shape = DRAW(),
                        .velocity = {carried.x + OUTWARD.x * PEEL + WIND.x * WIND_FACTOR +
                                         (DRAW() - .5) * 18,
                            carried.y + OUTWARD.y * PEEL + WIND.y * WIND_FACTOR - LIFT},
                        .terminal = {WIND.x * WIND_FACTOR,
                            WIND.y * WIND_FACTOR + (PAPER ? GRAVITY * (.24 + .21 * DRAW()) / DRAG
                                                       : SMOKE ? -16 - GRAVITY * .025 / DRAG
                                                               : -12 - GRAVITY * .055 / DRAG)},
                        .flutter = (4 + STRENGTH * .045) * (.5 + DRAW()),
                        .drag = DRAG,
                        .spin = (DRAW() < .5 ? -1 : 1) * (1.5 + DRAW() * 5),
                        .edge = SIDE};
                    const double SOURCE_NORMAL = EDGE.vertical ? SOURCE.x : SOURCE.y;
                    const double SOURCE_TANGENT = EDGE.vertical ? SOURCE.y : SOURCE.x;
                    if (PAPER && std::abs(SOURCE_NORMAL - EDGE.to) < 14 &&
                        SOURCE_TANGENT >= EDGE.low_to && SOURCE_TANGENT <= EDGE.high_to &&
                        DRAW() < .52) {
                        p.edge_fraction =
                            (SOURCE_TANGENT - EDGE.low_to) / (EDGE.high_to - EDGE.low_to);
                        p.edge_gap = std::min(SIZE * .42, std::abs(SOURCE_NORMAL - EDGE.to));
                        const Vec2 ANCHOR = EDGE.anchor(
                            EDGE.to, EDGE.low_to, EDGE.high_to, p.edge_fraction, p.edge_gap);
                        if (inside(ANCHOR, area.screen)) {
                            p.position = ANCHOR;
                            p.held_until = .10 + DRAW() * .20;
                        }
                    }
                    if (area.particle_count == MAX_PARTICLES) break;
                }
            }
        }
    }
}
