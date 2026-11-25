#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

#include "workspace/lava/LavaRenderer.hpp"
#include "workspace/lava/LavaShaders.hpp"

#ifdef HYPRIMATE_LAVA_RAW_ONLY
#include "GlHarness.hpp"
#include "gl/RawGlState.hpp"
#else
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "gl/RawGlBracket.hpp"
#endif

namespace hyprimate::workspace::lava {
    namespace {
#ifdef HYPRIMATE_LAVA_RAW_ONLY
        struct RawShadow {
            static void sync(const gl::SRawGlShadowState & /*state*/) {
            }
            static void forget_framebuffer(GLuint /*framebuffer*/) {
            }
        };
#else
        bool usable_backend() {
            return g_pHyprRenderer && Render::GL::g_pHyprOpenGL &&
                   g_pHyprRenderer->type() == Render::IHyprRenderer::RT_GL &&
                   g_pHyprRenderer->glBackend() == Render::GL::g_pHyprOpenGL;
        }

#endif

        template <std::size_t Units>
        class DrawState {
          public:
#ifdef HYPRIMATE_LAVA_RAW_ONLY
            explicit DrawState(const gl::SShadowedState &baseline) : m_state(baseline, m_shadow) {
            }
#else
            explicit DrawState(const gl::SShadowedState &baseline) : m_state(baseline) {
            }
#endif

          private:
#ifdef HYPRIMATE_LAVA_RAW_ONLY
            RawShadow m_shadow;
            gl::CRawGlState<Units, RawShadow> m_state;
#else
            Render::GL::CFramebufferBindingGuard m_bindings{g_pHyprRenderer->glBackend()};
            gl::CRawGlBracket<Units> m_state;
#endif
        };

#ifdef HYPRIMATE_LAVA_RAW_ONLY
        bool bound_image_matches(GLuint texture, int width, int height, bool coverage) {
            GLint binding = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
            if (std::bit_cast<GLuint>(binding) != texture) {
                return false;  // A non-2D texture cannot replace the current unit's binding.
            }
            GLint image_width = 0;
            GLint image_height = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &image_width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &image_height);
            if (width != image_width || height != image_height) {
                return false;
            }
            if (coverage) {
                GLint format = 0;
                GLint alpha = 0;
                GLint base_level = -1;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, &alpha);
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, &base_level);
                // Same linear UNORM/float representation as native accumulated captures.
                return (format == GL_RGBA8 || format == GL_RGBA16F || format == GL_RGBA32F) &&
                       alpha == GL_ALPHA && base_level == 0;
            }
            return true;
        }

        bool attached_output(GLuint output) {
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                return false;
            }
            GLint draw_buffer = 0;
            glGetIntegerv(GL_DRAW_BUFFER0, &draw_buffer);
            if (draw_buffer != GL_COLOR_ATTACHMENT0) {
                return false;
            }
            GLint type = GL_NONE;
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                &type);
            if (type != GL_TEXTURE) {
                return false;
            }
            GLint max_colors = 0;
            glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_colors);
            for (GLint slot = 1; slot < max_colors; ++slot) {
                glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
                    GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(slot),
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                    &type);
                if (type != GL_NONE) {
                    return false;
                }
            }
            GLint name = 0;
            GLint level = -1;
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                &name);
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,
                &level);
            // GL returns the unsigned texture name through a GLint parameter.
            return std::bit_cast<GLuint>(name) == output && level == 0;
        }

        bool validate_raw_output(GLuint output, int width, int height, bool coverage, GLint minor) {
            if (!attached_output(output) || (coverage && minor < 1)) {
                return false;
            }
            if (minor < 1) {
                return true;
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, output);
            return bound_image_matches(output, width, height, false);
        }
#endif

        bool bind_images(std::span<const GLuint> images,
            GLuint output,
            const gl::SShadowedState &baseline,
            bool coverage) {
#ifdef HYPRIMATE_LAVA_RAW_ONLY
            // Native captures already supply allocation metadata. External burn textures need
            // GLES 3.1 level queries to establish coverage and matching extents.
            GLint minor = 0;
            glGetIntegerv(GL_MINOR_VERSION, &minor);
            if (!validate_raw_output(
                    output, baseline.viewport_width, baseline.viewport_height, coverage, minor)) {
                return false;
            }
#else
            (void)baseline;
            (void)coverage;
#endif
            GLuint unit = 0;
            for (const GLuint TEXTURE : images) {
                if (TEXTURE == 0 || TEXTURE == output) {
                    return false;
                }
#ifdef HYPRIMATE_LAVA_RAW_ONLY
                if (glIsTexture(TEXTURE) == GL_FALSE) {
                    return false;
                }
#endif
                glActiveTexture(GL_TEXTURE0 + unit++);
                glBindTexture(GL_TEXTURE_2D, TEXTURE);
#ifdef HYPRIMATE_LAVA_RAW_ONLY
                if (minor >= 1 &&
                    !bound_image_matches(
                        TEXTURE, baseline.viewport_width, baseline.viewport_height, coverage)) {
                    return false;
                }
#endif
            }
            return true;
        }
    }  // namespace

    Renderer::Renderer() = default;

    Renderer::~Renderer() {
        release();
    }

    bool Renderer::initialize() {
#ifdef HYPRIMATE_LAVA_RAW_ONLY
        const gl::EglContextId CURRENT{
            .display = eglGetCurrentDisplay(), .context = eglGetCurrentContext()};
        if (!CURRENT.valid() || eglQueryAPI() != EGL_OPENGL_ES_API ||
            (m_context.valid() && m_context != CURRENT)) {
            return false;
        }
        EGLint version = 0;
        if (eglQueryContext(
                CURRENT.display, CURRENT.context, EGL_CONTEXT_CLIENT_VERSION, &version) !=
                EGL_TRUE ||
            version < 3) {
            return false;
        }
        m_context = CURRENT;
#else
        if (!usable_backend()) {
            return false;
        }
        const gl::ScopedCompositorContext CONTEXT;
        if (!CONTEXT || !m_context.ensure_current()) {
            return false;
        }
#endif
        if (m_program != 0) {
            return true;
        }
        const DrawState<0> STATE{{}};
#ifdef HYPRIMATE_LAVA_RAW_ONLY
        m_program = gl_harness::build_program(VERTEX_SHADER, FRAGMENT_SHADER);
#else
        auto shader = std::make_unique<CShader>();
        if (!shader->createProgram(VERTEX_SHADER, FRAGMENT_SHADER, true, true)) {
            return false;
        }
        m_program = shader->program();
        m_shader = std::move(shader);
#endif
        if (!initialize_bindings()) {
            release_objects();
            return false;
        }
        return true;
    }

    bool Renderer::initialize_bindings() {
        if (m_program == 0) {
            return false;
        }
        static constexpr std::array LOCATIONS{
            std::pair{"sourceImage", &Uniforms::source_image},
            std::pair{"destinationImage", &Uniforms::destination_image},
            std::pair{"sourceBottom", &Uniforms::source_bottom},
            std::pair{"sourceWindows", &Uniforms::source_windows},
            std::pair{"destinationBottom", &Uniforms::destination_bottom},
            std::pair{"destinationWindows", &Uniforms::destination_windows},
            std::pair{"burnUp", &Uniforms::burn_up},
            std::pair{"size", &Uniforms::size},
            std::pair{"progress", &Uniforms::progress},
            std::pair{"direction", &Uniforms::direction},
            std::pair{"variant", &Uniforms::variant},
            std::pair{"sharedA", &Uniforms::shared_a},
            std::pair{"sharedB", &Uniforms::shared_b},
            std::pair{"specialA", &Uniforms::special_a},
            std::pair{"specialB", &Uniforms::special_b},
        };
        for (const auto &[NAME, MEMBER] : LOCATIONS) {
            const GLint LOCATION = glGetUniformLocation(m_program, NAME);
            if (LOCATION < 0) {
                return false;
            }
            m_uniforms.*MEMBER = LOCATION;
        }
        glGenVertexArrays(1, &m_vao);
        if (m_vao == 0) {
            return false;
        }
        glBindVertexArray(m_vao);
        if (glIsVertexArray(m_vao) == GL_FALSE) {
            return false;
        }
        glUseProgram(m_program);
        glUniform1i(m_uniforms.source_image, 0);
        glUniform1i(m_uniforms.destination_image, 1);
        return true;
    }

    void Renderer::release() {
        if (m_program != 0 || m_vao != 0) {
#ifdef HYPRIMATE_LAVA_RAW_ONLY
            const gl::ScopedEglContext CONTEXT{m_context};
#else
            const gl::ScopedCompositorContext CONTEXT{m_context};
#endif
            if (CONTEXT) {
                const DrawState<0> STATE{{}};
                release_objects();
            }
#ifndef HYPRIMATE_LAVA_RAW_ONLY
            else {
                // CShader has no detach API. Its destructor must never delete in another context.
                [[maybe_unused]] const auto *const ABANDONED = m_shader.release();
            }
#endif
        }
        m_program = 0;
        m_vao = 0;
        m_bound_material.reset();
#ifdef HYPRIMATE_LAVA_RAW_ONLY
        m_context = {};
#else
        m_context.reset();
#endif
    }

    void Renderer::release_objects() {
        if (m_vao != 0) {
            glDeleteVertexArrays(1, &m_vao);
        }
#ifdef HYPRIMATE_LAVA_RAW_ONLY
        if (m_program != 0) {
            glDeleteProgram(m_program);
        }
#else
        m_shader.reset();
#endif
        m_program = 0;
        m_vao = 0;
    }


    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    bool Renderer::draw_raw(GLuint source,
        GLuint destination,
        GLuint output,
        GLuint framebuffer,
        int width,
        int height,
        double progress,
        Direction direction,
        const Material &material,
        WindowPlanes planes) {
        if (m_program == 0 || m_vao == 0 || source == 0 || destination == 0 || output == 0 ||
            framebuffer == 0 || source == destination || source == output ||
            destination == output || width <= 0 || height <= 0 || !std::isfinite(progress) ||
            progress < 0 || progress > 1 ||
            (direction != Direction::Forward && direction != Direction::Backward) ||
            material.variant < 0 || material.variant > 3) {
            return false;
        }
#ifdef HYPRIMATE_LAVA_RAW_ONLY
        if (!m_context.current() || glIsTexture(output) == GL_FALSE ||
            glIsFramebuffer(framebuffer) == GL_FALSE) {
            return false;
        }
#else
        if (!usable_backend() || !m_context.ensure_current()) {
            return false;
        }
#endif

        const gl::SShadowedState BASELINE{
            .viewport_width = width, .viewport_height = height, .framebuffer = framebuffer};
        const auto DRAW = [&]<std::size_t Units>(const std::array<GLuint, Units> &images) {
            const DrawState<Units> STATE{BASELINE};
            if (!bind_images(images, output, BASELINE, material.burn_up)) {
                return false;
            }
            draw_material(width, height, progress, direction, material);
            return true;
        };
        if (material.burn_up) {
            return DRAW(std::array{source,
                destination,
                planes.source_bottom,
                planes.source_windows,
                planes.destination_bottom,
                planes.destination_windows});
        }
        return DRAW(std::array{source, destination});
    }

    void Renderer::draw_material(
        int width, int height, double progress, Direction direction, const Material &material) {
        glUseProgram(m_program);
        glUniform2f(m_uniforms.size, static_cast<float>(width), static_cast<float>(height));
        glUniform1f(m_uniforms.progress, static_cast<float>(progress));
        glUniform1f(m_uniforms.direction, direction == Direction::Forward ? 1.F : -1.F);
        if (!m_bound_material || m_bound_material->burn_up != material.burn_up) {
            // In unified mode the caller's other units may contain the framebuffer texture.
            glUniform1i(m_uniforms.burn_up, material.burn_up ? 1 : 0);
            glUniform1i(m_uniforms.source_bottom, material.burn_up ? 2 : 0);
            glUniform1i(m_uniforms.source_windows, material.burn_up ? 3 : 0);
            glUniform1i(m_uniforms.destination_bottom, material.burn_up ? 4 : 0);
            glUniform1i(m_uniforms.destination_windows, material.burn_up ? 5 : 0);
        }
        if (m_bound_material != material) {
            glUniform1i(m_uniforms.variant, material.variant);
            glUniform4fv(m_uniforms.shared_a, 1, material.sharedA.data());
            glUniform4fv(m_uniforms.shared_b, 1, material.sharedB.data());
            glUniform4fv(m_uniforms.special_a, 1, material.specialA.data());
            glUniform2fv(m_uniforms.special_b, 1, material.specialB.data());
            m_bound_material = material;
        }
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
}  // namespace hyprimate::workspace::lava
