#include "GlesRuntimePrivate.hpp"


#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hyprimate::shader {
    using namespace gles_detail;







    namespace {





        GLint attachment(GLenum point, GLenum parameter) {
            GLint value = 0;
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, point, parameter, &value);
            return value;
        }

        bool valid_source_uv(const std::array<float, 4> &r) {
            // Permit float rounding at the far edge of a host-computed rect.
            constexpr float ONE = 1.000001F;
            return std::ranges::all_of(r, [](float f) { return std::isfinite(f); }) &&
                   r[0] >= 0 && r[1] >= 0 && r[2] > 0 && r[3] > 0 && r[0] + r[2] <= ONE &&
                   r[1] + r[3] <= ONE;
        }


        void frame_uniforms(const CompiledPass &gpu, const Timing &time, const Source &source,
            Extent extent, const std::array<float, 4> &domain, unsigned iter, unsigned iterations,
            bool blur_mask) {
            const auto &u = gpu.uniforms;
            const auto &f = time.frame;
            // Skip optimized-out uniforms rather than issuing no-op driver calls.
            if (u[Progress] >= 0) glUniform1f(u[Progress], f.progress);
            if (u[Elapsed] >= 0) glUniform1f(u[Elapsed], f.elapsed);
            if (u[Delta] >= 0) glUniform1f(u[Delta], f.delta);
            if (u[Duration] >= 0) glUniform1f(u[Duration], f.duration);
            if (u[Scale] >= 0) glUniform1f(u[Scale], f.scale);
            if (u[SizeXY] >= 0) glUniform2f(u[SizeXY], f.width, f.height);
            if (u[TargetSize] >= 0) glUniform2f(u[TargetSize], static_cast<GLfloat>(extent.width),
                static_cast<GLfloat>(extent.height));
            if (u[Pointer] >= 0) glUniform2f(u[Pointer], f.pointer_x, f.pointer_y);
            if (u[Seed] >= 0) glUniform4fv(u[Seed], 1, time.seed.data());
            if (u[SourceUV] >= 0) glUniform4fv(u[SourceUV], 1, source.uv.data());
            if (u[Domain] >= 0) glUniform4fv(u[Domain], 1, domain.data());
            if (u[EventID] >= 0) glUniform1i(u[EventID], static_cast<GLint>(f.event));
            if (u[PointerValid] >= 0) glUniform1i(u[PointerValid], f.pointer_valid);
            if (u[Fullscreen] >= 0) glUniform1i(u[Fullscreen], f.fullscreen);
            if (u[Iteration] >= 0) glUniform1i(u[Iteration], static_cast<GLint>(iter));
            if (u[Iterations] >= 0) glUniform1i(u[Iterations], static_cast<GLint>(iterations));
            if (u[DestinationUV] >= 0) glUniform4fv(u[DestinationUV], 1, source.destination_uv.data());
            if (u[Geometry] >= 0) glUniform4fv(u[Geometry], 1, f.geometry.data());
            if (u[PreviousGeometry] >= 0) glUniform4fv(u[PreviousGeometry], 1, f.previous_geometry.data());
            if (u[Velocity] >= 0) glUniform4fv(u[Velocity], 1, f.velocity.data());
            if (u[Direction] >= 0) glUniform1f(u[Direction], f.direction);
            if (u[Held] >= 0) glUniform1i(u[Held], f.held);
            if (u[Settling] >= 0) glUniform1i(u[Settling], f.settling);
            if (u[BlurMask] >= 0) glUniform1i(u[BlurMask], blur_mask);
            if (!gpu.uses_environment) return;
            const auto &env = source.environment;
            if (u[OutputSize] >= 0) glUniform2fv(u[OutputSize], 1, env.output_size.data());
            if (u[OutputOrigin] >= 0) glUniform2fv(u[OutputOrigin], 1, env.output_origin.data());
            if (u[OutputRect] >= 0) glUniform4fv(u[OutputRect], 1, env.output_rect.data());
            if (u[OriginalUV] >= 0) glUniform4fv(u[OriginalUV], 1, env.original_uv.data());
            if (u[OriginalAvailable] >= 0) glUniform1i(u[OriginalAvailable], env.original != 0);
            if (u[ProcessedAvailable] >= 0) glUniform1i(u[ProcessedAvailable], env.processed != 0);
            if (u[StateAvailable] >= 0) glUniform1i(u[StateAvailable], env.state != 0);
            if (u[DisturbancesAvailable] >= 0) glUniform1i(u[DisturbancesAvailable], env.disturbances != 0);
            if (u[InteractionCount] >= 0) glUniform1i(u[InteractionCount], static_cast<GLint>(env.interactions.size()));
        }
    }  // namespace














    bool Instance::Impl::owns_texture(GLuint name) const {
            return std::ranges::find(owner->assets, name) != owner->assets.end() ||
                   std::ranges::any_of(textures, [name](const TexturePair &pair) {
                       return pair.names[0] == name || pair.names[1] == name;
                   });
        }
    bool Instance::Impl::validate_target(const Source &source, const Target &target, std::string &error) const {
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                return fail(error, "borrowed output framebuffer is incomplete");
            }
            if (target.framebuffer == 0) {
                return true;
            }
            const auto type = attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE);
            if (type == GL_NONE) {
                return fail(error, "borrowed output is missing color attachment zero");
            }
            const auto color = type == GL_TEXTURE ?
                static_cast<GLuint>(attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME)) : 0;
            const auto &env = source.environment;
            const auto aliases_environment = [&](GLuint name) {
                return owner->package.environment.used() &&
                    (name == env.original || name == env.processed || name == env.state || name == env.disturbances);
            };
            if ((target.texture != 0 && target.texture != color) || color == source.texture ||
                (color && color == source.destination_texture) ||
                (color != 0 && (owns_texture(color) || aliases_environment(color)))) {
                return fail(error, "borrowed framebuffer color attachment aliases an input or disagrees with its texture");
            }
            // Even a disabled draw slot can form a GLES texture/framebuffer feedback loop.
            if (owner->samples_borrowed_target) {
                const auto aliases = [&](GLenum point) {
                    if (attachment(point, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) != GL_TEXTURE) {
                        return false;
                    }
                    const auto name = static_cast<GLuint>(attachment(point, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME));
                    return name == source.texture || name == source.destination_texture ||
                        owns_texture(name) || aliases_environment(name);
                };
                for (GLint i = 1; i < owner->limits.color_attachments; ++i) {
                    if (aliases(GL_COLOR_ATTACHMENT0 + i)) {
                        return fail(error, "borrowed output attachments alias a shader input");
                    }
                }
                if (aliases(GL_DEPTH_ATTACHMENT) || aliases(GL_STENCIL_ATTACHMENT)) {
                    return fail(error, "borrowed depth/stencil attachments alias a shader input");
                }
            }
            const auto component = attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE);
            if (component == GL_INT || component == GL_UNSIGNED_INT) {
                return fail(error, "integer borrowed output is not a native color target");
            }
            if (component == GL_FLOAT && (owner->blends_final || target.composite) && !owner->limits.float_blend &&
                (attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE) == 32 ||
                 attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE) == 32 ||
                 attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE) == 32 ||
                 attachment(GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE) == 32)) {
                return fail(error, "borrowed 32-bit float target does not support blending");
            }
            return true;
        }
    void Instance::Impl::submit_mesh(const Pass &pass) {
            const auto *mesh = pass_mesh(owner->package, pass);
            if (!mesh) {
                glDrawArrays(GL_TRIANGLES, 0, 3);
                return;
            }
            const auto primitive = draw_primitive(mesh->primitive);
            if (!mesh->indices.empty()) {
                if (mesh->instances == 1) {
                    glDrawElements(primitive, static_cast<GLsizei>(mesh->indices.size()), GL_UNSIGNED_INT, nullptr);
                } else {
                    glDrawElementsInstanced(primitive, static_cast<GLsizei>(mesh->indices.size()),
                        GL_UNSIGNED_INT, nullptr, mesh->instances);
                }
            } else if (mesh->instances == 1) {
                glDrawArrays(primitive, 0, mesh->count);
            } else {
                glDrawArraysInstanced(primitive, 0, mesh->count, mesh->instances);
            }
        }
    bool Instance::Impl::draw(std::size_t index, unsigned iteration, const Timing &time, const Source &source,
            const Target &target, unsigned &active_blocks, bool &clear_final, std::string &error) {
            constexpr std::array<float, 4> FULL_DOMAIN{0, 0, 1, 1};
            const auto &pass = owner->package.passes[index];
            const auto &gpu = owner->passes[index];
            auto &fbo = framebuffers[index];
            const bool final = fbo.name == 0 && !pass.transform_feedback;
            const auto size = final ? Extent{target.width, target.height} :
                pass.transform_feedback ? capture : fbo.size;
            if (final || pass.transform_feedback) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
            } else {
                attach_outputs(fbo, pass);
            }
            if (!pass.transform_feedback) {
                const auto x = final ? target.origin_x : 0;
                const auto y = final ? target.origin_y : 0;
                glViewport(x, y, size.width, size.height);
                enable(GL_SCISSOR_TEST, final);
                if (final) {
                    glScissor(x, y, size.width, size.height);
                }
                const bool composite = final && target.composite;
                owner->color.blend(static_cast<unsigned>(pass.outputs.size()), composite || pass.blend);
                const bool clear = !composite && (pass.clear || (final && clear_final));
                if (final) clear_final = false;
                if (clear) {
                    for (std::size_t i = 0; i < pass.outputs.size(); ++i) {
                        glClearBufferfv(GL_COLOR, static_cast<GLint>(i), TRANSPARENT.data());
                    }
                }
            }
            glUseProgram(gpu.program);
            if (pass.mesh >= 0) {
                configure_mesh(static_cast<unsigned>(pass.mesh), false);
            } else {
                glBindVertexArray(empty_vao);
            }
            for (unsigned i = 0; i < gpu.sampler_count; ++i) {
                const auto ref = gpu.samplers[i];
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, texture(ref, source));
                glBindSampler(i, linear_host_image(ref.kind) ? owner->source_sampler : 0);
            }
            for (unsigned i = 0; i < gpu.block_count; ++i) {
                const auto &block = gpu.blocks[i];
                glBindBufferRange(GL_UNIFORM_BUFFER, i, buffer(block.buffer), 0, block.bytes);
            }
            // A later TF destination may have been bound to a trailing slot by
            // an earlier pass. No stale UBO binding may outlive that pass.
            for (unsigned i = gpu.block_count; i < active_blocks; ++i) {
                glBindBufferBase(GL_UNIFORM_BUFFER, i, 0);
            }
            active_blocks = gpu.block_count;
            frame_uniforms(gpu, time, source, size, final ? target.domain : FULL_DOMAIN, iteration,
                pass.iterations, target.blur_mask);
            if (gpu.interactions >= 0) {
                const auto vectors = std::min(gpu.interaction_vectors,
                    static_cast<GLint>(source.environment.interactions.size() * 4));
                static_assert(sizeof(Interaction) == sizeof(float) * 16);
                // Linked programs are shared across instances. Overwrite the whole reflected
                // array so inactive slots cannot expose an earlier output's interactions.
                std::array<float, MAX_INTERACTIONS * 16> packed{};
                if (vectors > 0) {
                    std::memcpy(packed.data(), source.environment.interactions.data(),
                        static_cast<std::size_t>(vectors) * 4 * sizeof(float));
                }
                glUniform4fv(gpu.interactions, gpu.interaction_vectors, packed.data());
                stats.upload_bytes += static_cast<std::size_t>(gpu.interaction_vectors) * 4 * sizeof(float);
            }
            if (pass.transform_feedback) {
                const auto &mesh = owner->package.meshes[static_cast<unsigned>(pass.mesh)];
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glEnable(GL_RASTERIZER_DISCARD);
                glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, feedback);
                glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer(pass.feedback));
                glBeginTransformFeedback(draw_primitive(mesh.primitive));
                submit_mesh(pass);
                glEndTransformFeedback();
                // End does not detach the destination. Subsequent VAO and UBO
                // consumers must see sequential, not simultaneous, bindings.
                glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
                glDisable(GL_RASTERIZER_DISCARD);
            } else {
                if (final && target.composite) {
                    if (!gl_ok(error, "preparing direct shader submission")) return false;
                    for (const auto &clip : target.clips) {
                        glScissor(clip.x, clip.y, clip.width, clip.height);
                        if (target.submitted) *target.submitted = true;
                        submit_mesh(pass);
                        if (!gl_ok(error, "executing direct shader submission")) return false;
                    }
                } else {
                    submit_mesh(pass);
                }
            }
            if (!gl_ok(error, "executing native shader pass")) {
                return false;
            }
            if (pass.transform_feedback && pass.feedback.side == Side::Write) {
                buffers[pass.feedback.index].read ^= 1U;
            }
            for (const Ref output : pass.outputs) {
                if (output.side == Side::Write) {
                    textures[output.index].read ^= 1U;
                }
            }
            stats.draws += final && target.composite ? target.clips.size() : 1;
            switch (pass.phase) {
                case Phase::Initialize: ++stats.initialize_passes; break;
                case Phase::Update: ++stats.update_passes; break;
                case Phase::Render: ++stats.render_passes; break;
            }
            return true;
        }
    bool Instance::Impl::phase(Phase phase, const Timing &frame, const Source &source, const Target &target,
            unsigned &active_blocks, bool &clear_final, std::string &error) {
            const auto &passes = owner->package.passes;
            for (std::size_t i = 0; i < passes.size(); ++i) {
                if (passes[i].phase != phase) {
                    continue;
                }
                for (unsigned iteration = 0; iteration < passes[i].iterations; ++iteration) {
                    if (!draw(i, iteration, frame, source, target, active_blocks, clear_final, error)) {
                        // Only a partial simulation can invalidate committed state.
                        // Render scratch is produced again on the next invocation.
                        faulted = phase != Phase::Render;
                        return false;
                    }
                }
            }
            return true;
        }
    Instance::Instance() = default;
    Instance::~Instance() = default;

    bool Instance::initialize(Program &program, unsigned width, unsigned height, std::string &error) {
        error.clear();
        if (!program.m_impl || !program.m_impl->armed) {
            return fail(error, "no prepared GLES shader program");
        }
        const Extent capture{width, height};
        if (m_impl && m_impl->owner == program.m_impl && m_impl->capture == capture && !m_impl->faulted) {
            return true;
        }
        if (m_impl && m_impl->owner == program.m_impl && !m_impl->faulted && capture.bounded() &&
            m_impl->owner->limits.viewport_fits(width, height)) {
            const auto &textures = m_impl->owner->package.textures;
            bool same_storage = true;
            for (std::size_t i = 0; i < textures.size(); ++i) {
                if (textures[i].image.empty() &&
                    texture_extent(textures[i].size, capture) != m_impl->textures[i].size) {
                    same_storage = false;
                    break;
                }
            }
            if (same_storage) {
                m_impl->capture = capture;
                return true;
            }
        }
        if (!gl_ok(error, "entering instance preparation")) {
            return false;
        }
        // Allocating GL resources also mutates bindings that the caller's active TF may need.
        if (integer(GL_TRANSFORM_FEEDBACK_ACTIVE) && !integer(GL_TRANSFORM_FEEDBACK_PAUSED)) {
            return fail(error, "caller transform feedback is active and unpaused");
        }
        auto instance = std::make_unique<Impl>();
        instance->owner = program.m_impl;
        instance->capture = capture;
        if (!instance->allocate(error)) {
            discard_errors();
            return false;
        }
        if (!gl_ok(error, "restoring caller state after instance preparation")) {
            return false;
        }
        m_impl = std::move(instance);
        return true;
    }

    void Instance::advance(const Frame &frame) noexcept {
        if (!m_impl || (m_impl->have_tick && frame.tick <= m_impl->pending_frame.frame.tick)) {
            return;
        }
        auto &instance = *m_impl;
        const auto delta = instance.pending ? instance.pending_frame.frame.delta : 0;
        if (frame.seed != instance.pending_frame.frame.seed) {
            instance.pending_frame.seed = seed_values(frame.seed);
        }
        instance.pending_frame.frame = frame;
        instance.pending_frame.frame.delta += delta;
        instance.have_tick = true;
        instance.pending = true;
    }

    GLuint Instance::exported_texture(Ref resource) const noexcept {
        if (!m_impl || !m_impl->primed || m_impl->faulted || !m_impl->owner->armed) return 0;
        const auto &env = m_impl->owner->package.environment;
        return env.state == resource || env.emit == resource ? m_impl->texture(resource, {}) : 0;
    }

    bool Instance::render(const Source &source, const Target &target, bool advance_simulation,
        std::string &error) {
        error.clear();
        if (!m_impl || !m_impl->owner->armed || m_impl->faulted) {
            return fail(error, "no usable shader instance");
        }
        auto &instance = *m_impl;
        const auto &program = *instance.owner;
        if (!advance_simulation && !instance.primed) {
            return fail(error, "read-only shader draw requires a live initialization");
        }
        const Extent extent{target.width, target.height};
        constexpr auto MAX_COORDINATE = std::numeric_limits<GLint>::max();
        if (!extent.bounded() || !program.limits.viewport_fits(extent.width, extent.height) ||
            target.origin_x < 0 || target.origin_y < 0 ||
            std::int64_t{target.origin_x} + extent.width > MAX_COORDINATE ||
            std::int64_t{target.origin_y} + extent.height > MAX_COORDINATE ||
            !std::ranges::all_of(target.domain, [](float f) { return std::isfinite(f); }) ||
            target.domain[2] <= 0 || target.domain[3] <= 0) {
            return fail(error, "invalid borrowed shader target dimensions or domain");
        }
        if (target.composite &&
            (!program.direct() || target.clear_before_render ||
                !std::ranges::all_of(target.clips, [&](const Clip &clip) {
                    return clip.x >= target.origin_x && clip.y >= target.origin_y &&
                        clip.width > 0 && clip.height > 0 &&
                        std::int64_t{clip.x} + clip.width <= std::int64_t{target.origin_x} + extent.width &&
                        std::int64_t{clip.y} + clip.height <= std::int64_t{target.origin_y} + extent.height;
                }))) {
            return fail(error, "incompatible in-place shader presentation");
        }
        if (source.texture == 0 || source.width == 0 || source.height == 0 ||
            source.width > static_cast<unsigned>(program.limits.texture) ||
            source.height > static_cast<unsigned>(program.limits.texture) ||
            source.texture == target.texture || instance.owns_texture(source.texture) ||
            (target.framebuffer == 0 && target.texture != 0) || !valid_source_uv(source.uv) ||
            !glIsTexture(source.texture) || (target.framebuffer != 0 && !glIsFramebuffer(target.framebuffer))) {
            return fail(error, "invalid or aliased borrowed shader texture/framebuffer");
        }
        if (program.uses_destination &&
            (!source.destination_texture || source.destination_texture == target.texture ||
                instance.owns_texture(source.destination_texture) ||
                !valid_source_uv(source.destination_uv) || !glIsTexture(source.destination_texture))) {
            return fail(error, "invalid or aliased borrowed destination texture");
        }
        if (program.package.environment.used() && source.environment.interactions.size() > MAX_INTERACTIONS)
            return fail(error, "environment interaction batch exceeds the per-step limit");
        if (!gl_ok(error, "entering native shader draw")) {
            return false;
        }
        const auto current = integer(GL_CURRENT_PROGRAM);
        if (!can_use_program(current, error)) {
            return false;
        }
        const bool use_feedback = program.has_feedback && advance_simulation &&
                                  (!instance.primed || instance.pending);
        for (const GLenum type : {GL_ANY_SAMPLES_PASSED, GL_ANY_SAMPLES_PASSED_CONSERVATIVE,
                 GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN}) {
            if (type == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN && !use_feedback) {
                continue;
            }
            GLint query = 0;
            glGetQueryiv(type, GL_CURRENT_QUERY, &query);
            if (query != 0) {
                return fail(error, "native draw would contaminate a caller visibility/feedback query");
            }
        }
        const DrawState guard(current, program.sampler_count, program.block_count, use_feedback,
            program.color, program.max_outputs);
        TargetScope scoped_target(target, instance.saved_draw_buffers);
        scoped_target.select();
        if (!instance.validate_target(source, target, error)) {
            return false;
        }
        if (program.source_sampler != 0 || (program.package.environment.used() && program.sampler_count)) {
            // A non-2D texture can pass glIsTexture; catch that before any simulation work.
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, source.texture);
            if (program.uses_destination) glBindTexture(GL_TEXTURE_2D, source.destination_texture);
            if (program.package.environment.used()) {
                for (auto texture : {source.environment.original, source.environment.processed,
                         source.environment.state, source.environment.disturbances}) {
                    if (!texture) continue;
                    if (instance.owns_texture(texture) || !glIsTexture(texture))
                        return fail(error, "invalid or aliased borrowed environmental image");
                    glBindTexture(GL_TEXTURE_2D, texture);
                }
            }
        }
        if (!gl_ok(error, "validating borrowed shader resources")) {
            return false;
        }
        unsigned active_blocks = 0;
        bool clear_final = target.clear_before_render;
        if (advance_simulation) {
            if (!instance.primed &&
                !instance.phase(Phase::Initialize, instance.pending_frame, source, target,
                    active_blocks, clear_final, error)) {
                return false;
            }
            if (instance.pending &&
                !instance.phase(Phase::Update, instance.pending_frame, source, target,
                    active_blocks, clear_final, error)) {
                return false;
            }
            if (instance.pending || !instance.primed) {
                instance.committed_frame = instance.pending_frame;
                instance.pending = false;
                instance.primed = true;
            }
        }
        return instance.phase(Phase::Render, instance.committed_frame, source, target,
            active_blocks, clear_final, error);
    }

    void Instance::release() noexcept { m_impl.reset(); }

    void Instance::abandon() noexcept {
        if (m_impl) {
            m_impl->armed = false;
            if (m_impl->owner.use_count() == 1) {
                m_impl->owner->armed = false;
            }
            m_impl.reset();
        }
    }

    const Statistics &Instance::statistics() const noexcept { return m_impl ? m_impl->stats : EMPTY_STATS; }
}  // namespace hyprimate::shader
