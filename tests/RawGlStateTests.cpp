#include <array>
#include <cstdint>
#include <string_view>


#include "Check.hpp"
#include "GlHarness.hpp"
#include <GLES2/gl2ext.h>
#include "gl/RawGlState.hpp"

namespace {
    using hyprimate::gl::SRawGlShadowState;
    using Pixel = std::array<std::uint8_t, 4>;
    constexpr std::array<GLenum, 7> CAPS{GL_DEPTH_TEST,
        GL_CULL_FACE,
        GL_RASTERIZER_DISCARD,
        GL_SAMPLE_ALPHA_TO_COVERAGE,
        GL_SAMPLE_COVERAGE,
        GL_POLYGON_OFFSET_FILL,
        GL_PRIMITIVE_RESTART_FIXED_INDEX};

    GLint integer(GLenum name) {
        GLint value = 0;
        glGetIntegerv(name, &value);
        return value;
    }
    bool has_extension(std::string_view wanted) {
        const GLint COUNT = integer(GL_NUM_EXTENSIONS);
        for (GLint i = 0; i < COUNT; ++i) {
            const auto *name = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
            if (name != nullptr &&
                std::string_view{reinterpret_cast<const char *>(name)} == wanted) {
                return true;
            }
        }
        return false;
    }
    void enabled(GLenum cap, bool value) {
        if (value) {
            glEnable(cap);
        } else {
            glDisable(cap);
        }
    }
    SRawGlShadowState shadow_state() {
        SRawGlShadowState state;
        glGetIntegerv(GL_VIEWPORT, state.viewport.data());
        state.active_texture = integer(GL_ACTIVE_TEXTURE);
        state.array_buffer = integer(GL_ARRAY_BUFFER_BINDING);
        state.draw_framebuffer = integer(GL_DRAW_FRAMEBUFFER_BINDING);
        state.read_framebuffer = integer(GL_READ_FRAMEBUFFER_BINDING);
        state.src = integer(GL_BLEND_SRC_RGB);
        state.dst = integer(GL_BLEND_DST_RGB);
        state.scissor = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
        state.stencil = glIsEnabled(GL_STENCIL_TEST) != GL_FALSE;
        state.blend = glIsEnabled(GL_BLEND) != GL_FALSE;
        return state;
    }
    void check_shadow(const SRawGlShadowState &actual, const SRawGlShadowState &expected) {
        CHECK(actual.viewport == expected.viewport);
        CHECK(actual.active_texture == expected.active_texture);
        CHECK(actual.array_buffer == expected.array_buffer);
        CHECK(actual.draw_framebuffer == expected.draw_framebuffer);
        CHECK(actual.read_framebuffer == expected.read_framebuffer);
        CHECK(actual.src == expected.src);
        CHECK(actual.dst == expected.dst);
        CHECK(actual.scissor == expected.scissor);
        CHECK(actual.stencil == expected.stencil);
        CHECK(actual.blend == expected.blend);
    }

    // Mirrors Hyprland's skip-if-cached setters without linking the compositor. The
    // guard must repair both the real GLES state and these independently cached values.
    struct SShadow {
        SRawGlShadowState cache = shadow_state();
        unsigned syncs = 0;
        GLint outer_draw_framebuffer = cache.draw_framebuffer;
        GLint outer_read_framebuffer = cache.read_framebuffer;

        void forget_framebuffer(GLuint framebuffer) {
            if (outer_draw_framebuffer == static_cast<GLint>(framebuffer)) {
                outer_draw_framebuffer = 0;
            }
            if (outer_read_framebuffer == static_cast<GLint>(framebuffer)) {
                outer_read_framebuffer = 0;
            }
        }

        void sync(const SRawGlShadowState &state) {
            ++syncs;
            if (cache.active_texture != state.active_texture) {
                glActiveTexture(static_cast<GLenum>(state.active_texture));
            }
            if (cache.array_buffer != state.array_buffer) {
                glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.array_buffer));
            }
            if (cache.draw_framebuffer != state.draw_framebuffer) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.draw_framebuffer));
            }
            if (cache.read_framebuffer != state.read_framebuffer) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.read_framebuffer));
            }
            if (cache.viewport != state.viewport) {
                glViewport(
                    state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
            }
            if (cache.scissor != state.scissor) {
                enabled(GL_SCISSOR_TEST, state.scissor);
            }
            if (cache.stencil != state.stencil) {
                enabled(GL_STENCIL_TEST, state.stencil);
            }
            if (cache.blend != state.blend) {
                enabled(GL_BLEND, state.blend);
            }
            if (cache.src != state.src || cache.dst != state.dst) {
                glBlendFunc(static_cast<GLenum>(state.src), static_cast<GLenum>(state.dst));
            }
            cache = state;
        }
    };
    using Guard = hyprimate::gl::CRawGlState<2, SShadow>;

    struct SSnapshot {
        SRawGlShadowState shadow = shadow_state();
        GLint program = integer(GL_CURRENT_PROGRAM);
        GLint vao = integer(GL_VERTEX_ARRAY_BINDING);
        GLint equation_rgb = integer(GL_BLEND_EQUATION_RGB);
        GLint equation_alpha = integer(GL_BLEND_EQUATION_ALPHA);
        GLint src_alpha = integer(GL_BLEND_SRC_ALPHA);
        GLint dst_alpha = integer(GL_BLEND_DST_ALPHA);
        std::array<GLint, 4> scissor{};
        std::array<GLboolean, 4> color{};
        std::array<GLfloat, 2> depth_range{};
        std::array<GLboolean, CAPS.size()> caps{};
        GLboolean sample_mask =
            integer(GL_MINOR_VERSION) >= 1 ? glIsEnabled(GL_SAMPLE_MASK) : GL_FALSE;
        std::array<std::array<GLint, 3>, 2> units{};

        SSnapshot() {
            glGetIntegerv(GL_SCISSOR_BOX, scissor.data());
            glGetBooleanv(GL_COLOR_WRITEMASK, color.data());
            glGetFloatv(GL_DEPTH_RANGE, depth_range.data());
            for (std::size_t i = 0; i < CAPS.size(); ++i) {
                caps[i] = glIsEnabled(CAPS[i]);
            }
            for (std::size_t unit = 0; unit < units.size(); ++unit) {
                glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
                units[unit] = {integer(GL_TEXTURE_BINDING_2D),
                    integer(GL_TEXTURE_BINDING_3D),
                    integer(GL_SAMPLER_BINDING)};
            }
            glActiveTexture(static_cast<GLenum>(shadow.active_texture));
        }
        void check(const SSnapshot &expected) const {
            check_shadow(shadow, expected.shadow);
            CHECK(program == expected.program);
            CHECK(vao == expected.vao);
            CHECK(equation_rgb == expected.equation_rgb);
            CHECK(equation_alpha == expected.equation_alpha);
            CHECK(src_alpha == expected.src_alpha);
            CHECK(dst_alpha == expected.dst_alpha);
            CHECK(scissor == expected.scissor);
            CHECK(color == expected.color);
            CHECK(depth_range == expected.depth_range);
            CHECK(caps == expected.caps);
            CHECK(sample_mask == expected.sample_mask);
            CHECK(units == expected.units);
        }
    };

    struct STarget {
        GLuint texture = 0;
        GLuint framebuffer = 0;
        STarget() {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 8, 8);
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        }
        ~STarget() {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteTextures(1, &texture);
        }
        STarget(const STarget &) = delete;
        STarget &operator=(const STarget &) = delete;
    };

    constexpr auto VERTEX = R"(#version 300 es
        void main() {
            vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
    )";
    constexpr auto FRAGMENT = R"(#version 300 es
        precision highp float;
        uniform sampler2D image;
        out vec4 color;
        void main() { color = texture(image, vec2(0.5)); }
    )";

    void hostile_draw_and_early_exit() {
        STarget caller_draw, caller_read, output;
        const GLuint PROGRAM = gl_harness::build_program(VERTEX, FRAGMENT);
        CHECK(PROGRAM != 0);
        GLuint texture = 0, volume = 0, sampler = 0, buffer = 0, vao = 0;
        glGenTextures(1, &texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        const Pixel PIXEL{65, 129, 193, 255};
        const std::array<Pixel, 4> PIXELS{PIXEL, PIXEL, PIXEL, PIXEL};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, PIXELS.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
        glGenTextures(1, &volume);
        glBindTexture(GL_TEXTURE_3D, volume);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, caller_read.texture);
        glBindTexture(GL_TEXTURE_3D, volume);
        glGenSamplers(1, &sampler);
        // The image has no mip chain: inheriting this sampler makes it incomplete.
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glBindSampler(0, sampler);
        glBindSampler(1, sampler);
        glBindSampler(5, sampler);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, caller_draw.texture);
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glUseProgram(PROGRAM);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, caller_draw.framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, caller_read.framebuffer);
        glViewport(1, 2, 3, 4);
        glScissor(0, 0, 0, 0);
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_NEVER, 0, ~0U);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthRangef(0.25F, 0.75F);
        glDepthFunc(GL_NEVER);
        glCullFace(GL_FRONT_AND_BACK);
        glSampleCoverage(0, GL_FALSE);
        if (integer(GL_MINOR_VERSION) >= 1) {
            glSampleMaski(0, 0);
            glEnable(GL_SAMPLE_MASK);
        }
        for (const auto cap : CAPS) {
            glEnable(cap);
        }
        SShadow shadow;
        const SSnapshot BEFORE;
        CHECK(glGetError() == GL_NO_ERROR);

        const auto DRAW = [&](bool early) {
            const Guard GUARD{{.viewport_width = 8,
                                  .viewport_height = 8,
                                  .framebuffer = output.framebuffer,
                                  .blend = !early},
                shadow};
            check_shadow(shadow.cache, shadow_state());
            CHECK(integer(GL_ACTIVE_TEXTURE) == GL_TEXTURE0);
            CHECK(integer(GL_ARRAY_BUFFER_BINDING) == 0);
            CHECK(integer(GL_SAMPLER_BINDING) == 0);
            CHECK(integer(GL_BLEND_EQUATION_RGB) == GL_FUNC_ADD);
            CHECK(glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE);
            CHECK(glIsEnabled(GL_STENCIL_TEST) == GL_FALSE);
            CHECK((glIsEnabled(GL_BLEND) != GL_FALSE) == !early);
            for (const auto cap : CAPS) {
                CHECK(glIsEnabled(cap) == GL_FALSE);
            }
            if (integer(GL_MINOR_VERSION) >= 1) {
                CHECK(glIsEnabled(GL_SAMPLE_MASK) == GL_FALSE);
            }
            std::array<GLboolean, 4> color{};
            glGetBooleanv(GL_COLOR_WRITEMASK, color.data());
            CHECK((color == std::array<GLboolean, 4>{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE}));
            if (early) {
                return;
            }
            glUseProgram(PROGRAM);
            glUniform1i(glGetUniformLocation(PROGRAM, "image"), 0);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            Pixel actual{};
            glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
            CHECK(actual == PIXEL);
            // Simulate host setters used by work inside the raw bracket as well as raw
            // changes. Exit must not assume the cache is still at the entry baseline.
            auto changed = shadow.cache;
            changed.draw_framebuffer = static_cast<GLint>(caller_read.framebuffer);
            changed.read_framebuffer = static_cast<GLint>(caller_draw.framebuffer);
            changed.active_texture = GL_TEXTURE1;
            changed.array_buffer = static_cast<GLint>(buffer);
            changed.blend = true;
            changed.src = GL_ZERO;
            changed.dst = GL_ONE;
            changed.viewport = {2, 1, 4, 3};
            shadow.sync(changed);
            glBindTexture(GL_TEXTURE_2D, output.texture);
            glBindTexture(GL_TEXTURE_3D, 0);
            glBindSampler(1, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        };
        DRAW(true);
        SSnapshot{}.check(BEFORE);
        check_shadow(shadow.cache, BEFORE.shadow);
        DRAW(false);
        SSnapshot{}.check(BEFORE);
        check_shadow(shadow.cache, BEFORE.shadow);
        CHECK(shadow.syncs == 5);
        // These are skip-if-cached calls; neither may change the restored real state.
        shadow.sync(BEFORE.shadow);
        SSnapshot{}.check(BEFORE);
        CHECK(integer(GL_TEXTURE_BINDING_2D) == static_cast<GLint>(caller_draw.texture));
        CHECK(integer(GL_SAMPLER_BINDING) == static_cast<GLint>(sampler));
        CHECK(glGetError() == GL_NO_ERROR);

        glUseProgram(0);
        glDeleteProgram(PROGRAM);
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &buffer);
        glDeleteSamplers(1, &sampler);
        glDeleteTextures(1, &texture);
        glDeleteTextures(1, &volume);
        for (const auto cap : CAPS) {
            glDisable(cap);
        }
        if (integer(GL_MINOR_VERSION) >= 1) {
            glDisable(GL_SAMPLE_MASK);
        }
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    void deletion_does_not_resurrect_bindings() {
        STarget draw, read;
        GLuint texture = 0, volume = 0, sampler = 0, buffer = 0, vao = 0;
        const GLuint PROGRAM = gl_harness::build_program(VERTEX, FRAGMENT);
        CHECK(PROGRAM != 0);
        glGenTextures(1, &texture);
        glGenTextures(1, &volume);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindTexture(GL_TEXTURE_3D, volume);
        glGenSamplers(1, &sampler);
        glBindSampler(0, sampler);
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glUseProgram(PROGRAM);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw.framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read.framebuffer);
        SShadow shadow;
        {
            // The entry baseline itself is deleted too: exit must never replay it.
            const Guard GUARD{{.framebuffer = draw.framebuffer}, shadow};
            glDeleteTextures(1, &texture);
            glDeleteTextures(1, &volume);
            glDeleteSamplers(1, &sampler);
            glDeleteBuffers(1, &buffer);
            glDeleteVertexArrays(1, &vao);
            hyprimate::gl::delete_framebuffers(1, &draw.framebuffer);
            hyprimate::gl::delete_framebuffers(1, &read.framebuffer);
            // Pending deletion while still current is legal, but cannot be restored.
            glDeleteProgram(PROGRAM);
        }
        CHECK(integer(GL_ACTIVE_TEXTURE) == GL_TEXTURE0);
        CHECK(integer(GL_ARRAY_BUFFER_BINDING) == 0);
        CHECK(integer(GL_READ_FRAMEBUFFER_BINDING) == 0);
        CHECK(integer(GL_DRAW_FRAMEBUFFER_BINDING) == 0);
        CHECK(integer(GL_TEXTURE_BINDING_2D) == 0);
        CHECK(integer(GL_TEXTURE_BINDING_3D) == 0);
        CHECK(integer(GL_SAMPLER_BINDING) == 0);
        CHECK(integer(GL_VERTEX_ARRAY_BINDING) == 0);
        CHECK(integer(GL_CURRENT_PROGRAM) == 0);
        CHECK(glIsTexture(texture) == GL_FALSE);
        CHECK(glIsTexture(volume) == GL_FALSE);
        CHECK(glIsSampler(sampler) == GL_FALSE);
        CHECK(glIsBuffer(buffer) == GL_FALSE);
        CHECK(glIsVertexArray(vao) == GL_FALSE);
        CHECK(glIsFramebuffer(draw.framebuffer) == GL_FALSE);
        CHECK(glIsFramebuffer(read.framebuffer) == GL_FALSE);
        CHECK(glIsProgram(PROGRAM) == GL_FALSE);
        check_shadow(shadow.cache, shadow_state());
        CHECK(shadow.outer_draw_framebuffer == 0);
        CHECK(shadow.outer_read_framebuffer == 0);
        CHECK(glGetError() == GL_NO_ERROR);
        draw.framebuffer = 0;
        read.framebuffer = 0;
    }
    void indexed_state_is_restored(bool require_extension = false) {
        const GLint MAJOR = integer(GL_MAJOR_VERSION);
        const GLint MINOR = integer(GL_MINOR_VERSION);
        const bool CORE = !require_extension && (MAJOR > 3 || (MAJOR == 3 && MINOR >= 2));
        const bool has_ext = has_extension("GL_EXT_draw_buffers_indexed");
        const bool has_oes = has_extension("GL_OES_draw_buffers_indexed");
        if (!CORE && !has_ext && !has_oes) {
            (void)std::fprintf(stderr,
                "[skip] indexed state: GL ES %d.%d; no EXT/OES draw buffers indexed\n",
                MAJOR, MINOR);
            return;
        }
        const char *const API =
            CORE ? "core" : (has_ext ? "GL_EXT_draw_buffers_indexed" : "GL_OES_draw_buffers_indexed");
        (void)std::fprintf(stderr, "[run] indexed state: GL ES %d.%d; %s\n", MAJOR, MINOR, API);
        const auto color_mask = CORE ? &glColorMaski : reinterpret_cast<decltype(&glColorMaski)>(
            eglGetProcAddress(has_ext ? "glColorMaskiEXT" : "glColorMaskiOES"));
        const auto enable = CORE ? &glEnablei : reinterpret_cast<decltype(&glEnablei)>(
            eglGetProcAddress(has_ext ? "glEnableiEXT" : "glEnableiOES"));
        const auto disable = CORE ? &glDisablei : reinterpret_cast<decltype(&glDisablei)>(
            eglGetProcAddress(has_ext ? "glDisableiEXT" : "glDisableiOES"));
        const auto is_enabled = CORE ? &glIsEnabledi : reinterpret_cast<decltype(&glIsEnabledi)>(
            eglGetProcAddress(has_ext ? "glIsEnablediEXT" : "glIsEnablediOES"));
        const auto factors = CORE ? &glBlendFuncSeparatei
                                  : reinterpret_cast<decltype(&glBlendFuncSeparatei)>(
                                        eglGetProcAddress(has_ext ? "glBlendFuncSeparateiEXT"
                                                                  : "glBlendFuncSeparateiOES"));
        const auto equation = CORE ? &glBlendEquationSeparatei
                                   : reinterpret_cast<decltype(&glBlendEquationSeparatei)>(
                                         eglGetProcAddress(has_ext ? "glBlendEquationSeparateiEXT"
                                                                   : "glBlendEquationSeparateiOES"));
        CHECK(color_mask && enable && disable && is_enabled && factors && equation);
        if (!color_mask || !enable || !disable || !is_enabled || !factors || !equation) {
            return;
        }

        const std::array<std::array<GLboolean, 4>, 2> MASKS{{
            {GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE},
            {GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE}}};
        const std::array<std::array<GLenum, 4>, 2> FACTORS{{
            {GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ZERO},
            {GL_ONE, GL_ZERO, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA}}};
        const std::array<std::array<GLenum, 2>, 2> EQUATIONS{{
            {GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD},
            {GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT}}};
        constexpr std::array<GLboolean, 2> BLEND{GL_TRUE, GL_FALSE};
        for (GLuint i = 0; i < 2; ++i) {
            color_mask(i, MASKS[i][0], MASKS[i][1], MASKS[i][2], MASKS[i][3]);
            if (BLEND[i] == GL_TRUE) {
                enable(GL_BLEND, i);
            } else {
                disable(GL_BLEND, i);
            }
            factors(i, FACTORS[i][0], FACTORS[i][1], FACTORS[i][2], FACTORS[i][3]);
            equation(i, EQUATIONS[i][0], EQUATIONS[i][1]);
        }
        SShadow shadow;
        {
            const Guard GUARD{{}, shadow};
            for (GLuint i = 0; i < 2; ++i) {
                std::array<GLint, 4> mask{};
                glGetIntegeri_v(GL_COLOR_WRITEMASK, i, mask.data());
                for (const auto channel : mask) {
                    CHECK(channel == GL_TRUE);
                }
                CHECK(is_enabled(GL_BLEND, i) == GL_FALSE);
                for (const auto property : {GL_BLEND_SRC_RGB, GL_BLEND_SRC_ALPHA}) {
                    GLint value = 0;
                    glGetIntegeri_v(property, i, &value);
                    CHECK(value == GL_ONE);
                }
                for (const auto property : {GL_BLEND_DST_RGB, GL_BLEND_DST_ALPHA}) {
                    GLint value = 0;
                    glGetIntegeri_v(property, i, &value);
                    CHECK(value == GL_ZERO);
                }
                for (const auto property : {GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA}) {
                    GLint value = 0;
                    glGetIntegeri_v(property, i, &value);
                    CHECK(value == GL_FUNC_ADD);
                }
            }
        }
        for (GLuint i = 0; i < 2; ++i) {
            std::array<GLint, 4> mask{};
            glGetIntegeri_v(GL_COLOR_WRITEMASK, i, mask.data());
            for (std::size_t channel = 0; channel < mask.size(); ++channel) {
                CHECK(mask[channel] == MASKS[i][channel]);
            }
            CHECK((is_enabled(GL_BLEND, i) == GL_TRUE) == (BLEND[i] == GL_TRUE));
            GLint factor = 0;
            glGetIntegeri_v(GL_BLEND_SRC_RGB, i, &factor);
            CHECK(static_cast<GLenum>(factor) == FACTORS[i][0]);
            glGetIntegeri_v(GL_BLEND_DST_RGB, i, &factor);
            CHECK(static_cast<GLenum>(factor) == FACTORS[i][1]);
            glGetIntegeri_v(GL_BLEND_SRC_ALPHA, i, &factor);
            CHECK(static_cast<GLenum>(factor) == FACTORS[i][2]);
            glGetIntegeri_v(GL_BLEND_DST_ALPHA, i, &factor);
            CHECK(static_cast<GLenum>(factor) == FACTORS[i][3]);
            glGetIntegeri_v(GL_BLEND_EQUATION_RGB, i, &factor);
            CHECK(static_cast<GLenum>(factor) == EQUATIONS[i][0]);
            glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA, i, &factor);
            CHECK(static_cast<GLenum>(factor) == EQUATIONS[i][1]);
        }
        for (GLuint i = 0; i < 2; ++i) {
            disable(GL_BLEND, i);
            color_mask(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }
    void advanced_equation_is_restored() {
        if (!has_extension("GL_KHR_blend_equation_advanced")) {
            (void)std::fprintf(stderr, "[skip] advanced equation: GL_KHR_blend_equation_advanced unavailable\n");
            return;
        }
        (void)std::fprintf(stderr, "[run] advanced equation: GL_KHR_blend_equation_advanced\n");
        glBlendEquation(GL_MULTIPLY_KHR);
        const GLenum SET_ERROR = glGetError();
        CHECK(SET_ERROR == GL_NO_ERROR);
        CHECK(integer(GL_BLEND_EQUATION_RGB) == GL_MULTIPLY_KHR);
        CHECK(integer(GL_BLEND_EQUATION_ALPHA) == GL_MULTIPLY_KHR);
        if (SET_ERROR != GL_NO_ERROR) {
            glBlendEquation(GL_FUNC_ADD);
            return;
        }
        const bool INDEXED = integer(GL_MAJOR_VERSION) > 3 || integer(GL_MINOR_VERSION) >= 2 ||
                             has_extension("GL_EXT_draw_buffers_indexed") ||
                             has_extension("GL_OES_draw_buffers_indexed");
        if (INDEXED) {
            GLint slot_zero = 0;
            glGetIntegeri_v(GL_BLEND_EQUATION_RGB, 0, &slot_zero);
            CHECK(slot_zero == GL_MULTIPLY_KHR);
            CHECK(glGetError() == GL_NO_ERROR);
        }
        SShadow shadow;
        {
            const Guard GUARD{{}, shadow};
        }
        CHECK(glGetError() == GL_NO_ERROR);
        CHECK(integer(GL_BLEND_EQUATION_RGB) == GL_MULTIPLY_KHR);
        CHECK(integer(GL_BLEND_EQUATION_ALPHA) == GL_MULTIPLY_KHR);
        if (INDEXED) {
            GLint slot_zero = 0;
            glGetIntegeri_v(GL_BLEND_EQUATION_RGB, 0, &slot_zero);
            CHECK(slot_zero == GL_MULTIPLY_KHR);
            CHECK(glGetError() == GL_NO_ERROR);
        }
        glBlendEquation(GL_FUNC_ADD);
    }
    void mixed_advanced_equations_are_restored(bool require_extension = false) {
        const bool CORE = !require_extension &&
                          (integer(GL_MAJOR_VERSION) > 3 || integer(GL_MINOR_VERSION) >= 2);
        const bool EXT = has_extension("GL_EXT_draw_buffers_indexed");
        const bool OES = has_extension("GL_OES_draw_buffers_indexed");
        if (!has_extension("GL_KHR_blend_equation_advanced") || (!CORE && !EXT && !OES)) {
            (void)std::fprintf(stderr, "[skip] mixed advanced equations: required extensions unavailable\n");
            return;
        }
        const char *const API = CORE ? "core" : (EXT ? "EXT" : "OES");
        (void)std::fprintf(stderr, "[run] mixed advanced equations: %s\n", API);
        const auto equation = CORE ? &glBlendEquationSeparatei
                                   : reinterpret_cast<decltype(&glBlendEquationSeparatei)>(
                                         eglGetProcAddress(EXT ? "glBlendEquationSeparateiEXT"
                                                               : "glBlendEquationSeparateiOES"));
        CHECK(equation != nullptr);
        if (equation == nullptr) {
            return;
        }
        glBlendEquation(GL_MULTIPLY_KHR);
        const GLenum GLOBAL_ERROR = glGetError();
        CHECK(GLOBAL_ERROR == GL_NO_ERROR);
        equation(0, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT);
        const GLenum INDEXED_ERROR = glGetError();
        CHECK(INDEXED_ERROR == GL_NO_ERROR);
        if (GLOBAL_ERROR != GL_NO_ERROR || INDEXED_ERROR != GL_NO_ERROR) {
            glBlendEquation(GL_FUNC_ADD);
            return;
        }
        const auto check_equations = [] {
            constexpr std::array<std::array<GLint, 2>, 2> EXPECTED{{
                {GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT},
                {GL_MULTIPLY_KHR, GL_MULTIPLY_KHR}}};
            for (GLuint index = 0; index < 2; ++index) {
                GLint rgb = 0;
                GLint alpha = 0;
                glGetIntegeri_v(GL_BLEND_EQUATION_RGB, index, &rgb);
                glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA, index, &alpha);
                CHECK(rgb == EXPECTED[index][0]);
                CHECK(alpha == EXPECTED[index][1]);
            }
            CHECK(glGetError() == GL_NO_ERROR);
        };
        check_equations();
        SShadow shadow;
        {
            const Guard GUARD{{}, shadow};
        }
        CHECK(glGetError() == GL_NO_ERROR);
        check_equations();
        glBlendEquation(GL_FUNC_ADD);
        CHECK(glGetError() == GL_NO_ERROR);
    }



    void bound_program_pending_deletion_survives_bracket() {
        const GLuint PROGRAM = gl_harness::build_program(VERTEX, FRAGMENT);
        CHECK(PROGRAM != 0);
        glUseProgram(PROGRAM);
        glDeleteProgram(PROGRAM);
        SShadow shadow;
        {
            const Guard GUARD{{}, shadow};
        }
        CHECK(integer(GL_CURRENT_PROGRAM) == static_cast<GLint>(PROGRAM));
        GLint deleted = GL_FALSE;
        glGetProgramiv(PROGRAM, GL_DELETE_STATUS, &deleted);
        CHECK(deleted == GL_TRUE);
        glUseProgram(0);
        CHECK(glIsProgram(PROGRAM) == GL_FALSE);
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void reused_framebuffer_name_is_not_restored() {
        STarget target;
        const GLuint SAVED = target.framebuffer;
        SShadow shadow;
        {
            const Guard GUARD{{.framebuffer = SAVED}, shadow};
            hyprimate::gl::delete_framebuffers(1, &target.framebuffer);
            target.framebuffer = 0;
            // Binding a deleted nonzero name creates a new framebuffer with that name.
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, SAVED);
            CHECK(glIsFramebuffer(SAVED) == GL_TRUE);
        }
        CHECK(integer(GL_DRAW_FRAMEBUFFER_BINDING) == 0);
        CHECK(integer(GL_READ_FRAMEBUFFER_BINDING) == 0);
        CHECK(shadow.outer_draw_framebuffer == 0);
        glDeleteFramebuffers(1, &SAVED);
        CHECK(glGetError() == GL_NO_ERROR);
    }


    void zero_units_preserves_active_texture() {
        glActiveTexture(GL_TEXTURE5);
        glViewport(1, 1, 5, 5);
        SShadow shadow;
        const SSnapshot BEFORE;
        {
            const hyprimate::gl::CRawGlState<0, SShadow> GUARD{{}, shadow};
            CHECK(integer(GL_ACTIVE_TEXTURE) == GL_TEXTURE0);
        }
        SSnapshot{}.check(BEFORE);
        check_shadow(shadow.cache, BEFORE.shadow);
        CHECK(glGetError() == GL_NO_ERROR);
    }
}  // namespace

int main(int argc, char **argv) {
    const gl_harness::CContext CONTEXT{8, 8};
    if (!CONTEXT.valid()) {
        return 77;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--require-indexed-extension") {
        if (integer(GL_MAJOR_VERSION) != 3 || integer(GL_MINOR_VERSION) >= 2) {
            std::fprintf(stderr, "[skip] indexed extension: GLES 3.0/3.1 required, got %s\n",
                glGetString(GL_VERSION));
            return 77;
        }
        if (!has_extension("GL_EXT_draw_buffers_indexed") &&
            !has_extension("GL_OES_draw_buffers_indexed")) {
            (void)std::fprintf(stderr, "[skip] indexed extension: neither EXT nor OES advertised\n");
            return 77;
        }
        indexed_state_is_restored(true);
        mixed_advanced_equations_are_restored(true);
        return g_failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--require-es30" &&
        (integer(GL_MAJOR_VERSION) != 3 || integer(GL_MINOR_VERSION) != 0)) {
        std::fprintf(stderr, "ES 3.0 unavailable; got %s\n", glGetString(GL_VERSION));
        return 77;
    }
    hostile_draw_and_early_exit();
    deletion_does_not_resurrect_bindings();
    indexed_state_is_restored();
    advanced_equation_is_restored();
    mixed_advanced_equations_are_restored();
    bound_program_pending_deletion_survives_bracket();
    reused_framebuffer_name_is_not_restored();
    zero_units_preserves_active_texture();
    return g_failures == 0 ? 0 : 1;
}
