#pragma once

#include <algorithm>
#include <ranges>
#include <string_view>
#include <vector>

#include "GlesRuntime.hpp"

namespace hyprimate::shader::gles_detail {
    constexpr unsigned MAX_BLOCK_BINDINGS = 32;
    constexpr unsigned MAX_SHADER_SAMPLERS = MAX_TEXTURES + 6;
    constexpr GLenum SAMPLE_MASK = 0x8E51;
    constexpr GLenum MAX_CLIP_DISTANCES = 0x0D32;
    constexpr GLenum CLIP_DISTANCE0 = 0x3000;
    constexpr std::array<GLenum, 9> DRAW_CAPS{GL_STENCIL_TEST,
        GL_DEPTH_TEST,
        GL_CULL_FACE,
        GL_RASTERIZER_DISCARD,
        GL_SAMPLE_ALPHA_TO_COVERAGE,
        GL_SAMPLE_COVERAGE,
        GL_POLYGON_OFFSET_FILL,
        GL_PRIMITIVE_RESTART_FIXED_INDEX,
        GL_DITHER};

    GLint integer(GLenum name);
    bool fail(std::string &error, std::string_view message);
    void discard_errors();
    bool gl_ok(std::string &error, std::string_view operation);
    void enable(GLenum cap, bool enabled);
    bool can_use_program(GLuint current, std::string &error);
    void count_storage(Statistics &stats, std::size_t bytes);
    struct Limits {
        GLint texture = 0, color_attachments = 0, draw_buffers = 0, attributes = 0;
        GLint samplers = 0, block_bindings = 0, block_bytes = 0, feedback_components = 0;
        GLint64 index = 0;
        std::array<GLint, 2> viewport{};
        bool float_linear = false, float_blend = false;
        bool indexed_core = false, indexed_ext = false, indexed_oes = false;
        bool viewport_oes = false, viewport_nv = false, clip_distances = false;
        bool sample_mask = false, window_rectangles = false;
        void query() {
            texture = integer(GL_MAX_TEXTURE_SIZE);
            glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport.data());
            color_attachments = integer(GL_MAX_COLOR_ATTACHMENTS);
            draw_buffers = integer(GL_MAX_DRAW_BUFFERS);
            attributes = integer(GL_MAX_VERTEX_ATTRIBS);
            samplers = integer(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
            block_bindings = integer(GL_MAX_UNIFORM_BUFFER_BINDINGS);
            block_bytes = integer(GL_MAX_UNIFORM_BLOCK_SIZE);
            feedback_components = integer(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS);
            glGetInteger64v(GL_MAX_ELEMENT_INDEX, &index);
            const auto major = integer(GL_MAJOR_VERSION);
            const auto minor = integer(GL_MINOR_VERSION);
            indexed_core = major > 3 || (major == 3 && minor >= 2);
            sample_mask = major > 3 || (major == 3 && minor >= 1);
            const auto count = integer(GL_NUM_EXTENSIONS);
            for (GLint i = 0; i < count; ++i) {
                const auto *name = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
                if (!name) {
                    continue;
                }
                const std::string_view ext(reinterpret_cast<const char *>(name));
                float_linear |= ext == "GL_OES_texture_float_linear";
                float_blend |= ext == "GL_EXT_float_blend";
                indexed_ext |= ext == "GL_EXT_draw_buffers_indexed";
                indexed_oes |= ext == "GL_OES_draw_buffers_indexed";
                viewport_oes |= ext == "GL_OES_viewport_array";
                viewport_nv |= ext == "GL_NV_viewport_array";
                window_rectangles |= ext == "GL_EXT_window_rectangles";
                clip_distances |= ext == "GL_EXT_clip_cull_distance" ||
                                  ext == "GL_APPLE_clip_distance";
            }
        }

        bool viewport_fits(unsigned width, unsigned height) const {
            return width <= static_cast<unsigned>(viewport[0]) &&
                   height <= static_cast<unsigned>(viewport[1]);
        }
    };

    // Indexed attachment state is a host capability, not a shader requirement.
    // Resolve only when advertised; core ES 3.0 needs no extra entry points.
    struct IndexedColor {
        using Enable = void(GL_APIENTRYP)(GLenum, GLuint);
        using IsEnabled = GLboolean(GL_APIENTRYP)(GLenum, GLuint);
        using Mask = void(GL_APIENTRYP)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);
        using Equation = void(GL_APIENTRYP)(GLuint, GLenum, GLenum);
        using EquationSingle = void(GL_APIENTRYP)(GLuint, GLenum);
        using Factors = void(GL_APIENTRYP)(GLuint, GLenum, GLenum, GLenum, GLenum);
        using ColorMask = std::array<GLboolean, 4>;
        using GetFloatIndexed = void(GL_APIENTRYP)(GLenum, GLuint, GLfloat *);
        using GetIntegerIndexed = void(GL_APIENTRYP)(GLenum, GLuint, GLint *);
        using ViewportIndexed = void(GL_APIENTRYP)(GLuint, const GLfloat *);
        using ScissorIndexed = void(GL_APIENTRYP)(GLuint, GLint, GLint, GLsizei, GLsizei);
        using DepthRangeIndexed = void(GL_APIENTRYP)(GLuint, GLfloat, GLfloat);
        GetIntegerIndexed get_integer_indexed = nullptr;
        Enable enable_slot = nullptr, disable_slot = nullptr;
        IsEnabled enabled_slot = nullptr;
        Mask mask_slot = nullptr;
        Equation equation_slot = nullptr;
        EquationSingle equation_single = nullptr;
        Factors factors_slot = nullptr;
        GetFloatIndexed get_float_indexed = nullptr;
        ViewportIndexed viewport_indexed = nullptr;
        ScissorIndexed scissor_indexed = nullptr;
        DepthRangeIndexed depth_range_indexed = nullptr;
        Enable scissor_enable = nullptr, scissor_disable = nullptr;
        IsEnabled scissor_enabled = nullptr;
        bool sample_mask = false;
        unsigned clip_count = 0;

        bool load(GlResolver resolve, const char *suffix) {
            if (!resolve) {
                return false;
            }
            const auto proc = [&](const char *name) {
                return resolve((std::string(name) + suffix).c_str());
            };
            enable_slot = reinterpret_cast<Enable>(proc("glEnablei"));
            disable_slot = reinterpret_cast<Enable>(proc("glDisablei"));
            enabled_slot = reinterpret_cast<IsEnabled>(proc("glIsEnabledi"));
            mask_slot = reinterpret_cast<Mask>(proc("glColorMaski"));
            equation_slot = reinterpret_cast<Equation>(proc("glBlendEquationSeparatei"));
            equation_single = reinterpret_cast<EquationSingle>(proc("glBlendEquationi"));
            factors_slot = reinterpret_cast<Factors>(proc("glBlendFuncSeparatei"));
            return enable_slot && disable_slot && enabled_slot && mask_slot && equation_slot &&
                   equation_single && factors_slot;
        }

        bool load_viewport(GlResolver resolve, const char *suffix, bool share_enable) {
            if (!resolve) return false;
            const auto proc = [&](const char *name) {
                return resolve((std::string(name) + suffix).c_str());
            };
            get_float_indexed = reinterpret_cast<GetFloatIndexed>(proc("glGetFloati_v"));
            get_integer_indexed = glGetIntegeri_v;
            viewport_indexed = reinterpret_cast<ViewportIndexed>(proc("glViewportIndexedfv"));
            scissor_indexed = reinterpret_cast<ScissorIndexed>(proc("glScissorIndexed"));
            depth_range_indexed = reinterpret_cast<DepthRangeIndexed>(proc("glDepthRangeIndexedf"));
            scissor_enable = share_enable ? enable_slot : reinterpret_cast<Enable>(proc("glEnablei"));
            scissor_disable = share_enable ? disable_slot : reinterpret_cast<Enable>(proc("glDisablei"));
            scissor_enabled = share_enable ? enabled_slot : reinterpret_cast<IsEnabled>(proc("glIsEnabledi"));
            return get_float_indexed && get_integer_indexed && viewport_indexed && scissor_indexed &&
                   depth_range_indexed && scissor_enable && scissor_disable && scissor_enabled;
        }

        bool configure(const Limits &limits, GlResolver resolve) {
            if ((limits.indexed_core || limits.indexed_ext || limits.indexed_oes) &&
                !(limits.indexed_core && load(resolve, "")) &&
                !(limits.indexed_ext && load(resolve, "EXT")) &&
                !(limits.indexed_oes && load(resolve, "OES"))) return false;
            if ((limits.viewport_oes || limits.viewport_nv) &&
                !(limits.viewport_oes && load_viewport(resolve, "OES", indexed())) &&
                !(limits.viewport_nv && load_viewport(resolve, "NV", false))) return false;
            sample_mask = limits.sample_mask;
            if (limits.clip_distances) {
                const auto count = integer(MAX_CLIP_DISTANCES);
                if (count <= 0 || count > 32) return false;
                clip_count = static_cast<unsigned>(count);
            }
            if (get_float_indexed) {
                std::array<GLfloat, 4> viewport{};
                std::array<GLfloat, 2> range{};
                std::array<GLint, 4> scissor{};
                get_float_indexed(GL_VIEWPORT, 0, viewport.data());
                get_float_indexed(GL_DEPTH_RANGE, 0, range.data());
                get_integer_indexed(GL_SCISSOR_BOX, 0, scissor.data());
                (void)scissor_enabled(GL_SCISSOR_TEST, 0);
            }
            if (limits.viewport_oes || limits.viewport_nv || limits.clip_distances) {
                std::string error;
                if (!gl_ok(error, "querying indexed viewport and clip-distance state")) return false;
            }
            return true;
        }

        void viewport(GLint x, GLint y, GLsizei width, GLsizei height) const {
            if (viewport_indexed) {
                const std::array<GLfloat, 4> value{static_cast<GLfloat>(x), static_cast<GLfloat>(y),
                    static_cast<GLfloat>(width), static_cast<GLfloat>(height)};
                viewport_indexed(0, value.data());
            } else {
                glViewport(x, y, width, height);
            }
        }

        void scissor_box(GLint x, GLint y, GLsizei width, GLsizei height) const {
            if (scissor_indexed) scissor_indexed(0, x, y, width, height);
            else glScissor(x, y, width, height);
        }

        bool scissor_test() const {
            return (scissor_enabled ? scissor_enabled(GL_SCISSOR_TEST, 0) :
                glIsEnabled(GL_SCISSOR_TEST)) != GL_FALSE;
        }

        void scissor_test(bool enabled) const {
            if (scissor_enabled) (enabled ? scissor_enable : scissor_disable)(GL_SCISSOR_TEST, 0);
            else enable(GL_SCISSOR_TEST, enabled);
        }

        void depth_range(GLfloat near_value, GLfloat far_value) const {
            if (depth_range_indexed) depth_range_indexed(0, near_value, far_value);
            else glDepthRangef(near_value, far_value);
        }

        bool indexed() const {
            return enabled_slot != nullptr;
        }

        GLint get(GLenum property, unsigned i) const {
            if (!indexed()) {
                return integer(property);
            }
            GLint value = 0;
            glGetIntegeri_v(property, i, &value);
            return value;
        }

        ColorMask mask(unsigned i) const {
            ColorMask value{};
            if (indexed()) {
                std::array<GLint, 4> bits{};
                glGetIntegeri_v(GL_COLOR_WRITEMASK, i, bits.data());
                std::ranges::transform(bits, value.begin(), [](GLint bit) { return bit != 0; });
            } else {
                glGetBooleanv(GL_COLOR_WRITEMASK, value.data());
            }
            return value;
        }

        void color_mask(unsigned i, const ColorMask &m) const {
            if (indexed()) {
                mask_slot(i, m[0], m[1], m[2], m[3]);
            } else {
                glColorMask(m[0], m[1], m[2], m[3]);
            }
        }

        void equation(unsigned i, GLenum rgb, GLenum alpha) const {
            // Advanced KHR/core equations use the single setter. Separate setters
            // only accept the basic equations, even if both arguments are equal.
            if (rgb == alpha) {
                if (indexed()) equation_single(i, rgb);
                else glBlendEquation(rgb);
            } else if (indexed()) {
                equation_slot(i, rgb, alpha);
            } else {
                glBlendEquationSeparate(rgb, alpha);
            }
        }

        void factors(unsigned i, GLenum src, GLenum dst, GLenum src_a, GLenum dst_a) const {
            if (indexed()) {
                factors_slot(i, src, dst, src_a, dst_a);
            } else {
                glBlendFuncSeparate(src, dst, src_a, dst_a);
            }
        }

        void blend(unsigned count, bool enabled) const {
            if (indexed()) {
                const auto set = enabled ? enable_slot : disable_slot;
                for (unsigned i = 0; i < count; ++i) {
                    set(GL_BLEND, i);
                }
            } else {
                enable(GL_BLEND, enabled);
            }
        }

        void blend_slot(unsigned i, bool enabled) const {
            if (indexed()) {
                (enabled ? enable_slot : disable_slot)(GL_BLEND, i);
            } else {
                enable(GL_BLEND, enabled);
            }
        }
    };
    constexpr IndexedColor::ColorMask FULL_MASK{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    struct UploadState {
        static constexpr std::array<GLenum, 6> STORE{GL_UNPACK_ALIGNMENT,
            GL_UNPACK_ROW_LENGTH,
            GL_UNPACK_IMAGE_HEIGHT,
            GL_UNPACK_SKIP_PIXELS,
            GL_UNPACK_SKIP_ROWS,
            GL_UNPACK_SKIP_IMAGES};
        GLint texture = integer(GL_TEXTURE_BINDING_2D);
        GLint pbo = integer(GL_PIXEL_UNPACK_BUFFER_BINDING);
        std::array<GLint, STORE.size()> unpack{};

        UploadState() {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            for (std::size_t i = 0; i < STORE.size(); ++i) {
                unpack[i] = integer(STORE[i]);
                glPixelStorei(STORE[i], i == 0 ? 1 : 0);
            }
        }

        ~UploadState() {
            for (std::size_t i = 0; i < STORE.size(); ++i) {
                glPixelStorei(STORE[i], unpack[i]);
            }
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
            glBindTexture(GL_TEXTURE_2D, texture);
        }
    };
    struct DrawColorState {
        struct Slot {
            IndexedColor::ColorMask mask;
            GLint src, dst, src_alpha, dst_alpha, equation, equation_alpha;
            bool blend;
        };
        const IndexedColor &api;
        const unsigned count;
        std::array<Slot, MAX_TEXTURES> slots;

        DrawColorState(const IndexedColor &color, unsigned outputs)
            : api(color), count(color.indexed() ? outputs : 1) {
            for (unsigned i = 0; i < count; ++i) {
                slots[i] = {api.mask(i),
                    api.get(GL_BLEND_SRC_RGB, i),
                    api.get(GL_BLEND_DST_RGB, i),
                    api.get(GL_BLEND_SRC_ALPHA, i),
                    api.get(GL_BLEND_DST_ALPHA, i),
                    api.get(GL_BLEND_EQUATION_RGB, i),
                    api.get(GL_BLEND_EQUATION_ALPHA, i),
                    (api.indexed() ? api.enabled_slot(GL_BLEND, i) : glIsEnabled(GL_BLEND)) !=
                        GL_FALSE};
                api.color_mask(i, FULL_MASK);
                api.equation(i, GL_FUNC_ADD, GL_FUNC_ADD);
                api.factors(i, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }
        }

        ~DrawColorState() {
            for (unsigned i = 0; i < count; ++i) {
                const auto &slot = slots[i];
                api.blend_slot(i, slot.blend);
                api.factors(i, slot.src, slot.dst, slot.src_alpha, slot.dst_alpha);
                api.equation(i, slot.equation, slot.equation_alpha);
                api.color_mask(i, slot.mask);
            }
        }
    };
    struct DrawState {
        struct Unit {
            GLint texture = 0, sampler = 0;
        };
        struct BlockRange {
            GLint buffer = 0;
            GLint64 start = 0, bytes = 0;
        };
        std::array<Unit, MAX_SHADER_SAMPLERS> units{};
        std::array<GLboolean, DRAW_CAPS.size()> enabled{};
        std::array<BlockRange, MAX_BLOCK_BINDINGS> blocks{};
        const IndexedColor &state;
        const DrawColorState color;
        std::array<GLfloat, 4> indexed_viewport{};
        std::array<GLint, 4> indexed_scissor{};
        std::array<GLfloat, 2> indexed_depth_range{};
        std::array<GLboolean, 32> clips{};
        bool indexed_scissor_enabled = false, sample_mask_enabled = false;
        std::array<GLint, 4> viewport{}, scissor{};
        std::array<GLfloat, 2> depth_range{};
        GLfloat line_width = 1;
        GLint program, active_texture = 0, array_buffer = 0, vao = 0, framebuffer = 0;
        GLint block_buffer = 0;
        GLint feedback = 0, feedback_buffer = 0;
        unsigned unit_count, block_count;
        bool uses_feedback;

        DrawState(GLint current,
            unsigned samplers,
            unsigned uniform_blocks,
            bool use_feedback,
            const IndexedColor &colors,
            unsigned outputs)
            : state(colors),
              color(colors, outputs),
              program(current),
              unit_count(samplers),
              block_count(uniform_blocks),
              uses_feedback(use_feedback) {
            indexed_scissor_enabled = colors.scissor_test();
            if (colors.viewport_indexed) {
                colors.get_float_indexed(GL_VIEWPORT, 0, indexed_viewport.data());
                colors.get_float_indexed(GL_DEPTH_RANGE, 0, indexed_depth_range.data());
                colors.get_integer_indexed(GL_SCISSOR_BOX, 0, indexed_scissor.data());
            } else {
                glGetIntegerv(GL_VIEWPORT, viewport.data());
                glGetIntegerv(GL_SCISSOR_BOX, scissor.data());
                glGetFloatv(GL_DEPTH_RANGE, depth_range.data());
            }
            sample_mask_enabled = colors.sample_mask && glIsEnabled(SAMPLE_MASK);
            for (unsigned i = 0; i < colors.clip_count; ++i)
                clips[i] = glIsEnabled(CLIP_DISTANCE0 + i);
            glGetFloatv(GL_LINE_WIDTH, &line_width);
            array_buffer = integer(GL_ARRAY_BUFFER_BINDING);
            vao = integer(GL_VERTEX_ARRAY_BINDING);
            framebuffer = integer(GL_DRAW_FRAMEBUFFER_BINDING);
            if (unit_count != 0) {
                active_texture = integer(GL_ACTIVE_TEXTURE);
                for (unsigned i = 0; i < unit_count; ++i) {
                    glActiveTexture(GL_TEXTURE0 + i);
                    units[i] = {integer(GL_TEXTURE_BINDING_2D), integer(GL_SAMPLER_BINDING)};
                }
            }
            if (block_count != 0) {
                block_buffer = integer(GL_UNIFORM_BUFFER_BINDING);
                for (unsigned i = 0; i < block_count; ++i) {
                    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, i, &blocks[i].buffer);
                    if (blocks[i].buffer != 0) {
                        glGetInteger64i_v(GL_UNIFORM_BUFFER_START, i, &blocks[i].start);
                        glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, i, &blocks[i].bytes);
                    }
                }
            }
            if (uses_feedback) {
                feedback = integer(GL_TRANSFORM_FEEDBACK_BINDING);
                feedback_buffer = integer(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING);
            }
            for (std::size_t i = 0; i < DRAW_CAPS.size(); ++i) {
                enabled[i] = glIsEnabled(DRAW_CAPS[i]);
                if (enabled[i]) glDisable(DRAW_CAPS[i]);
            }
            colors.scissor_test(false);
            if (colors.sample_mask) enable(SAMPLE_MASK, false);
            for (unsigned i = 0; i < colors.clip_count; ++i) enable(CLIP_DISTANCE0 + i, false);
            colors.depth_range(0, 1);
            glLineWidth(1);
        }

        ~DrawState() {
            for (std::size_t i = 0; i < DRAW_CAPS.size(); ++i) {
                const auto cap = DRAW_CAPS[i];
                if (enabled[i] || cap == GL_RASTERIZER_DISCARD) enable(cap, enabled[i]);
            }
            state.scissor_test(indexed_scissor_enabled);
            state.depth_range(state.viewport_indexed ? indexed_depth_range[0] : depth_range[0],
                state.viewport_indexed ? indexed_depth_range[1] : depth_range[1]);
            glLineWidth(line_width);
            if (state.viewport_indexed) {
                state.viewport_indexed(0, indexed_viewport.data());
                state.scissor_box(indexed_scissor[0], indexed_scissor[1], indexed_scissor[2], indexed_scissor[3]);
            } else {
                glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
                glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
            }
            if (state.sample_mask) enable(SAMPLE_MASK, sample_mask_enabled);
            for (unsigned i = 0; i < state.clip_count; ++i)
                enable(CLIP_DISTANCE0 + i, clips[i] != GL_FALSE);
            for (unsigned i = 0; i < unit_count; ++i) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, units[i].texture);
                glBindSampler(i, units[i].sampler);
            }
            if (unit_count != 0) {
                glActiveTexture(active_texture);
            }
            for (unsigned i = 0; i < block_count; ++i) {
                const auto &range = blocks[i];
                if (range.buffer != 0 && range.bytes > 0) {
                    glBindBufferRange(GL_UNIFORM_BUFFER, i, range.buffer, range.start, range.bytes);
                } else {
                    glBindBufferBase(GL_UNIFORM_BUFFER, i, range.buffer);
                }
            }
            if (block_count != 0) {
                glBindBuffer(GL_UNIFORM_BUFFER, block_buffer);
            }
            if (uses_feedback) {
                glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, feedback_buffer);
                glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, feedback);
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
            glUseProgram(program);
        }
    };
    struct PrepareState {
        const UploadState pixels;
        GLint array_buffer = integer(GL_ARRAY_BUFFER_BINDING);
        GLint vao = integer(GL_VERTEX_ARRAY_BINDING);
        GLint framebuffer = integer(GL_DRAW_FRAMEBUFFER_BINDING);
        bool scissor = false;
        bool discard = glIsEnabled(GL_RASTERIZER_DISCARD);
        const IndexedColor &color;
        IndexedColor::ColorMask mask;
        GLuint zero_framebuffer = 0;

        explicit PrepareState(const IndexedColor &api) : scissor(api.scissor_test()),
            color(api), mask(api.mask(0)) {
            color.scissor_test(false);
            glDisable(GL_RASTERIZER_DISCARD);
            color.color_mask(0, FULL_MASK);
        }

        ~PrepareState() {
            if (zero_framebuffer != 0) {
                glDeleteFramebuffers(1, &zero_framebuffer);
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
            color.scissor_test(scissor);
            enable(GL_RASTERIZER_DISCARD, discard);
            color.color_mask(0, mask);
        }
    };
    struct TargetScope {
        const Target &target;
        std::vector<GLenum> &buffers;
        GLsizei count = 0;

        TargetScope(const Target &borrowed, std::vector<GLenum> &saved)
            : target(borrowed), buffers(saved) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
        }

        void select() {
            const GLenum dest = target.framebuffer != 0 ? GL_COLOR_ATTACHMENT0 : GL_BACK;
            const auto slots = target.framebuffer != 0 ? buffers.size() : 1;
            for (std::size_t i = 0; i < slots; ++i) {
                buffers[i] = integer(GL_DRAW_BUFFER0 + static_cast<GLenum>(i));
                if (buffers[i] != GL_NONE || i == 0) {
                    count = static_cast<GLsizei>(i + 1);
                }
            }
            if (count == 1 && buffers[0] == dest) {
                count = 0;
            } else {
                glDrawBuffers(1, &dest);
            }
        }

        ~TargetScope() {
            if (count != 0) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
                glDrawBuffers(count, buffers.data());
            }
        }
    };
}  // namespace hyprimate::shader::gles_detail
