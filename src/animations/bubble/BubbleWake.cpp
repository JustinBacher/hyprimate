#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "animations/bubble/BubbleMotion.hpp"
#include "animations/bubble/BubbleWake.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    namespace {
        // The ribbon is drawn this much wider than its film so the frayed edge has room.
        constexpr double FRAY = 1.3;
        constexpr double COLLAPSE_SECONDS = .1;

        bool finite(Vec2 value) {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }
    }  // namespace

    BubbleWake::BubbleWake(std::uint64_t seed) noexcept : random(seed), seed(unit_draw(random)) {
    }

    bool BubbleWake::full() const noexcept {
        return first + count >= nodes.size();
    }

    bool BubbleWake::extend(const BubbleTrailPoint &point, Vec2 origin) noexcept {
        if (full()) {
            return false;
        }
        const Vec2 POSITION{.x = origin.x + point.position.x, .y = origin.y + point.position.y};
        if (!finite(POSITION) || !finite(point.direction) || !std::isfinite(point.half_width) ||
            !std::isfinite(point.density) || point.half_width <= 0 || point.density <= 0) {
            return true;
        }
        Node node;
        node.point = point;
        node.point.position = POSITION;
        node.point.density = std::clamp(point.density, 0.0, 1.0);
        if (count > 0) {
            const auto &previous = std::span{nodes}.subspan(first, count).back();
            node.arc = previous.arc + std::hypot(POSITION.x - previous.point.position.x,
                                          POSITION.y - previous.point.position.y);
        }
        // Denser film lasts longer; every patch keeps its own time so it does not vanish in step.
        node.life = (.45 + (.55 * node.point.density)) * (.8 + (.4 * unit_draw(random)));
        nodes[first + count++] = node;
        return true;
    }

    bool BubbleWake::release(const BubbleRelease &bubble, Vec2 origin) noexcept {
        if (bubble_count >= bubbles.size()) {
            return false;
        }
        Free free;
        free.micro = bubble.micro;
        free.micro.centre = {
            .x = origin.x + bubble.micro.centre.x,
            .y = origin.y + bubble.micro.centre.y,
        };
        free.micro.anchor = free.micro.centre;
        free.micro.attach = 0;
        free.velocity = bubble.velocity;
        free.life = std::max(.05, bubble.life);
        free.thickness = bubble.micro.thickness;
        if (!finite(free.micro.centre) || !finite(free.velocity) ||
            !std::isfinite(free.micro.radius) || free.micro.radius <= 0 ||
            !std::isfinite(free.life)) {
            return true;
        }
        bubbles[bubble_count++] = free;
        return true;
    }

    void BubbleWake::hurry() noexcept {
        pace = 4;
    }

    void BubbleWake::advance(double elapsed) noexcept {
        if (!std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        const double DELTA_TIME = std::min(elapsed, STALL_SECONDS) * pace;
        for (auto &node : std::span{nodes}.subspan(first, count)) {
            node.age += DELTA_TIME;
        }
        while (count > 0 && std::span{nodes}.subspan(first, count).front().age >=
                                std::span{nodes}.subspan(first, count).front().life) {
            ++first;
            --count;
        }
        if (count == 0) {
            first = 0;
        }

        auto *kept = bubbles.begin();
        for (auto wake_bubble : std::span{bubbles}.first(bubble_count)) {
            // Air brakes the bubble within a fraction of a second; it then sinks very slowly.
            wake_bubble.velocity.x *= std::exp(-DELTA_TIME / .25);
            wake_bubble.velocity.y =
                (wake_bubble.velocity.y * std::exp(-DELTA_TIME / .25)) + (12 * DELTA_TIME);
            wake_bubble.micro.centre.x += wake_bubble.velocity.x * DELTA_TIME;
            wake_bubble.micro.centre.y += wake_bubble.velocity.y * DELTA_TIME;
            wake_bubble.micro.anchor = wake_bubble.micro.centre;
            wake_bubble.age += DELTA_TIME;
            wake_bubble.micro.thickness =
                wake_bubble.thickness *
                (1 - (.45 * std::min(wake_bubble.age / wake_bubble.life, 1.0)));
            wake_bubble.micro.collapse =
                std::clamp((wake_bubble.age - wake_bubble.life) / COLLAPSE_SECONDS, 0.0, 1.0);
            if (wake_bubble.micro.collapse >= 1) {
                continue;
            }
            *kept = wake_bubble;
            ++kept;
        }
        bubble_count = static_cast<std::size_t>(kept - bubbles.begin());
    }

    bool BubbleWake::done() const noexcept {
        return count == 0 && bubble_count == 0;
    }

    Box BubbleWake::bounds() const noexcept {
        double min_x = std::numeric_limits<double>::infinity();
        double min_y = min_x;
        double max_x = -min_x;
        double max_y = -min_x;
        const auto INCLUDE = [&](Vec2 centre_point, double extent_radius) {
            min_x = std::min(min_x, centre_point.x - extent_radius);
            min_y = std::min(min_y, centre_point.y - extent_radius);
            max_x = std::max(max_x, centre_point.x + extent_radius);
            max_y = std::max(max_y, centre_point.y + extent_radius);
        };
        for (const auto &node : std::span{nodes}.subspan(first, count)) {
            INCLUDE(node.point.position, (node.point.half_width * FRAY) + 2);
        }
        for (const auto &bubble : std::span{bubbles}.first(bubble_count)) {
            INCLUDE(bubble.micro.centre, (bubble.micro.radius * 1.4) + 3);
        }
        if (!(max_x > min_x) || !(max_y > min_y)) {
            return {};
        }
        return {.x = min_x, .y = min_y, .w = max_x - min_x, .h = max_y - min_y};
    }

    void BubbleWake::view(BubbleWakeView &out) const noexcept {
        const auto BOX = bounds();
        out.size = {.x = BOX.w, .y = BOX.h};
        out.seed = seed;
        out.node_count = count;
        out.bubble_count = bubble_count;
        const Vec2 CENTRE{.x = BOX.x + (BOX.w * .5), .y = BOX.y + (BOX.h * .5)};
        const auto ACTIVE_NODES = std::span{nodes}.subspan(first, count);
        auto current = ACTIVE_NODES.begin();
        auto previous = current;
        auto *output = out.nodes.begin();
        for (std::size_t i = 0; i < count; ++i, ++current, ++output) {
            const auto &node = *current;
            auto &output_node = *output;
            output_node.position = {
                .x = node.point.position.x - CENTRE.x,
                .y = node.point.position.y - CENTRE.y,
            };
            // The ribbon turns with its path: the tangent spans the neighbouring points.
            auto next = current;
            ++next;
            const auto &before = (i > 0 ? *previous : node).point.position;
            const auto &after = (i + 1 < count ? *next : node).point.position;
            previous = current;
            const Vec2 SPAN{.x = after.x - before.x, .y = after.y - before.y};
            const double SPAN_LENGTH = std::hypot(SPAN.x, SPAN.y);
            output_node.direction = SPAN_LENGTH > 1e-6
                                        ? Vec2{.x = SPAN.x / SPAN_LENGTH, .y = SPAN.y / SPAN_LENGTH}
                                        : node.point.direction;
            output_node.fade = std::clamp(node.age / node.life, 0.0, 1.0);
            output_node.half_width = node.point.half_width * (1 - (.35 * output_node.fade));
            output_node.arc = node.arc;
            output_node.density = node.point.density;
        }
        auto *bubble_output = out.bubbles.begin();
        for (const auto &bubble : std::span{bubbles}.first(bubble_count)) {
            auto microbubble = bubble.micro;
            microbubble.centre = {
                .x = microbubble.centre.x - CENTRE.x,
                .y = microbubble.centre.y - CENTRE.y,
            };
            microbubble.anchor = microbubble.centre;
            *bubble_output = microbubble;
            ++bubble_output;
        }
    }

    const BubbleTrailPoint *BubbleWake::last() const noexcept {
        return count > 0 ? &std::span{nodes}.subspan(first, count).back().point : nullptr;
    }

}  // namespace hyprimate
