#include "BubbleResidue.hpp"
#include "BubbleRenderer.hpp"

#include <aquamarine/output/Output.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr double LIFETIME = 0.55;

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

bool CBubbleResidue::deposit(const PHLMONITOR &monitor, Vector2D position, double radius, double seed) {
    if (!m_enabled || !monitor || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(radius) ||
        !std::isfinite(seed) || radius <= 0.0 ||
        footprint(position, radius).intersection(CBox{{}, monitor->m_size}).empty())
        return false;
    m_deposits.push_back({monitor, position, radius, seed, std::chrono::steady_clock::now()});
    damage(monitor, position, radius);
    return true;
}

void CBubbleResidue::tick(std::chrono::steady_clock::time_point now, const PHLMONITOR &monitor) {
    for (auto &deposit : m_deposits) {
        if (deposit.monitor.lock() != monitor)
            continue;
        deposit.age = std::max(0.0, std::chrono::duration<double>(now - deposit.start).count() / LIFETIME);
        damage(monitor, deposit.position, deposit.radius);
    }
    std::erase_if(m_deposits, [](const Deposit &deposit) { return deposit.monitor.expired() || deposit.age >= 1.0; });
}

void CBubbleResidue::queue(Render::CRenderContext &ctx, const PHLMONITOR &monitor,
                           const SP<CBubbleRenderer> &renderer) {
    if (!monitor || !renderer || ctx.readOnlyEffects() || ctx.m_renderingCapture || ctx.m_renderingSnapshot ||
        ctx.m_mode != Render::RENDER_MODE_NORMAL || ctx.m_data.pMonitor.lock() != monitor)
        return;
    const CBox screen{{}, monitor->m_transformedSize};
    for (const auto &deposit : m_deposits) {
        if (deposit.monitor.lock() != monitor)
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
        data.draw.frame.seed = deposit.seed;
        data.draw.canvas = clipped.size();
        data.draw.source = source;
        data.draw.outputOrigin = {clipped.x, clipped.y};
        data.draw.residueTime = deposit.age;
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
