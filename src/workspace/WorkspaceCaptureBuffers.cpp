#include "workspace/WorkspaceAnimationImpl.hpp"

namespace workspace_animation_detail {
    std::uint32_t capture_format(const PHLMONITOR &monitor) {
        const auto FORMAT = monitor->m_drmFormat;
        return monitor->useFP16() || FORMAT == DRM_FORMAT_XRGB2101010 ||
                       FORMAT == DRM_FORMAT_XBGR2101010 || FORMAT == DRM_FORMAT_ARGB2101010 ||
                       FORMAT == DRM_FORMAT_ABGR2101010
                   ? DRM_FORMAT_ABGR16161616F
                   : DRM_FORMAT_ABGR8888;
    }

    std::uint64_t framebuffer_bytes(const SP<Render::IFramebuffer> &framebuffer) {
        return framebuffer ? static_cast<std::uint64_t>(framebuffer->m_size.x) *
                                 static_cast<std::uint64_t>(framebuffer->m_size.y) *
                                 (framebuffer->m_drmFormat == DRM_FORMAT_ABGR8888 ? 4 : 8)
                           : 0;
    }

    // Visit only plugin-created captures. Native attachments and history images
    // borrowed by previews are absent. Count each color attachment's level zero
    // once; omit mipmaps, driver padding, depth/stencil and renderer-private data.
    void sample_resources(hyprimate::perf::Key key,
        CWorkspaceAnimation::Impl::MonitorState &entry) noexcept {
        if (!hyprimate::perf::enabled()) {
            return;
        }
        try {
            auto &attachments = entry.profile_attachments;
            attachments.clear();
            std::uint64_t gpu_bytes = 0;
            const auto ADD = [&](const SP<Render::IFramebuffer> &target) {
                if (!target || !hyprimate::gl::native_current(target) || !target->isAllocated() ||
                    (target->m_drmFormat != DRM_FORMAT_ABGR8888 &&
                        target->m_drmFormat != DRM_FORMAT_ABGR16161616F)) {
                    return;
                }
                const auto TEXTURE = target->getTexture();
                if (!TEXTURE || TEXTURE->m_texID == 0 ||
                    std::ranges::find(attachments, TEXTURE->m_texID) != attachments.end()) {
                    return;
                }
                const auto BYTES = hyprimate::workspace::capture_extent_bytes(target->m_size.x,
                    target->m_size.y,
                    target->m_drmFormat == DRM_FORMAT_ABGR16161616F);
                if (!BYTES || *BYTES > std::numeric_limits<std::uint64_t>::max() - gpu_bytes) {
                    return;
                }
                attachments.push_back(TEXTURE->m_texID);
                gpu_bytes += *BYTES;
            };
            const auto LAYER = [&](const hyprimate::workspace::WorkspaceLayer &layer) {
                ADD(layer.accumulated);
                ADD(layer.backdrop);
            };
            const auto FLOATING = [&](const auto &floating) {
                if (floating) {
                    for (const auto &window : *floating) {
                        LAYER(window.layer);
                        LAYER(window.previous_layer);
                    }
                }
            };
            const auto CAPTURE = [&](const auto &self,
                                     const hyprimate::workspace::WorkspaceCapture &capture) -> void {
                ADD(capture.composite);
                ADD(capture.wallpaper);
                LAYER(capture.bottom);
                LAYER(capture.windows);
                LAYER(capture.top);
                FLOATING(capture.floating);
                if (capture.previous) {
                    self(self, *capture.previous);
                }
            };
            ADD(entry.source);
            ADD(entry.destination);
            ADD(entry.presentation);
            ADD(entry.source_wallpaper);
            ADD(entry.destination_wallpaper);
            ADD(entry.opaque_backdrop);
            if (entry.source_resources) {
                ADD(entry.source_resources->blurFramebuffer());
            }
            CAPTURE(CAPTURE, entry.source_frame);
            CAPTURE(CAPTURE, entry.destination_frame);
            for (const auto *blend : {&entry.source_blend, &entry.destination_blend}) {
                if (blend->stale) {
                    CAPTURE(CAPTURE, *blend->stale);
                }
                FLOATING(blend->presentation);
            }
            for (const auto &preview : entry.profile_previews) {
                ADD(preview.lock());
            }
            // Known owned object storage, not a total heap census.
            const auto CPU_BYTES = sizeof(entry) +
                                   entry.profile_previews.capacity() *
                                       sizeof(decltype(entry.profile_previews)::value_type) +
                                   entry.profile_attachments.capacity() * sizeof(GLuint);
            hyprimate::perf::resource_sample(key, CPU_BYTES, gpu_bytes);
        } catch (...) {
            // Optional accounting must not make an otherwise drawable frame fail.
        }
    }

    bool copy_capture(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &target,
        Coverage coverage,
        bool downsample,
        const std::optional<CBox> &crop,
        CopySource ownership) {
        if (!source || !target || source == target || !source->isAllocated() ||
            !target->isAllocated() || (!crop && !downsample && source->m_size != target->m_size)) {
            return false;
        }
        const hyprimate::gl::ScopedCompositorContext CONTEXT{};
        if (!CONTEXT) {
            return false;
        }
        if ((ownership == CopySource::Retained && !hyprimate::gl::native_current(source)) ||
            !hyprimate::gl::native_current(target)) {
            return false;
        }
        auto *src = dynamic_cast<Render::GL::CGLFramebuffer *>(source.get());
        auto *dst = dynamic_cast<Render::GL::CGLFramebuffer *>(target.get());
        if (src == nullptr || dst == nullptr) {
            return false;
        }
        const auto SOURCE_ID = src->getFBID();
        const auto TARGET_ID = dst->getFBID();
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<0> STATE{{.framebuffer = TARGET_ID}};
        glBindFramebuffer(GL_READ_FRAMEBUFFER, SOURCE_ID);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
            glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            return false;
        }
        const auto LEFT = crop ? static_cast<GLint>(crop->x) : 0;
        const auto BOTTOM = crop ? static_cast<GLint>(crop->y) : 0;
        const auto WIDTH = static_cast<GLint>(target->m_size.x);
        const auto HEIGHT = static_cast<GLint>(target->m_size.y);
        const auto SOURCE_WIDTH = crop ? WIDTH : static_cast<GLint>(source->m_size.x);
        const auto SOURCE_HEIGHT = crop ? HEIGHT : static_cast<GLint>(source->m_size.y);
        const hyprimate::workspace::ProfileGpuScope GPU;
        glBlitFramebuffer(LEFT,
            BOTTOM,
            LEFT + SOURCE_WIDTH,
            BOTTOM + SOURCE_HEIGHT,
            0,
            0,
            WIDTH,
            HEIGHT,
            GL_COLOR_BUFFER_BIT,
            downsample ? GL_LINEAR : GL_NEAREST);
        if (coverage != Coverage::Preserve) {
            if (coverage == Coverage::ResetSource) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, SOURCE_ID);
            }
            const std::array<GLfloat, 4> CLEAR{
                0.F, 0.F, 0.F, coverage == Coverage::OpaqueTarget ? 1.F : 0.F};
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
            glClearBufferfv(GL_COLOR, 0, CLEAR.data());
        }
        return true;
    }

    bool copy_opaque(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &target,
        bool opaque,
        bool downsample) {
        return copy_capture(
            source, target, opaque ? Coverage::OpaqueTarget : Coverage::Preserve, downsample);
    }
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::allocate_capture(
    MonitorState &entry, const PHLMONITOR &monitor, std::optional<Vector2D> size) {
    if (!g_pHyprRenderer || !ready(monitor)) {
        return nullptr;
    }
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT) {
        return nullptr;
    }
    const auto SIZE = size.value_or(monitor->m_transformedSize);
    const auto FORMAT = capture_format(monitor);
    const auto BYTES =
        hyprimate::workspace::capture_extent_bytes(SIZE.x, SIZE.y, FORMAT != DRM_FORMAT_ABGR8888);
    if (!BYTES) {
        return nullptr;
    }
    std::uint64_t total = 0;
    for (const auto &state : m_states) {
        total += state->allocated_bytes;
    }
    if (!hyprimate::workspace::capture_allocation_allowed({.bytes = *BYTES,
            .monitor_bytes = entry.allocated_bytes,
            .total_bytes = total,
            .wide_color = FORMAT != DRM_FORMAT_ABGR8888})) {
        LOG(Log::DEBUG,
            "[hyprimate] workspace fallback reason=capture-budget monitor={} requested={} "
            "monitor_bytes={} total_bytes={}",
            monitor->m_name,
            *BYTES,
            entry.allocated_bytes,
            total);
        return nullptr;
    }
    const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
    const hyprimate::gl::FramebufferAllocationState STATE;
    auto framebuffer =
        hyprimate::gl::retain_native(g_pHyprRenderer->createFB("hyprimate workspace capture"));
    if (!framebuffer ||
        !framebuffer->alloc(static_cast<int>(SIZE.x), static_cast<int>(SIZE.y), FORMAT)) {
        entry.port.diagnostic("capture-allocation");
        return nullptr;
    }
    framebuffer->setImageDescription(monitor->workBufferImageDescription());
    entry.allocated_bytes += *BYTES;
    return framebuffer;
}

hyprimate::workspace::CaptureBufferFacts CWorkspaceAnimation::Impl::buffer_facts(
    const MonitorState &entry, const PHLMONITOR &monitor) {
    hyprimate::workspace::CaptureBufferFacts facts;
    facts.native_ready = monitor->m_activeWorkspace != nullptr &&
                         g_pHyprRenderer->type() == Render::IHyprRenderer::RT_GL &&
                         g_pHyprRenderer->glBackend() != nullptr;
    facts.compatible =
        entry.source == nullptr ||
        (entry.logical_size == monitor->m_size && entry.scale == monitor->m_scale &&
            entry.transform == monitor->m_transform &&
            entry.output_format == monitor->m_drmFormat &&
            entry.source->m_drmFormat == capture_format(monitor) &&
            entry.source->imageDescription() == monitor->workBufferImageDescription());
    facts.scene_safe = TurnPort::scene_safe();
    facts.hud_safe = entry.port.hud_safe();
    const auto *const ACTIVE_TURN = turn(monitor);
    facts.active = ACTIVE_TURN != nullptr && ACTIVE_TURN->active();
    const auto USABLE = [&](const SP<Render::IFramebuffer> &framebuffer) {
        return framebuffer != nullptr && hyprimate::gl::native_current(framebuffer) &&
               framebuffer->isAllocated() && framebuffer->m_size == monitor->m_transformedSize &&
               framebuffer->getTexture() != nullptr;
    };
    facts.base = {USABLE(entry.source),
        USABLE(entry.destination),
        USABLE(entry.presentation),
        USABLE(entry.source_wallpaper),
        USABLE(entry.destination_wallpaper),
        USABLE(entry.opaque_backdrop)};
    facts.planes = {USABLE(entry.source_frame.bottom.accumulated),
        USABLE(entry.source_frame.windows.accumulated),
        USABLE(entry.source_frame.top.backdrop),
        USABLE(entry.destination_frame.bottom.accumulated),
        USABLE(entry.destination_frame.windows.accumulated),
        USABLE(entry.destination_frame.top.backdrop)};
    return facts;
}

bool CWorkspaceAnimation::Impl::ensure_buffers(MonitorState &entry, const PHLMONITOR &monitor) {
    const auto KEY = profile_key();
    const hyprimate::workspace::ProfileCpuScope CPU{KEY, hyprimate::perf::Phase::Setup};
    const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
    // resources() is lazy and fatal on an unconfigured output. A completed native
    // POST frame, not resources(), is the readiness proof during connect/disconnect.
    if (g_pHyprRenderer == nullptr || !ready(monitor)) {
        return false;
    }
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT) {
        return false;
    }
    const auto FACTS = buffer_facts(entry, monitor);
    const auto PLAN = hyprimate::workspace::capture_buffer_plan(FACTS, capture_features());
    if (PLAN == hyprimate::workspace::CaptureBufferAction::Reject) {
        LOG(Log::DEBUG,
            "[hyprimate] workspace fallback reason=buffer-policy monitor={} native_ready={} "
            "compatible={} scene_safe={} hud_safe={} active={}",
            monitor->m_name,
            FACTS.native_ready,
            FACTS.compatible,
            FACTS.scene_safe,
            FACTS.hud_safe,
            FACTS.active);
        return false;
    }
    if (PLAN == hyprimate::workspace::CaptureBufferAction::Allocate) {
        const auto ALLOCATE = [&] { return allocate_capture(entry, monitor); };
        entry.source = ALLOCATE();
        entry.destination = ALLOCATE();
        entry.presentation = ALLOCATE();
        if (m_uses_wallpaper) {
            entry.source_wallpaper = ALLOCATE();
            entry.destination_wallpaper = ALLOCATE();
            entry.opaque_backdrop = ALLOCATE();
        }
        entry.source_resources.reset();
        entry.logical_size = monitor->m_size;
        entry.scale = monitor->m_scale;
        entry.transform = monitor->m_transform;
        entry.output_format = monitor->m_drmFormat;
        const auto LAYOUT = hyprimate::workspace::capture_plane_layout(capture_features());
        const auto PREPARE = [&](hyprimate::workspace::WorkspaceCapture &frame,
                                 const SP<Render::IFramebuffer> &composite,
                                 const SP<Render::IFramebuffer> &wallpaper) {
            frame.composite = composite;
            frame.wallpaper = wallpaper;
            frame.layered = LAYOUT.layered;
            if (LAYOUT.layered) {
                frame.bottom = {.accumulated = ALLOCATE(), .backdrop = wallpaper};
                frame.windows = {
                    .accumulated = LAYOUT.separate_windows ? ALLOCATE() : frame.bottom.accumulated,
                    .backdrop = frame.bottom.accumulated};
                frame.top = {.accumulated = composite,
                    .backdrop =
                        LAYOUT.separate_top_backdrop ? ALLOCATE() : frame.windows.accumulated};
            }
        };
        PREPARE(entry.source_frame, entry.source, entry.source_wallpaper);
        PREPARE(entry.destination_frame, entry.destination, entry.destination_wallpaper);
    }
    if (hyprimate::workspace::capture_buffer_plan(buffer_facts(entry, monitor),
            capture_features()) != hyprimate::workspace::CaptureBufferAction::Reuse) {
        return false;
    }
    if (entry.source_resources &&
        !hyprimate::gl::native_current(entry.source_resources->blurFramebuffer())) {
        entry.source_resources.reset();
    }
    if (!entry.source_resources) {
        const auto BLUR = hyprimate::gl::retain_native(
            g_pHyprRenderer->createFB("hyprimate workspace source blur"));
        if (!BLUR) {
            return false;
        }
        entry.source_resources =
            hyprimate::gl::retain_native(makeShared<Render::CSceneResources>(BLUR,
                Render::SSceneBufferDescription{.format = entry.source->m_drmFormat,
                    .imageDescription = entry.source->imageDescription()}));
        if (!entry.source_resources) {
            return false;
        }
    }
    const bool READY = initialize_selected_style();
    if (!READY) {
        entry.port.diagnostic("style-initialize");
    }
    return READY;
}

void CWorkspaceAnimation::Impl::release_floating_tail(
    MonitorState &entry, hyprimate::workspace::WorkspaceCapture &frame) {
    if (!frame.floating || frame.floating->size() <= entry.floating_used) {
        return;
    }
    const auto RELEASE = [&](const SP<Render::IFramebuffer> &target) {
        if (target) {
            entry.allocated_bytes -= framebuffer_bytes(target);
        }
    };
    for (std::size_t index = entry.floating_used; index < frame.floating->size(); ++index) {
        RELEASE((*frame.floating)[index].layer.accumulated);
        RELEASE((*frame.floating)[index].layer.backdrop);
    }
    frame.floating->resize(entry.floating_used);
}

void CWorkspaceAnimation::Impl::release_blend(
    MonitorState &entry, MonitorState::CaptureBlend &blend) {
    if (blend.stale) {
        const auto &old = *blend.stale;
        const auto RELEASE = [&](const SP<Render::IFramebuffer> &target) {
            if (target) {
                entry.allocated_bytes -= framebuffer_bytes(target);
            }
        };
        RELEASE(old.composite);
        RELEASE(old.wallpaper);
        RELEASE(old.bottom.accumulated);
        if (old.windows.accumulated != old.bottom.accumulated) {
            RELEASE(old.windows.accumulated);
        }
        if (old.top.backdrop != old.windows.accumulated) {
            RELEASE(old.top.backdrop);
        }
        if (old.floating) {
            for (const auto &window : *old.floating) {
                RELEASE(window.layer.accumulated);
                RELEASE(window.layer.backdrop);
            }
        }
    }
    blend.presentation.reset();
    blend.stale.reset();
}

bool CWorkspaceAnimation::Impl::begin_refresh(
    MonitorState &entry, const PHLMONITOR &monitor, TurnKind kind) {
    auto &blend = kind == TurnKind::SourceIsolated ? entry.source_blend : entry.destination_blend;
    const auto KEY = profile_key();
    const hyprimate::workspace::ProfileCpuScope CPU{KEY, hyprimate::perf::Phase::Setup};
    const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
    const hyprimate::gl::ScopedCompositorContext CONTEXT{};
    if (!CONTEXT) {
        return false;
    }
    const auto &live =
        kind == TurnKind::SourceIsolated ? entry.source_frame : entry.destination_frame;
    const auto CLONE = [&](const SP<Render::IFramebuffer> &input,
                           SP<Render::IFramebuffer> &output) {
        output = input ? allocate_capture(entry, monitor, input->m_size) : nullptr;
        return input && output && copy_opaque(input, output, false);
    };
    try {
        blend.stale = std::make_shared<hyprimate::workspace::WorkspaceCapture>();
        auto &old = *blend.stale;
        old.identity = live.identity;
        old.endpoint = live.endpoint;
        old.layered = live.layered;
        if (!CLONE(live.composite, old.composite) || !CLONE(live.wallpaper, old.wallpaper) ||
            !CLONE(live.bottom.accumulated, old.bottom.accumulated)) {
            return false;
        }
        old.bottom.backdrop = old.wallpaper;
        old.windows.backdrop = old.bottom.accumulated;
        old.top.accumulated = old.composite;
        if (live.windows.accumulated == live.bottom.accumulated) {
            old.windows.accumulated = old.bottom.accumulated;
        } else if (!CLONE(live.windows.accumulated, old.windows.accumulated)) {
            return false;
        }
        if (live.top.backdrop == live.windows.accumulated) {
            old.top.backdrop = old.windows.accumulated;
        } else if (!CLONE(live.top.backdrop, old.top.backdrop)) {
            return false;
        }
        if (live.floating) {
            old.floating = std::make_shared<std::vector<hyprimate::workspace::WorkspaceFloating>>();
            old.floating->reserve(live.floating->size());
            for (const auto &window : *live.floating) {
                old.floating->emplace_back();
                auto &item = old.floating->back();
                item.identity = window.identity;
                item.rect = window.rect;
                item.order = window.order;
                if (!CLONE(window.layer.accumulated, item.layer.accumulated) ||
                    !CLONE(window.layer.backdrop, item.layer.backdrop)) {
                    return false;
                }
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

void CWorkspaceAnimation::Impl::TurnPort::swap_slots() const {
    std::swap(entry->source, entry->destination);
    std::swap(entry->source_wallpaper, entry->destination_wallpaper);
    std::swap(entry->source_wallpaper_identity, entry->destination_wallpaper_identity);
    std::swap(entry->source_frame, entry->destination_frame);
    std::swap(entry->source_shell_identity, entry->destination_shell_identity);
    std::swap(entry->source_blend, entry->destination_blend);
}
