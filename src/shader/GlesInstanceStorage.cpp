#include <algorithm>
#include <cstdint>
#include <ranges>

#include "GlesRuntimePrivate.hpp"

namespace hyprimate::shader {
    using namespace gles_detail;

    namespace {
        constexpr std::array<std::uint8_t, MAX_EXTENT * 16> ZEROS{};
    }
    Instance::Impl::~Impl() {
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
    bool Instance::Impl::size_storage(std::string &error) {
        if (!capture.bounded() || !owner->limits.viewport_fits(capture.width, capture.height)) {
            return fail(error, "capture dimensions exceed the GLES/bounded extent");
        }
        const auto &package = owner->package;
        std::uint64_t bytes = 0;
        const auto add_bytes = [&](std::uint64_t amount) {
            if (amount > MAX_INSTANCE_BYTES || bytes > MAX_INSTANCE_BYTES - amount) return false;
            bytes += amount;
            return true;
        };
        if (!add_bytes(asset_budget_bytes) || !add_bytes(ancillary_bytes)) {
            return fail(error, "shader instance exceeds its 128 MiB storage budget");
        }
        for (std::size_t i = 0; i < package.textures.size(); ++i) {
            const auto &desc = package.textures[i];
            if (!desc.image.empty()) {
                continue;
            }
            const auto size = texture_extent(desc.size, capture);
            if (!size.bounded() || size.width > static_cast<unsigned>(owner->limits.texture) ||
                size.height > static_cast<unsigned>(owner->limits.texture)) {
                return fail(
                    error, "texture dimensions exceed the GLES/bounded extent: " + desc.name);
            }
            textures[i].size = size;
            const auto amount = std::uint64_t{size.width} * size.height * pixel_format(desc.format).bytes;
            if (!add_bytes(amount * (desc.ping_pong ? 2U : 1U)))
                return fail(error, "shader instance exceeds its 128 MiB storage budget");
        }
        fields.resize(package.fields.size());
        if (!fields.empty() && !owner->compute_device) {
            return fail(error, "field storage requires a prepared compute device");
        }
        for (std::size_t i = 0; i < package.fields.size(); ++i) {
            const auto &desc = package.fields[i];
            auto domain = desc.domain.value_or(compute::Domain{});
            if (desc.scale) {
                const auto size = texture_extent(Size{.scale = *desc.scale}, capture);
                domain = {size.width, size.height, 1};
            }
            auto &field = fields[i];
            field.descriptor = {desc.type, domain};
            const auto &caps = owner->compute_device->capabilities();
            const auto layout = compute::field_layout(field.descriptor, caps.width, caps.height,
                caps.max_field_bytes);
            if (!layout) return fail(error, "field '" + desc.name + "': " + layout.error());
            const auto sides = desc.ping_pong ? 2U : 1U;
            if (layout->storage_bytes > MAX_INSTANCE_BYTES / sides ||
                !add_bytes(static_cast<std::uint64_t>(layout->storage_bytes) * sides))
                return fail(error, "shader instance exceeds its 128 MiB storage budget");
        }
        for (const auto &pass : package.passes) {
            if (pass.kernel.empty() || pass.outputs.empty()) continue;
            const auto domain = fields[pass.outputs.front().index].descriptor.domain;
            if (std::ranges::any_of(pass.outputs, [&](Ref ref) {
                    return fields[ref.index].descriptor.domain != domain;
                })) {
                return fail(error, "incompatible compute output domains: " + pass.name);
            }
        }
        for (const auto &buffer : package.buffers) {
            const auto sides = buffer.ping_pong ? 2U : 1U;
            if (buffer.bytes > MAX_INSTANCE_BYTES / sides ||
                !add_bytes(static_cast<std::uint64_t>(buffer.bytes) * sides))
                return fail(error, "shader instance exceeds its 128 MiB storage budget");
        }
        for (std::size_t i = 0; i < package.passes.size(); ++i) {
            const auto &outputs = package.passes[i].outputs;
            if (outputs.empty() || outputs.front().kind != ResourceKind::Texture) {
                continue;
            }
            const auto size = textures[outputs.front().index].size;
            if (!owner->limits.viewport_fits(size.width, size.height) ||
                std::ranges::any_of(
                    outputs, [&](Ref ref) { return textures[ref.index].size != size; })) {
                return fail(
                    error, "incompatible GLES output dimensions: " + package.passes[i].name);
            }
            framebuffers[i].size = size;
        }
        return true;
    }
    GLuint Instance::Impl::texture(Ref ref, const Source &source) const {
        if (ref.kind == ResourceKind::Source) return source.texture;
        if (ref.kind == ResourceKind::Destination) return source.destination_texture;
        if (ref.kind == ResourceKind::Original || ref.kind == ResourceKind::Processed ||
            ref.kind == ResourceKind::EnvironmentState || ref.kind == ResourceKind::Disturbances)
            return environmental_texture(ref.kind, source.environment);
        if (ref.kind != ResourceKind::Texture) return 0;
        if (owner->assets[ref.index] != 0) return owner->assets[ref.index];
        const auto &pair = textures[ref.index];
        return pair.names[pair.read ^ static_cast<unsigned>(ref.side == Side::Write)];
    }
    compute::Field &Instance::Impl::field(Ref ref) {
        auto &pair = fields[ref.index];
        return pair.sides[pair.read ^ static_cast<unsigned>(ref.side == Side::Write)];
    }
    GLuint Instance::Impl::buffer(Ref ref) const {
        const auto &pair = buffers[ref.index];
        return pair.names[pair.read ^ static_cast<unsigned>(ref.side == Side::Write)];
    }
    void Instance::Impl::configure_mesh(unsigned index, bool create) {
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
            glVertexAttribPointer(attr.location,
                attr.components,
                GL_FLOAT,
                GL_FALSE,
                attr.stride,
                reinterpret_cast<const void *>(static_cast<std::uintptr_t>(attr.offset)));
            if (create) {
                glEnableVertexAttribArray(attr.location);
                glVertexAttribDivisor(attr.location, attr.divisor);
            }
            vao.attached[i] = name;
        }
    }
    void Instance::Impl::attach_outputs(DrawFramebuffer &fbo, const Pass &pass) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo.name);
        for (std::size_t i = 0; i < pass.outputs.size(); ++i) {
            const auto name = texture(pass.outputs[i], {});
            if (fbo.attached[i] != name) {
                glFramebufferTexture2D(
                    GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, name, 0);
                fbo.attached[i] = name;
            }
        }
    }
    bool Instance::Impl::clear_texture(
        PrepareState &saved, GLuint name, PixelFormat format, Extent size, std::string &error) {
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
                glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    y,
                    size.width,
                    rows,
                    format.layout,
                    format.type,
                    ZEROS.data());
                stats.upload_bytes += row * rows;
                y += rows;
            }
        }
        return gl_ok(error, "zeroing persistent texture");
    }
    bool Instance::Impl::allocate(std::string &error) {
        if (!size_storage(error)) {
            return false;
        }
        const auto &package = owner->package;
        PrepareState saved(owner->color);
        saved_draw_buffers.resize(static_cast<std::size_t>(owner->limits.draw_buffers));
        for (std::size_t i = 0; i < package.fields.size(); ++i) {
            auto &pair = fields[i];
            const auto sides = package.fields[i].ping_pong ? 2U : 1U;
            for (unsigned side = 0; side < sides; ++side) {
                const auto before = owner->compute_device->statistics();
                auto field = owner->compute_device->allocate(pair.descriptor);
                const auto after = owner->compute_device->statistics();
                compute_stats.allocations += after.allocations - before.allocations;
                compute_stats.pool_hits += after.pool_hits - before.pool_hits;
                compute_stats.barriers += after.barriers - before.barriers;
                compute_stats.fetch_barriers += after.fetch_barriers - before.fetch_barriers;
                compute_stats.framebuffer_barriers += after.framebuffer_barriers - before.framebuffer_barriers;
                compute_stats.update_barriers += after.update_barriers - before.update_barriers;
                compute_stats.image_barriers += after.image_barriers - before.image_barriers;
                compute_stats.upload_bytes += after.upload_bytes - before.upload_bytes;
                if (!field) return fail(error, "allocating field '" + package.fields[i].name + "': " + field.error());
                compute_stats.live_bytes += field->layout().storage_bytes;
                count_storage(stats, field->layout().storage_bytes);
                pair.sides[side] = std::move(*field);
            }
        }
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
                const auto *initial =
                    desc.initial.size() == desc.bytes ? desc.initial.data() : nullptr;
                glBufferData(
                    GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(desc.bytes), initial, GL_DYNAMIC_COPY);
                if (!gl_ok(error, "allocating buffer")) {
                    return false;
                }
                count_storage(stats, desc.bytes);
                if (initial) {
                    stats.upload_bytes += desc.bytes;
                    continue;
                }
                if (!desc.initial.empty()) {
                    glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        static_cast<GLsizeiptr>(desc.initial.size()),
                        desc.initial.data());
                    stats.upload_bytes += desc.initial.size();
                }
                for (std::size_t at = desc.initial.size(); at < desc.bytes;) {
                    const auto bytes = std::min(ZEROS.size(), desc.bytes - at);
                    glBufferSubData(GL_ARRAY_BUFFER,
                        static_cast<GLintptr>(at),
                        static_cast<GLsizeiptr>(bytes),
                        ZEROS.data());
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
                return fail(error,
                    "incomplete shader framebuffer (unsupported GLES format/MRT): " + pass.name);
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
}  // namespace hyprimate::shader
