#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "shared/Core.hpp"
#include "shared/NumericDomain.hpp"
#include "window/bubble/BubbleMotion.hpp"
#include "window/bubble/BubbleWake.hpp"

namespace hyprimate {
    namespace {
        // Strands are drawn this much wider than their film so a ragged border has room.
        constexpr double FRAY = 1.15;
        // Film left on the screen keeps a little of the window's motion; the air stops it within
        // a fraction of a second.
        constexpr double FILM_BRAKE = .18;
        // Surface tension straightens a strand's kinks at this rate.
        constexpr double STRAIGHTEN = .35;
        // A node holds film until drying has removed this much more than its laid-down thickness:
        // lenses of gathered liquid stay thicker than the strand's mean.
        constexpr double DRY_MARGIN = 1.45;
        // Free ends retract at a few tens of px/s: slowly, as viscous film on the glass does.
        constexpr double RETRACT_SPEED = 40;
        // A thread's break opens at this speed to this half length.
        constexpr double BREAK_SPEED = 90, BREAK_GAP = 5;
        // The bubbles: drag toward still air (time constant ~0.25 s) and a slow sink, since a
        // soap bubble is slightly denser than air; foam rises at 0.05 x buoyancy units per second.
        constexpr double BUBBLE_DAMPING = .45, SINK_SPEED = 12;
        constexpr double STEP = 1.0 / 120;

        bool finite(Vec2 value) {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }
        bool within_domain(double value) {
            constexpr double LIMIT = 1e9;
            return std::isfinite(value) && std::abs(value) <= LIMIT;
        }
        Vec2 sub(Vec2 left, Vec2 right) {
            return {.x = left.x - right.x, .y = left.y - right.y};
        }
        double distance(Vec2 left, Vec2 right) {
            return std::hypot(left.x - right.x, left.y - right.y);
        }
    }  // namespace

    BubbleWake::BubbleWake(std::uint64_t seed) noexcept : random(seed), seed(unit_draw(random)) {
        foam.reset(seed ^ 0x51ED270B27A3C1F5ULL,
            {.interaction = 1,
                .damping = BUBBLE_DAMPING,
                .buoyancy = -SINK_SPEED / .05},
            K_BUBBLE_WAKE_BUBBLES);
    }

    bool BubbleWake::dry(const Node &node) noexcept {
        return node.evaporated >= node.thickness * DRY_MARGIN;
    }

    BubbleWake::Strand *BubbleWake::strand(std::uint32_t strand_id) noexcept {
        for (auto &item : strands) {
            if (item.count > 0 && item.id == strand_id) {
                return &item;
            }
        }
        return nullptr;
    }

    bool BubbleWake::extend(const BubbleTrailPoint &point, Vec2 origin) noexcept {
        const Vec2 POSITION{.x = origin.x + point.position.x, .y = origin.y + point.position.y};
        if (!finite(POSITION) || !finite(point.velocity) || !std::isfinite(point.half_width) ||
            !std::isfinite(point.thickness) || point.half_width <= 0 || point.thickness <= 0) {
            return true;
        }
        Strand *current = strand(point.strand);
        if (current == nullptr) {
            auto *const FREE =
                std::ranges::find_if(strands, [](const Strand &item) { return item.count == 0; });
            if (FREE == strands.end()) {
                return false;
            }
            current = &*FREE;
            *current = {};
            current->id = point.strand;
            current->open = true;
            current->thread = point.half_width < 6;
        }
        if (!point.last && current->idle_closed) {
            // The prior run retracted its head while idle. Resume the joined strand at the new
            // point instead of carrying that stale retraction into the resumed head.
            current->head = 0;
            current->idle_closed = false;
            current->open = true;
        } else if (point.last) {
            current->open = false;
            current->idle_closed = false;
        }
        Node node;
        node.position = POSITION;
        node.velocity = point.velocity;
        node.half_width = point.half_width;
        node.provisional = point.provisional && !point.last;
        const auto ACTIVE = std::span{current->nodes}.first(current->count);
        if (!ACTIVE.empty() && ACTIVE.back().provisional) {
            // The window has moved on: its strand's newest point moves with it.
            auto &head = ACTIVE.back();
            node.thickness = head.thickness;
            node.evaporation = head.evaporation;
            head = node;
        } else {
            if (current->count == current->nodes.size()) {
                // The oldest film makes room; its free tail keeps the place it had retracted to.
                current->tail =
                    std::max(0.0, current->tail - distance(ACTIVE[0].position, ACTIVE[1].position));
                std::move(ACTIVE.begin() + 1, ACTIVE.end(), ACTIVE.begin());
                --current->count;
            }
            // Every patch dries at its own pace: thicker liquid lasts longer, threads go first.
            node.thickness = point.thickness * (.85 + (.3 * unit_draw(random)));
            const double SECONDS = (.75 + (.75 * unit_draw(random))) * (current->thread ? .6 : 1);
            node.evaporation = node.thickness / SECONDS;
            current->nodes[current->count++] = node;
        }
        if (point.last) {
            current->open = false;
        }
        current->idle = 0;
        return true;
    }

    bool BubbleWake::release(const BubbleRelease &bubble, Vec2 origin) noexcept {
        constexpr double LIMIT = 1e9;
        const auto &micro = bubble.micro;
        const Vec2 CENTRE{.x = origin.x + micro.centre.x, .y = origin.y + micro.centre.y};
        const Vec2 ANCHOR{.x = origin.x + micro.anchor.x, .y = origin.y + micro.anchor.y};
        if (!finite(CENTRE) || !finite(ANCHOR) || !finite(bubble.velocity) ||
            !within_domain(CENTRE.x) || !within_domain(CENTRE.y) ||
            !within_domain(ANCHOR.x) || !within_domain(ANCHOR.y) ||
            !within_domain(bubble.velocity.x) || !within_domain(bubble.velocity.y) ||
            !std::isfinite(micro.radius) || micro.radius <= 0 || micro.radius > LIMIT ||
            !within_domain(micro.thickness) || micro.thickness <= 0 ||
            !within_domain(micro.flow) || !std::isfinite(micro.seed) ||
            !within_domain(micro.squash.x) || !within_domain(micro.squash.y) ||
            !std::isfinite(micro.attach) || !std::isfinite(bubble.life) ||
            std::abs(bubble.life) > LIMIT) {
            return true;
        }
        const int SLOT = foam.spawn(
            CENTRE, bubble.velocity, micro.radius, micro.thickness, 1, std::max(.05, bubble.life));
        if (SLOT < 0) {
            return false;
        }
        // The same bubble as was drawn on the window: its film pattern, its outline still ringing.
        auto &film = foam.film(SLOT);
        film.seed = std::clamp(micro.seed, 0.0, 1.0);
        film.phase = 0;
        film.flow = micro.flow;
        film.squash = {.x = std::clamp(micro.squash.x, -1.0, 1.0),
            .y = std::clamp(micro.squash.y, -1.0, 1.0)};
        film.squash_rate = {
            .x = std::clamp(finite_or(bubble.squash_rate.x, 0.0), -LIMIT, LIMIT),
            .y = std::clamp(finite_or(bubble.squash_rate.y, 0.0), -LIMIT, LIMIT)};
        necks[static_cast<std::size_t>(SLOT)] = {
            .anchor = ANCHOR, .attach = std::clamp(micro.attach, 0.0, 1.0)};
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
        const double BRAKE = std::exp(-DELTA_TIME / FILM_BRAKE);
        const double STRAIGHTENING = .5 * (1 - std::exp(-DELTA_TIME / STRAIGHTEN));
        for (auto &item : strands) {
            if (item.count == 0) {
                continue;
            }
            item.age += DELTA_TIME;
            // A strand the window stopped drawing without pinching it off (movement switched off,
            // the window gone) is free from here.
            item.idle += DELTA_TIME;
            auto active = std::span{item.nodes}.first(item.count);
            if (item.open && item.idle > .1) {
                item.open = false;
                item.idle_closed = true;
                active.back().provisional = false;
            }
            for (auto &node : active) {
                if (node.provisional) {
                    continue;
                }
                node.position = {
                    .x = node.position.x + (node.velocity.x * (1 - BRAKE) * FILM_BRAKE),
                    .y = node.position.y + (node.velocity.y * (1 - BRAKE) * FILM_BRAKE)};
                node.velocity = {.x = node.velocity.x * BRAKE, .y = node.velocity.y * BRAKE};
                node.evaporated += node.evaporation * DELTA_TIME;
                if (node.cut > 0) {
                    node.cut = std::min(
                        BREAK_GAP + (.2 * node.half_width), node.cut + (BREAK_SPEED * DELTA_TIME));
                }
            }
            for (std::size_t i = 1; i + 1 < item.count; ++i) {
                auto &node = active[i];
                if (node.provisional) {
                    continue;
                }
                const Vec2 BEFORE = active[i - 1].position;
                const Vec2 AFTER = active[i + 1].position;
                node.position = {
                    .x = node.position.x +
                         ((((BEFORE.x + AFTER.x) * .5) - node.position.x) * STRAIGHTENING),
                    .y = node.position.y +
                         ((((BEFORE.y + AFTER.y) * .5) - node.position.y) * STRAIGHTENING)};
            }
            // Free ends draw back into the film.
            item.tail += RETRACT_SPEED * DELTA_TIME * std::min(1.0, item.age / .15);
            if (!item.open) {
                item.head += RETRACT_SPEED * DELTA_TIME;
            }
            // A thread is unstable along its length (Rayleigh–Plateau): it breaks into pieces,
            // and now and then a break closes round a tiny bubble.
            if (item.thread && item.count > 3 && item.age > .12 &&
                unit_draw(random) < 3 * DELTA_TIME) {
                const auto INDEX = 1 + static_cast<std::size_t>(
                                           unit_draw(random) * static_cast<double>(item.count - 2));
                auto &node = active[INDEX];
                if (node.cut == 0 && !node.provisional && !dry(node)) {
                    node.cut = .01;
                    if (unit_draw(random) < .35) {
                        foam.spawn(node.position,
                            node.velocity,
                            1.5 + (2.5 * unit_draw(random)),
                            node.thickness,
                            .4,
                            .25 + (.5 * unit_draw(random)));
                    }
                }
            }
            // Dried film at the old end is gone.
            std::size_t gone = 0;
            while (gone < item.count && dry(active[gone]) && !active[gone].provisional) {
                ++gone;
            }
            if (gone > 0) {
                for (std::size_t i = 0; i < gone && i + 1 < item.count; ++i) {
                    item.tail = std::max(
                        0.0, item.tail - distance(active[i].position, active[i + 1].position));
                }
                std::move(active.begin() + static_cast<std::ptrdiff_t>(gone),
                    active.end(),
                    active.begin());
                item.count -= gone;
            }
            if (item.count == 1 && !item.open) {
                item.count = 0;
            }
        }

        substep += DELTA_TIME;
        const BubbleFoamFlow STILL{};
        while (substep >= STEP) {
            foam.step(STEP, STILL, nullptr);
            substep -= STEP;
        }
        // A neck left on a pinched-off bubble snaps back into it.
        const double SNAP = std::exp(-DELTA_TIME / .05);
        for (std::size_t i = 0; i < necks.size(); ++i) {
            auto &neck = necks[i];
            const auto &film = foam.film(static_cast<int>(i));
            if (!film.live) {
                neck = {};
                continue;
            }
            neck.attach *= std::exp(-DELTA_TIME / .06);
            if (neck.attach < .01) {
                neck.attach = 0;
            }
            neck.anchor = {.x = film.position.x + ((neck.anchor.x - film.position.x) * SNAP),
                .y = film.position.y + ((neck.anchor.y - film.position.y) * SNAP)};
        }
    }

    bool BubbleWake::done() const noexcept {
        return foam.live_count() == 0 &&
               std::ranges::all_of(strands, [](const Strand &item) { return item.count == 0; });
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
        for (const auto &item : strands) {
            for (const auto &node : std::span{item.nodes}.first(item.count)) {
                INCLUDE(node.position, std::max(node.half_width * FRAY, 1.5) + 2);
            }
        }
        for (std::size_t i = 0; i < foam.capacity(); ++i) {
            const auto &film = foam.film(static_cast<int>(i));
            if (!film.live) {
                continue;
            }
            INCLUDE(film.position, (film.radius * 1.5) + 3);
            if (necks[i].attach > 0) {
                INCLUDE(necks[i].anchor, 2);
            }
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
        out.node_count = out.segment_count = 0;
        const Vec2 CENTRE{.x = BOX.x + (BOX.w * .5), .y = BOX.y + (BOX.h * .5)};
        for (const auto &item : strands) {
            if (item.count < 2) {
                continue;
            }
            const std::size_t BASE = out.node_count;
            const auto ACTIVE = std::span{item.nodes}.first(item.count);
            const auto OUTPUT = std::span{out.nodes}.subspan(BASE, item.count);
            double arc = 0;
            for (std::size_t i = 0; i < item.count; ++i) {
                const auto &node = ACTIVE[i];
                if (i > 0) {
                    arc += distance(node.position, ACTIVE[i - 1].position);
                }
                auto &view_node = OUTPUT[i];
                view_node.position = sub(node.position, CENTRE);
                // The strand turns with its path: the tangent spans the neighbouring points.
                const auto &before = ACTIVE[i > 0 ? i - 1 : i].position;
                const auto &after = ACTIVE[std::min(i + 1, item.count - 1)].position;
                const Vec2 SPAN = sub(after, before);
                const double LENGTH = std::hypot(SPAN.x, SPAN.y);
                view_node.direction = LENGTH > 1e-6
                                          ? Vec2{.x = SPAN.x / LENGTH, .y = SPAN.y / LENGTH}
                                          : Vec2{.x = 1, .y = 0};
                view_node.half_width = node.half_width;
                view_node.arc = arc;
                view_node.thickness = node.thickness;
                view_node.evaporated = node.evaporated;
            }
            out.node_count += item.count;
            // Runs of film between breaks and fully dried patches.
            const auto CLOSE = [&](std::size_t first, std::size_t last, double from, double end) {
                if (out.segment_count >= out.segments.size() || last <= first || end - from < 2) {
                    return;
                }
                out.segments[out.segment_count++] = {
                    .first = BASE + first, .count = last - first + 1, .from = from, .to = end};
            };
            std::size_t first = 0;
            double from = OUTPUT.front().arc + item.tail;
            for (std::size_t i = 1; i < item.count; ++i) {
                const auto &node = ACTIVE[i];
                const double ARC = OUTPUT[i].arc;
                if (dry(node) && !node.provisional) {
                    CLOSE(first, i, from, ARC);
                    first = i;
                    from = ARC;
                } else if (node.cut > 0 && i + 1 < item.count) {
                    CLOSE(first, i, from, ARC - node.cut);
                    first = i;
                    from = ARC + node.cut;
                }
            }
            CLOSE(first, item.count - 1, from, OUTPUT.back().arc - item.head);
        }
        out.bubble_count = foam.write(std::span(out.bubbles), 1, {.x = 1, .y = 1}, 0);
        for (std::size_t i = 0; i < out.bubble_count; ++i) {
            auto &micro = out.bubbles[i];
            if (micro.visibility <= 0) {
                continue;
            }
            micro.centre = sub(micro.centre, CENTRE);
            micro.anchor = necks[i].attach > 0 ? sub(necks[i].anchor, CENTRE) : micro.centre;
            micro.attach = necks[i].attach;
        }
    }

}  // namespace hyprimate
