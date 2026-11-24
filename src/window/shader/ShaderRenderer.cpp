#include "window/shader/ShaderRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <EGL/egl.h>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/SceneResources.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "gl/CompositorContext.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "shader/GlesRuntime.hpp"

namespace hyprimate::shader {
    namespace {
        constexpr unsigned MAX_EDGE = 8192;
        constexpr unsigned MAX_PIXELS = 16'777'216;
        using Image = CHyprAnimationCanvas::Image;

        struct Extent {
            unsigned width, height;
            bool operator==(const Extent &) const = default;
        };

        std::optional<Extent> extent(double width, double height, unsigned max_pixels = MAX_PIXELS) noexcept {
            if (!(width >= 1 && height >= 1 && width <= MAX_EDGE && height <= MAX_EDGE &&
                    width * height <= max_pixels && std::floor(width) == width &&
                    std::floor(height) == height)) {
                return std::nullopt;
            }
            return Extent{static_cast<unsigned>(width), static_cast<unsigned>(height)};
        }

        bool encloses(const Box &outer, const Box &inner) noexcept {
            constexpr double EPS = 1e-4;
            return inner.x >= outer.x - EPS && inner.y >= outer.y - EPS &&
                   inner.x + inner.w <= outer.x + outer.w + EPS &&
                   inner.y + inner.h <= outer.y + outer.h + EPS;
        }

        bool same_box(const Box &a, const Box &b) noexcept {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

        std::array<float, 4> relative(const Box &area, const Box &reference) noexcept {
            return {static_cast<float>((area.x - reference.x) / reference.w),
                static_cast<float>((area.y - reference.y) / reference.h),
                static_cast<float>(area.w / reference.w), static_cast<float>(area.h / reference.h)};
        }

        bool fail(std::string &error, const char *message) {
            error = message;
            return false;
        }

        Render::GL::CGLFramebuffer *gl_framebuffer(const Image &image) {
            return image ? dynamic_cast<Render::GL::CGLFramebuffer *>(image.get()) : nullptr;
        }

        // Blits ignore blend/color masks. Preserve only the bindings and scissor
        // that this copy changes; an ordinary drawing bracket would overwrite
        // indexed host blend/mask state unnecessarily.
        struct CopyBindings {
            GLint texture = 0, read = 0, draw = 0;
            bool scissor = false;
            CopyBindings() {
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
                scissor = glIsEnabled(GL_SCISSOR_TEST);
                glDisable(GL_SCISSOR_TEST);
            }
            ~CopyBindings() {
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
                if (scissor) glEnable(GL_SCISSOR_TEST);
                else glDisable(GL_SCISSOR_TEST);
            }
        };
    }

    class WindowProgram {
      public:
        ~WindowProgram() {
            const gl::ScopedCompositorContext context{owner};
            if (context) program.release();
            else program.abandon();
        }

        gl::CompositorContextOwner owner;
        Program program;
    };

    std::shared_ptr<WindowProgram> prepare_window_program(Event event, const std::string &path,
        const std::string &parameters, std::string &error) {
        auto package = load_package(path);
        if (!package) {
            error = std::move(package.error());
            return {};
        }
        if (!package->supports(event)) {
            error = "effect does not declare this event";
            return {};
        }
        auto values = parameter_values(*package, parameters);
        if (!values) {
            error = std::move(values.error());
            return {};
        }
        const gl::ScopedCompositorContext context;
        if (!context) {
            error = "no usable compositor GLES context";
            return {};
        }
        auto program = std::make_shared<WindowProgram>();
        if (!program->owner.ensure_current()) {
            error = "no usable compositor GLES context";
            return {};
        }
        if (!program->program.initialize(std::move(*package), *values, error, eglGetProcAddress)) {
            return {};
        }
        return program;
    }

    struct WindowRenderer::Impl {
        explicit Impl(std::shared_ptr<WindowProgram> prepared) : program(std::move(prepared)) {}
        std::shared_ptr<WindowProgram> program;
        Instance instance;
        std::optional<Extent> size;
        // Only individual source identity is retained, never a second ownership
        // reference to the host's snapshot. Its lease is owned by the window port.
        const Render::IFramebuffer *close_image = nullptr;
        GLuint close_image_name = 0, close_copy = 0;
        Box close_box{};
        Source close_source{};
    };

    WindowRenderer::WindowRenderer(std::shared_ptr<WindowProgram> program)
        : m_impl(std::make_unique<Impl>(std::move(program))) {}

    WindowRenderer::~WindowRenderer() {
        const gl::ScopedCompositorContext context{m_impl->program->owner};
        if (context) {
            m_impl->instance.release();
            if (m_impl->close_copy) glDeleteTextures(1, &m_impl->close_copy);
        } else {
            m_impl->instance.abandon();
        }
    }

    Box WindowRenderer::bounds(const Box &capture) const noexcept {
        const auto &pad = m_impl->program->program.package().bounds;
        return {capture.x - pad[0] * capture.w, capture.y - pad[1] * capture.h,
            capture.w * (1 + pad[0] + pad[2]), capture.h * (1 + pad[1] + pad[3])};
    }

    bool WindowRenderer::resize(const Box &capture, double scale, std::string &error) {
        const auto size = extent(std::ceil(capture.w * scale), std::ceil(capture.h * scale));
        if (!size || !is_usable(capture) || !(scale > 0) || !std::isfinite(scale)) {
            return fail(error, "capture dimensions exceed native shader limits");
        }
        if (size == m_impl->size) return true;
        const gl::ScopedCompositorContext context{m_impl->program->owner};
        if (!context) return fail(error, "compositor GLES context changed");
        if (!m_impl->instance.initialize(m_impl->program->program, size->width, size->height, error)) {
            return false;
        }
        m_impl->size = size;
        return true;
    }

    bool WindowRenderer::bind_close_source(const Box &source, const Image &image, std::string &error) {
        const gl::ScopedCompositorContext context{m_impl->program->owner};
        if (!context) return fail(error, "compositor GLES context changed");
        auto *framebuffer = gl_framebuffer(image);
        if (!framebuffer || !image->isAllocated() || image->getMirrorTexture() ||
            (image->m_drmFormat != DRM_FORMAT_ABGR8888 && image->m_drmFormat != DRM_FORMAT_ABGR16161616F)) {
            return fail(error, "unsupported native close snapshot framebuffer");
        }
        const auto texture = image->getTexture();
        if (!texture || !texture->m_texID || texture->m_type != Render::TEXTURE_RGBA ||
            texture->m_transform != HYPRUTILS_TRANSFORM_NORMAL || !is_usable(source)) {
            return fail(error, "unsupported native close snapshot texture");
        }
        const Box backing{0, 0, texture->m_size.x, texture->m_size.y};
        const auto size = extent(backing.w, backing.h, MAX_EDGE * MAX_EDGE);
        if (!size || image->m_size != texture->m_size) return fail(error, "invalid snapshot dimensions");
        if (m_impl->close_image) return fail(error, "native close snapshot already bound");
        const auto output = bounds(source);
        const double visible_width = std::min(backing.w, std::ceil(output.x + output.w)) -
                                     std::max(0.0, std::floor(output.x));
        const double visible_height = std::min(backing.h, std::ceil(output.y + output.h)) -
                                      std::max(0.0, std::floor(output.y));
        if (!extent(visible_width, visible_height)) return fail(error, "close output exceeds native shader limits");

        Source isolated{texture->m_texID, size->width, size->height};
        std::size_t copy_bytes = 0;
        if (!same_box(source, backing)) {
            // Raw GLSL can ignore hypr_source_uv. Only a fully captured, isolated
            // window rectangle can be exposed to the external program.
            const auto x = std::floor(source.x), y = std::floor(source.y);
            const Box crop{x, y, std::ceil(source.x + source.w) - x, std::ceil(source.y + source.h) - y};
            const auto cropped = extent(crop.w, crop.h);
            if (!encloses(backing, crop)) {
                return fail(error, "native close snapshot does not contain the complete window");
            }
            const auto left = std::max(crop.x, backing.x), top = std::max(crop.y, backing.y);
            const auto right = std::min(crop.x + crop.w, backing.w);
            const auto bottom = std::min(crop.y + crop.h, backing.h);
            const bool fp16 = image->m_drmFormat == DRM_FORMAT_ABGR16161616F;
            if (!cropped || !(right > left && bottom > top) ||
                std::size_t(cropped->width) * cropped->height * (fp16 ? 8 : 4) +
                    m_impl->instance.statistics().allocated_bytes > MAX_INSTANCE_BYTES) {
                return fail(error, "close crop exceeds native shader limits");
            }

            GLboolean feedback_active = GL_FALSE, feedback_paused = GL_FALSE;
            glGetBooleanv(GL_TRANSFORM_FEEDBACK_ACTIVE, &feedback_active);
            glGetBooleanv(GL_TRANSFORM_FEEDBACK_PAUSED, &feedback_paused);
            if (feedback_active && !feedback_paused) {
                return fail(error, "native close snapshot cannot be isolated during active transform feedback");
            }
            const CopyBindings bindings;
            const auto source_fbo = framebuffer->getFBID();
            if (!source_fbo || glGetError() != GL_NO_ERROR) {
                return fail(error, "snapshot framebuffer unavailable for isolation");
            }
            GLuint copy = 0, copy_fbo = 0;
            glGenTextures(1, &copy);
            glGenFramebuffers(1, &copy_fbo);
            glBindTexture(GL_TEXTURE_2D, copy);
            glTexStorage2D(GL_TEXTURE_2D, 1, fp16 ? GL_RGBA16F : GL_RGBA8,
                static_cast<GLsizei>(cropped->width), static_cast<GLsizei>(cropped->height));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copy_fbo);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy, 0);
            bool success = copy && copy_fbo &&
                glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (success) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, source_fbo);
                GLint read = 0;
                glGetIntegerv(GL_READ_BUFFER, &read);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                glBlitFramebuffer(static_cast<GLint>(left), static_cast<GLint>(top),
                    static_cast<GLint>(right), static_cast<GLint>(bottom),
                    static_cast<GLint>(left - x), static_cast<GLint>(top - y),
                    static_cast<GLint>(right - x), static_cast<GLint>(bottom - y),
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glReadBuffer(static_cast<GLenum>(read));
            }
            success = glGetError() == GL_NO_ERROR && success;
            glDeleteFramebuffers(1, &copy_fbo);
            if (!success) {
                glDeleteTextures(1, &copy);
                return fail(error, "could not isolate the native close snapshot");
            }
            m_impl->close_copy = copy;
            copy_bytes = static_cast<std::size_t>(right - left) * static_cast<std::size_t>(bottom - top) *
                (fp16 ? 8U : 4U);
            isolated = {copy, cropped->width, cropped->height, relative(source, crop)};
        }
        try {
            LOG(Log::DEBUG, "[hyprimate] shader close source copies={} copy_bytes={}",
                m_impl->close_copy ? 1 : 0, copy_bytes);
        } catch (const std::bad_alloc &) {}
        m_impl->close_image = image.get();
        m_impl->close_image_name = texture->m_texID;
        m_impl->close_box = source;
        m_impl->close_source = isolated;
        return true;
    }

    void WindowRenderer::advance(const Frame &frame) noexcept {
        m_impl->instance.advance(frame);
    }

    bool WindowRenderer::draw(const animation::Draw<CHyprAnimationCanvas> &draw, bool closing,
        std::string &error) {
        if (!draw.canvas || !draw.image || !*draw.image || !g_pHyprRenderer ||
            !m_impl->program->owner.ensure_current() || !is_usable(draw.source) ||
            !is_usable(draw.canvas_box)) {
            return fail(error, "unavailable native shader draw source or context");
        }
        auto &ctx = draw.canvas->context();
        const auto monitor = ctx.m_data.pMonitor.lock();
        if (!monitor || !monitor->resources()) return fail(error, "no compositor work buffer owner");
        const auto &input = *draw.image;
        const auto image = input->getTexture();
        if (!image || !image->m_texID || image->m_transform != HYPRUTILS_TRANSFORM_NORMAL ||
            (image->m_type != Render::TEXTURE_RGBA && image->m_type != Render::TEXTURE_RGBX) ||
            !ctx.shouldRenderContent(false)) {
            return fail(error, "unsupported framebuffer source for native shader");
        }
        const auto output_size = extent(draw.canvas_box.w, draw.canvas_box.h);
        if (!output_size) return fail(error, "shader output dimensions exceed limits");

        Source source;
        if (closing) {
            if (input.get() != m_impl->close_image || image->m_texID != m_impl->close_image_name ||
                !same_box(draw.source, m_impl->close_box)) {
                return fail(error, "native close snapshot identity or placement changed");
            }
            source = m_impl->close_source;
        } else {
            // The mapped window transformer supplies Hyprland's clear + window-only
            // raster lease. Its rounded padding is transparent, not adjacent content.
            const Box backing{draw.image_origin.x, draw.image_origin.y, image->m_size.x, image->m_size.y};
            const auto size = extent(backing.w, backing.h);
            if (!size || !encloses(backing, draw.source) || !gl_framebuffer(input) ||
                input->m_size != image->m_size || draw.image_size.x != backing.w || draw.image_size.y != backing.h) {
                return fail(error, "incompatible mapped window raster extent");
            }
            source = {image->m_texID, size->width, size->height, relative(draw.source, backing)};
        }

        const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
        const auto state = ctx.saveDrawState();
        const Vector2D size{static_cast<double>(output_size->width), static_cast<double>(output_size->height)};
        Image layer;
        if (closing) {
            const auto resources = monitor->resources();
            if (resources) {
                const gl::FramebufferAllocationState allocation;
                layer = resources->getUnusedWorkBuffer(input->m_drmFormat, image->m_imageDescription, size);
                if (ctx.sceneResources()) layer = ctx.sceneResources()->prepareWorkBuffer(std::move(layer));
            }
        } else {
            layer = gl::get_work_buffer(ctx, size);
        }
        auto *framebuffer = gl_framebuffer(layer);
        if (!framebuffer || layer == input || layer == ctx.m_data.currentFB || !layer->isAllocated()) {
            return fail(error, "no independent compositor work framebuffer for shader output");
        }
        const auto texture = layer->getTexture();
        const auto current = ctx.m_data.currentFB ? ctx.m_data.currentFB->getTexture() : nullptr;
        if (!texture || !texture->m_texID || texture->m_type != Render::TEXTURE_RGBA ||
            texture->m_texID == source.texture || (current && current->m_texID == texture->m_texID) ||
            texture->m_size != size) {
            return fail(error, "unexpected native shader output framebuffer texture");
        }
        const auto id = framebuffer->getFBID();
        if (!id) return fail(error, "missing compositor work framebuffer binding");
        const Target target{
            .framebuffer = id, .texture = texture->m_texID,
            .width = output_size->width, .height = output_size->height,
            .domain = relative(draw.canvas_box, draw.source),
            .clear_before_render = true,
        };
        const bool live = draw.real_pass && !draw.canvas->captures_pixels() && !draw.canvas->blur_mask();
        if (!m_impl->instance.render(source, target, live, error)) return false;
        draw.canvas->layer() = std::move(layer);
        return true;
    }
}  // namespace hyprimate::shader
