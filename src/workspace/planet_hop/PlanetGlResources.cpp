#include <algorithm>
#include <cstddef>
#include <new>
#include <vector>

#include "gl/RawGlState.hpp"
#include "gl/Storage.hpp"
#include "workspace/planet_hop/PlanetGlPrivate.hpp"

namespace hyprimate::workspace {
    bool PlanetGlRenderer::initialize_raw(PlanetPrograms programs) {
        if (programs.planet == 0 || programs.quad == 0 || programs.sky == 0) {
            return false;
        }
        if (m_planet.id == programs.planet && m_quad.id == programs.quad &&
            m_sky.id == programs.sky) {
            return true;
        }
        if (m_planet.id != 0) {
            release_raw();
        }
        m_planet.id = programs.planet;
        m_quad.id = programs.quad;
        m_sky.id = programs.sky;
        detail::cache_program_uniforms(
            m_planet.id, m_planet.uniforms, m_quad.id, m_quad.uniforms, m_sky.id, m_sky.uniforms);
        glGenVertexArrays(1, &m_vao);
        glGenFramebuffers(1, &m_framebuffer);
        for (auto &buffer : m_depth_buffers) {
            glGenRenderbuffers(1, &buffer.id);
        }
        glGenSamplers(1, &m_sampler);
        glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenSamplers(1, &m_mip_sampler);
        glSamplerParameteri(m_mip_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glSamplerParameteri(m_mip_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_mip_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_mip_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (m_vao == 0 || m_framebuffer == 0 ||
            std::ranges::any_of(
                m_depth_buffers, [](const auto &buffer) { return buffer.id == 0; }) ||
            m_sampler == 0 || m_mip_sampler == 0) {
            release_raw();
            return false;
        }
        m_depth_dirty = true;
        return true;
    }
    void PlanetGlRenderer::release_raw() {
        for (auto *program_state : {&m_planet, &m_quad, &m_sky}) {
            program_state->id = 0;
        }
        glDeleteVertexArrays(1, &m_vao);
        for (auto &mesh : m_meshes) {
            glDeleteVertexArrays(1, &mesh.vao);
            glDeleteBuffers(1, &mesh.vertices);
            glDeleteBuffers(1, &mesh.indices);
            mesh = {};
        }
        hyprimate::gl::delete_framebuffer(m_framebuffer);
        for (auto &buffer : m_depth_buffers) {
            glDeleteRenderbuffers(1, &buffer.id);
            buffer = {};
        }
        glDeleteSamplers(1, &m_sampler);
        glDeleteSamplers(1, &m_mip_sampler);
        m_mip_sampler = 0;
        glDeleteTextures(1, &m_depth_texture);
        glDeleteTextures(1, &m_neutral_texture);
        m_neutral_texture = 0;
        m_vao = m_mesh_vao = m_sampler = m_depth_texture = 0;
        m_index_count = 0;
        m_framebuffer = 0;
        m_output_texture = m_output_depth = 0;
        m_next_depth_buffer = 0;
        m_depth_dirty = true;
    }
    bool PlanetGlRenderer::upload_depth() {
        if (!m_depth_dirty) {
            return true;
        }
        if (m_depth_pixels.empty()) {
            glDeleteTextures(1, &m_depth_texture);
            m_depth_texture = 0;
            m_depth_dirty = false;
            return true;
        }
        GLuint candidate = 0;
        bool allocated = false;
        {
            const gl::ClientPixelUnpack UNPACK;
            glActiveTexture(GL_TEXTURE2);
            glGenTextures(1, &candidate);
            glBindTexture(GL_TEXTURE_2D, candidate);
            allocated =
                candidate != 0 && gl::allocate_texture_2d(GL_R8, m_depth_width, m_depth_height);
            if (allocated) {
                glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    m_depth_width,
                    m_depth_height,
                    GL_RED,
                    GL_UNSIGNED_BYTE,
                    m_depth_pixels.data());
            }
        }
        if (!allocated) {
            glDeleteTextures(1, &candidate);
            return false;
        }
        glDeleteTextures(1, &m_depth_texture);
        m_depth_texture = candidate;
        m_depth_dirty = false;
        return true;
    }
    bool PlanetGlRenderer::prepare_neutral() {
        if (m_neutral_texture != 0) {
            return true;
        }
        // Unknown is an opaque neutral identity material, not another workspace's
        // wallpaper. Respect compositor unpack/PBO state even on this one-pixel upload.
        const gl::ClientPixelUnpack UNPACK;
        glActiveTexture(GL_TEXTURE2);
        GLuint candidate = 0;
        glGenTextures(1, &candidate);
        glBindTexture(GL_TEXTURE_2D, candidate);
        const bool ALLOCATED = candidate != 0 && gl::allocate_texture_2d(GL_RGBA8, 1, 1);
        if (ALLOCATED) {
            constexpr std::array<unsigned char, 4> NEUTRAL{72, 78, 86, 255};
            glTexSubImage2D(
                GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, NEUTRAL.data());
        }
        if (!ALLOCATED) {
            glDeleteTextures(1, &candidate);
            return false;
        }
        m_neutral_texture = candidate;
        return m_neutral_texture != 0;
    }
    bool PlanetGlRenderer::prepare_mesh(int quality) {
        quality = std::clamp(quality, 0, 2);
        auto &mesh = m_meshes[static_cast<std::size_t>(quality)];
        if (mesh.vao != 0) {
            m_mesh_vao = mesh.vao;
            m_index_count = mesh.count;
            return true;
        }
        const int COLUMNS = std::array{24, 40, 64}[static_cast<std::size_t>(quality)];
        const int ROWS = COLUMNS;
        const auto VERTICES_PER_SIDE =
            static_cast<std::size_t>(COLUMNS + 1) * static_cast<std::size_t>(ROWS + 1);
        // Independent front/back vertices share their perimeter positions.
        // Keeping the same total cell count as the previous latitude mesh
        // closes the body without introducing a second high-detail surface.
        // Finish fallible CPU construction before publishing any GL cache entry.
        std::vector<std::array<float, 3>> vertices;
        std::vector<GLuint> indices;
        try {
            vertices.reserve(2 * VERTICES_PER_SIDE);
            indices.reserve(
                static_cast<std::size_t>(COLUMNS) * static_cast<std::size_t>(ROWS) * 12U);
            for (std::size_t side = 0; side < 2; ++side) {
                const auto OFFSET = static_cast<GLuint>(side * VERTICES_PER_SIDE);
                for (int row = 0; row <= ROWS; ++row) {
                    for (int column = 0; column <= COLUMNS; ++column) {
                        vertices.push_back(
                            {static_cast<float>(column) / static_cast<float>(COLUMNS),
                                static_cast<float>(row) / static_cast<float>(ROWS),
                                side == 0 ? 1.F : -1.F});
                    }
                }
                for (int row = 0; row < ROWS; ++row) {
                    for (int column = 0; column < COLUMNS; ++column) {
                        const auto FIRST =
                            OFFSET + static_cast<GLuint>((row * (COLUMNS + 1)) + column);
                        const auto NEXT = FIRST + static_cast<GLuint>(COLUMNS + 1);
                        if (side == 0) {
                            indices.insert(
                                indices.end(), {FIRST, NEXT, FIRST + 1, FIRST + 1, NEXT, NEXT + 1});
                        } else {
                            indices.insert(
                                indices.end(), {FIRST, FIRST + 1, NEXT, FIRST + 1, NEXT + 1, NEXT});
                        }
                    }
                }
            }
        } catch (const std::bad_alloc &) {
            return false;
        }
        Mesh candidate;
        const auto DISCARD = [&] {
            glDeleteVertexArrays(1, &candidate.vao);
            glDeleteBuffers(1, &candidate.vertices);
            glDeleteBuffers(1, &candidate.indices);
        };
        glGenVertexArrays(1, &candidate.vao);
        glGenBuffers(1, &candidate.vertices);
        glGenBuffers(1, &candidate.indices);
        if (candidate.vao == 0 || candidate.vertices == 0 || candidate.indices == 0) {
            DISCARD();
            return false;
        }
        glBindVertexArray(candidate.vao);
        glBindBuffer(GL_ARRAY_BUFFER, candidate.vertices);
        if (!gl::allocate_buffer(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(vertices.size() * sizeof(vertices[0])),
                vertices.data(),
                GL_STATIC_DRAW)) {
            DISCARD();
            return false;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, candidate.indices);
        if (!gl::allocate_buffer(GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(indices.size() * sizeof(indices[0])),
                indices.data(),
                GL_STATIC_DRAW)) {
            DISCARD();
            return false;
        }
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertices[0]), nullptr);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3,
            1,
            GL_FLOAT,
            GL_FALSE,
            sizeof(vertices[0]),
            reinterpret_cast<const void *>(2 * sizeof(float)));
        candidate.count = static_cast<GLsizei>(indices.size());
        mesh = candidate;
        m_mesh_vao = mesh.vao;
        m_index_count = mesh.count;
        return true;
    }
    void PlanetGlRenderer::abandon_raw() {
        m_planet = {};
        m_quad = {};
        m_sky = {};
        m_vao = m_mesh_vao = m_sampler = m_depth_texture = 0;
        m_mip_sampler = m_neutral_texture = m_framebuffer = 0;
        m_output_texture = m_output_depth = 0;
        m_depth_buffers = {};
        m_meshes = {};
        m_index_count = 0;
        m_next_depth_buffer = 0;
        m_depth_dirty = true;
    }
    bool PlanetGlRenderer::bind_output(const PlanetFrameTextures &textures) {
        // Replace storage transactionally: a failed growth must retain cached sizes.
        auto *depth_buffer = std::ranges::find_if(m_depth_buffers, [&](const auto &buffer) {
            return buffer.width == textures.width && buffer.height == textures.height;
        });
        GLuint candidate = 0;
        if (depth_buffer == m_depth_buffers.end()) {
            GLint maximum = 0;
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximum);
            if (textures.width > maximum || textures.height > maximum) {
                return false;
            }
            depth_buffer =
                m_depth_buffers.begin() + static_cast<std::ptrdiff_t>(m_next_depth_buffer);
            GLint prior = 0;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &prior);
            glGenRenderbuffers(1, &candidate);
            glBindRenderbuffer(GL_RENDERBUFFER, candidate);
            const bool ALLOCATED =
                candidate != 0 &&
                gl::allocate_renderbuffer(GL_DEPTH_COMPONENT24, textures.width, textures.height);
            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prior));
            if (!ALLOCATED) {
                glDeleteRenderbuffers(1, &candidate);
                return false;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        const GLuint DEPTH = candidate != 0 ? candidate : depth_buffer->id;
        const bool CHANGED = textures.output != m_output_texture || DEPTH != m_output_depth;
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures.output, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, DEPTH);
        const auto ATTACHED = [](GLenum attachment, GLenum type, GLuint name) {
            GLint actual_type = 0;
            GLint actual_name = 0;
            glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &actual_type);
            if (actual_type != static_cast<GLint>(type)) {
                return false;
            }
            glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &actual_name);
            return static_cast<GLuint>(actual_name) == name;
        };
        if ((CHANGED && (!ATTACHED(GL_COLOR_ATTACHMENT0, GL_TEXTURE, textures.output) ||
                            !ATTACHED(GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, DEPTH))) ||
            glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            m_output_texture = m_output_depth = 0;
            glFramebufferRenderbuffer(
                GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffer->id);
            glDeleteRenderbuffers(1, &candidate);
            return false;
        }
        if (candidate != 0) {
            glDeleteRenderbuffers(1, &depth_buffer->id);
            *depth_buffer = {.id = candidate, .width = textures.width, .height = textures.height};
            m_next_depth_buffer = (m_next_depth_buffer + 1) % m_depth_buffers.size();
        }
        m_output_texture = textures.output;
        m_output_depth = DEPTH;
        glViewport(0, 0, textures.width, textures.height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        for (GLuint unit = 0; unit < 5; ++unit) {
            glBindSampler(unit, m_sampler);
        }
        return true;
    }
}  // namespace hyprimate::workspace
