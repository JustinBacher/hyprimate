// Raster GPGPU sequence adapted from SLIBS gpgpu_gles.h, ©2017–2023
// Yuichiro Nakada, pinned at ce1a1bf1186d201fc600b1d34bbd1f9ef217b143.
// The upstream README identifies that file as public domain. See
// THIRD_PARTY_NOTICES.md for provenance and the adaptation mapping.
#include "Backend.hpp"
#include "KernelSource.hpp"
#include "../gl/ShaderCompile.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace hyprimate::compute::detail {
    namespace {
        constexpr GLenum SAMPLE_MASK = 0x8E51; // GLES 3.1
        constexpr GLenum MAX_CLIP_DISTANCES = 0x0D32;
        constexpr GLenum CLIP_DISTANCE0 = 0x3000;
        constexpr std::array<GLenum, 9> RASTER_CAPS{
            GL_STENCIL_TEST, GL_DEPTH_TEST, GL_CULL_FACE,
            GL_RASTERIZER_DISCARD, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE,
            GL_POLYGON_OFFSET_FILL, GL_PRIMITIVE_RESTART_FIXED_INDEX, GL_DITHER};

        bool has_extension(std::span<const std::string_view> extensions, std::string_view name) {
            return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
        }
        unsigned limit(GLenum name) {
            return static_cast<unsigned>(std::max(integer(name), 0));
        }
        void set_enabled(GLenum cap, bool enabled) {
            if (enabled) glEnable(cap);
            else glDisable(cap);
        }
        struct ColorState {
            std::array<GLboolean, 4> mask{};
            bool blend{};
        };
        struct IndexedApi {
            using Set = void (GL_APIENTRYP)(GLenum, GLuint);
            using Enabled = GLboolean (GL_APIENTRYP)(GLenum, GLuint);
            using Mask = void (GL_APIENTRYP)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);
            Set enable{}, disable{};
            Enabled is_enabled{};
            Mask color_mask{};

            bool load(const GlResolver& resolver, std::string_view suffix) {
                if (!resolver.resolve) return false;
                const auto get = [&](std::string_view name) {
                    const auto full = std::string(name) + std::string(suffix);
                    return resolver.resolve(full.c_str(), resolver.owner);
                };
                enable = reinterpret_cast<Set>(get("glEnablei"));
                disable = reinterpret_cast<Set>(get("glDisablei"));
                is_enabled = reinterpret_cast<Enabled>(get("glIsEnabledi"));
                color_mask = reinterpret_cast<Mask>(get("glColorMaski"));
                return enable && disable && is_enabled && color_mask;
            }
            explicit operator bool() const { return is_enabled != nullptr; }
        };
        struct ViewportApi {
            using Get = void (GL_APIENTRYP)(GLenum, GLuint, GLfloat*);
            using Set = void (GL_APIENTRYP)(GLuint, const GLfloat*);
            Get get{};
            Set set{};
            IndexedApi::Set enable{}, disable{};
            IndexedApi::Enabled is_enabled{};

            bool load(const GlResolver& resolver, std::string_view suffix, const IndexedApi* shared) {
                if (!resolver.resolve) return false;
                const auto get_proc = [&](std::string_view name) {
                    const auto full = std::string(name) + std::string(suffix);
                    return resolver.resolve(full.c_str(), resolver.owner);
                };
                get = reinterpret_cast<Get>(get_proc("glGetFloati_v"));
                set = reinterpret_cast<Set>(get_proc("glViewportIndexedfv"));
                if (shared && *shared) {
                    enable = shared->enable;
                    disable = shared->disable;
                    is_enabled = shared->is_enabled;
                } else {
                    enable = reinterpret_cast<IndexedApi::Set>(get_proc("glEnablei"));
                    disable = reinterpret_cast<IndexedApi::Set>(get_proc("glDisablei"));
                    is_enabled = reinterpret_cast<IndexedApi::Enabled>(get_proc("glIsEnabledi"));
                }
                return get && set && enable && disable && is_enabled;
            }
        };
        constexpr std::string_view VERTEX_SOURCE = R"glsl(#version 300 es
precision highp float;
precision highp int;
void main() {
    highp vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";
    }

    struct RasterBackend {
        GLuint framebuffer{}, vao{};
        unsigned max_outputs{}, max_width{}, max_height{}, touched{};
        IndexedApi indexed;
        ViewportApi viewport_api;
        bool sample_mask_supported{}, sample_mask{}, scissor{}, active{};
        std::vector<ColorState> colors;
        std::array<GLboolean, RASTER_CAPS.size()> caps{};
        std::vector<GLboolean> clips;
        std::vector<GLenum> draw_buffers;
        std::array<GLint, 4> viewport{};
        std::array<GLfloat, 4> indexed_viewport{};
        GLint draw_framebuffer{}, vertex_array{};
    };

    void RasterDeleter::operator()(RasterBackend* backend) const noexcept { delete backend; }

    Result<RasterPointer> configure_raster(const GlResolver& resolver, Capabilities& caps,
                                           std::span<const std::string_view> extensions) {
        caps.slibs = {};
        auto fail = [&](std::string message) -> Result<RasterPointer> {
            caps.slibs.available = false;
            caps.slibs.reason = std::move(message);
            return std::unexpected(caps.slibs.reason);
        };
        if (caps.major < 3) return fail("SLIBS raster requires OpenGL ES 3.0 or newer");
        const auto draw_buffers = limit(GL_MAX_DRAW_BUFFERS);
        const auto attachments = limit(GL_MAX_COLOR_ATTACHMENTS);
        caps.slibs.inputs = std::min(limit(GL_MAX_TEXTURE_IMAGE_UNITS), caps.combined_samplers);
        caps.slibs.outputs = std::min(draw_buffers, attachments);
        caps.slibs.parameter_components = limit(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS);
        caps.slibs.combined_output_resources = caps.slibs.outputs;
        std::array<GLint, 2> viewport{};
        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport.data());
        const auto max_width = std::min({caps.width, limit(GL_MAX_TEXTURE_SIZE), MAX_EXTENT,
                                         static_cast<unsigned>(std::max(viewport[0], 0))});
        const auto max_height = std::min({caps.height, limit(GL_MAX_TEXTURE_SIZE), MAX_EXTENT,
                                          static_cast<unsigned>(std::max(viewport[1], 0))});
        if (!caps.slibs.outputs || !caps.slibs.parameter_components || !max_width || !max_height)
            return fail("SLIBS raster has no usable fragment, framebuffer, texture or viewport limits");

        RasterPointer backend(new RasterBackend);
        backend->max_outputs = caps.slibs.outputs;
        backend->max_width = max_width;
        backend->max_height = max_height;
        backend->colors.resize(backend->max_outputs);
        backend->draw_buffers.resize(backend->max_outputs);
        for (unsigned i = 0; i < backend->max_outputs; ++i)
            backend->draw_buffers[i] = GL_COLOR_ATTACHMENT0 + i;
        backend->sample_mask_supported = caps.major > 3 || (caps.major == 3 && caps.minor >= 1);
        const bool core_indexed = caps.major > 3 || (caps.major == 3 && caps.minor >= 2);
        const bool ext_indexed = has_extension(extensions, "GL_EXT_draw_buffers_indexed");
        const bool oes_indexed = has_extension(extensions, "GL_OES_draw_buffers_indexed");
        if ((core_indexed || ext_indexed || oes_indexed) &&
            !(core_indexed && backend->indexed.load(resolver, "")) &&
            !(ext_indexed && backend->indexed.load(resolver, "EXT")) &&
            !(oes_indexed && backend->indexed.load(resolver, "OES")))
            return fail("SLIBS raster cannot resolve advertised indexed blend and color-mask state functions");
        const bool oes_viewport = has_extension(extensions, "GL_OES_viewport_array");
        const bool nv_viewport = has_extension(extensions, "GL_NV_viewport_array");
        if ((oes_viewport || nv_viewport) &&
            !(oes_viewport && backend->viewport_api.load(resolver, "OES", &backend->indexed)) &&
            !(nv_viewport && backend->viewport_api.load(resolver, "NV", nullptr)))
            return fail("SLIBS raster cannot resolve advertised indexed viewport and scissor state functions");
        if (has_extension(extensions, "GL_EXT_clip_cull_distance") ||
            has_extension(extensions, "GL_APPLE_clip_distance")) {
            const auto count = limit(MAX_CLIP_DISTANCES);
            // The exposed tokens form a contiguous range starting at CLIP_DISTANCE0.
            if (!count) return fail("SLIBS raster cannot query advertised clip-distance limit");
            backend->clips.resize(count);
        }
        glGenFramebuffers(1, &backend->framebuffer);
        glGenVertexArrays(1, &backend->vao);
        if (!backend->framebuffer || !backend->vao) {
            release_raster(*backend);
            return fail("SLIBS raster could not allocate its framebuffer and vertex array");
        }
        caps.slibs.available = true;
        return backend;
    }

    void release_raster(RasterBackend& backend) noexcept {
        if (backend.framebuffer) glDeleteFramebuffers(1, &backend.framebuffer);
        if (backend.vao) glDeleteVertexArrays(1, &backend.vao);
        backend.framebuffer = 0;
        backend.vao = 0;
    }

    Result<GLuint> prepare_raster(const KernelDesc& desc) {
        std::string error;
        const GLuint vertex = gl::compile_shader(GL_VERTEX_SHADER, VERTEX_SOURCE, error);
        if (!vertex) return std::unexpected("kernel '" + desc.name + "' SLIBS " + error);
        const auto source = kernel_source(desc, Backend::Slibs);
        const GLuint fragment = gl::compile_shader(GL_FRAGMENT_SHADER, source, error);
        if (!fragment) {
            glDeleteShader(vertex);
            return std::unexpected("kernel '" + desc.name + "' SLIBS " + error);
        }
        const GLuint program = glCreateProgram();
        if (!program) {
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            return std::unexpected("kernel '" + desc.name + "' SLIBS link: cannot create program");
        }
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint status = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (status != GL_TRUE) {
            error = gl::shader_log(program, true);
            glDeleteProgram(program);
            return std::unexpected("kernel '" + desc.name + "' SLIBS link: " + error);
        }
        return program;
    }

    void begin_raster(RasterBackend& backend, unsigned outputs) {
        backend.active = true;
        backend.touched = std::min(outputs, backend.max_outputs);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &backend.draw_framebuffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &backend.vertex_array);
        if (backend.viewport_api.get) {
            backend.viewport_api.get(GL_VIEWPORT, 0, backend.indexed_viewport.data());
            backend.scissor = backend.viewport_api.is_enabled(GL_SCISSOR_TEST, 0) != GL_FALSE;
            backend.viewport_api.disable(GL_SCISSOR_TEST, 0);
        } else {
            glGetIntegerv(GL_VIEWPORT, backend.viewport.data());
            backend.scissor = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
            glDisable(GL_SCISSOR_TEST);
        }
        for (std::size_t i = 0; i < RASTER_CAPS.size(); ++i) {
            backend.caps[i] = glIsEnabled(RASTER_CAPS[i]);
            glDisable(RASTER_CAPS[i]);
        }
        if (backend.sample_mask_supported) {
            backend.sample_mask = glIsEnabled(SAMPLE_MASK) != GL_FALSE;
            glDisable(SAMPLE_MASK);
        }
        for (std::size_t i = 0; i < backend.clips.size(); ++i) {
            const auto cap = CLIP_DISTANCE0 + static_cast<GLenum>(i);
            backend.clips[i] = glIsEnabled(cap);
            glDisable(cap);
        }
        if (backend.indexed) {
            for (unsigned i = 0; i < backend.touched; ++i) {
                auto& color = backend.colors[i];
                std::array<GLint, 4> mask{};
                glGetIntegeri_v(GL_COLOR_WRITEMASK, i, mask.data());
                for (std::size_t c = 0; c < mask.size(); ++c) color.mask[c] = mask[c] != 0;
                color.blend = backend.indexed.is_enabled(GL_BLEND, i) != GL_FALSE;
                backend.indexed.color_mask(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                backend.indexed.disable(GL_BLEND, i);
            }
        } else if (backend.touched) {
            auto& color = backend.colors[0];
            glGetBooleanv(GL_COLOR_WRITEMASK, color.mask.data());
            color.blend = glIsEnabled(GL_BLEND) != GL_FALSE;
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    void end_raster(RasterBackend& backend) noexcept {
        if (!backend.active) return;
        if (backend.indexed) {
            for (unsigned i = 0; i < backend.touched; ++i) {
                const auto& color = backend.colors[i];
                backend.indexed.color_mask(i, color.mask[0], color.mask[1], color.mask[2], color.mask[3]);
                (color.blend ? backend.indexed.enable : backend.indexed.disable)(GL_BLEND, i);
            }
        } else if (backend.touched) {
            const auto& color = backend.colors[0];
            glColorMask(color.mask[0], color.mask[1], color.mask[2], color.mask[3]);
            set_enabled(GL_BLEND, color.blend);
        }
        for (std::size_t i = 0; i < RASTER_CAPS.size(); ++i)
            set_enabled(RASTER_CAPS[i], backend.caps[i] != GL_FALSE);
        if (backend.viewport_api.get)
            (backend.scissor ? backend.viewport_api.enable : backend.viewport_api.disable)(GL_SCISSOR_TEST, 0);
        else
            set_enabled(GL_SCISSOR_TEST, backend.scissor);
        if (backend.sample_mask_supported) set_enabled(SAMPLE_MASK, backend.sample_mask);
        for (std::size_t i = 0; i < backend.clips.size(); ++i)
            set_enabled(CLIP_DISTANCE0 + static_cast<GLenum>(i), backend.clips[i] != GL_FALSE);
        if (backend.viewport_api.get)
            backend.viewport_api.set(0, backend.indexed_viewport.data());
        else
            glViewport(backend.viewport[0], backend.viewport[1], backend.viewport[2], backend.viewport[3]);
        glBindVertexArray(static_cast<GLuint>(backend.vertex_array));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(backend.draw_framebuffer));
        backend.active = false;
        backend.touched = 0;
    }

    Result<void> submit_raster(RasterBackend& backend, Layout layout, std::span<const OutputBinding> outputs) {
        if (!backend.active || outputs.empty() || outputs.size() > backend.touched)
            return std::unexpected("SLIBS raster submission exceeds its active output scope");
        if (!layout.width || !layout.height || layout.width > backend.max_width || layout.height > backend.max_height ||
            layout.width > static_cast<unsigned>(std::numeric_limits<GLsizei>::max()) ||
            layout.height > static_cast<unsigned>(std::numeric_limits<GLsizei>::max()))
            return std::unexpected("SLIBS raster submission exceeds its framebuffer viewport limits");
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            const auto& output = outputs[i];
            if (!output.texture || glIsTexture(output.texture) != GL_TRUE ||
                (output.format != GL_R32I && output.format != GL_RGBA32I &&
                 output.format != GL_R32UI && output.format != GL_RGBA32UI))
                return std::unexpected("SLIBS raster submission has an invalid output texture or format");
            for (std::size_t j = 0; j < i; ++j)
                if (output.texture == outputs[j].texture)
                    return std::unexpected("SLIBS raster submission aliases output textures");
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, backend.framebuffer);
        for (std::size_t i = 0; i < outputs.size(); ++i)
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                                   GL_TEXTURE_2D, outputs[i].texture, 0);
        glDrawBuffers(static_cast<GLsizei>(outputs.size()), backend.draw_buffers.data());
        const GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        if (status == GL_FRAMEBUFFER_COMPLETE) {
            glBindVertexArray(backend.vao);
            if (backend.viewport_api.get) {
                const std::array<GLfloat, 4> viewport{0, 0, static_cast<GLfloat>(layout.width),
                                                      static_cast<GLfloat>(layout.height)};
                backend.viewport_api.set(0, viewport.data());
            } else {
                glViewport(0, 0, static_cast<GLsizei>(layout.width), static_cast<GLsizei>(layout.height));
            }
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        // Do not let the private FBO retain names after a field is returned to the pool.
        for (std::size_t i = 0; i < outputs.size(); ++i)
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i),
                                   GL_TEXTURE_2D, 0, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            return std::unexpected("SLIBS raster output framebuffer incomplete (status " + std::to_string(status) + ")");
        return {};
    }
}
