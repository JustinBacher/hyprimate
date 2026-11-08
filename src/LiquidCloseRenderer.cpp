#include "LiquidCloseRenderer.hpp"

#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/desktop/state/FadingOutState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <ranges>
#include <vector>

namespace {

using liquid_glass::Transition;
using liquid_glass::Vec2;

// Symbols are selected by these exact demangled signatures, verified against the installed headers.
constexpr const char* CREATE_SIGNATURE =
    "Desktop::CWindowFadeout::create(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>, "
    "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>, float, Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>)";
constexpr const char* RENDER_SIGNATURE =
    "Render::IHyprRenderer::renderFadeouts(Render::CRenderContext&, Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
    "Desktop::eFadeoutPlane, Hyprutils::Memory::CSharedPointer<Workspace::CHLWorkspace>, Render::eSceneMode)";

// findFunctionsByName substring-matches MANGLED symbol names; the exact demangled signature then selects one.
constexpr const char* CREATE_NAME               = "CWindowFadeout6create";
constexpr const char* RENDER_NAME               = "renderFadeouts";
constexpr double      MAX_GAP_SECONDS           = 0.25;
constexpr double      PENDING_TIMEOUT_SECONDS   = 1.0;
// Logical padding around the deformed extents so the sampled mesh never clips at the snapshot edge.
constexpr double      EXTENTS_PADDING           = 16.0;
constexpr size_t      BATCH_CAPACITY            = 16;

using CreateFn = SP<Desktop::CWindowFadeout> (*)(PHLWINDOW, SP<Render::IFramebuffer>, float, SP<Render::IFramebuffer>);
using RenderFn = void (*)(Render::IHyprRenderer*, Render::CRenderContext&, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE, Render::eSceneMode);

// Returns the address of the single symbol whose demangled signature matches exactly, or nullptr otherwise.
void* exactSymbol(const std::vector<SFunctionMatch>& matches, const char* signature) {
    void* found = nullptr;
    int   count = 0;
    for (const auto& match : matches) {
        if (match.demangled == signature) {
            found = match.address;
            ++count;
        }
    }
    return count == 1 ? found : nullptr;
}

// Maps snapshot-space texels into the output: F(p) = B.pos + p * B.size / T.
CBox projectToOutput(const CBox& extentsDevice, const CBox& renderBox, const Vector2D& snapshotSize) {
    const double kx = renderBox.w / snapshotSize.x;
    const double ky = renderBox.h / snapshotSize.y;
    return CBox{renderBox.x + extentsDevice.x * kx, renderBox.y + extentsDevice.y * ky, extentsDevice.w * kx, extentsDevice.h * ky};
}

// Owned placement: the snapshot at unit scale (device px), with the window's centre riding the native fadeout's
// centre. Dividing out the native box's current scale cancels popout scale whatever it already is at this moment.
CBox ownedRenderBox(const CBox& sourceDevice, const CBox& nativeBox, const Vector2D& snapshotSize) {
    const double kx = nativeBox.w / snapshotSize.x;
    const double ky = nativeBox.h / snapshotSize.y;
    const double mx = sourceDevice.x + sourceDevice.w * 0.5;
    const double my = sourceDevice.y + sourceDevice.h * 0.5;
    const double cx = nativeBox.x + mx * kx;
    const double cy = nativeBox.y + my * ky;
    return CBox{cx - mx, cy - my, snapshotSize.x, snapshotSize.y};
}

// A bound record matches only its captured workspace (an expired one matches nothing); an unbound record matches
// any workspace, as the native fadeout does.
bool recordOnWorkspace(const CLiquidCloseRenderer::SClosing& record, const PHLWORKSPACE& workspace) {
    if (!record.workspaceBound)
        return true;
    const auto captured = record.workspace.lock();
    return captured && captured == workspace;
}

bool sameMonitor(const PHLMONITORREF& ref, const PHLMONITOR& monitor) {
    return ref.lock().get() == monitor.get();
}

CLiquidCloseRenderer* activeRenderer() {
    return CLiquidCloseRenderer::s_active;
}

SP<Desktop::CWindowFadeout> hookCreate(PHLWINDOW window, SP<Render::IFramebuffer> snapshot, float sourceAlpha, SP<Render::IFramebuffer> workspaceSnapshot) {
    auto* original = reinterpret_cast<CreateFn>(activeRenderer() ? activeRenderer()->createOriginal() : nullptr);
    if (!original)
        return nullptr;

    // Never move from hooked by-value parameters: disassembly of the pinned host shows callers keep the
    // argument's control-block pointer in a register and release it after the call, ignoring a moved-from slot.
    auto created = original(window, snapshot, sourceAlpha, workspaceSnapshot);
    if (auto* self = activeRenderer()) {
        try {
            self->onFadeoutCreated(window, created);
        } catch (...) {
            // Association is best effort; the native fadeout is already created and must be returned intact.
        }
    }
    return created;
}

void hookRender(Render::IHyprRenderer* self, Render::CRenderContext& ctx, PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace,
                Render::eSceneMode mode) {
    auto* active = activeRenderer();
    if (active && mode == Render::eSceneMode::MONITOR && active->hasRecordFor(monitor, plane, workspace)) {
        bool submitted = false;
        try {
            submitted = active->renderBatch(ctx, monitor, plane, workspace);
        } catch (...) {
            // The batch may be partially submitted, so the native pass must not run on top of it.
            return;
        }
        if (submitted)
            return;
        // Not handled (batch larger than the preallocated scratch): nothing was submitted, draw natively.
    }

    auto* original = reinterpret_cast<RenderFn>(active ? active->renderOriginal() : nullptr);
    if (original)
        original(self, ctx, monitor, plane, workspace, mode);
}

} // namespace

CLiquidCloseRenderer* CLiquidCloseRenderer::s_active = nullptr;

CLiquidCloseRenderer::CLiquidCloseRenderer(HANDLE handle, SP<CSolidFill> solid) : m_handle(handle), m_solid(std::move(solid)) {}

CLiquidCloseRenderer::~CLiquidCloseRenderer() {
    shutdown();
}

bool CLiquidCloseRenderer::install(std::string& error) {
    shutdown();

    void* createAddress = exactSymbol(HyprlandAPI::findFunctionsByName(m_handle, CREATE_NAME), CREATE_SIGNATURE);
    void* renderAddress = exactSymbol(HyprlandAPI::findFunctionsByName(m_handle, RENDER_NAME), RENDER_SIGNATURE);
    if (!createAddress || !renderAddress) {
        error = "close symbols not found by exact signature";
        return false;
    }

    m_batch.reserve(BATCH_CAPACITY);
    s_active     = this;
    m_createHook = HyprlandAPI::createFunctionHook(m_handle, createAddress, reinterpret_cast<const void*>(&hookCreate));
    m_renderHook = HyprlandAPI::createFunctionHook(m_handle, renderAddress, reinterpret_cast<const void*>(&hookRender));
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

void CLiquidCloseRenderer::shutdown() {
    // Hooks come down first so no new trampoline call can reach this object.
    // removeFunctionHook unhooks (if active) and releases the host-owned hook.
    if (m_renderHook)
        HyprlandAPI::removeFunctionHook(m_handle, m_renderHook);
    if (m_createHook)
        HyprlandAPI::removeFunctionHook(m_handle, m_createHook);
    m_renderHook = nullptr;
    m_createHook = nullptr;
    s_active = nullptr;
    clear();
}

void CLiquidCloseRenderer::clear() {
    // Owned closes may still have a last frame on screen. Damage each such monitor once (damageMonitor also schedules
    // a frame) before the records go, so that frame is cleared. Native fadeouts continue on their own.
    if (g_pHyprRenderer) {
        std::vector<PHLMONITORREF> damaged;
        for (const auto& record : m_records) {
            const auto monitor = record->monitor.lock();
            if (!record->owned || !monitor)
                continue;
            if (std::ranges::find_if(damaged, [&monitor](const PHLMONITORREF& ref) { return ref.lock().get() == monitor.get(); }) != damaged.end())
                continue;
            damaged.push_back(PHLMONITORREF{monitor});
            g_pHyprRenderer->damageMonitor(monitor);
        }
    }
    m_pending.clear();
    m_records.clear();
}

void CLiquidCloseRenderer::stage(const PHLWINDOW& window, const SLiquidPose& pose, double strength, std::optional<liquid_glass::EdgeFrame> closeFrame,
                                 double drive) {
    if (!window || !std::isfinite(strength) || strength <= 0.0)
        return;

    const auto monitor = window->m_monitor.lock();
    if (!monitor)
        return;

    const double scale = monitor->m_scale;
    if (!std::isfinite(scale) || scale <= 0.0)
        return;

    const CBox capture = window->getFullWindowBoundingBox();
    if (capture.empty())
        return;

    // Window position inside the snapshot: the snapshot's presentation offsets, then monitor-local device px.
    const auto presentation = Render::resolveWindowSnapshotPresentation(window->presentation().renderPresentation(Render::eSceneMode::MONITOR));
    const CBox source = capture.copy().translate(presentation.workspaceOffset + presentation.floatingOffset).translate(-monitor->m_position).scale(scale);
    if (source.empty())
        return;

    std::erase_if(m_pending, [&window](const SPending& p) { return p.window.get() == window.get(); });
    m_pending.push_back(SPending{
        .window         = PHLWINDOWREF{window},
        .mesh           = pose.mesh,
        .transition     = pose.transition,
        .compensation   = pose.compensation,
        .sourceDevice   = source,
        .scale          = scale,
        .captureLogical = Vec2{capture.w, capture.h},
        .strength       = strength,
        .drive          = drive,
        .closeFrame     = closeFrame,
        .staged         = clock::now(),
    });
}

void CLiquidCloseRenderer::onFadeoutCreated(const PHLWINDOW& window, const SP<Desktop::CWindowFadeout>& created) {
    const auto it = std::ranges::find_if(m_pending, [&window](const SPending& p) { return p.window.get() == window.get(); });
    if (it == m_pending.end())
        return;

    SPending pending = std::move(*it);
    m_pending.erase(it);

    if (!created || window.get() != pending.window.get())
        return;
    if (std::chrono::duration<double>(clock::now() - pending.staged).count() > PENDING_TIMEOUT_SECONDS)
        return;

    const auto monitor  = created->monitor().lock();
    const auto snapshot = created->framebuffer(Render::eSceneMode::MONITOR);
    if (!monitor || !snapshot || !snapshot->getTexture() || monitor->m_scale != pending.scale)
        return;
    // Retained records and their snapshots are bounded by the batch scratch they must fit in; beyond that the native
    // fadeout runs unmodified.
    if (m_records.size() >= BATCH_CAPACITY)
        return;

    auto record = std::make_unique<SClosing>();
    const SP<Desktop::IFadeout> base = created;
    record->fadeout        = WP<Desktop::IFadeout>{base};
    record->snapshot       = snapshot;
    record->monitor        = PHLMONITORREF{monitor};
    record->workspace      = created->workspace();
    record->workspaceBound = bool(record->workspace.lock());
    record->plane          = Render::sceneFadeoutPlane(Render::eSceneMode::MONITOR, created->plane(), created->source().pinned);
    record->zIndex         = created->zIndex();
    record->noScreenShare  = created->source().noScreenShare;
    record->mesh           = pending.mesh;
    record->compensation   = pending.compensation;
    record->sourceAlpha    = created->alpha();
    record->sourceDevice   = pending.sourceDevice;
    record->scale          = pending.scale;
    record->captureLogical = pending.captureLogical;
    record->snapshotSize   = snapshot->getTexture()->m_size;
    record->lastAdvance    = clock::now();

    // Owned only for a Liquid close without texture blur: the edge transition begins Close from the copied pose, so
    // an in-progress opening is kept as the start of the close.
    const bool liquid      = record->mesh.mode() == liquid_glass::MotionMode::Liquid;
    const bool textureBlur = created->effects().textureBlur.enabled;
    if (liquid && !textureBlur) {
        record->transition = pending.transition;
        // Continuing an opening keeps its edge; otherwise the staged frame (invalid when none, which leaves it idle).
        record->transition.begin(Transition::Close, pending.closeFrame.value_or(record->transition.frame()), record->mesh.params(), pending.strength);
    }
    record->strength = pending.strength;
    record->drive    = pending.drive;
    record->owned    = record->transition.active() && record->sourceAlpha > 0.F;
    if (record->owned)
        record->renderBox = ownedRenderBox(record->sourceDevice, created->renderBox(), record->snapshotSize);

    fillVertices(*record);
    m_records.push_back(std::move(record));
}

void CLiquidCloseRenderer::fillVertices(SClosing& record) const {
    SLiquidPose pose{record.mesh, record.transition, record.compensation};
    const CBox  logical = record.sourceDevice.copy().scale(1.0 / record.scale);
    const Vec2  c       = record.compensation;
    pose.displacementGain = record.transition.displacementGain(record.mesh, {logical.w, logical.h}, c);
    CBox              extents = liquidRenderExtents(pose, logical);
    extents.expand(EXTENTS_PADDING);
    record.extentsDevice = extents.copy().scale(record.scale);

    fillLiquidRenderMesh(pose, record.sourceDevice, record.scale, record.extentsDevice, Vec2{0.0, 0.0},
                         Vec2{record.snapshotSize.x, record.snapshotSize.y}, record.vertices);

    const auto liquid = liquidMeniscusExtents(pose, logical);
    record.hasLiquid  = liquid.has_value();
    if (liquid) {
        record.liquidDevice = liquid->copy().scale(record.scale);
        fillLiquidMeniscusMesh(pose, record.sourceDevice, record.scale, record.liquidDevice, record.liquidVertices);
    }
}

bool CLiquidCloseRenderer::tick(clock::time_point now, std::vector<PHLMONITORREF>& activeMonitors) {
    activeMonitors.clear();

    std::erase_if(m_pending, [now](const SPending& p) { return std::chrono::duration<double>(now - p.staged).count() > PENDING_TIMEOUT_SECONDS; });

    std::erase_if(m_records, [&](const std::unique_ptr<SClosing>& record) {
        const auto fadeout     = record->fadeout.lock();
        const bool nativeAlive = fadeout && !fadeout->done();
        const auto monitor     = record->monitor.lock();

        // An owned record that is erased still has a last drawn frame on screen: damage its monitor so it clears.
        const auto prune = [&]() {
            if (record->owned && monitor)
                activeMonitors.push_back(PHLMONITORREF{monitor});
            return true;
        };

        if (!monitor || monitor->m_scale != record->scale)
            return prune();

        if (record->owned) {
            // A suppressed close keeps its record (so the native fade stays suppressed) until the native fadeout ends.
            if (record->transition.closed())
                return nativeAlive ? false : prune();

            if (nativeAlive)
                record->renderBox = ownedRenderBox(record->sourceDevice, fadeout->renderBox(), record->snapshotSize);

            const double elapsed = std::clamp(std::chrono::duration<double>(now - record->lastAdvance).count(), 0.0, MAX_GAP_SECONDS);
            advanceLiquidPose(record->mesh, record->transition, elapsed, record->captureLogical, record->strength, record->drive);
            record->lastAdvance = now;
            fillVertices(*record);
            activeMonitors.push_back(PHLMONITORREF{monitor});
            return false;
        }

        if (!nativeAlive)
            return true;

        // A non-owned close that has settled needs no more simulation or vertex refills; the native fadeout keeps
        // scheduling its own alpha frames and the settled mesh is still substituted.
        if (record->mesh.settled())
            return false;

        const double elapsed = std::clamp(std::chrono::duration<double>(now - record->lastAdvance).count(), 0.0, MAX_GAP_SECONDS);
        record->mesh.advance(elapsed, record->captureLogical);
        record->lastAdvance = now;
        fillVertices(*record);
        activeMonitors.push_back(PHLMONITORREF{monitor});
        return false;
    });

    return !activeMonitors.empty();
}

CLiquidCloseRenderer::SClosing* CLiquidCloseRenderer::recordFor(const SP<Desktop::IFadeout>& fadeout) const {
    for (const auto& record : m_records) {
        if (record->fadeout.lock().get() == fadeout.get())
            return record.get();
    }
    return nullptr;
}

bool CLiquidCloseRenderer::hasRecordFor(const PHLMONITOR& monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE& workspace) const {
    for (const auto& record : m_records) {
        if (!sameMonitor(record->monitor, monitor) || record->plane != plane)
            continue;
        if (!recordOnWorkspace(*record, workspace))
            continue;
        return true;
    }
    return false;
}

bool CLiquidCloseRenderer::renderBatch(Render::CRenderContext& ctx, const PHLMONITOR& monitor, Desktop::eFadeoutPlane plane, const PHLWORKSPACE& workspace) {
    if (!monitor)
        return true;

    // Mirrors IHyprRenderer::renderFadeouts (MONITOR mode). m_batch keeps the capacity reserved at install and is
    // never grown here: a larger batch returns false so the caller delegates it to the native renderer.
    auto& batch = m_batch;
    batch.clear();

    // Native pass: live fadeouts in the native selection. An owned close whose native fadeout has finished is left
    // to the orphan pass below, so each owned close is drawn exactly once.
    for (const auto& fadeout : Desktop::fadingOutState()->fadeouts()) {
        if (!fadeout || fadeout->monitor().lock() != monitor)
            continue;

        const auto source = fadeout->source();
        if (!ctx.shouldRenderContent(source.noScreenShare))
            continue;
        if (Render::sceneFadeoutPlane(Render::eSceneMode::MONITOR, fadeout->plane(), source.pinned) != plane)
            continue;
        const auto fadeoutWorkspace = fadeout->workspace().lock();
        if (fadeoutWorkspace && fadeoutWorkspace != workspace)
            continue;

        SClosing* record = recordFor(fadeout);
        if (record && !recordOnWorkspace(*record, workspace))
            record = nullptr;
        if (record && record->owned && fadeout->done())
            continue;

        if (batch.size() == batch.capacity()) {
            batch.clear();
            return false;
        }
        batch.push_back(SBatchItem{fadeout, record, fadeout->zIndex()});
    }

    // Orphan pass: owned closes whose native fadeout finished or vanished keep drawing from their transition.
    for (const auto& record : m_records) {
        if (!record->owned || record->transition.closed() || !sameMonitor(record->monitor, monitor))
            continue;
        if (record->plane != plane || !recordOnWorkspace(*record, workspace) || !ctx.shouldRenderContent(record->noScreenShare))
            continue;
        const auto fadeout = record->fadeout.lock();
        if (fadeout && !fadeout->done())
            continue;

        if (batch.size() == batch.capacity()) {
            batch.clear();
            return false;
        }
        batch.push_back(SBatchItem{nullptr, record.get(), record->zIndex});
    }

    std::ranges::sort(batch, {}, &SBatchItem::zIndex);

    const auto drawBackdrop = [&](const auto& effects) {
        if (effects.dimAroundAlpha > 0.F) {
            CRectPassElement::SRectData data;
            data.box   = {0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
            data.color = CHyprColor(0, 0, 0, effects.dimAroundAlpha);
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CRectPassElement>(data));
        }

        if (effects.preBlur) {
            CRectPassElement::SRectData data;
            data.box           = effects.preBlur->box;
            data.color         = CHyprColor(0, 0, 0, 0);
            data.blur          = true;
            data.blurA         = effects.preBlur->alpha;
            data.round         = effects.preBlur->round;
            data.roundingPower = effects.preBlur->roundingPower;
            data.xray          = effects.preBlur->xray;
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CRectPassElement>(data));
        }
    };

    CRegion fakeDamage{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
    for (const auto& item : batch) {
        SClosing* record = item.record;

        if (record && record->owned) {
            // A closed owned close draws nothing and suppresses its native fade, dim and blur included.
            if (record->transition.closed())
                continue;
            const float alpha = record->sourceAlpha;
            if (alpha <= 0.F)
                continue;
            const auto tex = record->snapshot ? record->snapshot->getTexture() : nullptr;
            if (!tex)
                continue;

            // Native backdrop effects apply only while the native fadeout is still live.
            if (item.fadeout)
                drawBackdrop(item.fadeout->effects());

            CLiquidClosePassElement::SData data;
            data.tex       = tex;
            data.box       = projectToOutput(record->extentsDevice, record->renderBox, record->snapshotSize);
            data.vertices  = &record->vertices;
            data.alpha     = alpha;
            data.scale     = record->scale;
            // Owned closes get a fresh pass element each frame; damage is the monitor clipped to the screen side of
            // the edge, the same visible half-plane the window transformer uses, not the shared full-monitor copy.
            CBox clip{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
            const CBox restLogical = CBox{record->renderBox.x + record->sourceDevice.x, record->renderBox.y + record->sourceDevice.y,
                                          record->sourceDevice.w, record->sourceDevice.h}
                                         .scale(1.0 / record->scale);
            const SLiquidPose visiblePose{record->mesh, record->transition, record->compensation};
            if (const auto visible = liquidVisibleBox(visiblePose, restLogical))
                clip = clip.intersection(visible->copy().scale(record->scale));
            data.damage = CRegion{clip};
            data.solid     = m_solid ? m_solid->texture() : nullptr;
            data.liquid    = record->hasLiquid && data.solid ? &record->liquidVertices : nullptr;
            data.liquidBox = projectToOutput(record->liquidDevice, record->renderBox, record->snapshotSize);
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CLiquidClosePassElement>(std::move(data)));
            continue;
        }

        // Non-owned: a native fadeout (the orphan pass only yields owned records, so item.fadeout is non-null here).
        const auto& fadeout = item.fadeout;
        if (!fadeout)
            continue;
        const auto FB = fadeout->framebuffer(Render::eSceneMode::MONITOR);
        if (!FB || !FB->getTexture())
            continue;

        const auto effects = fadeout->effects();
        drawBackdrop(effects);

        const auto tex = FB->getTexture();
        // Texture blur rides on a stock element; the mesh path only replaces the plain texture draw.
        if (record && sameMonitor(record->monitor, monitor) && !effects.textureBlur.enabled && tex->m_size.x == record->snapshotSize.x
            && tex->m_size.y == record->snapshotSize.y) {
            CLiquidClosePassElement::SData data;
            data.tex      = tex;
            data.box      = projectToOutput(record->extentsDevice, fadeout->renderBox(), record->snapshotSize);
            data.vertices = &record->vertices;
            data.alpha    = fadeout->alpha();
            data.scale    = record->scale;
            data.damage   = fakeDamage;
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CLiquidClosePassElement>(std::move(data)));
            continue;
        }

        CTexPassElement::SRenderData data;
        data.tex                   = tex;
        data.box                   = fadeout->renderBox();
        data.a                     = fadeout->alpha();
        data.damage                = fakeDamage;
        data.blur                  = effects.textureBlur.enabled;
        data.blurA                 = effects.textureBlur.alpha;
        data.forceBlurBlend        = effects.textureBlur.forceBlend;
        data.blurShapeInvalid      = true;
        data.ignoreAlpha           = effects.textureBlur.ignoreAlpha;
        data.blockBlurOptimization = effects.textureBlur.blockBlurOptimization;

        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(std::move(data)));
    }
    batch.clear();
    return true;
}

CLiquidClosePassElement::CLiquidClosePassElement(SData data) : m_data(std::move(data)) {}

std::vector<UP<IPassElement>> CLiquidClosePassElement::draw(Render::CRenderContext& ctx) {
    if (!m_data.tex || !m_data.vertices || m_data.vertices->empty())
        return {};

    Render::GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage        = &m_data.damage;
    textureData.a             = m_data.alpha;
    textureData.allowCustomUV = true;
    // Edge liquid first, so the window mesh draws over the meniscus where they meet.
    if (m_data.liquid && m_data.solid)
        Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, m_data.solid, m_data.liquidBox, *m_data.liquid, textureData);
    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, m_data.tex, m_data.box, *m_data.vertices, textureData);
    return {};
}

bool CLiquidClosePassElement::needsLiveBlur(Render::CRenderContext&) {
    return false;
}

bool CLiquidClosePassElement::needsPrecomputeBlur(Render::CRenderContext&) {
    return false;
}

const char* CLiquidClosePassElement::passName() {
    return "liquid-glass-close";
}

ePassElementType CLiquidClosePassElement::type() {
    return EK_CUSTOM;
}

std::optional<CBox> CLiquidClosePassElement::boundingBox(Render::CRenderContext&) {
    // Same convention as CTexPassElement: monitor-local logical coordinates. The edge liquid widens the box.
    CBox box = m_data.box;
    if (m_data.liquid && m_data.solid) {
        const double x0 = std::min(box.x, m_data.liquidBox.x);
        const double y0 = std::min(box.y, m_data.liquidBox.y);
        const double x1 = std::max(box.x + box.w, m_data.liquidBox.x + m_data.liquidBox.w);
        const double y1 = std::max(box.y + box.h, m_data.liquidBox.y + m_data.liquidBox.h);
        box             = CBox{x0, y0, x1 - x0, y1 - y0};
    }
    return box.copy().scale(1.0 / m_data.scale).round();
}

CRegion CLiquidClosePassElement::opaqueRegion(Render::CRenderContext&) {
    return {};
}
