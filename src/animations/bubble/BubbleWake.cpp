#include <algorithm>
#include <cmath>
#include <limits>

#include "animations/bubble/BubbleWake.hpp"

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
        seed_ = unitDraw(random_);
    }

    bool BubbleWake::full() const noexcept {
        return first_ + count_ >= nodes_.size();
    }

    bool BubbleWake::extend(const BubbleTrailPoint &point, Vec2 origin) noexcept {
        if (full()) return false;
        const Vec2 position{origin.x + point.position.x, origin.y + point.position.y};
        if (!finite(position) || !finite(point.direction) || !std::isfinite(point.halfWidth) ||
            !std::isfinite(point.density) || point.halfWidth <= 0 || point.density <= 0)
            return true;
        Node node;
        node.point = point;
        node.point.position = position;
        node.point.density = std::clamp(point.density, 0.0, 1.0);
        if (count_ > 0) {
            const auto &previous = nodes_[first_ + count_ - 1];
            node.arc = previous.arc + std::hypot(position.x - previous.point.position.x,
                                          position.y - previous.point.position.y);
        }
        // Denser film lasts longer; every patch keeps its own time so it does not vanish in step.
        node.life = (.45 + .55 * node.point.density) * (.8 + .4 * unitDraw(random_));
        nodes_[first_ + count_++] = node;
        return true;
    }

    bool BubbleWake::release(const BubbleRelease &bubble, Vec2 origin) noexcept {
        if (bubbleCount_ >= bubbles_.size()) return false;
        Free free;
        free.micro = bubble.micro;
        free.micro.centre = {origin.x + bubble.micro.centre.x, origin.y + bubble.micro.centre.y};
        free.micro.anchor = free.micro.centre;
        free.micro.attach = 0;
        free.velocity = bubble.velocity;
        free.life = std::max(.05, bubble.life);
        free.thickness = bubble.micro.thickness;
        if (!finite(free.micro.centre) || !finite(free.velocity) ||
            !std::isfinite(free.micro.radius) || free.micro.radius <= 0 ||
            !std::isfinite(free.life))
            return true;
        bubbles_[bubbleCount_++] = free;
        return true;
    }

    void BubbleWake::hurry() noexcept {
        pace_ = 4;
    }

    void BubbleWake::advance(double elapsed) noexcept {
        if (!std::isfinite(elapsed) || elapsed <= 0) return;
        const double dt = std::min(elapsed, kStallSeconds) * pace_;
        for (std::size_t i = first_; i < first_ + count_; ++i) nodes_[i].age += dt;
        while (count_ > 0 && nodes_[first_].age >= nodes_[first_].life) {
            ++first_;
            --count_;
        }
        if (count_ == 0) first_ = 0;

        std::size_t kept = 0;
        for (std::size_t i = 0; i < bubbleCount_; ++i) {
            auto b = bubbles_[i];
            // Air brakes the bubble within a fraction of a second; it then sinks very slowly.
            b.velocity.x *= std::exp(-dt / .25);
            b.velocity.y = b.velocity.y * std::exp(-dt / .25) + 12 * dt;
            b.micro.centre.x += b.velocity.x * dt;
            b.micro.centre.y += b.velocity.y * dt;
            b.micro.anchor = b.micro.centre;
            b.age += dt;
            b.micro.thickness = b.thickness * (1 - .45 * std::min(b.age / b.life, 1.0));
            b.micro.collapse = std::clamp((b.age - b.life) / kCollapseSeconds, 0.0, 1.0);
            if (b.micro.collapse >= 1) continue;
            bubbles_[kept++] = b;
        }
        bubbleCount_ = kept;
    }

    bool BubbleWake::done() const noexcept {
        return count_ == 0 && bubbleCount_ == 0;
    }

    Box BubbleWake::bounds() const noexcept {
        double x0 = std::numeric_limits<double>::infinity(), y0 = x0;
        double x1 = -x0, y1 = -x0;
        const auto include = [&](Vec2 p, double r) {
            x0 = std::min(x0, p.x - r);
            y0 = std::min(y0, p.y - r);
            x1 = std::max(x1, p.x + r);
            y1 = std::max(y1, p.y + r);
        };
        for (std::size_t i = first_; i < first_ + count_; ++i)
            include(nodes_[i].point.position, nodes_[i].point.halfWidth * kFray + 2);
        for (std::size_t i = 0; i < bubbleCount_; ++i)
            include(bubbles_[i].micro.centre, bubbles_[i].micro.radius * 1.4 + 3);
        if (!(x1 > x0 && y1 > y0)) return {};
        return {x0, y0, x1 - x0, y1 - y0};
    }

    void BubbleWake::view(BubbleWakeView &out) const noexcept {
        const auto box = bounds();
        out.size = {box.w, box.h};
        out.seed = seed_;
        out.nodeCount = count_;
        out.bubbleCount = bubbleCount_;
        const Vec2 centre{box.x + box.w * .5, box.y + box.h * .5};
        for (std::size_t i = 0; i < count_; ++i) {
            const auto &node = nodes_[first_ + i];
            auto &v = out.nodes[i];
            v.position = {node.point.position.x - centre.x, node.point.position.y - centre.y};
            // The ribbon turns with its path: the tangent spans the neighbouring points.
            const auto &before = nodes_[first_ + (i > 0 ? i - 1 : i)].point.position;
            const auto &after = nodes_[first_ + std::min(i + 1, count_ - 1)].point.position;
            const Vec2 span{after.x - before.x, after.y - before.y};
            const double l = std::hypot(span.x, span.y);
            v.direction = l > 1e-6 ? Vec2{span.x / l, span.y / l} : node.point.direction;
            v.fade = std::clamp(node.age / node.life, 0.0, 1.0);
            v.halfWidth = node.point.halfWidth * (1 - .35 * v.fade);
            v.arc = node.arc;
            v.density = node.point.density;
        }
        for (std::size_t i = 0; i < bubbleCount_; ++i) {
            auto m = bubbles_[i].micro;
            m.centre = {m.centre.x - centre.x, m.centre.y - centre.y};
            m.anchor = m.centre;
            out.bubbles[i] = m;
        }
    }

    const BubbleTrailPoint *BubbleWake::last() const noexcept {
        return count_ > 0 ? &nodes_[first_ + count_ - 1].point : nullptr;
    }

}  // namespace hyprimate
