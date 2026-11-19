#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include "Bubble.hpp"
#include "BubbleMotion.hpp"
#include "BubbleRenderer.hpp"
#include "BubbleVestige.hpp"
#include "animations/shared/Core.hpp"

namespace {
    CBox footprint(Vector2D position, double radius) {
        return {position.x - radius, position.y - radius, radius * 2.0, radius * 2.0};
    }

    CBox on_screen(const CBubbleVestige::ScreenFacts &screen, const CBox &source) {
        const CBox SCREEN{0, 0, screen.screen.w * screen.scale, screen.screen.h * screen.scale};
        const auto CLIPPED = source.copy().intersection(SCREEN);
        if (CLIPPED.empty()) {
            return {};
        }
        const double LEFT = std::floor(CLIPPED.x);
        const double TOP = std::floor(CLIPPED.y);
        return CBox{LEFT,
            TOP,
            std::ceil(CLIPPED.x + CLIPPED.w) - LEFT,
            std::ceil(CLIPPED.y + CLIPPED.h) - TOP}
            .intersection(SCREEN);
    }


    // Wakes alive at once over every monitor; beyond this the oldest dissolve faster.
    constexpr std::size_t MAX_WAKES = 12;
}  // namespace

bool CBubbleVestige::deposit(MonitorId monitor,
    hyprimate::Vec2 centre,
    std::span<const hyprimate::BubbleImpact> impacts,
    const SP<SBubbleImpactClock> &clock) {
    if (!m_enabled || !monitor || !std::isfinite(centre.x) || !std::isfinite(centre.y)) {
        return false;
    }
    bool accepted = false;
    const auto NOW = std::chrono::steady_clock::now();
    for (const auto &impact : impacts) {
        const hyprimate::Vec2 POSITION{centre.x + impact.position.x, centre.y + impact.position.y};
        if (!std::isfinite(POSITION.x) || !std::isfinite(POSITION.y) ||
            !std::isfinite(impact.radius) || !std::isfinite(impact.delay) ||
            !std::isfinite(impact.drift.x) || !std::isfinite(impact.drift.y) ||
            impact.radius <= 0) {
            continue;
        }
        const double LIFETIME = std::clamp(0.35 + (impact.radius * 0.07), 0.35, 0.7);
        const double EXTENT =
            (impact.radius * 1.3) + (std::hypot(impact.drift.x, impact.drift.y) * LIFETIME) + 2.0;
        if (impact.delay <= -LIFETIME) {
            continue;
        }
        // Delays are animation seconds on the source transition's clock; without one the bead keeps
        // wall time.
        const double CLOCK_NOW = clock ? clock->time : 0.0;
        m_deposits.push_back({
            .monitor = monitor,
            .position = POSITION,
            .radius = EXTENT,
            .impact_radius = impact.radius,
            .drift = {impact.drift.x, impact.drift.y},
            .lifetime = LIFETIME,
            .last = NOW,
            .age = -impact.delay,
            .clock = impact.delay > 0 ? clock : SP<SBubbleImpactClock>{},
            .land = CLOCK_NOW + impact.delay,
        });
        accepted = true;
    }
    return accepted;
}

void CBubbleVestige::tick(const ScreenFacts &screen) {
    for (auto &deposit : m_deposits) {
        if (deposit.monitor != screen.monitor) {
            continue;
        }
        if (deposit.clock && deposit.clock->cancelled) {
            deposit.age = -1;  // its droplet vanished with the aborted transition; erased below
        } else if (deposit.clock) {
            // In flight: follow the transition that threw the droplet, then keep its own time once
            // landed or once the transition has ended.
            deposit.age = deposit.clock->time - deposit.land;
            if (deposit.age >= 0 || deposit.clock->ended) {
                deposit.clock.reset();
            }
        } else {
            deposit.age += std::clamp(std::chrono::duration<double>(screen.now - deposit.last).count(),
                0.0,
                hyprimate::STALL_SECONDS);
        }
        deposit.last = screen.now;
    }
    std::erase_if(m_deposits, [](const Deposit &deposit) {
        return deposit.age >= deposit.lifetime ||
               (deposit.clock && deposit.clock->cancelled);
    });
    for (auto &wake : m_wakes) {
        if (wake.monitor != screen.monitor) {
            continue;
        }
        wake.queued = false;  // a new frame of this monitor begins
        const double ELAPSED = std::clamp(
            std::chrono::duration<double>(screen.now - wake.last).count(), 0.0, hyprimate::STALL_SECONDS);
        wake.last = screen.now;
        // Both the region it covered and the one it covers now: retracting film leaves no stale
        // pixels behind.
        wake.film.advance(ELAPSED);
    }
    std::erase_if(
        m_wakes, [](const Wake &wake) { return wake.film.done(); });
}

CBubbleVestige::Wake *CBubbleVestige::wake_for(MonitorId monitor, std::uint64_t wake_id) {
    if (wake_id == 0) {
        return nullptr;
    }
    for (auto &wake : m_wakes) {
        if (wake.id == wake_id && wake.monitor == monitor) {
            return &wake;
        }
    }
    return nullptr;
}

CBubbleVestige::Wake &CBubbleVestige::begin_wake(
    MonitorId monitor, WindowId owner) {
    if (m_wakes.size() >= MAX_WAKES) {
        for (auto &wake : std::span{m_wakes}.first(m_wakes.size() - MAX_WAKES + 1)) {
            wake.film.hurry();
        }
    }
    const auto WAKE_ID = m_next_wake++;
    m_wakes.push_back({
        .monitor = monitor,
        .owner = owner,
        .id = WAKE_ID,
        .film = hyprimate::BubbleWake{WAKE_ID * 0x9E3779B97F4A7C15ULL},
        .last = std::chrono::steady_clock::now(),
    });
    return m_wakes.back();
}

std::uint64_t CBubbleVestige::trail(MonitorId monitor,
    WindowId owner,
    std::uint64_t wake,
    hyprimate::Vec2 centre,
    std::span<const hyprimate::BubbleTrailPoint> points,
    std::span<const hyprimate::BubbleRelease> releases) {
    if (!m_enabled || !monitor || !std::isfinite(centre.x) || !std::isfinite(centre.y)) {
        return 0;
    }
    const hyprimate::Vec2 ORIGIN{.x = centre.x, .y = centre.y};
    Wake *current = wake_for(monitor, wake);
    for (const auto &point : points) {
        // A wake with no room for another strand hands new ones to a fresh wake.
        if ((current == nullptr) || !current->film.extend(point, ORIGIN)) {
            current = &begin_wake(monitor, owner);
            current->film.extend(point, ORIGIN);
        }
    }
    for (const auto &release : releases) {
        if ((current == nullptr) || !current->film.release(release, ORIGIN)) {
            current = &begin_wake(monitor, owner);
            current->film.release(release, ORIGIN);
        }
    }
    if (current == nullptr) {
        return wake;
    }
    return current->id;
}

bool CBubbleVestige::queued(const Wake &wake, bool live) const {
    return live ? wake.queued : std::ranges::find(m_capture_wakes, wake.id) != m_capture_wakes.end();
}

void CBubbleVestige::queue_wake(Render::CRenderContext &ctx,
    const ScreenFacts &screen,
    Wake &wake,
    const SP<CBubbleRenderer> &renderer, bool live) {
    if (live) wake.queued = true;
    else m_capture_wakes.push_back(wake.id);
    const auto BOUNDS = wake.film.bounds();
    if (!hyprimate::is_usable(BOUNDS)) {
        return;
    }
    const auto SOURCE = CBox{BOUNDS.x, BOUNDS.y, BOUNDS.w, BOUNDS.h}.scale(screen.scale);
    const auto CLIPPED = on_screen(screen, SOURCE);
    if (CLIPPED.empty()) {
        return;
    }
    CBubblePassElement::SData data;
    data.renderer = renderer;
    data.draw.frame.size = {.x = BOUNDS.w, .y = BOUNDS.h};
    data.draw.canvas = CLIPPED.size();
    data.draw.source = SOURCE;
    data.draw.output_origin = {.x = CLIPPED.x, .y = CLIPPED.y};
    data.draw.on_glass = true;
    wake.film.view(data.draw.wake.emplace());
    data.box = CLIPPED;
    data.scale = screen.scale;
    data.damage = CRegion{CLIPPED};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubblePassElement>(std::move(data)));
}

void CBubbleVestige::queue_under(Render::CRenderContext &ctx,
    const ScreenFacts &screen,
    WindowId window,
    const SP<CBubbleRenderer> &renderer, bool live) {
    if (!window || !renderer) {
        return;
    }
    for (auto &wake : m_wakes) {
        if (!queued(wake, live) && wake.monitor == screen.monitor && wake.owner == window) {
            queue_wake(ctx, screen, wake, renderer, live);
        }
    }
}

void CBubbleVestige::queue(
    Render::CRenderContext &ctx, const ScreenFacts &screen, const SP<CBubbleRenderer> &renderer, bool live) {
    if (!renderer) {
        return;
    }
    for (const auto &deposit : m_deposits) {
        if (deposit.monitor != screen.monitor || deposit.age < 0) {
            continue;
        }
        const auto SOURCE = footprint({deposit.position.x, deposit.position.y}, deposit.radius).scale(screen.scale);
        const auto CLIPPED = on_screen(screen, SOURCE);
        if (CLIPPED.empty()) {
            continue;
        }
        CBubblePassElement::SData data;
        data.renderer = renderer;
        data.draw.frame.size = {.x = deposit.radius * 2.0, .y = deposit.radius * 2.0};
        data.draw.canvas = CLIPPED.size();
        data.draw.source = SOURCE;
        data.draw.output_origin = {.x = CLIPPED.x, .y = CLIPPED.y};
        data.draw.on_glass = true;
        data.draw.glass_count = 1;
        auto &particle = data.draw.glass_particles.front();
        particle.centre = {.x = deposit.drift.x * deposit.age, .y = deposit.drift.y * deposit.age};
        particle.tail = particle.centre;
        particle.radius = deposit.impact_radius * (1.0 + (.3 * std::min(deposit.age / .03, 1.0))) *
                          std::sqrt(std::max(0.0, 1.0 - (deposit.age / deposit.lifetime)));
        data.box = CLIPPED;
        data.scale = screen.scale;
        data.damage = CRegion{CLIPPED};
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubblePassElement>(std::move(data)));
    }
    // Wakes whose window was not drawn this frame (closed, or on a hidden workspace) lie over the
    // windows instead.
    for (auto &wake : m_wakes) {
        if (!queued(wake, live) && wake.monitor == screen.monitor) {
            queue_wake(ctx, screen, wake, renderer, live);
        }
    }
    if (!live) m_capture_wakes.clear();
}

bool CBubbleVestige::active(MonitorId monitor) const {
    const auto HERE = [&](const auto &entry) { return entry.monitor == monitor; };
    return std::ranges::any_of(m_deposits, HERE) || std::ranges::any_of(m_wakes, HERE);
}

void CBubbleVestige::set_enabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        clear();
    }
}

void CBubbleVestige::clear(std::optional<MonitorId> monitor) {
    if (monitor) {
        std::erase_if(m_deposits, [&](const Deposit &deposit) { return deposit.monitor == *monitor; });
        std::erase_if(m_wakes, [&](const Wake &wake) { return wake.monitor == *monitor; });
    } else {
        m_deposits.clear();
        m_wakes.clear();
    }
    m_capture_wakes.clear();
}
