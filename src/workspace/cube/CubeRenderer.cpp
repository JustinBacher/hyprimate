#include "workspace/cube/CubeRenderer.hpp"

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "gl/NativeResource.hpp"
#include "gl/RawGlBracket.hpp"
#include "workspace/WorkspaceFrame.hpp"

namespace hyprimate::workspace {
    CubeRenderer::~CubeRenderer() { release(); }

    bool CubeRenderer::initialize() {
        const gl::ScopedCompositorContext CONTEXT{};
        if (!CONTEXT || !m_owner.ensure_current()) { return false; }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<2> BRACKET{{}};
        return m_material.initialize();
    }

    void CubeRenderer::release() {
        const gl::ScopedCompositorContext CONTEXT{m_owner};
        if (CONTEXT) {
            const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
            const gl::CRawGlBracket<2> BRACKET{{}};
            m_material.release();
        } else {
            // A replacement context may have recycled our GL names. Never delete them there.
            m_material.abandon();
        }
        m_owner.reset();
    }

    bool CubeRenderer::draw(Render::CRenderContext & /*context*/, const WorkspaceFrame &frame,
        double progress, Direction direction, const cube::Settings &settings) {
        const auto &source = frame.source.composite;
        const auto &destination = frame.destination.composite;
        const auto &output = frame.presentation;
        if (!m_owner.ensure_current() || g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL ||
            !source || !destination || !output || source->m_size != destination->m_size ||
            source->m_size != output->m_size || !gl::native_current(source) ||
            !gl::native_current(destination) || !gl::native_current(output)) {
            return false;
        }
        const auto src = source->getTexture();
        const auto dst = destination->getTexture();
        const auto out = output->getTexture();
        if (!src || !dst || !out) { return false; }
        // All three buffers came from this GL backend. Only bind the presentation target.
        const auto fbo = static_cast<Render::GL::CGLFramebuffer *>(output.get())->getFBID(); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
        const auto width = static_cast<int>(output->m_size.x);
        const auto height = static_cast<int>(output->m_size.y);
        if (fbo == 0 || width <= 0 || height <= 0) { return false; }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<2> BRACKET{{.viewport_width = width, .viewport_height = height, .framebuffer = fbo}};
        return m_material.draw(src->m_texID, dst->m_texID, out->m_texID, width, height, progress, direction, settings);
    }
} // namespace hyprimate::workspace
