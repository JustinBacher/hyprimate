#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "gl/RawGlState.hpp"
#include "gl/Storage.hpp"
#include "window/paper/PaperMaterial.hpp"
#include "window/paper/PaperRenderMeshMath.hpp"

namespace {
    using namespace paper_render_mesh_math;
    bool finite(hyprimate::Vec2 p) {
        return std::isfinite(p.x) && std::isfinite(p.y);
    }
    // State not covered by the compositor's RawGlBracket. Keep it local so tests and
    // production exercise exactly the same depth and scratch-attachment lifecycle.
    struct LocalState {
        GLint depth_function = 0, renderbuffer = 0;
        GLboolean depth_mask = GL_TRUE;
        GLfloat depth_clear = 1;
        std::array<GLfloat, 4> color_clear{};
        std::array<GLboolean, 4> color_mask{};
        bool blend_changed = false;
        GLint blend_equation_rgb = 0, blend_equation_alpha = 0;
        explicit LocalState(bool blend) : blend_changed(blend) {
            if (blend_changed) {
                glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb);
                glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_equation_alpha);
            }
            glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
            glGetFloatv(GL_DEPTH_CLEAR_VALUE, &depth_clear);
            glGetFloatv(GL_COLOR_CLEAR_VALUE, color_clear.data());
            glGetBooleanv(GL_COLOR_WRITEMASK, color_mask.data());
        }
        ~LocalState() {
            if (blend_changed) {
                glBlendEquationSeparate(static_cast<GLenum>(blend_equation_rgb),
                    static_cast<GLenum>(blend_equation_alpha));
            }
            glDepthFunc(static_cast<GLenum>(depth_function));
            glDepthMask(depth_mask);
            glClearDepthf(depth_clear);
            glClearColor(color_clear[0], color_clear[1], color_clear[2], color_clear[3]);
            glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(renderbuffer));
        }
        LocalState(const LocalState &) = delete;
        LocalState &operator=(const LocalState &) = delete;
    };
    bool texture_storage(GLuint texture, GLenum internal, GLsizei width, GLsizei height) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return hyprimate::gl::allocate_texture_2d(internal, width, height);
    }
}  // namespace

bool CPaperMaterial::initialize(GLuint program) {
    if (m_program != 0) return m_program == program;
    if (program == 0) return false;
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) return false;
    m_canvas = glGetUniformLocation(program, "canvasSize");
    m_center = glGetUniformLocation(program, "center");
    m_unit = glGetUniformLocation(program, "pixelsPerUnit");
    m_uv = glGetUniformLocation(program, "uvTransform");
    m_aspect = glGetUniformLocation(program, "shapeScale");
    m_uv_bounds = glGetUniformLocation(program, "uvBounds");
    m_image = glGetUniformLocation(program, "image");
    m_scene_depth = glGetUniformLocation(program, "sceneDepth");
    m_opacity = glGetUniformLocation(program, "opacity");
    m_shading = glGetUniformLocation(program, "shading");
    m_depth_pass = glGetUniformLocation(program, "depthPass");
    m_light_basis = glGetUniformLocation(program, "lightBasis");
    m_light_bounds = glGetUniformLocation(program, "lightBounds");
    m_light_range = glGetUniformLocation(program, "lightRange");
    m_shadow_size = glGetUniformLocation(program, "shadowSize");
    m_thickness = glGetUniformLocation(program, "thickness");
    m_shadow_strength = glGetUniformLocation(program, "shadowStrength");
    glGenVertexArrays(1, &m_vao);
    if (m_vao == 0) {
        release();
        return false;
    }
    glBindVertexArray(m_vao);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    GLint vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    bool configured = static_cast<GLuint>(vao) == m_vao;
    for (GLuint attribute = 0; attribute < 3; ++attribute) {
        GLint enabled = GL_FALSE;
        glGetVertexAttribiv(attribute, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        configured &= enabled == GL_TRUE;
    }
    if (!configured) {
        release();
        return false;
    }
    m_program = program;
    return true;
}

void CPaperMaterial::release() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_buffer);
    hyprimate::gl::delete_framebuffer(m_framebuffer);
    hyprimate::gl::delete_framebuffer(m_shadow_framebuffer);
    glDeleteTextures(1, &m_color);
    glDeleteTextures(1, &m_depth);
    glDeleteRenderbuffers(1, &m_depth_buffer);
    m_program = m_vao = m_buffer = m_framebuffer = m_shadow_framebuffer = m_color = m_depth =
        m_depth_buffer = 0;
    m_width = m_height = m_shadow_width = m_shadow_height = 0;
    m_capacity = 0;
}

bool CPaperMaterial::upload(std::span<const SPaperVertex> vertices) {
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    const auto BYTES = vertices.size() * sizeof(SPaperVertex);
    if (BYTES > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) return false;
    if (BYTES > m_capacity) {
        const auto LIMIT = static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max());
        const auto CAPACITY = BYTES + std::min(BYTES / 2, LIMIT - BYTES);
        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        if (buffer == 0) return false;
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        if (!hyprimate::gl::allocate_buffer(
                GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(CAPACITY), nullptr, GL_STREAM_DRAW)) {
            glDeleteBuffers(1, &buffer);
            return false;
        }
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SPaperVertex), nullptr);
        glVertexAttribPointer(1,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(SPaperVertex),
            reinterpret_cast<const void *>(offsetof(SPaperVertex, u)));
        glVertexAttribPointer(2,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(SPaperVertex),
            reinterpret_cast<const void *>(offsetof(SPaperVertex, nx)));
        glDeleteBuffers(1, &m_buffer);
        m_buffer = buffer;
        m_capacity = CAPACITY;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(BYTES), vertices.data());
    return true;
}

bool CPaperMaterial::resize(GLsizei width, GLsizei height, bool shaded) {
    // Round in a wider type before narrowing or asking the driver.
    const auto rounded = [](GLsizei value) -> GLsizei {
        if (value <= 0) return 0;
        const auto result = ((static_cast<std::int64_t>(value) + 63) / 64) * 64;
        return result <= std::numeric_limits<GLsizei>::max() ? static_cast<GLsizei>(result) : 0;
    };
    width = rounded(width);
    height = rounded(height);
    if (width == 0 || height == 0) return false;
    const auto SHADOW_WIDTH = shaded ? std::min(1024, width) : 0;
    const auto SHADOW_HEIGHT = shaded ? std::min(1024, height) : 0;
    if (width <= m_width && height <= m_height && SHADOW_WIDTH <= m_shadow_width &&
        SHADOW_HEIGHT <= m_shadow_height)
        return true;
    width = std::max(m_width, width);
    height = std::max(m_height, height);
    GLint texture_limit = 0, renderbuffer_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer_limit);
    if (width > std::min(texture_limit, renderbuffer_limit) ||
        height > std::min(texture_limit, renderbuffer_limit))
        return false;
    struct Pending {
        GLuint framebuffer = 0, shadow_framebuffer = 0, color = 0, depth = 0, depth_buffer = 0;
        ~Pending() {
            hyprimate::gl::delete_framebuffer(framebuffer);
            hyprimate::gl::delete_framebuffer(shadow_framebuffer);
            glDeleteTextures(1, &color);
            glDeleteTextures(1, &depth);
            glDeleteRenderbuffers(1, &depth_buffer);
        }
    } pending;
    const bool SHADOW = SHADOW_WIDTH > m_shadow_width || SHADOW_HEIGHT > m_shadow_height;
    const bool COLOR = width > m_width || height > m_height;
    const auto BACKING_WIDTH = std::max(m_shadow_width, SHADOW_WIDTH);
    const auto BACKING_HEIGHT = std::max(m_shadow_height, SHADOW_HEIGHT);
    glActiveTexture(GL_TEXTURE1);
    if (SHADOW) {
        glGenTextures(1, &pending.depth);
        glGenFramebuffers(1, &pending.shadow_framebuffer);
        if (pending.depth == 0 || pending.shadow_framebuffer == 0 ||
            !texture_storage(pending.depth, GL_DEPTH_COMPONENT24, BACKING_WIDTH, BACKING_HEIGHT))
            return false;
        glBindFramebuffer(GL_FRAMEBUFFER, pending.shadow_framebuffer);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, pending.depth, 0);
        constexpr GLenum NONE = GL_NONE;
        glDrawBuffers(1, &NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    }
    if (COLOR) {
        glGenTextures(1, &pending.color);
        glGenRenderbuffers(1, &pending.depth_buffer);
        glGenFramebuffers(1, &pending.framebuffer);
        if (pending.color == 0 || pending.depth_buffer == 0 || pending.framebuffer == 0 ||
            !texture_storage(pending.color, GL_RGBA8, width, height))
            return false;
        glBindRenderbuffer(GL_RENDERBUFFER, pending.depth_buffer);
        if (!hyprimate::gl::allocate_renderbuffer(GL_DEPTH_COMPONENT24, width, height))
            return false;
        glBindFramebuffer(GL_FRAMEBUFFER, pending.framebuffer);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pending.color, 0);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pending.depth_buffer);
        if (!hyprimate::gl::framebuffer_attachment_is(
                GL_COLOR_ATTACHMENT0, pending.color, GL_TEXTURE) ||
            !hyprimate::gl::framebuffer_attachment_is(
                GL_DEPTH_ATTACHMENT, pending.depth_buffer, GL_RENDERBUFFER) ||
            glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return false;
    }
    // Publish both targets together; pending then destroys only the replaced objects.
    if (SHADOW) {
        std::swap(m_depth, pending.depth);
        std::swap(m_shadow_framebuffer, pending.shadow_framebuffer);
        m_shadow_width = BACKING_WIDTH;
        m_shadow_height = BACKING_HEIGHT;
    }
    if (COLOR) {
        std::swap(m_color, pending.color);
        std::swap(m_depth_buffer, pending.depth_buffer);
        std::swap(m_framebuffer, pending.framebuffer);
        m_width = width;
        m_height = height;
    }
    return true;
}

bool CPaperMaterial::draw(const hyprimate::PaperSurface &surface,
    const SPaperTarget &target,
    GLuint window,
    GLuint output_framebuffer) {
    if (m_program == 0 || window == 0 || window == m_color || window == m_depth ||
        !hyprimate::is_usable(target.source) || !finite(target.canvas) || !finite(target.origin) ||
        !finite(target.texture_origin) || !finite(target.texture_size) || target.canvas.x < 1 ||
        target.canvas.y < 1 || target.canvas.x > 32768 || target.canvas.y > 32768 ||
        target.texture_size.x < 1 || target.texture_size.y < 1 || !finite(surface.size) ||
        surface.size.x <= 0 || surface.size.y <= 0 || !std::isfinite(surface.opacity) ||
        !std::isfinite(surface.shading) || !std::isfinite(surface.thickness) ||
        !std::isfinite(surface.shadow_strength) || !std::isfinite(target.alpha))
        return false;
    if (std::any_of(surface.faces.begin(),
            surface.faces.end(),
            [](const auto &face) {
                return std::any_of(
                    face.crease.begin(),
                    face.crease.end(),
                    [](float value) { return !std::isfinite(value); });
            }))
        return false;
    const bool CASTS_SHADOW =
        surface.shadow_strength > 0 && std::any_of(surface.points.begin(),
                                           surface.points.end(),
                                           [](const auto &point) { return point.position.z > 0; });
    const LocalState STATE(CASTS_SHADOW);
    const auto WIDTH = static_cast<GLsizei>(std::ceil(target.canvas.x));
    const auto HEIGHT = static_cast<GLsizei>(std::ceil(target.canvas.y));
    if (!resize(WIDTH, HEIGHT, surface.shading > 0) || !m_mesh.build(surface) ||
        !upload(m_mesh.vertices()))
        return false;
    glViewport(0, 0, WIDTH, HEIGHT);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearDepthf(1);
    glUseProgram(m_program);
    glUniform2f(m_canvas, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
    const double SHAPE =
        std::sqrt(target.source.w / target.source.h) / std::sqrt(surface.size.x / surface.size.y);
    glUniform2f(m_aspect, static_cast<float>(SHAPE), static_cast<float>(1 / SHAPE));
    const bool SHADED = surface.shading > 0;
    const auto SHADOW_WIDTH = std::min(WIDTH, 1024), SHADOW_HEIGHT = std::min(HEIGHT, 1024);
    if (SHADED) {
        const auto LIGHT = normalized(Vec3{-.32, -.42, 1});
        const auto RIGHT = normalized(Vec3{LIGHT.z, 0, -LIGHT.x});
        const auto UP = cross(LIGHT, RIGHT);
        const std::array<GLfloat, 9> BASIS{static_cast<float>(RIGHT.x),
            static_cast<float>(RIGHT.y),
            static_cast<float>(RIGHT.z),
            static_cast<float>(UP.x),
            static_cast<float>(UP.y),
            static_cast<float>(UP.z),
            static_cast<float>(LIGHT.x),
            static_cast<float>(LIGHT.y),
            static_cast<float>(LIGHT.z)};
        const double INF = std::numeric_limits<double>::infinity();
        Vec3 low{INF, INF, INF}, high{-INF, -INF, -INF};
        for (const auto &vertex : m_mesh.vertices()) {
            const Vec3 POINT{vertex.x * SHAPE, vertex.y / SHAPE, vertex.z};
            const Vec3 PROJECTED{dot(RIGHT, POINT), dot(UP, POINT), dot(LIGHT, POINT)};
            low.x = std::min(low.x, PROJECTED.x);
            low.y = std::min(low.y, PROJECTED.y);
            low.z = std::min(low.z, PROJECTED.z);
            high.x = std::max(high.x, PROJECTED.x);
            high.y = std::max(high.y, PROJECTED.y);
            high.z = std::max(high.z, PROJECTED.z);
        }
        // Fit every physical patch, not the camera crop: hidden/offscreen folds cast.
        const double PAD = std::max(.01, static_cast<double>(surface.thickness) * 4);
        glUniformMatrix3fv(m_light_basis, 1, GL_FALSE, BASIS.data());
        glUniform4f(m_light_bounds,
            static_cast<float>((low.x + high.x) * .5),
            static_cast<float>((low.y + high.y) * .5),
            static_cast<float>((high.x - low.x) * .5 + PAD),
            static_cast<float>((high.y - low.y) * .5 + PAD));
        glUniform2f(
            m_light_range, static_cast<float>(low.z - PAD), static_cast<float>(high.z + PAD));
        glUniform2f(
            m_shadow_size, static_cast<float>(SHADOW_WIDTH), static_cast<float>(SHADOW_HEIGHT));
        glUniform1f(m_thickness, std::max(.0001F, surface.thickness));
    }
    glUniform2f(m_center,
        static_cast<float>(target.source.x + target.source.w * .5 - target.origin.x),
        static_cast<float>(target.source.y + target.source.h * .5 - target.origin.y));
    glUniform1f(
        m_unit, static_cast<float>(std::sqrt(target.source.w) * std::sqrt(target.source.h)));
    const auto &S = target.source;
    const auto &O = target.texture_origin;
    const auto &T = target.texture_size;
    glUniform4f(m_uv,
        static_cast<float>((S.x - O.x) / T.x),
        static_cast<float>((S.y - O.y) / T.y),
        static_cast<float>(S.w / T.x),
        static_cast<float>(S.h / T.y));
    glUniform4f(m_uv_bounds,
        static_cast<float>(std::max(.5, (S.x - O.x + .5)) / T.x),
        static_cast<float>(std::max(.5, (S.y - O.y + .5)) / T.y),
        static_cast<float>(std::min(T.x - .5, S.x + S.w - O.x - .5) / T.x),
        static_cast<float>(std::min(T.y - .5, S.y + S.h - O.y - .5) / T.y));
    glUniform1f(m_opacity, std::clamp(surface.opacity * target.alpha, 0.F, 1.F));
    glUniform1f(m_shading, std::clamp(surface.shading, 0.F, 1.F));
    glUniform1i(m_image, 0);
    glUniform1i(m_scene_depth, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, window);
    glBindSampler(0, 0);
    glActiveTexture(GL_TEXTURE1);
    // Never sample an attached depth texture, even in the depth-only branch: GLES
    // considers that a framebuffer feedback loop. Bind it only for the colour pass.
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindSampler(1, 0);
    glEnable(GL_SCISSOR_TEST);
    if (SHADED) {
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glScissor(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_framebuffer);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUniform1i(m_depth_pass, 1);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_mesh.vertices().size()));
    }
    glViewport(0, 0, WIDTH, HEIGHT);
    glScissor(0, 0, WIDTH, HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (CASTS_SHADOW) {
        glEnable(GL_BLEND);
        // Max coverage is order-independent across overlapping folds and shared patches.
        glBlendEquation(GL_MAX);
        glDisable(GL_DEPTH_TEST);
        glUniform1i(m_depth_pass, 2);
        glUniform1f(m_shadow_strength,
            static_cast<float>(std::clamp(surface.shadow_strength, 0.0, 1.0)) * .18F);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_mesh.vertices().size()));
        // Only the nearest sheet fragment owns a texel. Blending translucent
        // folds here would bring back the hidden copy when draw order changes.
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }
    glUniform1i(m_depth_pass, 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_mesh.vertices().size()));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, output_framebuffer);
    glBlitFramebuffer(0,
        0,
        WIDTH,
        HEIGHT,
        0,
        target.output_top_first ? HEIGHT : 0,
        WIDTH,
        target.output_top_first ? 0 : HEIGHT,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);
    return true;
}
