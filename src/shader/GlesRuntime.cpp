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
        constexpr std::array<std::uint8_t, MAX_EXTENT * 16> ZEROS{};













    }  // namespace








    namespace {

        // UBO indexed bindings are context state; TF indexed bindings belong to
        // the selected TF object and are preserved by reselecting the caller's object.


        struct TexturePair {
            Extent size;
            std::array<GLuint, 2> names{};
            unsigned read = 0;
        };

        struct BufferPair {
            std::array<GLuint, 2> names{};
            unsigned read = 0;
        };

        struct DrawFramebuffer {
            GLuint name = 0;
            Extent size;
            std::array<GLuint, MAX_TEXTURES> attached{};
        };

        struct VertexArray {
            GLuint name = 0;
            std::array<GLuint, 16> attached{};
        };


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

        std::array<float, 4> seed_values(std::uint64_t seed) {
            std::array<float, 4> values{};
            for (auto &value : values) {
                // SplitMix64. A high 24-bit integer converts to float without rounding to 1.
                auto x = (seed += 0x9e3779b97f4a7c15ULL);
                x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
                x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
                x ^= x >> 31;
                value = static_cast<float>(x >> 40) * (1.F / 16777216.F);
            }
            return values;
        }

        struct Timing {
            Frame frame;
            std::array<float, 4> seed = seed_values(0);
        };

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

    struct Instance::Impl {
        std::shared_ptr<Program::Impl> owner;
        Extent capture;
        Statistics stats;
        std::array<TexturePair, MAX_TEXTURES> textures;
        std::array<BufferPair, MAX_BUFFERS> buffers;
        std::array<DrawFramebuffer, MAX_PASSES> framebuffers;
        std::array<VertexArray, MAX_MESHES> vaos;
        std::vector<GLenum> saved_draw_buffers;
        GLuint empty_vao = 0, feedback = 0;
        Timing pending_frame, committed_frame;
        bool have_tick = false, pending = false, primed = false, faulted = false, armed = true;

        ~Impl() {
            if (!armed) {
                return;
            }
            if (feedback != 0) {
                glDeleteTransformFeedbacks(1, &feedback);
            }
            if (empty_vao != 0) {
                glDeleteVertexArrays(1, &empty_vao);
            }
            for (const auto &vao : vaos) {
                if (vao.name != 0) {
                    glDeleteVertexArrays(1, &vao.name);
                }
            }
            for (const auto &fbo : framebuffers) {
                if (fbo.name != 0) {
                    glDeleteFramebuffers(1, &fbo.name);
                }
            }
            for (const auto &buffer : buffers) {
                if (buffer.names[0] != 0) {
                    glDeleteBuffers(static_cast<GLsizei>(buffer.names.size()), buffer.names.data());
                }
            }
            for (const auto &texture : textures) {
                if (texture.names[0] != 0) {
                    glDeleteTextures(static_cast<GLsizei>(texture.names.size()), texture.names.data());
                }
            }
        }

        bool size_storage(std::string &error) {
            if (!capture.bounded() || !owner->limits.viewport_fits(capture.width, capture.height)) {
                return fail(error, "capture dimensions exceed the GLES/bounded extent");
            }
            const auto &package = owner->package;
            std::uint64_t bytes = 0;
            for (std::size_t i = 0; i < package.textures.size(); ++i) {
                const auto &desc = package.textures[i];
                if (!desc.image.empty()) {
                    continue;
                }
                const auto size = texture_extent(desc.size, capture);
                if (!size.bounded() || size.width > static_cast<unsigned>(owner->limits.texture) ||
                    size.height > static_cast<unsigned>(owner->limits.texture)) {
                    return fail(error, "texture dimensions exceed the GLES/bounded extent: " + desc.name);
                }
                textures[i].size = size;
                bytes += std::uint64_t{size.width} * size.height * pixel_format(desc.format).bytes *
                         (desc.ping_pong ? 2 : 1);
            }
            for (const auto &buffer : package.buffers) {
                bytes += buffer.bytes * (buffer.ping_pong ? 2 : 1);
            }
            if (bytes > MAX_INSTANCE_BYTES) {
                return fail(error, "shader instance exceeds its 128 MiB storage budget");
            }
            for (std::size_t i = 0; i < package.passes.size(); ++i) {
                const auto &outputs = package.passes[i].outputs;
                if (outputs.empty() || outputs.front().kind != ResourceKind::Texture) {
                    continue;
                }
                const auto size = textures[outputs.front().index].size;
                if (!owner->limits.viewport_fits(size.width, size.height) ||
                    std::ranges::any_of(outputs, [&](Ref ref) { return textures[ref.index].size != size; })) {
                    return fail(error, "incompatible GLES output dimensions: " + package.passes[i].name);
                }
                framebuffers[i].size = size;
            }
            return true;
        }

        GLuint texture(Ref ref, const Source &source) const {
            if (ref.kind == ResourceKind::Source) {
                return source.texture;
            }
            if (ref.kind == ResourceKind::Destination) {
                return source.destination_texture;
            }
            if (ref.kind >= ResourceKind::Original) return environmental_texture(ref.kind, source.environment);
            if (owner->assets[ref.index] != 0) {
                return owner->assets[ref.index];
            }
            const auto &pair = textures[ref.index];
            return pair.names[pair.read ^ static_cast<unsigned>(ref.side == Side::Write)];
        }

        GLuint buffer(Ref ref) const {
            const auto &pair = buffers[ref.index];
            return pair.names[pair.read ^ static_cast<unsigned>(ref.side == Side::Write)];
        }

        void configure_mesh(unsigned index, bool create) {
            const auto &mesh = owner->package.meshes[index];
            auto &vao = vaos[index];
            glBindVertexArray(vao.name);
            if (create && owner->elements[index] != 0) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, owner->elements[index]);
            }
            for (std::size_t i = 0; i < mesh.attributes.size(); ++i) {
                const auto &attr = mesh.attributes[i];
                const auto name = buffer(attr.buffer);
                if (vao.attached[i] == name) {
                    continue;
                }
                glBindBuffer(GL_ARRAY_BUFFER, name);
                glVertexAttribPointer(attr.location, attr.components, GL_FLOAT, GL_FALSE, attr.stride,
                    reinterpret_cast<const void *>(static_cast<std::uintptr_t>(attr.offset)));
                if (create) {
                    glEnableVertexAttribArray(attr.location);
                    glVertexAttribDivisor(attr.location, attr.divisor);
                }
                vao.attached[i] = name;
            }
        }

        void attach_outputs(DrawFramebuffer &fbo, const Pass &pass) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo.name);
            for (std::size_t i = 0; i < pass.outputs.size(); ++i) {
                const auto name = texture(pass.outputs[i], {});
                if (fbo.attached[i] != name) {
                    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                        GL_TEXTURE_2D, name, 0);
                    fbo.attached[i] = name;
                }
            }
        }

        bool clear_texture(PrepareState &saved, GLuint name, PixelFormat format, Extent size,
            std::string &error) {
            if (saved.zero_framebuffer == 0) {
                glGenFramebuffers(1, &saved.zero_framebuffer);
                if (saved.zero_framebuffer == 0) {
                    return fail(error, "cannot create texture initialization framebuffer");
                }
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, saved.zero_framebuffer);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, name, 0);
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glClearBufferfv(GL_COLOR, 0, TRANSPARENT.data());
            } else {
                // A legal sampled floating-point format need not be color-renderable.
                const auto row = std::size_t{size.width} * format.bytes;
                const auto batch = static_cast<unsigned>(ZEROS.size() / row);
                for (unsigned y = 0; y < size.height;) {
                    const auto rows = std::min(batch, size.height - y);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, size.width, rows, format.layout,
                        format.type, ZEROS.data());
                    stats.upload_bytes += row * rows;
                    y += rows;
                }
            }
            return gl_ok(error, "zeroing persistent texture");
        }

        bool allocate(std::string &error) {
            if (!size_storage(error)) {
                return false;
            }
            const auto &package = owner->package;
            PrepareState saved(owner->color);
            saved_draw_buffers.resize(static_cast<std::size_t>(owner->limits.draw_buffers));
            for (std::size_t i = 0; i < package.textures.size(); ++i) {
                const auto &desc = package.textures[i];
                if (!desc.image.empty()) {
                    continue;
                }
                auto &pair = textures[i];
                const auto format = pixel_format(desc.format);
                const auto size = pair.size;
                for (unsigned side = 0; side < (desc.ping_pong ? 2U : 1U); ++side) {
                    auto &name = pair.names[side];
                    texture_storage(name, desc, size);
                    if (!gl_ok(error, "allocating texture")) {
                        return false;
                    }
                    count_storage(stats, std::size_t{size.width} * size.height * format.bytes);
                    if (desc.lifetime == Lifetime::Persistent &&
                        !clear_texture(saved, name, format, size, error)) {
                        return false;
                    }
                }
            }
            for (std::size_t i = 0; i < package.buffers.size(); ++i) {
                const auto &desc = package.buffers[i];
                auto &pair = buffers[i];
                for (unsigned side = 0; side < (desc.ping_pong ? 2U : 1U); ++side) {
                    auto &name = pair.names[side];
                    glGenBuffers(1, &name);
                    glBindBuffer(GL_ARRAY_BUFFER, name);
                    const auto *initial = desc.initial.size() == desc.bytes ? desc.initial.data() : nullptr;
                    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.bytes), initial,
                        GL_DYNAMIC_COPY);
                    if (!gl_ok(error, "allocating buffer")) {
                        return false;
                    }
                    count_storage(stats, desc.bytes);
                    if (initial) {
                        stats.upload_bytes += desc.bytes;
                        continue;
                    }
                    if (!desc.initial.empty()) {
                        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(desc.initial.size()),
                            desc.initial.data());
                        stats.upload_bytes += desc.initial.size();
                    }
                    for (std::size_t at = desc.initial.size(); at < desc.bytes;) {
                        const auto bytes = std::min(ZEROS.size(), desc.bytes - at);
                        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(at),
                            static_cast<GLsizeiptr>(bytes), ZEROS.data());
                        at += bytes;
                        stats.upload_bytes += bytes;
                    }
                    if (!gl_ok(error, "initializing buffer")) {
                        return false;
                    }
                }
            }
            for (std::size_t i = 0; i < package.passes.size(); ++i) {
                auto &fbo = framebuffers[i];
                if (fbo.size.width == 0) {
                    continue;
                }
                const auto &pass = package.passes[i];
                glGenFramebuffers(1, &fbo.name);
                if (fbo.name == 0) {
                    return fail(error, "cannot create shader output framebuffer");
                }
                attach_outputs(fbo, pass);
                std::array<GLenum, MAX_TEXTURES> slots;
                for (std::size_t j = 0; j < pass.outputs.size(); ++j) {
                    slots[j] = GL_COLOR_ATTACHMENT0 + j;
                }
                glDrawBuffers(static_cast<GLsizei>(pass.outputs.size()), slots.data());
                if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    return fail(error, "incomplete shader framebuffer (unsupported GLES format/MRT): " + pass.name);
                }
            }
            for (unsigned i = 0; i < package.meshes.size(); ++i) {
                glGenVertexArrays(1, &vaos[i].name);
                if (vaos[i].name == 0) {
                    return fail(error, "cannot create shader mesh VAO");
                }
                configure_mesh(i, true);
            }
            if (std::ranges::any_of(package.passes, [](const Pass &pass) { return pass.mesh < 0; })) {
                glGenVertexArrays(1, &empty_vao);
                if (empty_vao == 0) {
                    return fail(error, "cannot create shader fullscreen VAO");
                }
            }
            if (owner->has_feedback) {
                glGenTransformFeedbacks(1, &feedback);
                if (feedback == 0) {
                    return fail(error, "cannot create shader transform feedback object");
                }
            }
            return gl_ok(error, "preparing shader instance");
        }

        bool owns_texture(GLuint name) const {
            return std::ranges::find(owner->assets, name) != owner->assets.end() ||
                   std::ranges::any_of(textures, [name](const TexturePair &pair) {
                       return pair.names[0] == name || pair.names[1] == name;
                   });
        }

        bool validate_target(const Source &source, const Target &target, std::string &error) const {
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

        void submit_mesh(const Pass &pass) {
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

        bool draw(std::size_t index, unsigned iteration, const Timing &time, const Source &source,
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

        bool phase(Phase phase, const Timing &frame, const Source &source, const Target &target,
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
    };

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
