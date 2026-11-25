#include <algorithm>
#include <cmath>
#include <ranges>

#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>

#include "AnimationCloseRenderer.hpp"
#include "AnimationCloseRendererPrivate.hpp"
#include "HyprWindowPort.hpp"
#include "StyleTypes.hpp"
#include "window/WindowAnimations.hpp"
#include "window/construction/ConstructionRenderer.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

using namespace hyprimate::animation::close_detail;

void CAnimationCloseRenderer::note_monitor_snapshot(
    const PHLWINDOW &window, const PHLMONITOR &monitor, Render::CRenderContext &ctx) {
    using namespace hyprimate::animation;
    if (!ctx.active() || ctx.m_mode != Render::RENDER_MODE_FULL_FAKE || !ctx.m_renderingSnapshot ||
        ctx.m_renderingCapture || ctx.readOnlyEffects() || WorkspaceCaptureScope::for_context(ctx) ||
        WorkspaceCaptureScope::is_destination_live(ctx) || !monitor ||
        ctx.m_data.pMonitor.lock() != monitor || !ctx.m_data.outFB || !m_port.session_unlocked()) {
        return;
    }
    const auto PENDING = pending_for(window);
    if (!PENDING) { return; }
    const auto EXPECTED = m_animations.pending_public_close(*PENDING);
    if (!EXPECTED) { return; }
    std::erase_if(m_snapshots, [&](const Snapshot &receipt) {
        return receipt.pending == *PENDING || receipt.producer.expired() ||
            !m_animations.pending_public_close(receipt.pending);
    });
    if (m_snapshots.size() >= 16) { return; }

    // Stock makeSnapshotFB renders the monitor presentation (not workspace-neutral
    // coordinates) to outFB. As with its close-event observation, keep facts only.
    const auto OBSERVED = m_port.observe(PHLWINDOWREF{window});
    const auto &place = EXPECTED->place;
    if (!OBSERVED || !OBSERVED->mapped || OBSERVED->no_screen_share || OBSERVED->fullscreen ||
        OBSERVED->monitor != m_port.monitor_identity(monitor) || OBSERVED->monitor != place.monitor ||
        OBSERVED->workspace != place.workspace || OBSERVED->pinned != place.pinned ||
        OBSERVED->scale != EXPECTED->scale || !same_box(OBSERVED->screen, place.screen) ||
        !same_box(OBSERVED->body, place.body) || !same_box(OBSERVED->capture, place.capture) ||
        OBSERVED->body.w < 5. || OBSERVED->body.h < 5.) { // renderWindow clamps the surface to 5x5.
        return;
    }
    const auto &capture = OBSERVED->capture;
    const double X = OBSERVED->workspace_offset.x + OBSERVED->floating_offset.x - OBSERVED->screen.x;
    const double Y = OBSERVED->workspace_offset.y + OBSERVED->floating_offset.y - OBSERVED->screen.y;
    const double SCALE = OBSERVED->scale;
    const hyprimate::Box SOURCE{(capture.x + X) * SCALE, (capture.y + Y) * SCALE,
        capture.w * SCALE, capture.h * SCALE};
    const hyprimate::Vec2 EXTENT{monitor->m_transformedSize.x, monitor->m_transformedSize.y};
    if (!same_box(SOURCE, EXPECTED->source_device) || !hyprimate::is_usable(clip_monitor(SOURCE, EXTENT)) ||
        !same_extent(ctx.m_data.outFB->m_size, EXTENT)) {
        return;
    }
    m_snapshots.push_back({.pending = *PENDING, .producer = WP<Render::IFramebuffer>{ctx.m_data.outFB},
        .monitor = monitor, .screen = OBSERVED->screen, .extent = EXTENT,
        .transform = static_cast<std::uint32_t>(monitor->m_transform)});
}

void CAnimationCloseRenderer::on_fadeout_created(
    const PHLWINDOW &window, const SP<Desktop::CWindowFadeout> &created,
    hyprimate::animation::Handle pending) {
    using namespace hyprimate::animation;
    if (!window || pending_for(window) != pending) {
        return;
    }
    const auto CALLBACK = callback_ticket();
    const auto MODEL = m_animations.callback_ticket();
    const auto WINDOW_ID = m_port.identity(PHLWINDOWREF{window});
    prune_records();
    if (!CALLBACK.valid() || !MODEL.valid() || pending_for(window) != pending) {
        return;
    }
    const auto FADEOUT = created ? m_port.register_fadeout(created) : 0;
    auto facts =
        FADEOUT != 0 ? m_port.fadeout_facts(FADEOUT).value_or(FadeoutFacts{}) : FadeoutFacts{};
    facts.now = Clock::now();
    const auto IMAGE = created ? created->framebuffer(Render::eSceneMode::MONITOR) : nullptr;
    if (const auto EXPECTED = m_animations.pending_public_close(pending)) {
        const auto FOUND = std::ranges::find(m_snapshots, pending, &Snapshot::pending);
        const auto SNAPSHOT = FOUND == m_snapshots.end() ? std::optional<Snapshot>{} : std::optional{*FOUND};
        if (FOUND != m_snapshots.end()) { m_snapshots.erase(FOUND); }
        const auto MONITOR = created ? created->monitor().lock() : PHLMONITOR{};
        if (!SNAPSHOT || !MONITOR || SNAPSHOT->monitor.lock() != MONITOR ||
            !same_box(SNAPSHOT->screen, monitor_box(MONITOR)) ||
            SNAPSHOT->transform != static_cast<std::uint32_t>(MONITOR->m_transform) ||
            !same_extent(MONITOR->m_transformedSize, SNAPSHOT->extent) ||
            SNAPSHOT->producer.lock() != IMAGE || !usable_monitor_image(IMAGE, SNAPSHOT->extent) ||
            !CConstructionRenderer::supports_snapshot(*IMAGE) ||
            !m_port.session_unlocked() || facts.no_screen_share || facts.workspace != EXPECTED->place.workspace ||
            created->source().type != Desktop::eFadeoutSource::WINDOW ||
            !supported_close_effects(created->effects())) {
            m_animations.interrupt_public_close(pending);
            return;
        }
    }
    // Publish before binding can suppress native drawing or call back into this renderer.
    // No vector reference or iterator survives bind_fadeout; nested creation may grow it.
    const bool INSERTED = created && FADEOUT != 0 &&
                          std::ranges::none_of(m_records,
                              [&](const Record &record) { return record.fadeout == FADEOUT; });
    if (INSERTED) {
        const SP<Desktop::IFadeout> BASE = created;
        const auto WORKSPACE = created->workspace();
        m_records.push_back(Record{.fadeout = FADEOUT,
            .monitor = created->monitor(),
            .native = WP<Desktop::IFadeout>{BASE},
            .workspace = WORKSPACE,
            .workspace_bound = static_cast<bool>(WORKSPACE.lock()),
            .plane = Render::sceneFadeoutPlane(
                Render::eSceneMode::MONITOR, created->plane(), created->source().pinned),
            .z_index = created->zIndex(),
            .no_screen_share = created->source().noScreenShare,
            .blur = created->effects().preBlur,
            .binding_renderer = CALLBACK,
            .binding_model = MODEL});
    }
    const auto FINISH_BINDING = [&] {
        if (!INSERTED || !CALLBACK.valid() || !MODEL.valid()) {
            return;
        }
        const auto FOUND = std::ranges::find(m_records, FADEOUT, &Record::fadeout);
        if (FOUND == m_records.end()) {
            return;
        }
        FOUND->binding_renderer = {};
        FOUND->binding_model = {};
        // A failed association must not make this plane eligible or leak its provisional record.
        if (!m_animations.dying_view_for(FADEOUT) && !m_animations.suppresses(FADEOUT)) {
            m_records.erase(FOUND);
        }
    };
    try {
        m_animations.bind_fadeout(WINDOW_ID, facts, IMAGE);
    } catch (...) {
        FINISH_BINDING();
        throw;
    }
    FINISH_BINDING();
}
