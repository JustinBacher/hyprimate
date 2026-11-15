
#include "BubbleResidue.hpp"
#include "BubbleRenderer.hpp"
#include <aquamarine/output/Output.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <algorithm>
#include <cmath>

namespace {
CBox footprint(Vector2D position, double radius) {
    return {position.x - radius, position.y - radius, radius * 2.0, radius * 2.0};
}

void damage(const PHLMONITOR &monitor, Vector2D position, double radius) {
    if (!monitor || !g_pHyprRenderer)
        return;
    auto box = footprint(position, radius).intersection(CBox{{}, monitor->m_size});
    if (box.empty())
        return;
    box.translate(monitor->m_position);
    g_pHyprRenderer->damageBox(box);
    monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
}
} // namespace

bool CBubbleResidue::deposit(const PHLMONITOR &monitor, Vector2D centre,
                             std::span<const liquid_glass::BubbleImpact> impacts, const SP<SBubbleImpactClock> &clock) {
    if (!m_enabled || !monitor || !std::isfinite(centre.x) || !std::isfinite(centre.y))
        return false;
    bool       accepted = false;
    const auto now = std::chrono::steady_clock::now();
    for (const auto &impact : impacts) {
        const Vector2D position{centre.x + impact.position.x, centre.y + impact.position.y};
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(impact.radius) ||
            !std::isfinite(impact.delay) || !std::isfinite(impact.drift.x) || !std::isfinite(impact.drift.y) ||
            impact.radius <= 0)
            continue;
        const double lifetime = std::clamp(0.35 + impact.radius * 0.07, 0.35, 0.7);
        const double extent = impact.radius * 1.3 + std::hypot(impact.drift.x, impact.drift.y) * lifetime + 2.0;
        if (impact.delay <= -lifetime || footprint(position, extent).intersection(CBox{{}, monitor->m_size}).empty())
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
        if (deposit.monitor.lock() != monitor)
            continue;
        if (deposit.clock && deposit.clock->cancelled) {
            deposit.age = -1; // its droplet vanished with the aborted transition; erased below
        } else if (deposit.clock) {
            // In flight: follow the transition that threw the droplet, then keep its own time once
            // landed or once the transition has ended.
            deposit.age = deposit.clock->time - deposit.land;
            if (deposit.age >= 0 || deposit.clock->ended)
                deposit.clock.reset();
        } else {
            deposit.age +=
                std::clamp(std::chrono::duration<double>(now - deposit.last).count(), 0.0, liquid_glass::kStallSeconds);
        }
        deposit.last = now;
        damage(monitor, deposit.position, deposit.radius);
    }
    std::erase_if(m_deposits, [](const Deposit &deposit) {
        return deposit.monitor.expired() || deposit.age >= deposit.lifetime ||
               (deposit.clock && deposit.clock->cancelled);
    });
}

void CBubbleResidue::queue(Render::CRenderContext &ctx, const PHLMONITOR &monitor,
                           const SP<CBubbleRenderer> &renderer) {
    if (!monitor || !renderer || ctx.readOnlyEffects() || ctx.m_renderingCapture || ctx.m_renderingSnapshot ||
        ctx.m_mode != Render::RENDER_MODE_NORMAL || ctx.m_data.pMonitor.lock() != monitor)
        return;
    const CBox screen{{}, monitor->m_transformedSize};
    for (const auto &deposit : m_deposits) {
        if (deposit.monitor.lock() != monitor || deposit.age < 0)
            continue;
        const auto source = footprint(deposit.position, deposit.radius).scale(monitor->m_scale);
        auto       clipped = source.copy().intersection(screen);
        if (clipped.empty())
            continue;
        const double x = std::floor(clipped.x), y = std::floor(clipped.y);
        clipped =
            CBox{x, y, std::ceil(clipped.x + clipped.w) - x, std::ceil(clipped.y + clipped.h) - y}.intersection(screen);
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
}

bool CBubbleResidue::active(const PHLMONITOR &monitor) const {
    return std::ranges::any_of(m_deposits, [&](const Deposit &deposit) { return deposit.monitor.lock() == monitor; });
}

void CBubbleResidue::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled)
        clear();
}

void CBubbleResidue::clear() {
    for (const auto &deposit : m_deposits)
        damage(deposit.monitor.lock(), deposit.position, deposit.radius);
    m_deposits.clear();
}
