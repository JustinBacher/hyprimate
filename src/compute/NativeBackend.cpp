#include "Backend.hpp"
#include "KernelSource.hpp"
#include "../gl/ShaderCompile.hpp"
#include "../gl/Storage.hpp"

#include <array>
#include <algorithm>
#include <cstdint>

namespace hyprimate::compute::detail {
    namespace {
        // Keep ES 3.1 entry points dynamically resolved; the rest of the project
        // only needs the GLES 3.0 headers and library symbols.
        constexpr GLenum COMPUTE_SHADER = 0x91B9;
        constexpr GLenum MAX_COMPUTE_TEXTURE_IMAGE_UNITS = 0x91BC;
        constexpr GLenum MAX_COMPUTE_IMAGE_UNIFORMS = 0x91BD;
        constexpr GLenum MAX_COMPUTE_WORK_GROUP_COUNT = 0x91BE;
        constexpr GLenum MAX_COMPUTE_WORK_GROUP_SIZE = 0x91BF;
        constexpr GLenum MAX_COMPUTE_UNIFORM_COMPONENTS = 0x8263;
        constexpr GLenum MAX_COMPUTE_WORK_GROUP_INVOCATIONS = 0x90EB;
        constexpr GLenum MAX_IMAGE_UNITS = 0x8F38;
        constexpr GLenum MAX_COMBINED_IMAGE_UNIFORMS = 0x90CF;
        constexpr GLenum MAX_COMBINED_SHADER_OUTPUT_RESOURCES = 0x8F39;
        constexpr GLenum IMAGE_BINDING_NAME = 0x8F3A;
        constexpr GLenum IMAGE_BINDING_LEVEL = 0x8F3B;
        constexpr GLenum IMAGE_BINDING_LAYERED = 0x8F3C;
        constexpr GLenum IMAGE_BINDING_LAYER = 0x8F3D;
        constexpr GLenum IMAGE_BINDING_ACCESS = 0x8F3E;
        constexpr GLenum IMAGE_BINDING_FORMAT = 0x906E;
        constexpr GLenum WRITE_ONLY = 0x88B9;

        unsigned limit(GLenum name) {
            const GLint value = integer(name);
            return value > 0 ? static_cast<unsigned>(value) : 0;
        }

        unsigned indexed_limit(GLenum name, GLuint index) {
            GLint value = 0;
            glGetIntegeri_v(name, index, &value);
            return value > 0 ? static_cast<unsigned>(value) : 0;
        }

        bool valid_format(GLenum value) {
            return value == GL_R32I || value == GL_RGBA32I || value == GL_R32UI || value == GL_RGBA32UI;
        }

        bool matches_output(const ImageBinding& actual, const OutputBinding& expected) {
            return static_cast<GLuint>(actual.name) == expected.texture && actual.level == 0 &&
                   actual.layered == GL_FALSE && actual.layer == 0 &&
                   actual.access == static_cast<GLint>(WRITE_ONLY) && actual.format == static_cast<GLint>(expected.format);
        }
    }

    Result<void> NativeBackend::configure(const GlResolver& resolver, Capabilities& caps) {
        dispatch = nullptr;
        bind_image = nullptr;
        barrier = nullptr;
        caps.native = {};
        caps.image_units = 0;
        caps.group_count = {};
        caps.local_size = {};
        caps.invocations = 0;
        const auto fail = [&](std::string message) -> Result<void> {
            caps.native.reason = std::move(message);
            return std::unexpected(caps.native.reason);
        };
        if (caps.major < 3 || (caps.major == 3 && caps.minor < 1))
            return fail("native compute requires OpenGL ES 3.1 or later");
        if (!resolver.resolve) return fail("native compute has no GL procedure resolver");

        dispatch = reinterpret_cast<Dispatch>(resolver.resolve("glDispatchCompute", resolver.owner));
        bind_image = reinterpret_cast<BindImage>(resolver.resolve("glBindImageTexture", resolver.owner));
        barrier = reinterpret_cast<Barrier>(resolver.resolve("glMemoryBarrier", resolver.owner));
        if (!dispatch || !bind_image || !barrier) {
            dispatch = nullptr;
            bind_image = nullptr;
            barrier = nullptr;
            return fail("native compute requires glDispatchCompute, glBindImageTexture and glMemoryBarrier");
        }

        caps.combined_samplers = limit(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
        caps.image_units = limit(MAX_IMAGE_UNITS);
        caps.native.inputs = std::min(limit(MAX_COMPUTE_TEXTURE_IMAGE_UNITS), caps.combined_samplers);
        caps.native.outputs = std::min({limit(MAX_COMPUTE_IMAGE_UNIFORMS), limit(MAX_COMBINED_IMAGE_UNIFORMS), caps.image_units});
        caps.native.parameter_components = limit(MAX_COMPUTE_UNIFORM_COMPONENTS);
        caps.native.combined_output_resources = limit(MAX_COMBINED_SHADER_OUTPUT_RESOURCES);
        caps.native.outputs = std::min(caps.native.outputs, caps.native.combined_output_resources);
        for (GLuint i = 0; i < 3; ++i) {
            caps.group_count[i] = indexed_limit(MAX_COMPUTE_WORK_GROUP_COUNT, i);
            caps.local_size[i] = indexed_limit(MAX_COMPUTE_WORK_GROUP_SIZE, i);
        }
        caps.invocations = limit(MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
        if (!caps.native.outputs || !caps.native.combined_output_resources || !caps.native.parameter_components ||
            !caps.group_count[0] || !caps.group_count[1] || !caps.group_count[2] ||
            caps.local_size[0] < 8 || caps.local_size[1] < 8 || caps.local_size[2] < 1 || caps.invocations < 64)
            return fail("native compute limits do not support the 8x8x1 wrapper or a writable image");
        // The owner marks native.available only after probe() succeeds.
        return {};
    }
    Result<GLuint> NativeBackend::prepare(const KernelDesc& desc) const {
        const auto source = kernel_source(desc, Backend::Native);
        std::string error;
        const GLuint shader = gl::compile_shader(COMPUTE_SHADER, source, error);
        if (!shader) return std::unexpected("kernel '" + desc.name + "' native " + error);
        const GLuint program = glCreateProgram();
        if (!program) {
            glDeleteShader(shader);
            return std::unexpected("kernel '" + desc.name + "' native link: cannot create program");
        }
        glAttachShader(program, shader);
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        glDetachShader(program, shader);
        glDeleteShader(shader);
        if (linked != GL_TRUE) {
            error = "kernel '" + desc.name + "' native link: " + gl::shader_log(program, true);
            glDeleteProgram(program);
            return std::unexpected(std::move(error));
        }
        return program;
    }

    void NativeBackend::capture(std::span<ImageBinding> bindings) const {
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            const auto unit = static_cast<GLuint>(i);
            auto& b = bindings[i];
            glGetIntegeri_v(IMAGE_BINDING_NAME, unit, &b.name);
            glGetIntegeri_v(IMAGE_BINDING_LEVEL, unit, &b.level);
            glGetIntegeri_v(IMAGE_BINDING_LAYERED, unit, &b.layered);
            glGetIntegeri_v(IMAGE_BINDING_LAYER, unit, &b.layer);
            glGetIntegeri_v(IMAGE_BINDING_ACCESS, unit, &b.access);
            glGetIntegeri_v(IMAGE_BINDING_FORMAT, unit, &b.format);
        }
    }

    void NativeBackend::restore(std::span<const ImageBinding> bindings) const noexcept {
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            const auto& b = bindings[i];
            bind_image(static_cast<GLuint>(i), static_cast<GLuint>(b.name), b.level,
                       static_cast<GLboolean>(b.layered), b.layer,
                       static_cast<GLenum>(b.access), static_cast<GLenum>(b.format));
        }
    }

    Result<void> NativeBackend::probe() {
        if (!dispatch || !bind_image || !barrier)
            return std::unexpected("native probe: compute procedures are unavailable");

        // This cold probe alone performs a blocking readback; dispatches do not.
        // The caller has already rejected state in which changing programs is unsafe.
        struct Restore {
            NativeBackend& backend;
            GLint program = 0, texture_binding = 0, read_framebuffer = 0;
            ImageBinding image{};
            GLuint texture = 0, probe_program = 0, framebuffer = 0;
            explicit Restore(NativeBackend& b) : backend(b) {
                glGetIntegerv(GL_CURRENT_PROGRAM, &program);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_binding);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
                backend.capture(std::span(&image, 1));
            }
            ~Restore() {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_framebuffer));
                backend.restore(std::span<const ImageBinding>(&image, 1));
                glUseProgram(static_cast<GLuint>(program));
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_binding));
                if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
                if (probe_program) glDeleteProgram(probe_program);
                if (texture) glDeleteTextures(1, &texture);
            }
        } saved(*this);

        KernelDesc desc;
        desc.name = "native probe";
        desc.source = "void kernel(ivec3 id, inout uvec4 value) { value = uvec4(0x12345678u, 0x87654321u, 0xabcdef01u, 0x10203040u); }";
        desc.outputs.push_back({"value", {Element::Uint, 4}});
        auto program = prepare(desc);
        if (!program) return std::unexpected(program.error());
        saved.probe_program = *program;
        glGenTextures(1, &saved.texture);
        if (!saved.texture) return std::unexpected("native probe: cannot create texture");
        glBindTexture(GL_TEXTURE_2D, saved.texture);
        if (!gl::allocate_texture_2d(GL_RGBA32UI, 1, 1))
            return std::unexpected("native probe: immutable RGBA32UI storage allocation failed");
        constexpr std::array<GLuint, 4> initial{0, 0, 0, 0};
        {
            const gl::ClientPixelUnpack unpack;
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, initial.data());
        }
        bind_image(0, saved.texture, 0, GL_FALSE, 0, WRITE_ONLY, GL_RGBA32UI);
        ImageBinding actual{};
        capture(std::span(&actual, 1));
        if (!matches_output(actual, {saved.texture, GL_RGBA32UI}))
            return std::unexpected("native probe: image binding was not accepted");
        glUseProgram(saved.probe_program);
        if (integer(GL_CURRENT_PROGRAM) != static_cast<GLint>(saved.probe_program))
            return std::unexpected("native probe: cannot use compute program");
        const GLint domain = glGetUniformLocation(saved.probe_program, "hc_domain");
        const GLint width = glGetUniformLocation(saved.probe_program, "hc_width");
        if (domain < 0 || width < 0)
            return std::unexpected("native probe: generated dispatch uniforms are unavailable");
        glUniform3i(domain, 1, 1, 1);
        glUniform1i(width, 1);
        glValidateProgram(saved.probe_program);
        GLint valid = GL_FALSE;
        glGetProgramiv(saved.probe_program, GL_VALIDATE_STATUS, &valid);
        if (valid != GL_TRUE)
            return std::unexpected("native probe: compute program validation: " + gl::shader_log(saved.probe_program, true));
        dispatch(1, 1, 1);
        barrier(FRAMEBUFFER_BARRIER);
        glGenFramebuffers(1, &saved.framebuffer);
        if (!saved.framebuffer) return std::unexpected("native probe: cannot create read framebuffer");
        glBindFramebuffer(GL_READ_FRAMEBUFFER, saved.framebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, saved.texture, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
            integer(GL_READ_BUFFER) != GL_COLOR_ATTACHMENT0)
            return std::unexpected("native probe: read framebuffer is incomplete or misconfigured");
        struct PackState {
            std::array<GLenum, 4> fields{GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS};
            GLint pbo = integer(GL_PIXEL_PACK_BUFFER_BINDING);
            std::array<GLint, 4> values{};
            PackState() {
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    values[i] = integer(fields[i]);
                    glPixelStorei(fields[i], i == 0 ? 1 : 0);
                }
            }
            ~PackState() {
                for (std::size_t i = 0; i < fields.size(); ++i) glPixelStorei(fields[i], values[i]);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(pbo));
            }
        } pack;
        // ANGLE_PACK_REVERSE_ROW_ORDER cannot affect a single-row transfer.
        std::array<GLuint, 4> result{};
        glReadPixels(0, 0, 1, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, result.data());
        constexpr std::array<GLuint, 4> expected{0x12345678u, 0x87654321u, 0xabcdef01u, 0x10203040u};
        if (result != expected)
            return std::unexpected("native probe: dispatch did not write the expected sentinel texel");
        return {};
    }

    Result<void> NativeBackend::submit(Layout layout, std::span<const OutputBinding> outputs, const Capabilities& caps) const {
        if (!dispatch || !bind_image || !caps.native.available)
            return std::unexpected("native dispatch: backend is unavailable");
        if (!layout.width || !layout.height || !layout.elements ||
            layout.width > caps.width || layout.height > caps.height ||
            static_cast<std::uint64_t>(layout.width) * layout.height < layout.elements ||
            static_cast<std::uint64_t>(layout.width) * layout.height > caps.max_pixels)
            return std::unexpected("native dispatch: invalid physical layout");
        if (caps.local_size[0] < 8 || caps.local_size[1] < 8 || caps.local_size[2] < 1 || caps.invocations < 64)
            return std::unexpected("native dispatch: 8x8x1 local size exceeds device limits");
        const GLuint x = layout.width / 8 + (layout.width % 8 != 0);
        const GLuint y = layout.height / 8 + (layout.height % 8 != 0);
        if (x > caps.group_count[0] || y > caps.group_count[1] || caps.group_count[2] < 1)
            return std::unexpected("native dispatch: workgroup count exceeds device limits");
        if (outputs.empty() || outputs.size() > caps.image_units || outputs.size() > caps.native.outputs ||
            outputs.size() > caps.native.combined_output_resources)
            return std::unexpected("native dispatch: output count exceeds device limits");
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            if (!outputs[i].texture || !valid_format(outputs[i].format))
                return std::unexpected("native dispatch: invalid output image");
            for (std::size_t j = 0; j < i; ++j)
                if (outputs[i].texture == outputs[j].texture)
                    return std::unexpected("native dispatch: duplicate output image");
        }
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            const auto& output = outputs[i];
            bind_image(static_cast<GLuint>(i), output.texture, 0, GL_FALSE, 0, WRITE_ONLY, output.format);
            ImageBinding actual{};
            const GLuint unit = static_cast<GLuint>(i);
            glGetIntegeri_v(IMAGE_BINDING_NAME, unit, &actual.name);
            glGetIntegeri_v(IMAGE_BINDING_LEVEL, unit, &actual.level);
            glGetIntegeri_v(IMAGE_BINDING_LAYERED, unit, &actual.layered);
            glGetIntegeri_v(IMAGE_BINDING_LAYER, unit, &actual.layer);
            glGetIntegeri_v(IMAGE_BINDING_ACCESS, unit, &actual.access);
            glGetIntegeri_v(IMAGE_BINDING_FORMAT, unit, &actual.format);
            if (!matches_output(actual, output))
                return std::unexpected("native dispatch: output image binding was not accepted");
        }
        dispatch(x, y, 1);
        return {};
    }
}
