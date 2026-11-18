#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <aquamarine/output/Output.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include "Bubble.hpp"
#include "BubbleMotion.hpp"
#include "BubbleRenderer.hpp"
#include "BubbleResidue.hpp"
#include "animations/shared/Core.hpp"

namespace {
    CBox footprint(Vector2D position, double radius) {
        return {position.x - radius, position.y - radius, radius * 2.0, radius * 2.0};
    }

    void damage(const PHLMONITOR &monitor, Vector2D position, double radius) {
        if (!monitor || !g_pHyprRenderer) {
            return;
        }
        auto box = footprint(position, radius).intersection(CBox{{}, monitor->m_size});
        if (box.empty()) {
            return;
        }
        box.translate(monitor->m_position);
        g_pHyprRenderer->damageBox(box);
        monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }

    void damage(const PHLMONITOR &monitor, const hyprimate::Box &logical) {
        if (!monitor || !g_pHyprRenderer || !hyprimate::is_usable(logical)) {
            return;
        }
        auto box = CBox{logical.x, logical.y, logical.w, logical.h}.expand(2.0).intersection(
            CBox{{}, monitor->m_size});
        if (box.empty()) {
            return;
        }
        box.translate(monitor->m_position);
        g_pHyprRenderer->damageBox(box);
        monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }

    // The device-pixel box a monitor-local logical source box covers on screen, pixel aligned.
    CBox on_screen(const PHLMONITOR &monitor, const CBox &source) {
        const CBox SCREEN{{}, monitor->m_transformedSize};
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

    // Only the monitor's own normal frame draws residue: never captures, snapshots or read-only
    // passes.
    bool draws_residue(const Render::CRenderContext &ctx, const PHLMONITOR &monitor) {
        return monitor && !ctx.readOnlyEffects() && !ctx.m_renderingCapture &&
               !ctx.m_renderingSnapshot && ctx.m_mode == Render::RENDER_MODE_NORMAL &&
               ctx.m_data.pMonitor.lock() == monitor;
    }

    // Wakes alive at once over every monitor; beyond this the oldest dissolve faster.
    constexpr std::size_t MAX_WAKES = 12;
}  // namespace

bool CBubbleResidue::deposit(const PHLMONITOR &monitor,
    Vector2D centre,
    std::span<const hyprimate::BubbleImpact> impacts,
    const SP<SBubbleImpactClock> &clock) {
    if (!m_enabled || !monitor || !std::isfinite(centre.x) || !std::isfinite(centre.y)) {
        return false;
    }
    bool accepted = false;
    const auto NOW = std::chrono::steady_clock::now();
    for (const auto &impact : impacts) {
        const Vector2D POSITION{centre.x + impact.position.x, centre.y + impact.position.y};
        if (!std::isfinite(POSITION.x) || !std::isfinite(POSITION.y) ||
            !std::isfinite(impact.radius) || !std::isfinite(impact.delay) ||
            !std::isfinite(impact.drift.x) || !std::isfinite(impact.drift.y) ||
            impact.radius <= 0) {
            continue;
        }
        const double LIFETIME = std::clamp(0.35 + (impact.radius * 0.07), 0.35, 0.7);
        const double EXTENT =
            (impact.radius * 1.3) + (std::hypot(impact.drift.x, impact.drift.y) * LIFETIME) + 2.0;
        if (impact.delay <= -LIFETIME ||
            footprint(POSITION, EXTENT).intersection(CBox{{}, monitor->m_size}).empty()) {
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
        damage(monitor, POSITION, EXTENT);
        accepted = true;
    }
    return accepted;
}

void CBubbleResidue::tick(std::chrono::steady_clock::time_point now, const PHLMONITOR &monitor) {
    for (auto &deposit : m_deposits) {
        if (deposit.monitor.lock() != monitor) {
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
            deposit.age += std::clamp(std::chrono::duration<double>(now - deposit.last).count(),
                0.0,
                hyprimate::STALL_SECONDS);
        }
        deposit.last = now;
        damage(monitor, deposit.position, deposit.radius);
    }
    std::erase_if(m_deposits, [](const Deposit &deposit) {
        return deposit.monitor.expired() || deposit.age >= deposit.lifetime ||
               (deposit.clock && deposit.clock->cancelled);
    });
    for (auto &wake : m_wakes) {
        if (wake.monitor.lock() != monitor) {
            continue;
        }
        wake.queued = false;  // a new frame of this monitor begins
        const double ELAPSED = std::clamp(
            std::chrono::duration<double>(now - wake.last).count(), 0.0, hyprimate::STALL_SECONDS);
        wake.last = now;
        // Both the region it covered and the one it covers now: retracting film leaves no stale
        // pixels behind.
        damage(monitor, wake.film.bounds());
        wake.film.advance(ELAPSED);
        damage(monitor, wake.film.bounds());
    }
    std::erase_if(
        m_wakes, [](const Wake &wake) { return wake.monitor.expired() || wake.film.done(); });
}

CBubbleResidue::Wake *CBubbleResidue::wake_for(const PHLMONITOR &monitor, std::uint64_t wake_id) {
    if (wake_id == 0) {
        return nullptr;
    }
    for (auto &wake : m_wakes) {
        if (wake.id == wake_id && wake.monitor.lock() == monitor) {
            return &wake;
        }
    }
    return nullptr;
}

CBubbleResidue::Wake &CBubbleResidue::begin_wake(
    const PHLMONITOR &monitor, const PHLWINDOW &owner) {
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

std::uint64_t CBubbleResidue::trail(const PHLMONITOR &monitor,
    const PHLWINDOW &owner,
    std::uint64_t wake,
    Vector2D centre,
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
    damage(monitor, current->film.bounds());
    return current->id;
}

void CBubbleResidue::queue_wake(Render::CRenderContext &ctx,
    const PHLMONITOR &monitor,
    Wake &wake,
    const SP<CBubbleRenderer> &renderer) {
    wake.queued = true;
    const auto BOUNDS = wake.film.bounds();
    if (!hyprimate::is_usable(BOUNDS)) {
        return;
    }
    const auto SOURCE = CBox{BOUNDS.x, BOUNDS.y, BOUNDS.w, BOUNDS.h}.scale(monitor->m_scale);
    const auto CLIPPED = on_screen(monitor, SOURCE);
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
    data.scale = monitor->m_scale;
    data.damage = CRegion{CLIPPED};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubblePassElement>(std::move(data)));
}

void CBubbleResidue::queue_under(Render::CRenderContext &ctx,
    const PHLMONITOR &monitor,
    const PHLWINDOW &window,
    const SP<CBubbleRenderer> &renderer) {
    if (!window || !renderer || !draws_residue(ctx, monitor)) {
        return;
    }
    for (auto &wake : m_wakes) {
        if (!wake.queued && wake.monitor.lock() == monitor && wake.owner.lock() == window) {
            queue_wake(ctx, monitor, wake, renderer);
        }
    }
}

void CBubbleResidue::queue(
    Render::CRenderContext &ctx, const PHLMONITOR &monitor, const SP<CBubbleRenderer> &renderer) {
    if (!renderer || !draws_residue(ctx, monitor)) {
        return;
    }
    for (const auto &deposit : m_deposits) {
        if (deposit.monitor.lock() != monitor || deposit.age < 0) {
            continue;
        }
        const auto SOURCE = footprint(deposit.position, deposit.radius).scale(monitor->m_scale);
        const auto CLIPPED = on_screen(monitor, SOURCE);
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
        data.scale = monitor->m_scale;
        data.damage = CRegion{CLIPPED};
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubblePassElement>(std::move(data)));
    }
    // Wakes whose window was not drawn this frame (closed, or on a hidden workspace) lie over the
    // windows instead.
    for (auto &wake : m_wakes) {
        if (!wake.queued && wake.monitor.lock() == monitor) {
            queue_wake(ctx, monitor, wake, renderer);
        }
    }
}

bool CBubbleResidue::active(const PHLMONITOR &monitor) const {
    const auto HERE = [&](const auto &entry) { return entry.monitor.lock() == monitor; };
    return std::ranges::any_of(m_deposits, HERE) || std::ranges::any_of(m_wakes, HERE);
}

void CBubbleResidue::set_enabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        clear();
    }
}

void CBubbleResidue::clear() {
    for (const auto &deposit : m_deposits) {
        damage(deposit.monitor.lock(), deposit.position, deposit.radius);
    }
    m_deposits.clear();
    for (const auto &wake : m_wakes) {
        damage(wake.monitor.lock(), wake.film.bounds());
    }
    m_wakes.clear();
}
