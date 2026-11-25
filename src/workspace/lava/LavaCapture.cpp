#include <bit>
#include <cmath>
#include <limits>

#include <hyprgraphics/egl/Egl.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "gl/NativeResource.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/lava/LavaRenderer.hpp"

namespace hyprimate::workspace::lava {
    namespace {
        bool valid_extent(double extent) {
            return extent >= 1 && extent <= std::numeric_limits<int>::max() &&
                   std::trunc(extent) == extent;
        }

        bool usable_capture(const SP<Render::IFramebuffer> &framebuffer) {
            return framebuffer && gl::native_current(framebuffer) && framebuffer->isAllocated();
        }

        GLuint texture_name(const SP<Render::IFramebuffer> &framebuffer, bool coverage = false) {
            const auto TEXTURE = framebuffer->getTexture();
            if (!TEXTURE || TEXTURE->m_size != framebuffer->m_size ||
                (TEXTURE->m_type != Render::TEXTURE_RGBA &&
                    (coverage || TEXTURE->m_type != Render::TEXTURE_RGBX))) {
                return 0;
            }
            if (coverage && TEXTURE->m_drmFormat != framebuffer->m_drmFormat) {
                return 0;
            }
            return TEXTURE->m_texID;
        }

        bool aggregate_capture(const WorkspaceCapture &capture) {
            return capture.layered && capture.live_mix == 1.0 &&
                   (!capture.floating || capture.floating->empty()) &&
                   capture.composite == capture.top.accumulated &&
                   capture.windows.backdrop == capture.bottom.accumulated &&
                   capture.top.backdrop == capture.windows.accumulated &&
                   capture.bottom.accumulated != capture.windows.accumulated &&
                   capture.composite != capture.windows.accumulated &&
                   capture.composite != capture.bottom.accumulated;
        }
    }  // namespace
    bool Renderer::draw(const WorkspaceFrame &frame,
        double progress,
        Direction direction,
        const Material &material) {
        if (m_program == 0 || !g_pHyprRenderer ||
            g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
            return false;
        }
        const auto &source = frame.source.composite;
        const auto &destination = frame.destination.composite;
        const auto &output = frame.presentation;
        if (!usable_capture(source) || !usable_capture(destination) || !usable_capture(output) ||
            source->m_size != output->m_size || destination->m_size != output->m_size ||
            !valid_extent(output->m_size.x) || !valid_extent(output->m_size.y)) {
            return false;
        }
        WindowPlanes planes;
        if (material.burn_up) {
            const auto *const FORMAT =
                Hyprgraphics::Egl::getPixelFormatFromDRM(output->m_drmFormat);
            if (frame.native_hud || !aggregate_capture(frame.source) ||
                !aggregate_capture(frame.destination) || FORMAT == nullptr || !FORMAT->withAlpha ||
                source->m_drmFormat != output->m_drmFormat ||
                destination->m_drmFormat != output->m_drmFormat) {
                return false;
            }
            const auto PLANE = [&](const SP<Render::IFramebuffer> &buffer) -> GLuint {
                return usable_capture(buffer) && buffer->m_size == output->m_size &&
                               buffer->m_drmFormat == output->m_drmFormat
                           ? texture_name(buffer, true)
                           : 0;
            };
            planes = {.source_bottom = PLANE(frame.source.bottom.accumulated),
                .source_windows = PLANE(frame.source.windows.accumulated),
                .destination_bottom = PLANE(frame.destination.bottom.accumulated),
                .destination_windows = PLANE(frame.destination.windows.accumulated)};
        }
        auto *const NATIVE = dynamic_cast<Render::GL::CGLFramebuffer *>(output.get());
        if (NATIVE == nullptr || output->getMirrorTexture()) {
            return false;  // Only a sole native color target can exclude attachment feedback.
        }
        return draw_raw(texture_name(source, material.burn_up),
            texture_name(destination, material.burn_up),
            texture_name(output),
            NATIVE->getFBID(),
            static_cast<int>(output->m_size.x),
            static_cast<int>(output->m_size.y),
            progress,
            direction,
            material,
            planes);
    }
}  // namespace hyprimate::workspace::lava
