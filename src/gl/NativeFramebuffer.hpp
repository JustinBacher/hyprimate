#pragma once

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "gl/CompositorContext.hpp"
#include "gl/FramebufferAllocationState.hpp"

namespace hyprimate::gl {
    // Host allocation can upload nullptr through an ambient unpack PBO and disturb texture/depth
    // state. Preserve only that allocation state: framebuffer/viewport remain the host's contract.
    // Rendering callbacks must already have the compositor current; never switch their context.
    [[nodiscard]] inline SP<Render::IFramebuffer> get_work_buffer(
        Render::CRenderContext &context, const Vector2D &size) {
        CompositorContextOwner current;
        if (!current.ensure_current()) {
            return nullptr;
        }
        const FramebufferAllocationState ALLOCATION;
        return g_pHyprRenderer->getWorkBuffer(context, size);
    }

    [[nodiscard]] inline bool allocate_framebuffer(
        Render::IFramebuffer &framebuffer, int width, int height, DRMFormat format) {
        CompositorContextOwner current;
        if (!current.ensure_current()) {
            return false;
        }
        const FramebufferAllocationState ALLOCATION;
        return framebuffer.alloc(width, height, format);
    }
}  // namespace hyprimate::gl
