#include <algorithm>
#include <cmath>
#include <limits>

#include "animations/bubble/BubbleWake.hpp"

namespace hyprimate {
    namespace {
        // Strands are drawn this much wider than their film so a ragged border has room.
        constexpr double kFray = 1.15;
        // Film left on the screen keeps a little of the window's motion; the air stops it within
        // a fraction of a second.
        constexpr double kFilmBrake = .18;
        // Surface tension straightens a strand's kinks at this rate.
        constexpr double kStraighten = .35;
        // A node holds film until drying has removed this much more than its laid-down thickness:
        // lenses of gathered liquid stay thicker than the strand's mean.
        constexpr double kDryMargin = 1.45;
        // Free ends retract at a few tens of px/s: slowly, as viscous film on the glass does.
        constexpr double kRetractSpeed = 40;
        // A thread's break opens at this speed to this half length.
        constexpr double kBreakSpeed = 90, kBreakGap = 5;
        // The bubbles: drag toward still air (time constant ~0.25 s) and a slow sink, since a
        // soap bubble is slightly denser than air; foam rises at 0.05 x buoyancy units per second.
        constexpr double kBubbleDamping = .45, kSinkSpeed = 12;
        constexpr double kStep = 1.0 / 120;

        bool finite(Vec2 v) {
            return std::isfinite(v.x) && std::isfinite(v.y);
        }
        Vec2 sub(Vec2 a, Vec2 b) {
            return {a.x - b.x, a.y - b.y};
        }
        double distance(Vec2 a, Vec2 b) {
            return std::hypot(a.x - b.x, a.y - b.y);
        }
    }  // namespace

    BubbleWake::BubbleWake(std::uint64_t seed) noexcept : random_(seed) {
        seed_ = unitDraw(random_);
        foam_.reset(seed ^ 0x51ED270B27A3C1F5ULL,
            {.interaction = 1, .damping = kBubbleDamping, .buoyancy = -kSinkSpeed / .05, .linger = .5},
            kBubbleWakeBubbles);
    }

    bool BubbleWake::dry(const Node &node) noexcept {
        return node.evaporated >= node.thickness * kDryMargin;
    }

    BubbleWake::Strand *BubbleWake::strand(std::uint32_t id) noexcept {
        for (auto &s : strands_)
            if (s.count > 0 && s.id == id) return &s;
        return nullptr;
    }

    bool BubbleWake::extend(const BubbleTrailPoint &point, Vec2 origin) noexcept {
        const Vec2 position{origin.x + point.position.x, origin.y + point.position.y};
        if (!finite(position) || !finite(point.velocity) || !std::isfinite(point.halfWidth) ||
            !std::isfinite(point.thickness) || point.halfWidth <= 0 || point.thickness <= 0)
            return true;
        Strand *s = strand(point.strand);
        if (!s) {
            const auto free = std::ranges::find_if(strands_, [](const Strand &x) { return x.count == 0; });
            if (free == strands_.end()) return false;
            s = &*free;
            *s = {};
            s->id = point.strand;
            s->open = true;
            s->thread = point.halfWidth < 6;
        }
        Node node;
        node.position = position;
        node.velocity = point.velocity;
        node.halfWidth = point.halfWidth;
        node.provisional = point.provisional && !point.last;
        if (s->count > 0 && s->nodes[s->count - 1].provisional) {
            // The window has moved on: its strand's newest point moves with it.
            auto &head = s->nodes[s->count - 1];
            node.thickness = head.thickness;
            node.evaporation = head.evaporation;
            head = node;
        } else {
            if (s->count == s->nodes.size()) {
                // The oldest film makes room; its free tail keeps the place it had retracted to.
                s->tail = std::max(0.0, s->tail - distance(s->nodes[0].position, s->nodes[1].position));
                std::move(s->nodes.begin() + 1, s->nodes.end(), s->nodes.begin());
                --s->count;
            }
            // Every patch dries at its own pace: thicker liquid lasts longer, threads go first.
            node.thickness = point.thickness * (.85 + .3 * unitDraw(random_));
            const double seconds = (.75 + .75 * unitDraw(random_)) * (s->thread ? .6 : 1);
            node.evaporation = node.thickness / seconds;
            s->nodes[s->count++] = node;
        }
        if (point.last) s->open = false;
        s->idle = 0;
        return true;
    }

    bool BubbleWake::release(const BubbleRelease &bubble, Vec2 origin) noexcept {
        const auto &m = bubble.micro;
        const Vec2 centre{origin.x + m.centre.x, origin.y + m.centre.y};
        if (!finite(centre) || !finite(bubble.velocity) || !std::isfinite(m.radius) || m.radius <= 0 ||
            !std::isfinite(bubble.life))
            return true;
        const int slot = foam_.spawn(centre, bubble.velocity, m.radius, m.thickness, 1, std::max(.05, bubble.life));
        if (slot < 0) return false;
        // The same bubble as was drawn on the window: its film pattern, its outline still ringing.
        auto &f = foam_.film(slot);
        f.seed = m.seed;
        f.phase = 0;
        f.flow = m.flow;
        f.squash = m.squash;
        f.squashRate = finite(bubble.squashRate) ? bubble.squashRate : Vec2{};
        necks_[std::size_t(slot)] = {{origin.x + m.anchor.x, origin.y + m.anchor.y}, std::clamp(m.attach, 0.0, 1.0)};
        return true;
    }

    void BubbleWake::hurry() noexcept {
        pace_ = 4;
    }

    void BubbleWake::advance(double elapsed) noexcept {
        if (!std::isfinite(elapsed) || elapsed <= 0) return;
        const double dt = std::min(elapsed, kStallSeconds) * pace_;
        const double brake = std::exp(-dt / kFilmBrake);
        const double straighten = .5 * (1 - std::exp(-dt / kStraighten));
        for (auto &s : strands_) {
            if (s.count == 0) continue;
            s.age += dt;
            // A strand the window stopped drawing without pinching it off (movement switched off,
            // the window gone) is free from here.
            s.idle += dt;
            if (s.open && s.idle > .1) {
                s.open = false;
                s.nodes[s.count - 1].provisional = false;
            }
            for (std::size_t i = 0; i < s.count; ++i) {
                auto &n = s.nodes[i];
                if (n.provisional) continue;
                n.position = {n.position.x + n.velocity.x * (1 - brake) * kFilmBrake,
                    n.position.y + n.velocity.y * (1 - brake) * kFilmBrake};
                n.velocity = {n.velocity.x * brake, n.velocity.y * brake};
                n.evaporated += n.evaporation * dt;
                if (n.cut > 0) n.cut = std::min(kBreakGap + .2 * n.halfWidth, n.cut + kBreakSpeed * dt);
            }
            for (std::size_t i = 1; i + 1 < s.count; ++i) {
                auto &n = s.nodes[i];
                if (n.provisional) continue;
                const Vec2 a = s.nodes[i - 1].position, b = s.nodes[i + 1].position;
                n.position = {n.position.x + ((a.x + b.x) * .5 - n.position.x) * straighten,
                    n.position.y + ((a.y + b.y) * .5 - n.position.y) * straighten};
            }
            // Free ends draw back into the film.
            s.tail += kRetractSpeed * dt * std::min(1.0, s.age / .15);
            if (!s.open) s.head += kRetractSpeed * dt;
            // A thread is unstable along its length (Rayleigh–Plateau): it breaks into pieces,
            // and now and then a break closes round a tiny bubble.
            if (s.thread && s.count > 3 && s.age > .12 && unitDraw(random_) < 3 * dt) {
                const auto at = 1 + std::size_t(unitDraw(random_) * double(s.count - 2));
                auto &n = s.nodes[at];
                if (n.cut == 0 && !n.provisional && !dry(n)) {
                    n.cut = .01;
                    if (unitDraw(random_) < .35)
                        foam_.spawn(n.position, n.velocity, 1.5 + 2.5 * unitDraw(random_),
                            n.thickness, .4, .25 + .5 * unitDraw(random_));
                }
            }
            // Dried film at the old end is gone.
            std::size_t gone = 0;
            while (gone < s.count && dry(s.nodes[gone]) && !s.nodes[gone].provisional) ++gone;
            if (gone > 0) {
                for (std::size_t i = 0; i < gone && i + 1 < s.count; ++i)
                    s.tail = std::max(0.0, s.tail - distance(s.nodes[i].position, s.nodes[i + 1].position));
                std::move(s.nodes.begin() + std::ptrdiff_t(gone),
                    s.nodes.begin() + std::ptrdiff_t(s.count),
                    s.nodes.begin());
                s.count -= gone;
            }
            if (s.count == 1 && !s.open) s.count = 0;
        }

        substep_ += dt;
        const BubbleFoamFlow still{};
        while (substep_ >= kStep) {
            foam_.step(kStep, still, nullptr);
            substep_ -= kStep;
        }
        // A neck left on a pinched-off bubble snaps back into it.
        const double snap = std::exp(-dt / .05);
        for (std::size_t i = 0; i < necks_.size(); ++i) {
            auto &neck = necks_[i];
            const auto &f = foam_.film(int(i));
            if (!f.live) {
                neck = {};
                continue;
            }
            neck.attach *= std::exp(-dt / .06);
            if (neck.attach < .01) neck.attach = 0;
            neck.anchor = {f.position.x + (neck.anchor.x - f.position.x) * snap,
                f.position.y + (neck.anchor.y - f.position.y) * snap};
        }
    }

    bool BubbleWake::done() const noexcept {
        return foam_.liveCount() == 0 &&
               std::ranges::all_of(strands_, [](const Strand &s) { return s.count == 0; });
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
        for (const auto &s : strands_)
            for (std::size_t i = 0; i < s.count; ++i)
                include(s.nodes[i].position, std::max(s.nodes[i].halfWidth * kFray, 1.5) + 2);
        for (std::size_t i = 0; i < foam_.capacity(); ++i) {
            const auto &f = foam_.film(int(i));
            if (!f.live) continue;
            include(f.position, f.radius * 1.5 + 3);
            if (necks_[i].attach > 0) include(necks_[i].anchor, 2);
        }
        if (!(x1 > x0 && y1 > y0)) return {};
        return {x0, y0, x1 - x0, y1 - y0};
    }

    void BubbleWake::view(BubbleWakeView &out) const noexcept {
        const auto box = bounds();
        out.size = {box.w, box.h};
        out.seed = seed_;
        out.nodeCount = out.segmentCount = 0;
        const Vec2 centre{box.x + box.w * .5, box.y + box.h * .5};
        for (const auto &s : strands_) {
            if (s.count < 2) continue;
            const std::size_t base = out.nodeCount;
            double arc = 0;
            for (std::size_t i = 0; i < s.count; ++i) {
                const auto &node = s.nodes[i];
                if (i > 0) arc += distance(node.position, s.nodes[i - 1].position);
                auto &v = out.nodes[base + i];
                v.position = sub(node.position, centre);
                // The strand turns with its path: the tangent spans the neighbouring points.
                const auto &before = s.nodes[i > 0 ? i - 1 : i].position;
                const auto &after = s.nodes[std::min(i + 1, s.count - 1)].position;
                const Vec2 span = sub(after, before);
                const double l = std::hypot(span.x, span.y);
                v.direction = l > 1e-6 ? Vec2{span.x / l, span.y / l} : Vec2{1, 0};
                v.halfWidth = node.halfWidth;
                v.arc = arc;
                v.thickness = node.thickness;
                v.evaporated = node.evaporated;
            }
            out.nodeCount += s.count;
            // Runs of film between breaks and fully dried patches.
            const auto close = [&](std::size_t first, std::size_t last, double from, double to) {
                if (out.segmentCount >= out.segments.size() || last <= first || to - from < 2) return;
                out.segments[out.segmentCount++] = {base + first, last - first + 1, from, to};
            };
            std::size_t first = 0;
            double from = out.nodes[base].arc + s.tail;
            for (std::size_t i = 1; i < s.count; ++i) {
                const auto &node = s.nodes[i];
                const double at = out.nodes[base + i].arc;
                if (dry(node) && !node.provisional) {
                    close(first, i, from, at);
                    first = i;
                    from = at;
                } else if (node.cut > 0 && i + 1 < s.count) {
                    close(first, i, from, at - node.cut);
                    first = i;
                    from = at + node.cut;
                }
            }
            close(first, s.count - 1, from, out.nodes[base + s.count - 1].arc - s.head);
        }
        out.bubbleCount = foam_.write(std::span(out.bubbles), 1, {1, 1}, 0);
        for (std::size_t i = 0; i < out.bubbleCount; ++i) {
            auto &m = out.bubbles[i];
            if (m.visibility <= 0) continue;
            m.centre = sub(m.centre, centre);
            m.anchor = necks_[i].attach > 0 ? sub(necks_[i].anchor, centre) : m.centre;
            m.attach = necks_[i].attach;
        }
    }

}  // namespace hyprimate
