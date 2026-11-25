#include <algorithm>
#include <cmath>
#include <span>

#include "window/bubble/BubbleMotion.hpp"
#include "window/bubble/BubbleMotionPrivate.hpp"

namespace hyprimate {
    using namespace bubble_motion_detail;

    double BubbleMotion::exposure(double angle) const noexcept {
        if (length(m_behind) < .5) {
            return 0;
        }
        return std::max(0.0, dot(outline(angle).normal, m_behind));
    }
    BubbleBud BubbleMotion::bud(const Attached &attached_bubble) const noexcept {
        const Vec2 CENTRE{.x = m_body.x + (m_body.w * .5), .y = m_body.y + (m_body.h * .5)};
        const Vec2 HALF{.x = m_body.w * .5, .y = m_body.h * .5};
        const double COSINE = std::cos(attached_bubble.angle);
        const double SINE = std::sin(attached_bubble.angle);
        const double FACTOR = std::pow(
            std::pow(std::abs(COSINE), 12.0) + std::pow(std::abs(SINE), 12.0), -1.0 / 12.0);
        BubbleBud result;
        result.foot = bubble_round_point({.x = m_size.x * .5, .y = m_size.y * .5},
            m_shape,
            add(CENTRE, Vec2{.x = HALF.x * COSINE * FACTOR, .y = HALF.y * SINE * FACTOR}));
        result.width = 1.25 * attached_bubble.radius;
        const double FORMED = .22 * attached_bubble.radius;
        double height = 0;
        switch (attached_bubble.stage) {
            case Stage::Bud:
                height = attached_bubble.radius *
                         ((.5 * smoothstep(0, .45, attached_bubble.growth)) -
                             (.28 * smoothstep(.45, 1, attached_bubble.growth)));
                break;
            case Stage::Ride:
                height = FORMED;
                break;
            case Stage::Pinch:
                height = FORMED + (.3 * attached_bubble.radius * attached_bubble.pinch);
                break;
            case Stage::Merge:
                height = attached_bubble.radius * attached_bubble.growth *
                         (.22 + (.5 * (1 - attached_bubble.growth)));
                break;
            case Stage::Pop:
                height = FORMED;
                break;
        }
        result.height = std::clamp(height * (1 - attached_bubble.collapse), 0.0, .5 * result.width);
        return result;
    }

    BubbleMicro BubbleMotion::draw(const Attached &attached_bubble) const noexcept {
        const auto OUTLINE = outline(attached_bubble.angle);
        const double GROWTH = smoothstep(0, 1, attached_bubble.growth);
        const double RADIUS = attached_bubble.radius * (.35 + (.65 * GROWTH));
        BubbleMicro micro;
        micro.centre = add(OUTLINE.point,
            mul(OUTLINE.normal, RADIUS * (-.55 + (1.4 * GROWTH) + (.9 * attached_bubble.pinch))));
        micro.radius = RADIUS;
        micro.growth = attached_bubble.growth;
        micro.anchor = OUTLINE.point;
        micro.attach = (1 - attached_bubble.collapse) * (1 - (.8 * attached_bubble.pinch));
        micro.collapse = attached_bubble.collapse;
        micro.thickness = attached_bubble.thickness;
        micro.seed = attached_bubble.seed;
        micro.visibility =
            attached_bubble.stage == Stage::Bud     ? smoothstep(.2, .6, attached_bubble.growth)
            : attached_bubble.stage == Stage::Merge ? smoothstep(0, .5, attached_bubble.growth)
                                                    : 1.0;
        micro.squash =
            add(attached_bubble.squash, mul(mode(OUTLINE.normal), .12 * attached_bubble.pinch));
        return micro;
    }

    std::size_t BubbleMotion::microbubbles(std::span<BubbleMicro> out) const {
        const std::size_t COUNT = std::min(m_attached_count, out.size());
        auto attached = std::span(m_attached).first(COUNT).begin();
        for (auto &micro : out.first(COUNT)) {
            micro = draw(*attached++);
        }
        return COUNT;
    }

    void BubbleMotion::nucleate(double elapsed, Vec2 acceleration) {
        if (m_params.microbubbles <= 0 || m_params.strength <= 0) {
            return;
        }
        const double LIMIT = drag_limit();
        const double STRETCH = LIMIT > 0 ? std::clamp(length(m_lag) / LIMIT, 0.0, 1.0) : 0;
        const double TRAVEL = std::clamp((m_speed - 120) / 1100, 0.0, 1.5);
        const double JOLT = m_speed > 60 ? std::clamp(length(acceleration) / 14000, 0.0, 1.0) : 0;
        if (TRAVEL <= 0 && JOLT <= 0) {
            return;
        }
        const double TURN = turning(m_velocity, m_speed, acceleration);
        m_spawn += m_params.microbubbles * 40 * ((TRAVEL * (.35 + STRETCH + TURN)) + (.8 * JOLT)) *
                   elapsed;
        while (m_spawn >= m_threshold) {
            m_spawn -= m_threshold;
            m_threshold = -std::log(1 - draw01());
            if (m_attached_count >= m_attached.size()) {
                continue;
            }
            Attached attached;
            attached.radius = std::clamp(
                13 * std::exp(.45 * gauss()) * std::clamp(reference() / 600, .85, 1.5), 4.0, 34.0);
            double best = 0;
            double total = 0;
            for (int i = 0; i < 6; ++i) {
                const double ANGLE = TAU * draw01();
                const double EXPOSURE = exposure(ANGLE);
                const double WEIGHT = .12 + (1.6 * EXPOSURE * EXPOSURE) +
                                      (.5 * std::pow(std::abs(std::sin(2 * ANGLE)), 8.0));
                total += WEIGHT;
                if (draw01() * total < WEIGHT) {
                    best = ANGLE;
                }
            }
            attached.angle = best;
            const auto HERE = outline(attached.angle);
            const bool CROWDED = std::ranges::any_of(
                std::span(m_attached).first(m_attached_count), [&](const Attached &other) {
                    return std::abs(std::remainder(other.angle - attached.angle, TAU)) *
                               HERE.stretch <
                           .8 * (other.radius + attached.radius);
                });
            if (CROWDED) {
                continue;
            }
            attached.grow = (.16 + (.3 * draw01())) * std::pow(attached.radius / 8, .3);
            attached.patience = .35 + (1.2 * draw01());
            attached.hold = .25 + (.85 * draw01());
            attached.pinch_rate = 1 / (.1 + (.14 * draw01()));
            attached.thickness = 300 + (550 * draw01());
            attached.seed = draw01();
            attached.pops = draw01() < .35;
            m_attached[m_attached_count++] = attached;
        }
    }

    void BubbleMotion::release(const Attached &attached) {
        if (m_release_count >= m_releases.size()) {
            return;
        }
        const auto OUTLINE = outline(attached.angle);
        BubbleRelease result;
        result.micro = draw(attached);
        result.velocity = add(add(mul(m_velocity, .5 + (.3 * draw01())),
                                  mul(OUTLINE.tangent, attached.slip * OUTLINE.stretch)),
            mul(OUTLINE.normal, 25 + (35 * draw01())));
        result.squash_rate = add(mul(attached.squash_rate, 1.0), mul(mode(OUTLINE.normal), -2.5));
        result.life = .5 + (.9 * draw01());
        m_releases[m_release_count++] = result;
        const auto FOOT = bud(attached).foot;
        const Vec2 HALF = bubble_round_half({.x = m_size.x * .5, .y = m_size.y * .5}, m_shape);
        const double THETA =
            std::atan2(FOOT.y / std::max(HALF.y, 1.0), FOOT.x / std::max(HALF.x, 1.0));
        auto *rate = m_wave_rate.begin();
        std::size_t index = 0;
        for (const double MODE : MODES) {
            const double AMPLITUDE =
                -5 * attached.radius * m_params.strength / static_cast<double>(index + 1);
            *rate = add(*rate,
                Vec2{.x = AMPLITUDE * std::cos(MODE * THETA),
                    .y = AMPLITUDE * std::sin(MODE * THETA)});
            ++rate;
            ++index;
        }
        start_strand(attached.angle,
            std::max(1.0, .22 * attached.radius),
            380 + (200 * draw01()),
            20 + (40 * draw01()));
    }

    void BubbleMotion::migrate(double elapsed, Vec2 acceleration) {
        const double TURN = turning(m_velocity, m_speed, acceleration);
        const double SLOW = m_speed < 80 ? 1 : 0;
        std::size_t kept = 0;
        // NOLINTNEXTLINE(misc-const-correctness): The cursor writes retained bubbles below.
        auto *destination = m_attached.begin();
        for (const auto &current : std::span(m_attached).first(m_attached_count)) {
            auto attached = current;
            attached.age += elapsed;
            const auto OUTLINE = outline(attached.angle);
            // In the window's frame the bubble feels the opposite of its acceleration, and the air
            // the film moves through combs it back; sharp corners trap it.
            const Vec2 FORCE = sub(mul(acceleration, -.05), mul(m_velocity, 1.1));
            const double TRAP = 3.2 + (40 * std::min(1.0, OUTLINE.curvature * attached.radius));
            attached.slip += dot(FORCE, OUTLINE.tangent) / std::max(OUTLINE.stretch, 1.0) * elapsed;
            attached.slip = std::clamp(attached.slip * std::exp(-TRAP * elapsed), -3.0, 3.0);
            attached.angle += attached.slip * elapsed;

            const double SLIDING = std::clamp(attached.slip * OUTLINE.stretch / 900, -.1, .1);
            const Vec2 TARGET =
                add(mul(mode(OUTLINE.tangent), std::abs(SLIDING)), mul(mode(OUTLINE.normal), -.05));
            const double FREQUENCY =
                TAU * std::clamp(12 * std::sqrt(8 / std::max(attached.radius, 1.0)), 6.0, 22.0);
            ring(attached.squash, attached.squash_rate, TARGET, FREQUENCY, .3, elapsed);
            bool gone = false;
            switch (attached.stage) {
                case Stage::Bud:
                    attached.growth = std::min(1.0, attached.age / attached.grow);
                    if (attached.growth >= 1) {
                        attached.stage = Stage::Ride;
                    }
                    break;
                case Stage::Ride: {
                    attached.patience -= elapsed * (SLOW != 0 ? 2.5 : .5);
                    if (attached.patience <= 0) {
                        attached.stage = attached.pops ? Stage::Pop : Stage::Merge;
                        break;
                    }
                    const double INSTABILITY = (m_speed / 700 * std::sqrt(attached.radius / 8) *
                                                   (.25 + exposure(attached.angle))) +
                                               (length(acceleration) / 9000) + (.5 * TURN);
                    if (INSTABILITY > attached.hold &&
                        draw01() < 9 * (INSTABILITY - attached.hold) * elapsed) {
                        attached.stage = Stage::Pinch;
                    }
                    break;
                }
                case Stage::Pinch:
                    attached.pinch =
                        std::min(1.0, attached.pinch + (attached.pinch_rate * elapsed));
                    if (attached.pinch >= 1) {
                        release(attached);
                        gone = true;
                    }
                    break;
                case Stage::Merge:
                    attached.growth -= elapsed / .28;
                    gone = attached.growth <= 0;
                    break;
                case Stage::Pop:
                    attached.collapse += elapsed / .09;
                    gone = attached.collapse >= 1;
                    break;
            }
            if (!gone) {
                *destination++ = attached;
                ++kept;
            }
        }
        m_attached_count = kept;
        // Neighbours push apart; formed bubbles can coalesce.
        const auto ACTIVE = std::span(m_attached).first(m_attached_count);
        for (auto first = ACTIVE.begin(); first != ACTIVE.end(); ++first) {
            auto &first_bubble = *first;
            if (first_bubble.stage != Stage::Ride && first_bubble.stage != Stage::Bud) {
                continue;
            }
            for (auto second = first + 1; second != ACTIVE.end(); ++second) {
                auto &second_bubble = *second;
                if (second_bubble.stage != Stage::Ride && second_bubble.stage != Stage::Bud) {
                    continue;
                }
                const double GAP = std::remainder(second_bubble.angle - first_bubble.angle, TAU);
                const double STRETCH = std::max(outline(first_bubble.angle).stretch, 1.0);
                const double FIRST_RADIUS =
                    first_bubble.radius * (.35 + (.65 * first_bubble.growth));
                const double SECOND_RADIUS =
                    second_bubble.radius * (.35 + (.65 * second_bubble.growth));
                const double OVERLAP =
                    (.95 * (FIRST_RADIUS + SECOND_RADIUS)) - (std::abs(GAP) * STRETCH);
                if (OVERLAP <= 0) {
                    continue;
                }
                if (first_bubble.stage == Stage::Ride && second_bubble.stage == Stage::Ride &&
                    draw01() < 4 * elapsed * OVERLAP / (FIRST_RADIUS + SECOND_RADIUS)) {
                    auto &big =
                        first_bubble.radius >= second_bubble.radius ? first_bubble : second_bubble;
                    auto &small = &big == &first_bubble ? second_bubble : first_bubble;
                    big.radius = std::max(big.radius,
                        std::min(40.0,
                            std::cbrt((big.radius * big.radius * big.radius) +
                                      (small.radius * small.radius * small.radius))));
                    big.squash_rate =
                        add(big.squash_rate, mul(mode(outline(big.angle).tangent), 3));
                    small.stage = Stage::Merge;
                    continue;
                }
                const double PUSH = 6 * OVERLAP / STRETCH * elapsed;
                first_bubble.slip -= std::copysign(PUSH, GAP);
                second_bubble.slip += std::copysign(PUSH, GAP);
            }
        }
    }
}  // namespace hyprimate
