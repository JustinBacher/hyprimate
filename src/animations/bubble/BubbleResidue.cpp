#include <algorithm>
#include <cmath>
#include <optional>

#include <aquamarine/output/Output.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include "BubbleRenderer.hpp"
#include "BubbleResidue.hpp"

namespace {
    CBox footprint(Vector2D position, double radius) {
        return {position.x - radius, position.y - radius, radius * 2.0, radius * 2.0};
    }

    void damage(const PHLMONITOR &monitor, Vector2D position, double radius) {
        if (!monitor || !g_pHyprRenderer) return;
        auto box = footprint(position, radius).intersection(CBox{{}, monitor->m_size});
        if (box.empty()) return;
        box.translate(monitor->m_position);
        g_pHyprRenderer->damageBox(box);
        monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }

    void damage(const PHLMONITOR &monitor, const hyprimate::Box &logical) {
        if (!monitor || !g_pHyprRenderer || !hyprimate::isUsable(logical)) return;
        auto box = CBox{logical.x, logical.y, logical.w, logical.h}.expand(2.0).intersection(
            CBox{{}, monitor->m_size});
        if (box.empty()) return;
        box.translate(monitor->m_position);
        g_pHyprRenderer->damageBox(box);
        monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }

    // The device-pixel box a monitor-local logical source box covers on screen, pixel aligned.
    CBox onScreen(const PHLMONITOR &monitor, const CBox &source) {
        const CBox screen{{}, monitor->m_transformedSize};
        auto clipped = source.copy().intersection(screen);
        if (clipped.empty()) return {};
        const double x = std::floor(clipped.x), y = std::floor(clipped.y);
        return CBox{
            x, y, std::ceil(clipped.x + clipped.w) - x, std::ceil(clipped.y + clipped.h) - y}
            .intersection(screen);
    }

    // Wakes alive at once over every monitor; beyond this the oldest dissolve faster.
    constexpr std::size_t kMaxWakes = 12;
}  // namespace

bool CBubbleResidue::deposit(const PHLMONITOR &monitor,
    Vector2D centre,
    std::span<const hyprimate::BubbleImpact> impacts,
    const SP<SBubbleImpactClock> &clock) {
    if (!m_enabled || !monitor || !std::isfinite(centre.x) || !std::isfinite(centre.y))
        return false;
    bool accepted = false;
    const auto now = std::chrono::steady_clock::now();
    for (const auto &impact : impacts) {
        const Vector2D position{centre.x + impact.position.x, centre.y + impact.position.y};
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(impact.radius) || !std::isfinite(impact.delay) ||
            !std::isfinite(impact.drift.x) || !std::isfinite(impact.drift.y) || impact.radius <= 0)
            continue;
        const double lifetime = std::clamp(0.35 + impact.radius * 0.07, 0.35, 0.7);
        const double extent =
            impact.radius * 1.3 + std::hypot(impact.drift.x, impact.drift.y) * lifetime + 2.0;
        if (impact.delay <= -lifetime ||
            footprint(position, extent).intersection(CBox{{}, monitor->m_size}).empty())
            continue;
        // Delays are animation seconds on the source transition's clock; without one the bead keeps
        // wall time.
        const double clockNow = clock ? clock->time : 0.0;
        m_deposits.push_back({monitor,
            position,
            extent,
            impact.radius,
            {impact.drift.x, impact.drift.y},
            lifetime,
            now,
            -impact.delay,
            impact.delay > 0 ? clock : SP<SBubbleImpactClock>{},
            clockNow + impact.delay});
        damage(monitor, position, extent);
        accepted = true;
    }
    return accepted;
}

void CBubbleResidue::tick(std::chrono::steady_clock::time_point now, const PHLMONITOR &monitor) {
    for (auto &deposit : m_deposits) {
        if (deposit.monitor.lock() != monitor) continue;
        if (deposit.clock && deposit.clock->cancelled) {
            deposit.age = -1;  // its droplet vanished with the aborted transition; erased below
        } else if (deposit.clock) {
            // In flight: follow the transition that threw the droplet, then keep its own time once
            // landed or once the transition has ended.
            deposit.age = deposit.clock->time - deposit.land;
            if (deposit.age >= 0 || deposit.clock->ended) deposit.clock.reset();
        } else {
            deposit.age += std::clamp(std::chrono::duration<double>(now - deposit.last).count(),
                0.0,
                hyprimate::kStallSeconds);
        }
        deposit.last = now;
        damage(monitor, deposit.position, deposit.radius);
    }
    std::erase_if(m_deposits, [](const Deposit &deposit) {
        return deposit.monitor.expired() || deposit.age >= deposit.lifetime ||
               (deposit.clock && deposit.clock->cancelled);
    });
    for (auto &wake : m_wakes) {
        if (wake.monitor.lock() != monitor) continue;
        const double elapsed = std::clamp(
            std::chrono::duration<double>(now - wake.last).count(), 0.0, hyprimate::kStallSeconds);
        wake.last = now;
        // Both the region it covered and the one it covers now: retracting film leaves no stale
        // pixels behind.
        damage(monitor, wake.film.bounds());
        wake.film.advance(elapsed);
        damage(monitor, wake.film.bounds());
    }
    std::erase_if(
        m_wakes, [](const Wake &wake) { return wake.monitor.expired() || wake.film.done(); });
}

CBubbleResidue::Wake *CBubbleResidue::wakeFor(const PHLMONITOR &monitor, std::uint64_t id) {
    if (id == 0) return nullptr;
    for (auto &wake : m_wakes)
        if (wake.id == id && wake.monitor.lock() == monitor) return &wake;
    return nullptr;
}

CBubbleResidue::Wake &CBubbleResidue::beginWake(const PHLMONITOR &monitor) {
    if (m_wakes.size() >= kMaxWakes)
        for (std::size_t i = 0; i + kMaxWakes <= m_wakes.size(); ++i) m_wakes[i].film.hurry();
    const auto id = m_nextWake++;
    m_wakes.push_back({monitor,
        id,
        hyprimate::BubbleWake{id * 0x9E3779B97F4A7C15ULL},
        std::chrono::steady_clock::now()});
    return m_wakes.back();
}

std::uint64_t CBubbleResidue::trail(const PHLMONITOR &monitor,
    std::uint64_t wake,
    Vector2D centre,
    std::span<const hyprimate::BubbleTrailPoint> points,
    std::span<const hyprimate::BubbleRelease> releases) {
    if (!m_enabled || !monitor || !std::isfinite(centre.x) || !std::isfinite(centre.y)) return 0;
    const hyprimate::Vec2 origin{centre.x, centre.y};
    Wake *current = wakeFor(monitor, wake);
    for (const auto &point : points) {
        if (!current || point.start || current->film.full()) {
            // A full wake hands its newest point on, so the ribbon continues without a gap.
            std::optional<hyprimate::BubbleTrailPoint> carry;
            if (current && !point.start && current->film.last()) carry = *current->film.last();
            current = &beginWake(monitor);
            if (carry) current->film.extend(*carry, {});
        }
        current->film.extend(point, origin);
    }
    for (const auto &release : releases) {
        if (!current || !current->film.release(release, origin)) {
            current = &beginWake(monitor);
            current->film.release(release, origin);
        }
    }
    if (!current) return wake;
    damage(monitor, current->film.bounds());
    return current->id;
}

void CBubbleResidue::queue(
    Render::CRenderContext &ctx, const PHLMONITOR &monitor, const SP<CBubbleRenderer> &renderer) {
    if (!monitor || !renderer || ctx.readOnlyEffects() || ctx.m_renderingCapture ||
        ctx.m_renderingSnapshot || ctx.m_mode != Render::RENDER_MODE_NORMAL ||
        ctx.m_data.pMonitor.lock() != monitor)
        return;
    for (const auto &deposit : m_deposits) {
        if (deposit.monitor.lock() != monitor || deposit.age < 0) continue;
        const auto source = footprint(deposit.position, deposit.radius).scale(monitor->m_scale);
        const auto clipped = onScreen(monitor, source);
        if (clipped.empty()) continue;
        CBubblePassElement::SData data;
        data.renderer = renderer;
        data.draw.frame.size = {deposit.radius * 2.0, deposit.radius * 2.0};
        data.draw.canvas = clipped.size();
        data.draw.source = source;
        data.draw.outputOrigin = {clipped.x, clipped.y};
        data.draw.onGlass = true;
        data.draw.glassCount = 1;
        auto &particle = data.draw.glassParticles[0];
        particle.centre = {deposit.drift.x * deposit.age, deposit.drift.y * deposit.age};
        particle.tail = particle.centre;
        particle.radius = deposit.impactRadius * (1.0 + .3 * std::min(deposit.age / .03, 1.0)) *
                          std::sqrt(std::max(0.0, 1.0 - deposit.age / deposit.lifetime));
        data.box = clipped;
        data.scale = monitor->m_scale;
        data.damage = CRegion{clipped};
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubblePassElement>(std::move(data)));
    }
    for (const auto &wake : m_wakes) {
        if (wake.monitor.lock() != monitor) continue;
        const auto bounds = wake.film.bounds();
        if (!hyprimate::isUsable(bounds)) continue;
        const auto source = CBox{bounds.x, bounds.y, bounds.w, bounds.h}.scale(monitor->m_scale);
        const auto clipped = onScreen(monitor, source);
        if (clipped.empty()) continue;
        CBubblePassElement::SData data;
        data.renderer = renderer;
        data.draw.frame.size = {bounds.w, bounds.h};
        data.draw.canvas = clipped.size();
        data.draw.source = source;
        data.draw.outputOrigin = {clipped.x, clipped.y};
        data.draw.onGlass = true;
        wake.film.view(data.draw.wake.emplace());
        data.box = clipped;
        data.scale = monitor->m_scale;
        data.damage = CRegion{clipped};
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBubblePassElement>(std::move(data)));
    }
}

bool CBubbleResidue::active(const PHLMONITOR &monitor) const {
    const auto here = [&](const auto &entry) { return entry.monitor.lock() == monitor; };
    return std::ranges::any_of(m_deposits, here) || std::ranges::any_of(m_wakes, here);
}

void CBubbleResidue::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) clear();
}

void CBubbleResidue::clear() {
    for (const auto &deposit : m_deposits)
        damage(deposit.monitor.lock(), deposit.position, deposit.radius);
    m_deposits.clear();
    for (const auto &wake : m_wakes) damage(wake.monitor.lock(), wake.film.bounds());
    m_wakes.clear();
}
