#include "workspace/WorkspaceAnimationImpl.hpp"

namespace {
    void track_preview(CWorkspaceAnimation::Impl::MonitorState &entry,
        const SP<Render::IFramebuffer> &image) noexcept {
        if (!hyprimate::perf::enabled() || !image) {
            return;
        }
        try {
            std::erase_if(entry.profile_previews, [](const auto &item) { return item.expired(); });
            entry.profile_previews.emplace_back(image);
        } catch (...) {
            // Losing an optional lower-bound sample must not cancel a capture.
        }
    }

    SP<Render::IFramebuffer> preview_target(const PHLMONITOR &monitor, int longest_side) {
        const hyprimate::gl::ScopedCompositorContext CONTEXT{};
        if (!CONTEXT) {
            return {};
        }
        const auto SIZE = monitor->m_transformedSize;
        const double SCALE = std::min(1.0, longest_side / std::max(SIZE.x, SIZE.y));
        auto image =
            hyprimate::gl::retain_native(g_pHyprRenderer->createFB("hyprimate workspace preview"));
        if (!image) {
            return {};
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::FramebufferAllocationState STATE;
        if (!image->alloc(std::max(1, static_cast<int>(std::lround(SIZE.x * SCALE))),
                std::max(1, static_cast<int>(std::lround(SIZE.y * SCALE))),
                capture_format(monitor))) {
            return {};
        }
        image->setImageDescription(monitor->workBufferImageDescription());
        return image;
    }

    template <class Allowed>
    bool write_thumbnail(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &thumbnail,
        const Allowed &allowed) {
        const hyprimate::gl::ScopedCompositorContext CONTEXT{};
        if (!CONTEXT) {
            return false;
        }
        if (!allowed() || !copy_opaque(source, thumbnail, true, true) || !allowed()) {
            return false;
        }
        if (!hyprimate::gl::native_current(thumbnail)) {
            return false;
        }
        // Only plugin-owned thumbnails receive mip levels; never modify a native client texture.
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<1> STATE{{}};
        const hyprimate::workspace::ProfileGpuScope GPU;
        glBindTexture(GL_TEXTURE_2D, thumbnail->getTexture()->m_texID);
        glGenerateMipmap(GL_TEXTURE_2D);
        return allowed();
    }

    bool write_thumbnail(
        const SP<Render::IFramebuffer> &source, const SP<Render::IFramebuffer> &thumbnail) {
        return write_thumbnail(source, thumbnail, safe_scene);
    }
}  // namespace

void CWorkspaceAnimation::Impl::remember_wallpaper(const PHLMONITOR &monitor,
    const PHLWORKSPACE &workspace,
    const SP<Render::IFramebuffer> &source) {
    if (!workspace || !source) {
        return;
    }
    using Observation = hyprimate::workspace::
        WallpaperObservation<PHLWORKSPACEREF, PHLMONITORREF, WallpaperSignature>;
    const Observation OBSERVATION{.workspace = workspace,
        .monitor = monitor,
        .signature = {.allocation = {.size = monitor->m_transformedSize,
                          .format = source->m_drmFormat,
                          .description = source->imageDescription()},
            .transform = monitor->m_transform}};
    const auto COPY = [&](SP<Render::IFramebuffer> image) {
        if (!image) {
            image = preview_target(monitor, 256);
        }
        return write_thumbnail(source, image) ? image : nullptr;
    };
    hyprimate::workspace::WorkspaceTurn<TurnPort>::observe_wallpaper(m_observed, OBSERVATION, COPY);
}

std::vector<CWorkspaceAnimation::Impl::InventoryPort::Fact>
CWorkspaceAnimation::Impl::InventoryPort::workspaces() const {
    std::vector<Fact> facts;
    facts.reserve(workspace_count());
    for (const auto &weak : State::Workspace::state()->workspaces()) {
        const auto WORKSPACE = weak.lock();
        if (!WORKSPACE) {
            continue;
        }
        const auto MONITOR = WORKSPACE->m_monitor.lock();
        const bool SPECIAL = WORKSPACE->type() == Workspace::eWorkspaceType::SPECIAL;
        Fact fact{.workspace = WORKSPACE,
            .endpoint = {.identity = SPECIAL ? owner->m_compositions.get({}, WORKSPACE)
                                             : owner->m_compositions.get(WORKSPACE, {}),
                .numbered = numbered(WORKSPACE),
                .special = SPECIAL,
                .owner = MONITOR ? static_cast<std::uint64_t>(MONITOR->m_id) : UINT64_MAX,
                .monitor_x = MONITOR ? MONITOR->m_position.x : 0,
                .monitor_y = MONITOR ? MONITOR->m_position.y : 0,
                .monitor_width = MONITOR ? MONITOR->m_size.x : 1,
                .monitor_height = MONITOR ? MONITOR->m_size.y : 1},
            .name = WORKSPACE->addressableName(),
            .wallpaper = {},
            .owner_ready = owner->ready(MONITOR),
            .local = MONITOR == monitor,
            .has_windows = WORKSPACE->getWindowCount() != 0,
            .has_retiring = !Desktop::fadingOutState()->fadeouts().empty()};
        if (MONITOR) {
            const WallpaperSignature SIGNATURE{
                .allocation = {.size = MONITOR->m_transformedSize,
                    .format = capture_format(MONITOR),
                    .description = MONITOR->workBufferImageDescription()},
                .transform = MONITOR->m_transform};
            fact.wallpaper = owner->m_observed.find(
                PHLWORKSPACEREF{WORKSPACE}, PHLMONITORREF{MONITOR}, SIGNATURE);
        }
        facts.push_back(std::move(fact));
    }
    return facts;
}

bool CWorkspaceAnimation::Impl::InventoryPort::allowed() const {
    if (revoked || Impl::s_active != owner || !safe_scene() || !owner->ready(monitor) ||
        owner->state(monitor) != entry || owner->m_inventory_revision != inventory_revision) {
        revoked = true;
        return false;
    }
    const auto *current = owner->turn(monitor);
    const bool ALLOWED = current && current == turn_identity && current->drawable() &&
                         current->revision() == turn_revision &&
                         owner->composition_id(monitor) == composition_identity;
    revoked = !ALLOWED;
    return ALLOWED;
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::InventoryPort::local_wallpaper(int side) const {
    if (!allowed()) {
        return {};
    }
    const ScopedCapture CAPTURE_SCOPE{owner->m_capture_depth};
    const auto WALLPAPER = entry->destination_frame.wallpaper;
    const auto IMAGE = preview_target(monitor, side);
    if (!write_thumbnail(WALLPAPER, IMAGE, [&] { return allowed(); })) {
        return nullptr;
    }
    track_preview(*entry, IMAGE);
    return IMAGE;
}

hyprimate::workspace::PreviewCaptureFacts CWorkspaceAnimation::Impl::InventoryPort::capture_facts(
    const PHLWORKSPACEREF &workspace) const {
    const auto WORKSPACE = workspace.lock();
    const auto MONITOR = WORKSPACE ? WORKSPACE->m_monitor.lock() : nullptr;
    return {.ready = owner->ready(MONITOR),
        .local = MONITOR == monitor,
        .pixels = MONITOR ? MONITOR->m_transformedSize.x * MONITOR->m_transformedSize.y : 0.0};
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::InventoryPort::capture(
    const PHLWORKSPACEREF &workspace,
    const SP<Render::IFramebuffer> &wallpaper,
    hyprimate::workspace::PreviewCapturePlan plan) const {
    const auto WORKSPACE = workspace.lock();
    return WORKSPACE ? owner->capture_preview(*this, WORKSPACE, wallpaper, plan) : nullptr;
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::capture_preview(const InventoryPort &port,
    const PHLWORKSPACE &workspace,
    const SP<Render::IFramebuffer> &paper,
    hyprimate::workspace::PreviewCapturePlan plan) {
    const auto KEY = profile_key();
    const hyprimate::workspace::ProfileCpuScope CPU{KEY, hyprimate::perf::Phase::Setup};
    const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
    // Local isolated scene resources can switch EGL when destroyed.
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT) {
        return {};
    }
    struct DrawData {
        RenderFn original;
        PHLMONITOR monitor;
        PHLWORKSPACE workspace;
        SP<Render::IFramebuffer> wallpaper;
        bool native_background = false;
        const InventoryPort *port = nullptr;
        bool remote = false;
        [[nodiscard]] bool allowed() const {
            if (!port->allowed() || workspace->m_monitor.lock() != monitor ||
                !port->owner->ready(monitor) || remote != (monitor != port->monitor) ||
                (wallpaper && !hyprimate::gl::native_current(wallpaper))) {
                port->revoked = true;
                return false;
            }
            return true;
        }
    } data{.original = render_original(),
        .monitor = workspace->m_monitor.lock(),
        .workspace = workspace,
        .wallpaper = paper,
        .native_background = plan.native_background,
        .port = &port,
        .remote = plan.remote};
    const auto ALLOWED = [&] { return data.allowed(); };
    if (!ALLOWED()) {
        return {};
    }
    const auto OWNER = data.monitor;
    const ScopedCapture CAPTURE_SCOPE{m_capture_depth};
    // The presentation target is scratch only before the ordinary output pass.
    auto scratch = port.entry->presentation;
    auto resources = port.entry->source_resources;
    if (plan.remote) {
        const auto EXTENT = OWNER->m_transformedSize;
        scratch = preview_target(OWNER, static_cast<int>(std::max(EXTENT.x, EXTENT.y)));
        if (!scratch) {
            return {};
        }
        const auto BLUR = hyprimate::gl::retain_native(
            g_pHyprRenderer->createFB("hyprimate workspace preview blur"));
        if (!BLUR) {
            return {};
        }
        resources = hyprimate::gl::retain_native(makeShared<Render::CSceneResources>(BLUR,
            Render::SSceneBufferDescription{
                .format = scratch->m_drmFormat, .imageDescription = scratch->imageDescription()}));
    }
    if (!scratch || !hyprimate::gl::native_current(scratch) || !resources ||
        !hyprimate::gl::native_current(resources->blurFramebuffer())) {
        return {};
    }
    const auto DRAW = [](void *context, Render::CRenderContext &ctx) {
        const auto &args = *static_cast<DrawData *>(context);
        if (!args.allowed()) {
            return;
        }
        auto &renderer = *g_pHyprRenderer;
        const auto NOW = Time::steadyNow();
        if (!args.native_background) {
            CTexPassElement::SRenderData layer;
            // The queued pass retains the attachment independently of its FB.
            layer.tex = args.wallpaper ? hyprimate::gl::retain_native(args.wallpaper->getTexture())
                                       : nullptr;
            if (!layer.tex) {
                args.port->revoked = true;
                return;
            }
            layer.box = {
                0, 0, args.monitor->m_transformedSize.x, args.monitor->m_transformedSize.y};
            Render::IHyprRenderer::addPassElement(
                ctx, makeUnique<CTexPassElement>(std::move(layer)));
        } else {
            // Wallpaper clients expose a current background, not workspace-specific images.
            (renderer.*PreviewDrawAccess::BACKGROUND)(ctx, args.monitor);
            if (!args.allowed()) {
                return;
            }
            // Native render callbacks may remove layers while building the pass.
            const auto LAYERS =
                args.monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND];
            for (const auto &layer : LAYERS) {
                if (const auto SURFACE = layer.lock()) {
                    (renderer.*PreviewDrawAccess::LAYER)(
                        ctx, SURFACE, args.monitor, NOW, false, false, true);
                    if (!args.allowed()) {
                        return;
                    }
                }
            }
        }
        args.original(&renderer,
            ctx,
            args.monitor,
            args.workspace,
            NOW,
            Render::eSceneMode::WORKSPACE_WINDOWS,
            {},
            1.F);
    };
    const auto IMAGE = preview_target(OWNER, plan.side);
    const bool CAPTURED =
        IMAGE && data.original && ALLOWED() &&
        hyprimate::workspace::capture_workspace(*g_pHyprRenderer,
            m_begin_render,
            OWNER,
            scratch,
            resources,
            TurnKind::SourceIsolated,
            DRAW,
            &data,
            [](void *context) { return static_cast<DrawData *>(context)->allowed(); }) &&
        write_thumbnail(scratch, IMAGE, ALLOWED);
    if (!CAPTURED) {
        return nullptr;
    }
    track_preview(*port.entry, IMAGE);
    return IMAGE;
}
