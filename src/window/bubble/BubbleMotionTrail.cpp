#include <algorithm>
#include <cmath>
#include <span>

#include "window/bubble/BubbleMotion.hpp"
#include "window/bubble/BubbleMotionPrivate.hpp"

namespace hyprimate {
    using namespace bubble_motion_detail;

    double BubbleMotion::trailing_angle() const noexcept {
        const Vec2 DIRECTION = length(m_behind) > .5 ? m_behind : Vec2{.x = 1, .y = 0};
        return std::atan2(
            DIRECTION.y / std::max(m_body.h, 1.0), DIRECTION.x / std::max(m_body.w, 1.0));
    }
    bool BubbleMotion::start_strand(
        double angle, double half_width, double thickness, double reach) {
        for (auto &strand : m_strands) {
            if (strand.live) {
                continue;
            }
            strand = {};
            strand.id = m_next_strand++;
            strand.angle = angle;
            strand.half_width = half_width;
            strand.thickness = thickness;
            strand.reach = reach;
            strand.live = true;
            strand.thread = half_width < 6;
            return true;
        }
        return false;
    }

    void BubbleMotion::emit_trail(double elapsed, Vec2 acceleration) {
        const bool DRAWING = m_params.trail > 0 && m_params.strength > 0;
        const double TRAVEL = std::clamp((m_speed - TRAIL_SPEED) / 1100, 0.0, 1.5);
        const double LIMIT = drag_limit();
        const double STRETCH = LIMIT > 0 ? std::clamp(length(m_lag) / LIMIT, 0.0, 1.0) : 0;
        const double TURN = turning(m_velocity, m_speed, acceleration);
        const double DENSITY =
            std::clamp(2 * m_params.trail *
                           std::pow(std::clamp((m_speed - TRAIL_SPEED) / 2200, 0.0, 1.0), .8) *
                           (.7 + (.5 * STRETCH) + (.6 * TURN)),
                0.0,
                1.0);
        if (DRAWING && TRAVEL > 0) {
            const bool SHEET = std::ranges::any_of(
                m_strands, [](const Strand &strand) { return strand.live && !strand.thread; });
            if (!SHEET && DENSITY > .04 && draw01() < 12 * elapsed) {
                const Vec2 DIRECTION = length(m_behind) > .5 ? m_behind : Vec2{.x = 1, .y = 0};
                const Vec2 EXTENT =
                    bubble_round_half({.x = m_size.x * .5, .y = m_size.y * .5}, m_shape);
                const double ACROSS =
                    std::abs(EXTENT.x * DIRECTION.y) + std::abs(EXTENT.y * DIRECTION.x);
                start_strand(trailing_angle(),
                    std::max(6.0, ACROSS * (.03 + (.07 * DENSITY)) * (.7 + (.6 * draw01()))),
                    480 + (380 * DENSITY),
                    (260 + (420 * draw01())) * (.6 + (.8 * DENSITY)));
            }
            m_thread_hazard += m_params.trail * 2.4 * TRAVEL * (1 + TURN) * elapsed;
            if (draw01() < m_thread_hazard * elapsed * 8) {
                m_thread_hazard = 0;
                const Vec2 DIRECTION = length(m_behind) > .5 ? m_behind : Vec2{.x = 1, .y = 0};
                const Vec2 UNIT{.x = DIRECTION.x / std::max(m_body.w, 1.0),
                    .y = DIRECTION.y / std::max(m_body.h, 1.0)};
                const double SIGN_X = UNIT.x >= 0 ? 1 : -1;
                const double SIGN_Y = UNIT.y >= 0 ? 1 : -1;
                const bool FLIP = std::abs(UNIT.x) > std::abs(UNIT.y);
                const double PICK = draw01() < .5 ? 1 : -1;
                const double CORNER_X = FLIP ? SIGN_X : PICK;
                const double CORNER_Y = FLIP ? PICK : SIGN_Y;
                start_strand(std::atan2(CORNER_Y, CORNER_X),
                    1.2 + (2.5 * draw01()),
                    350 + (400 * draw01()),
                    50 + (200 * draw01()));
            }
        }
        const double TRAILING = trailing_angle();
        for (auto &strand : m_strands) {
            if (!strand.live) {
                continue;
            }
            if (!strand.thread) {
                strand.angle +=
                    std::remainder(TRAILING - strand.angle, TAU) * (1 - std::exp(-elapsed / .15));
            }
            const auto OUTLINE = outline(strand.angle);
            const double MOVED = m_speed * elapsed;
            strand.travel += MOVED;
            strand.length += MOVED;
            strand.slack = m_speed < .6 * TRAIL_SPEED ? strand.slack + elapsed : 0;
            const bool LAST = !DRAWING || strand.length >= strand.reach || strand.slack > .08 ||
                              dot(OUTLINE.normal, m_behind) < .1;
            const double SPACING = std::clamp(m_speed * .012, 6.0, 14.0);
            const bool FIXED = strand.travel >= SPACING || LAST;
            if (FIXED) {
                strand.travel = 0;
            }
            if (m_trail_count < m_trail.size()) {
                BubbleTrailPoint point;
                point.position =
                    sub(OUTLINE.point, mul(OUTLINE.normal, std::min(2.0, strand.half_width)));
                point.velocity = mul(m_velocity, FILM_INHERIT);
                point.half_width =
                    strand.half_width * (1 - ((strand.thread ? .2 : .45) *
                                                 std::min(1.0, strand.length / strand.reach)));
                point.thickness = strand.thickness;
                point.strand = strand.id;
                point.provisional = !FIXED;
                point.last = LAST;
                m_trail[m_trail_count++] = point;
            }
            if (LAST) {
                strand.live = false;
            }
        }
    }

    std::span<const BubbleTrailPoint> BubbleMotion::trail() const noexcept {
        return std::span(m_trail).first(m_trail_count);
    }

    std::span<const BubbleRelease> BubbleMotion::releases() const noexcept {
        return std::span(m_releases).first(m_release_count);
    }

    void BubbleMotion::clear_emissions() noexcept {
        m_trail_count = m_release_count = 0;
    }
}  // namespace hyprimate
