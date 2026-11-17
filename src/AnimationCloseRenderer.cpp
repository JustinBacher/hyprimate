#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/desktop/state/FadingOutState.hpp>
#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "AnimationCloseRenderer.hpp"
#include "AnimationTransformer.hpp"
#include "NativeFadeoutPass.hpp"
#include "animations/bubble/BubbleCloseStyle.hpp"
#include "animations/bubble/BubbleCloseStyleData.hpp"
#include "animations/bubble/BubbleLiveState.hpp"
#include "animations/cloth/ClothCloseStyle.hpp"
#include "animations/cloth/ClothCloseStyleData.hpp"
#include "animations/liquid/LiquidCloseStyle.hpp"
#include "animations/liquid/LiquidCloseStyleData.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/paper/PaperCloseStyle.hpp"
#include "animations/paper/PaperCloseStyleData.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"
#include "animations/shared/PaperRenderer.hpp"
#include "animations/swarm/SwarmCloseStyle.hpp"
#include "animations/swarm/SwarmCloseStyleData.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

namespace {

    using hyprimate::STALL_SECONDS;
    using hyprimate::Vec2;

    // Symbols are selected by these exact demangled signatures, verified against the installed
    // headers.
    constexpr const char *CREATE_SIGNATURE =
        "Desktop::CWindowFadeout::create(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>,"
        " "
        "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>, float, "
        "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>)";
    constexpr const char *RENDER_SIGNATURE =
        "Render::IHyprRenderer::renderFadeouts(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
        "Desktop::eFadeoutPlane, Hyprutils::Memory::CSharedPointer<Workspace::CHLWorkspace>, "
        "Render::eSceneMode)";

    // findFunctionsByName substring-matches MANGLED symbol names; the exact demangled signature
    // then selects one.
    constexpr const char *CREATE_NAME = "CWindowFadeout6create";
    constexpr const char *RENDER_NAME = "renderFadeouts";
    constexpr double PENDING_TIMEOUT_SECONDS = 1.0;
    constexpr size_t BATCH_CAPACITY = 16;

    using CreateFn = SP<Desktop::CWindowFadeout> (*)(
        PHLWINDOW, SP<Render::IFramebuffer>, float, SP<Render::IFramebuffer>);
    using RenderFn = void (*)(Render::IHyprRenderer *,
        Render::CRenderContext &,
        PHLMONITOR,
        Desktop::eFadeoutPlane,
        PHLWORKSPACE,
        Render::eSceneMode);

    // Returns the address of the single symbol whose demangled signature matches exactly, or
    // nullptr otherwise.
    void *exactSymbol(const std::vector<SFunctionMatch> &matches, const char *signature) {
        void *found = nullptr;
        int count = 0;
        for (const auto &match : matches) {
            if (match.demangled == signature) {
                found = match.address;
                ++count;
            }
        }
        return count == 1 ? found : nullptr;
    }

    // Owned placement in CBox terms; the arithmetic lives in hyprimate::ownedRenderBox.
    CBox ownedRenderBox(
        const CBox &sourceDevice, const CBox &nativeBox, const Vector2D &snapshotSize) {
        return toCBox(hyprimate::ownedRenderBox(
            toBox(sourceDevice), toBox(nativeBox), Vec2{.x = snapshotSize.x, .y = snapshotSize.y}));
    }

    // Exact box equality: a box that did not move is not damaged again.
    bool sameBox(const CBox &a, const CBox &b) {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }

    // Adds `box` to `damage` unless it is empty or already listed.
    void appendDamage(std::vector<CBox> &damage, const CBox &box) {
        if (box.empty() || std::ranges::any_of(
                               damage, [&box](const CBox &other) { return sameBox(other, box); })) {
            return;
        }
        damage.push_back(box);
    }

    // Outward rounding keeps every pixel in the style's footprint inside the damaged box.
    CBox drawnBoxOf(const CAnimationCloseRenderer::SClosing &record, const PHLMONITOR &monitor) {
        const CBox device = std::visit(
            [&](const auto &style) { return close_style::footprint(style, record); }, record.style);

        const double scale = record.scale;
        const double x0 = std::floor((device.x / scale) + monitor->m_position.x);
        const double y0 = std::floor((device.y / scale) + monitor->m_position.y);
        const double x1 = std::ceil(((device.x + device.w) / scale) + monitor->m_position.x);
        const double y1 = std::ceil(((device.y + device.h) / scale) + monitor->m_position.y);
        return CBox{x0, y0, x1 - x0, y1 - y0};
    }

    // An owned record stops drawing: its last drawn box is damaged once so those pixels clear.
    void releaseDrawn(CAnimationCloseRenderer::SClosing &record, std::vector<CBox> &damage) {
        if (record.owned) {
            appendDamage(damage, record.drawnBox);
        }
        record.drawnBox = CBox{};
    }

    // A bound record matches only its captured workspace (an expired one matches nothing); an
    // unbound record matches any workspace, as the native fadeout does.
    bool recordOnWorkspace(
        const CAnimationCloseRenderer::SClosing &record, const PHLWORKSPACE &workspace) {
        if (!record.workspaceBound) {
            return true;
        }
        const auto captured = record.workspace.lock();
        return captured && captured == workspace;
    }

    bool sameMonitor(const PHLMONITORREF &ref, const PHLMONITOR &monitor) {
        return ref.lock().get() == monitor.get();
    }

    CAnimationCloseRenderer *activeRenderer() {
        return CAnimationCloseRenderer::s_active;
    }

    SP<Desktop::CWindowFadeout> hookCreate(PHLWINDOW window,
        SP<Render::IFramebuffer> snapshot,
        float sourceAlpha,
        SP<Render::IFramebuffer> workspaceSnapshot) {
        auto *original = reinterpret_cast<CreateFn>(
            activeRenderer() ? activeRenderer()->createOriginal() : nullptr);
        if (!original) {
            return nullptr;
        }

        // Never move from hooked by-value parameters: disassembly of the pinned host shows callers
        // keep the argument's control-block pointer in a register and release it after the call,
        // ignoring a moved-from slot.
        auto created = original(window, snapshot, sourceAlpha, workspaceSnapshot);
        if (auto *self = activeRenderer()) {
            try {
                self->onFadeoutCreated(window, created);
            } catch (...) {
                // Association is best effort; the native fadeout is already created and must be
                // returned intact.
            }
        }
        return created;
    }

    void hookRender(Render::IHyprRenderer *self,
        Render::CRenderContext &ctx,
        PHLMONITOR monitor,
        Desktop::eFadeoutPlane plane,
        PHLWORKSPACE workspace,
        Render::eSceneMode mode) {
        auto *active = activeRenderer();
        if (active && mode == Render::eSceneMode::MONITOR &&
            active->hasRecordFor(monitor, plane, workspace)) {
            bool submitted = false;
            try {
                submitted = active->renderBatch(ctx, monitor, plane, workspace);
            } catch (...) {
                // The batch may be partially submitted, so the native pass must not run on top of
                // it.
                return;
            }
            if (submitted) {
                return;
            }
            // Not handled (batch larger than the preallocated scratch): nothing was submitted, draw
            // natively.
        }

        auto *original = reinterpret_cast<RenderFn>(active ? active->renderOriginal() : nullptr);
        if (original) {
            original(self, ctx, monitor, plane, workspace, mode);
        }
    }

    bool running(const SStyleState &style) {
        return std::visit([](const auto &value) { return close_style::running(value); }, style);
    }
    bool finished(const SStyleState &style) {
        return std::visit([](const auto &value) { return close_style::finished(value); }, style);
    }

}  // namespace

bool closeAnimationEnabled(const hyprimate::config::SConfig &config) {
    using hyprimate::config::Style;
    switch (config.style) {
        case Style::Liquid:
            return close_style::closeEnabled(SLiquidCloseData{}, config);
        case Style::Paper:
            return close_style::closeEnabled(SPaperCloseData{}, config);
        case Style::Cloth:
            return close_style::closeEnabled(SClothCloseData{}, config);
        case Style::Swarm:
            return close_style::closeEnabled(SSwarmCloseData{}, config);
        case Style::Bubble:
            return close_style::closeEnabled(SBubbleCloseData{}, config);
    }
    return false;
}

CAnimationCloseRenderer *CAnimationCloseRenderer::s_active = nullptr;

CAnimationCloseRenderer::CAnimationCloseRenderer(
    HANDLE handle, SP<CSolidFill> solid, SP<CPaperShade> shade, SP<CSwarmSprites> sprites)
    : m_handle(handle),
      m_solid(std::move(solid)),
      m_shade(std::move(shade)),
      m_sprites(std::move(sprites)) {
}

CAnimationCloseRenderer::~CAnimationCloseRenderer() {
    shutdown();
}

bool CAnimationCloseRenderer::install(std::string &error) {
    shutdown();

    const void *createAddress =
        exactSymbol(HyprlandAPI::findFunctionsByName(m_handle, CREATE_NAME), CREATE_SIGNATURE);
    const void *renderAddress =
        exactSymbol(HyprlandAPI::findFunctionsByName(m_handle, RENDER_NAME), RENDER_SIGNATURE);
    if (!createAddress || !renderAddress) {
        error = "close symbols not found by exact signature";
        return false;
    }

    m_batch.reserve(BATCH_CAPACITY);
    s_active = this;
    m_createHook = HyprlandAPI::createFunctionHook(
        m_handle, createAddress, reinterpret_cast<const void *>(&hookCreate));
    m_renderHook = HyprlandAPI::createFunctionHook(
        m_handle, renderAddress, reinterpret_cast<const void *>(&hookRender));
    if (!m_createHook || !m_renderHook || !m_createHook->hook()) {
        shutdown();
        error = "failed to hook CWindowFadeout::create";
        return false;
    }
    if (!m_renderHook->hook()) {
        shutdown();
        error = "failed to hook IHyprRenderer::renderFadeouts";
        return false;
    }

    return true;
}

void CAnimationCloseRenderer::shutdown() {
    // Hooks come down first so no new trampoline call can reach this object.
    // removeFunctionHook unhooks (if active) and releases the host-owned hook.
    if (m_renderHook) {
        HyprlandAPI::removeFunctionHook(m_handle, m_renderHook);
    }
    if (m_createHook) {
        HyprlandAPI::removeFunctionHook(m_handle, m_createHook);
    }
    m_renderHook = nullptr;
    m_createHook = nullptr;
    s_active = nullptr;
    clear();
}

void CAnimationCloseRenderer::clear() {
    // Owned closes may still have a last frame on screen. Damage their last drawn boxes so those
    // pixels clear before the records go. Called from teardown where no tick damage loop runs, so
    // this damages directly. Native fadeouts continue on their own.
    if (g_pHyprRenderer) {
        for (const auto &record : m_records) {
            if (record->owned && !record->drawnBox.empty()) {
                g_pHyprRenderer->damageBox(record->drawnBox);
            }
        }
    }
    m_pending.clear();
    m_records.clear();
}

void CAnimationCloseRenderer::stage(const PHLWINDOW &window,
    SStyleState &state,
    bool stateActive,
    const hyprimate::config::SConfig &config,
    hyprimate::MaterialParams material,
    const std::function<hyprimate::EdgeInput()> &edgeInput,
    const std::function<std::span<const hyprimate::Box>()> &obstacles,
    const std::function<std::uint64_t()> &nextSeed,
    const std::function<hyprimate::EdgeSet()> &swarmSides) {
    auto pending = pendingFor(window);
    const bool canStage = pending.has_value();
    if (!pending) {
        pending.emplace();
    }
    const bool staged = std::visit(
        [&](auto &style) {
            return close_style::stage(style,
                *pending,
                window,
                stateActive,
                config,
                material,
                edgeInput,
                obstacles,
                nextSeed,
                swarmSides);
        },
        state);
    if (staged && canStage) {
        push(std::move(*pending));
    }
}

std::optional<CAnimationCloseRenderer::SPending> CAnimationCloseRenderer::pendingFor(
    const PHLWINDOW &window) const {
    if (!window) {
        return std::nullopt;
    }

    const auto monitor = window->m_monitor.lock();
    if (!monitor) {
        return std::nullopt;
    }

    const double scale = monitor->m_scale;
    if (!std::isfinite(scale) || scale <= 0.0) {
        return std::nullopt;
    }

    const CBox capture = window->getFullWindowBoundingBox();
    if (capture.empty()) {
        return std::nullopt;
    }

    // Window position inside the snapshot: the snapshot's presentation offsets, then monitor-local
    // device px.
    const auto presentation = Render::resolveWindowSnapshotPresentation(
        window->presentation().renderPresentation(Render::eSceneMode::MONITOR));
    const CBox source = capture.copy()
                            .translate(presentation.workspaceOffset + presentation.floatingOffset)
                            .translate(-monitor->m_position)
                            .scale(scale);
    if (source.empty()) {
        return std::nullopt;
    }

    SPending pending;
    pending.window = PHLWINDOWREF{window};
    pending.sourceDevice = source;
    pending.scale = scale;
    pending.captureLogical = Vec2{.x = capture.w, .y = capture.h};
    pending.staged = clock::now();
    return pending;
}

void CAnimationCloseRenderer::push(SPending pending) {
    std::erase_if(m_pending,
        [&pending](const SPending &p) { return p.window.get() == pending.window.get(); });
    m_pending.push_back(std::move(pending));
}

void CAnimationCloseRenderer::onFadeoutCreated(
    const PHLWINDOW &window, const SP<Desktop::CWindowFadeout> &created) {
    const auto it = std::ranges::find_if(
        m_pending, [&window](const SPending &p) { return p.window.get() == window.get(); });
    if (it == m_pending.end()) {
        return;
    }

    SPending pending = std::move(*it);
    m_pending.erase(it);

    if (!created || window.get() != pending.window.get()) {
        return;
    }
    if (std::chrono::duration<double>(clock::now() - pending.staged).count() >
        PENDING_TIMEOUT_SECONDS) {
        return;
    }

    const auto monitor = created->monitor().lock();
    const auto snapshot = created->framebuffer(Render::eSceneMode::MONITOR);
    if (!monitor || !snapshot || !snapshot->getTexture() || monitor->m_scale != pending.scale) {
        return;
    }
    // Retained records and their snapshots are bounded by the batch scratch they must fit in;
    // beyond that the native fadeout runs unmodified.
    if (m_records.size() >= BATCH_CAPACITY) {
        return;
    }

    if (!std::visit(
            [&](const auto &style) {
                return close_style::valid(style, pending, created->effects().textureBlur.enabled);
            },
            pending.style)) {
        return;
    }

    auto record = std::make_unique<SClosing>();
    const SP<Desktop::IFadeout> base = created;
    record->fadeout = WP<Desktop::IFadeout>{base};
    record->snapshot = snapshot;
    record->monitor = PHLMONITORREF{monitor};
    record->workspace = created->workspace();
    record->workspaceBound = bool(record->workspace.lock());
    record->plane = Render::sceneFadeoutPlane(
        Render::eSceneMode::MONITOR, created->plane(), created->source().pinned);
    record->zIndex = created->zIndex();
    record->noScreenShare = created->source().noScreenShare;
    record->style = std::move(pending.style);
    record->sourceAlpha = created->alpha();
    record->sourceDevice = pending.sourceDevice;
    record->scale = pending.scale;
    record->captureLogical = pending.captureLogical;
    record->snapshotSize = snapshot->getTexture()->m_size;
    record->lastAdvance = clock::now();

    const bool textureBlur = created->effects().textureBlur.enabled;
    std::visit(
        [&](auto &style) { close_style::begin(style, pending, textureBlur); }, record->style);
    record->closeData = std::move(pending.closeData);
    record->owned = running(record->style) && record->sourceAlpha > 0.F;
    if (!record->owned &&
        std::visit(
            [](const auto &style) { return close_style::needsOwned(style); }, record->style)) {
        return;
    }
    if (record->owned) {
        // A soap membrane ruptures where the snapshot was taken, independent of native slide/pop
        // geometry.
        record->renderBox =
            std::holds_alternative<SBubbleStyle>(record->style)
                ? CBox{0.0, 0.0, record->snapshotSize.x, record->snapshotSize.y}
                : ownedRenderBox(record->sourceDevice, created->renderBox(), record->snapshotSize);
    }

    fillVertices(*record);
    m_records.push_back(std::move(record));
}

void CAnimationCloseRenderer::fillVertices(SClosing &record) {
    std::visit([&](auto &style) { close_style::fill(style, record); }, record.style);
}

void CAnimationCloseRenderer::tick(
    clock::time_point now, const PHLMONITOR &rendering, std::vector<CBox> &damage) {
    damage.clear();

    std::erase_if(m_pending, [now](const SPending &p) {
        return std::chrono::duration<double>(now - p.staged).count() > PENDING_TIMEOUT_SECONDS;
    });

    std::erase_if(m_records, [&](const std::unique_ptr<SClosing> &record) {
        const auto fadeout = record->fadeout.lock();
        const bool nativeAlive = fadeout && !fadeout->done();
        const auto monitor = record->monitor.lock();

        if (!monitor || monitor->m_scale != record->scale) {
            releaseDrawn(*record, damage);
            return true;
        }

        if (record->owned) {
            // A suppressed close keeps its record (so the native fade stays suppressed) until the
            // native fadeout ends.
            if (finished(record->style)) {
                releaseDrawn(*record, damage);
                return !nativeAlive;
            }

            if (nativeAlive && !std::holds_alternative<SBubbleStyle>(record->style)) {
                record->renderBox = ownedRenderBox(
                    record->sourceDevice, fadeout->renderBox(), record->snapshotSize);
            }

            bool advanced = false;
            if (monitor == rendering) {
                const double elapsed =
                    std::clamp(std::chrono::duration<double>(now - record->lastAdvance).count(),
                        0.0,
                        STALL_SECONDS);
                std::visit(
                    [&](auto &style) { close_style::advance(style, *record, elapsed, true); },
                    record->style);
                fillVertices(*record);
                record->lastAdvance = now;
                advanced = true;
            }

            // An advancing record damages its box every frame, which also schedules its next frame.
            // A record that did not advance only damages when the native fadeout moved its box.
            const CBox current = drawnBoxOf(*record, monitor);
            if (advanced || !sameBox(current, record->drawnBox)) {
                appendDamage(damage, record->drawnBox);
                appendDamage(damage, current);
            }
            record->drawnBox = current;
            return false;
        }

        // Unowned closes contribute no damage: the native fadeout damages its own box.
        if (!nativeAlive) {
            return true;
        }

        // A non-owned close that has settled needs no more simulation or vertex refills; the native
        // fadeout keeps scheduling its own alpha frames and the settled mesh is still substituted.
        if (std::visit(
                [](const auto &style) { return close_style::settled(style); }, record->style) ||
            monitor != rendering) {
            return false;
        }

        const double elapsed = std::clamp(
            std::chrono::duration<double>(now - record->lastAdvance).count(), 0.0, STALL_SECONDS);
        std::visit([&](auto &style) { close_style::advance(style, *record, elapsed, false); },
            record->style);
        record->lastAdvance = now;
        return false;
    });
}

bool CAnimationCloseRenderer::needsFrame(const PHLMONITOR &monitor) const {
    if (!monitor) return false;
    return std::ranges::any_of(m_records, [&](const auto &record) {
        return record->owned && sameMonitor(record->monitor, monitor) &&
               (!finished(record->style) || !record->drawnBox.empty());
    });
}

CAnimationCloseRenderer::SClosing *CAnimationCloseRenderer::recordFor(
    const SP<Desktop::IFadeout> &fadeout) const {
    for (const auto &record : m_records) {
        if (record->fadeout.lock().get() == fadeout.get()) {
            return record.get();
        }
    }
    return nullptr;
}

bool CAnimationCloseRenderer::hasRecordFor(
    const PHLMONITOR &monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE &workspace) const {
    for (const auto &record : m_records) {
        if (!sameMonitor(record->monitor, monitor) || record->plane != plane) {
            continue;
        }
        if (!recordOnWorkspace(*record, workspace)) {
            continue;
        }
        return true;
    }
    return false;
}

bool CAnimationCloseRenderer::renderBatch(Render::CRenderContext &ctx,
    const PHLMONITOR &monitor,
    Desktop::eFadeoutPlane plane,
    const PHLWORKSPACE &workspace) {
    if (!monitor) {
        return true;
    }

    // Mirrors IHyprRenderer::renderFadeouts (MONITOR mode). m_batch keeps the capacity reserved at
    // install and is never grown here: a larger batch returns false so the caller delegates it to
    // the native renderer.
    auto &batch = m_batch;
    batch.clear();

    // Native pass: live fadeouts in the native selection. An owned close whose native fadeout has
    // finished is left to the orphan pass below, so each owned close is drawn exactly once.
    for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
        if (!fadeout || fadeout->monitor().lock() != monitor) {
            continue;
        }

        const auto source = fadeout->source();
        if (!ctx.shouldRenderContent(source.noScreenShare)) {
            continue;
        }
        if (Render::sceneFadeoutPlane(
                Render::eSceneMode::MONITOR, fadeout->plane(), source.pinned) != plane) {
            continue;
        }
        const auto fadeoutWorkspace = fadeout->workspace().lock();
        if (fadeoutWorkspace && fadeoutWorkspace != workspace) {
            continue;
        }

        SClosing *record = recordFor(fadeout);
        if (record && !recordOnWorkspace(*record, workspace)) {
            record = nullptr;
        }
        if (record && record->owned && fadeout->done()) {
            continue;
        }

        if (batch.size() == batch.capacity()) {
            batch.clear();
            return false;
        }
        batch.push_back(
            SBatchItem{.fadeout = fadeout, .record = record, .zIndex = fadeout->zIndex()});
    }

    // Orphan pass: owned closes whose native fadeout finished or vanished keep drawing from their
    // transition.
    for (const auto &record : m_records) {
        if (!record->owned || finished(record->style) || !sameMonitor(record->monitor, monitor)) {
            continue;
        }
        if (record->plane != plane || !recordOnWorkspace(*record, workspace) ||
            !ctx.shouldRenderContent(record->noScreenShare)) {
            continue;
        }
        const auto fadeout = record->fadeout.lock();
        if (fadeout && !fadeout->done()) {
            continue;
        }

        if (batch.size() == batch.capacity()) {
            batch.clear();
            return false;
        }
        batch.push_back(
            SBatchItem{.fadeout = nullptr, .record = record.get(), .zIndex = record->zIndex});
    }

    std::ranges::sort(batch, {}, &SBatchItem::zIndex);

    CNativeFadeoutPass native{ctx, monitor};
    for (const auto &item : batch) {
        SClosing *record = item.record;

        if (record && record->owned) {
            // A closed owned close draws nothing and suppresses its native fade, dim and blur
            // included.
            if (finished(record->style)) {
                continue;
            }
            const float alpha = record->sourceAlpha;
            if (alpha <= 0.F) {
                continue;
            }
            const auto tex = record->snapshot ? record->snapshot->getTexture() : nullptr;
            if (!tex) {
                continue;
            }

            // Native backdrop effects apply only while the native fadeout is still live.
            if (item.fadeout) {
                native.drawBackdrop(item.fadeout->effects());
            }

            std::visit(
                [&](auto &style) {
                    close_style::render(
                        style, *record, ctx, monitor, tex, m_solid, m_shade, m_sprites);
                },
                record->style);
            continue;
        }

        // Non-owned: a native fadeout (the orphan pass only yields owned records, so item.fadeout
        // is non-null here).
        const auto &fadeout = item.fadeout;
        if (!fadeout) {
            continue;
        }
        const auto FB = fadeout->framebuffer(Render::eSceneMode::MONITOR);
        if (!FB || !FB->getTexture()) {
            continue;
        }

        const auto effects = fadeout->effects();
        native.drawBackdrop(effects);

        const auto tex = FB->getTexture();
        // Texture blur rides on a stock element; the mesh path only replaces the plain texture
        // draw.
        if (record && sameMonitor(record->monitor, monitor) && !effects.textureBlur.enabled &&
            tex->m_size.x == record->snapshotSize.x && tex->m_size.y == record->snapshotSize.y) {
            if (std::visit(
                    [&](auto &style) {
                        return close_style::renderNative(
                            style, *record, ctx, fadeout, tex, native.damage());
                    },
                    record->style))
                continue;
        }

        native.drawTexture(*fadeout, tex, effects);
    }
    batch.clear();
    return true;
}
