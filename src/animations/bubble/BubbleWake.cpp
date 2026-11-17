#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "animations/bubble/BubbleMotion.hpp"
#include "animations/bubble/BubbleWake.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    namespace {
        // The ribbon is drawn this much wider than its film so the frayed edge has room.
        constexpr double kFray = 1.3;
        constexpr double kCollapseSeconds = .1;

        bool finite(Vec2 v) {
            return std::isfinite(v.x) && std::isfinite(v.y);
        }
    }  // namespace

    BubbleWake::BubbleWake(std::uint64_t seed) noexcept : random_(seed) {
        seed_ = unit_draw(random_);
    }

    bool BubbleWake::full() const noexcept {
        return first_ + count_ >= nodes_.size();
    }

    bool BubbleWake::extend(const BubbleTrailPoint &point, Vec2 origin) noexcept {
        if (full()) {
            return false;
        }
        const Vec2 POSITION{.x = origin.x + point.position.x, .y = origin.y + point.position.y};
        if (!finite(POSITION) || !finite(point.direction) || !std::isfinite(point.halfWidth) ||
            !std::isfinite(point.density) || point.halfWidth <= 0 || point.density <= 0) {
            return true;
        }
        Node node;
        node.point = point;
        node.point.position = POSITION;
        node.point.density = std::clamp(point.density, 0.0, 1.0);
        if (count_ > 0) {
            const auto &previous = nodes_[first_ + count_ - 1];
            node.arc = previous.arc + std::hypot(POSITION.x - previous.point.position.x,
                                          POSITION.y - previous.point.position.y);
        }
        // Denser film lasts longer; every patch keeps its own time so it does not vanish in step.
        node.life = (.45 + (.55 * node.point.density)) * (.8 + (.4 * unit_draw(random_)));
        nodes_[first_ + count_++] = node;
        return true;
    }

    bool BubbleWake::release(const BubbleRelease &bubble, Vec2 origin) noexcept {
        if (bubbleCount_ >= bubbles_.size()) {
            return false;
        }
        Free free;
        free.micro = bubble.micro;
        free.micro.centre = {
            .x = origin.x + bubble.micro.centre.x, .y = origin.y + bubble.micro.centre.y};
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
        bubbles_[bubbleCount_++] = free;
        return true;
    }

    void BubbleWake::hurry() noexcept {
        pace_ = 4;
    }

    void BubbleWake::advance(double elapsed) noexcept {
        if (!std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        const double DELTA_TIME = std::min(elapsed, STALL_SECONDS) * pace_;
        for (std::size_t i = first_; i < first_ + count_; ++i) {
            nodes_[i].age += DELTA_TIME;
        }
        while (count_ > 0 && nodes_[first_].age >= nodes_[first_].life) {
            ++first_;
            --count_;
        }
        if (count_ == 0) {
            first_ = 0;
        }

        std::size_t kept = 0;
        for (std::size_t i = 0; i < bubbleCount_; ++i) {
            auto wakeBubble = bubbles_[i];
            // Air brakes the bubble within a fraction of a second; it then sinks very slowly.
            wakeBubble.velocity.x *= std::exp(-DELTA_TIME / .25);
            wakeBubble.velocity.y =
                (wakeBubble.velocity.y * std::exp(-DELTA_TIME / .25)) + (12 * DELTA_TIME);
            wakeBubble.micro.centre.x += wakeBubble.velocity.x * DELTA_TIME;
            wakeBubble.micro.centre.y += wakeBubble.velocity.y * DELTA_TIME;
            wakeBubble.micro.anchor = wakeBubble.micro.centre;
            wakeBubble.age += DELTA_TIME;
            wakeBubble.micro.thickness =
                wakeBubble.thickness *
                (1 - (.45 * std::min(wakeBubble.age / wakeBubble.life, 1.0)));
            wakeBubble.micro.collapse =
                std::clamp((wakeBubble.age - wakeBubble.life) / kCollapseSeconds, 0.0, 1.0);
            if (wakeBubble.micro.collapse >= 1) {
                continue;
            }
            bubbles_[kept++] = wakeBubble;
        }
        bubbleCount_ = kept;
    }

    bool BubbleWake::done() const noexcept {
        return count_ == 0 && bubbleCount_ == 0;
    }

    Box BubbleWake::bounds() const noexcept {
        double minX = std::numeric_limits<double>::infinity();
        double minY = minX;
        double maxX = -minX;
        double maxY = -minX;
        const auto INCLUDE = [&](Vec2 p, double r) {
            minX = std::min(minX, p.x - r);
            minY = std::min(minY, p.y - r);
            maxX = std::max(maxX, p.x + r);
            maxY = std::max(maxY, p.y + r);
        };
        for (std::size_t i = first_; i < first_ + count_; ++i) {
            INCLUDE(nodes_[i].point.position, (nodes_[i].point.halfWidth * kFray) + 2);
        }
        for (std::size_t i = 0; i < bubbleCount_; ++i) {
            INCLUDE(bubbles_[i].micro.centre, (bubbles_[i].micro.radius * 1.4) + 3);
        }
        if (!(maxX > minX && maxY > minY)) {
            return {};
        }
        return {.x = minX, .y = minY, .w = maxX - minX, .h = maxY - minY};
    }

    void BubbleWake::view(BubbleWakeView &out) const noexcept {
        const auto BOX = bounds();
        out.size = {.x = BOX.w, .y = BOX.h};
        out.seed = seed_;
        out.nodeCount = count_;
        out.bubbleCount = bubbleCount_;
        const Vec2 CENTRE{.x = BOX.x + (BOX.w * .5), .y = BOX.y + (BOX.h * .5)};
        for (std::size_t i = 0; i < count_; ++i) {
            const auto &node = nodes_[first_ + i];
            auto &outputNode = out.nodes[i];
            outputNode.position = {
                .x = node.point.position.x - CENTRE.x, .y = node.point.position.y - CENTRE.y};
            // The ribbon turns with its path: the tangent spans the neighbouring points.
            const auto &before = nodes_[first_ + (i > 0 ? i - 1 : i)].point.position;
            const auto &after = nodes_[first_ + std::min(i + 1, count_ - 1)].point.position;
            const Vec2 SPAN{.x = after.x - before.x, .y = after.y - before.y};
            const double SPAN_LENGTH = std::hypot(SPAN.x, SPAN.y);
            outputNode.direction = SPAN_LENGTH > 1e-6
                                       ? Vec2{.x = SPAN.x / SPAN_LENGTH, .y = SPAN.y / SPAN_LENGTH}
                                       : node.point.direction;
            outputNode.fade = std::clamp(node.age / node.life, 0.0, 1.0);
            outputNode.halfWidth = node.point.halfWidth * (1 - (.35 * outputNode.fade));
            outputNode.arc = node.arc;
            outputNode.density = node.point.density;
        }
        for (std::size_t i = 0; i < bubbleCount_; ++i) {
            auto microbubble = bubbles_[i].micro;
            microbubble.centre = {
                .x = microbubble.centre.x - CENTRE.x, .y = microbubble.centre.y - CENTRE.y};
            microbubble.anchor = microbubble.centre;
            out.bubbles[i] = microbubble;
        }
    }

    const BubbleTrailPoint *BubbleWake::last() const noexcept {
        return count_ > 0 ? &nodes_[first_ + count_ - 1].point : nullptr;
    }

}  // namespace hyprimate
