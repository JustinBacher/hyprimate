#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>
#include <numbers>

#include "gl/Storage.hpp"
#include "workspace/planet_hop/PlanetGlRenderer.hpp"

namespace hyprimate::workspace {
    namespace {
        constexpr std::array<float, 16> IDENTITY{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        constexpr std::array<const char *, 43> PLANET_UNIFORMS{"viewProjection",
            "planetRotation",
            "planetCenter",
            "curvature",
            "endpointEnvelope",
            "elevation",
            "depthStrength",
            "depthImage",
            "depthSource",
            "samplingQuality",
            "image",
            "uvTransform",
            "layer",
            "opacity",
            "lightDirection",
            "nightBrightness",
            "indirectBrightness",
            "cloudCoverage",
            "cloudOpacity",
            "cloudSpeed",
            "time",
            "atmosphereIntensity",
            "atmosphereThickness",
            "lightingEnabled",
            "cloudsEnabled",
            "backdrop",
            "reconstructLayer",
            "uDepthEnabled",
            "radius",
            "coverage",
            "aspect",
            "atmosphereEnabled",
            "cameraPosition",
            "planetRadius",
            "cloudHeight",
            "parametricMesh",
            "fullSphere",
            "previousImage",
            "previousBackdrop",
            "previousReconstruct",
            "liveMix",
            "identityTint",
            "opaqueCapture"};
        constexpr std::array<const char *, 21> QUAD_UNIFORMS{"viewProjection",
            "model",
            "image",
            "uvTransform",
            "opacity",
            "imageRect",
            "backdrop",
            "reconstructLayer",
            "forceOpaque",
            "secondaryImage",
            "secondaryBackdrop",
            "secondaryReconstruct",
            "mixAmount",
            "previousImage",
            "previousBackdrop",
            "previousReconstruct",
            "liveMix",
            "secondaryPreviousImage",
            "secondaryPreviousBackdrop",
            "secondaryPreviousReconstruct",
            "secondaryLiveMix"};
        constexpr std::array<const char *, 7> SKY_UNIFORMS{"inverseViewProjection",
            "cameraPosition",
            "endpointEnvelope",
            "starDensity",
            "starOffsetNear",
            "starOffsetMid",
            "starOffsetFar"};
        float capture_mix(const PlanetLayerTextures &layer) {
            return layer.previous_image != 0 && std::isfinite(layer.live_mix)
                       ? std::clamp(layer.live_mix, 0.0F, 1.0F)
                       : 1.0F;
        }

    }  // namespace

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
        const auto CACHE = [](auto &target, const auto &names) {
            target.uniforms.fill(-1);
            for (std::size_t i = 0; i < names.size(); ++i) {
                target.uniforms[i] = glGetUniformLocation(target.id, names[i]);
            }
        };
        CACHE(m_planet, PLANET_UNIFORMS);
        CACHE(m_quad, QUAD_UNIFORMS);
        CACHE(m_sky, SKY_UNIFORMS);
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
        glDeleteFramebuffers(1, &m_framebuffer);
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

    void PlanetGlRenderer::draw_quad(const QuadDraw &quad) const {
        const auto &primary = quad.primary;
        const auto &view_projection = quad.view_projection.get();
        const auto &model = quad.model.get();
        const auto OPACITY = quad.opacity;
        const auto &rect = quad.rect;
        const auto FORCE_OPAQUE = quad.force_opaque;
        const auto &secondary = quad.secondary;
        const auto MIX = quad.mix;
        const auto TOP_FIRST = quad.top_first;
        if (primary.image == 0 || OPACITY <= 0) {
            return;
        }
        const auto &uniforms = m_quad.uniforms;
        glUseProgram(m_quad.id);
        glUniformMatrix4fv(uniforms[0], 1, GL_FALSE, view_projection.data());
        glUniformMatrix4fv(uniforms[1], 1, GL_FALSE, model.data());
        glUniform1i(uniforms[2], 0);
        glUniform4f(uniforms[3], 0, TOP_FIRST ? 0 : 1, 1, TOP_FIRST ? 1 : -1);
        glUniform1f(uniforms[4], OPACITY);
        glUniform4fv(uniforms[5], 1, rect.data());
        glUniform1i(uniforms[6], 1);
        glUniform1i(uniforms[7], static_cast<GLint>(primary.backdrop != 0));
        glUniform1i(uniforms[8], static_cast<GLint>(FORCE_OPAQUE));
        glUniform1i(uniforms[9], 3);
        glUniform1i(uniforms[10], 4);
        glUniform1i(uniforms[11], static_cast<GLint>(secondary.backdrop != 0));
        glUniform1f(uniforms[12], MIX);
        glUniform1i(uniforms[13], 5);
        glUniform1i(uniforms[14], 6);
        glUniform1i(uniforms[15], static_cast<GLint>(primary.previous_backdrop != 0));
        glUniform1f(uniforms[16], capture_mix(primary));
        glUniform1i(uniforms[17], 7);
        glUniform1i(uniforms[18], 8);
        glUniform1i(uniforms[19], static_cast<GLint>(secondary.previous_backdrop != 0));
        glUniform1f(uniforms[20], capture_mix(secondary));
        const auto BIND = [](GLenum unit, GLuint texture) {
            glActiveTexture(unit);
            glBindTexture(GL_TEXTURE_2D, texture);
        };
        BIND(GL_TEXTURE0, primary.image);
        BIND(GL_TEXTURE1, primary.backdrop != 0 ? primary.backdrop : primary.image);
        if (MIX > 0) {
            BIND(GL_TEXTURE3, secondary.image != 0 ? secondary.image : primary.image);
            BIND(GL_TEXTURE4, secondary.backdrop != 0 ? secondary.backdrop : primary.image);
        }
        if (capture_mix(primary) < 1) {
            glBindSampler(5, m_sampler);
            glBindSampler(6, m_sampler);
            BIND(GL_TEXTURE5, primary.previous_image);
            BIND(GL_TEXTURE6,
                primary.previous_backdrop != 0 ? primary.previous_backdrop
                                               : primary.previous_image);
        }
        if (MIX > 0 && capture_mix(secondary) < 1) {
            glBindSampler(7, m_sampler);
            glBindSampler(8, m_sampler);
            BIND(GL_TEXTURE7, secondary.previous_image);
            BIND(GL_TEXTURE8,
                secondary.previous_backdrop != 0 ? secondary.previous_backdrop
                                                 : secondary.previous_image);
        }
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
    bool PlanetGlRenderer::valid_draw(
        const DrawContext &context, std::span<const PlanetNeighborTextures> neighbors) {
        const auto &textures = context.textures.get();
        const auto &frame = context.frame.get();
        const auto PROGRESS = context.progress;
        if (textures.width <= 0 || textures.height <= 0 || textures.output == 0 ||
            !std::isfinite(PROGRESS) || PROGRESS < 0 || PROGRESS > 1 ||
            !std::isfinite(frame.radius) || !std::isfinite(frame.aspect) || frame.radius <= 0 ||
            frame.aspect <= 0) {
            return false;
        }
        const auto VALID = [&](const PlanetCaptureTextures &capture) {
            if (capture.composite == 0 || capture.wallpaper == 0 ||
                capture.composite == textures.output || capture.wallpaper == textures.output ||
                capture.previous_wallpaper == textures.output) {
                return false;
            }
            const auto LAYER_VALID = [&](const PlanetLayerTextures &layer) {
                return layer.image != textures.output && layer.backdrop != textures.output &&
                       layer.previous_image != textures.output &&
                       layer.previous_backdrop != textures.output;
            };
            if (!LAYER_VALID(capture.bottom) || !LAYER_VALID(capture.windows) ||
                !LAYER_VALID(capture.top)) {
                return false;
            }
            return std::ranges::all_of(
                capture.floating, [&](const auto &item) { return LAYER_VALID(item.layer); });
        };
        if (!VALID(textures.source) || !VALID(textures.destination)) {
            return false;
        }
        if (std::ranges::any_of(neighbors, [&](const auto &neighbor) {
                return neighbor.image == textures.output ||
                       neighbor.previous_image == textures.output;
            })) {
            return false;
        }
        return true;
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
    void PlanetGlRenderer::draw_endpoint(const DrawContext &context) {
        const auto &textures = context.textures.get();
        const auto PROGRESS = context.progress;
        const auto &full_screen = context.full_screen.get();
        const auto &capture = PROGRESS == 0 ? textures.source : textures.destination;
        if (!textures.native_hud) {
            draw_quad({.primary = {.image = capture.composite, .backdrop = 0},
                .view_projection = IDENTITY,
                .model = full_screen,
                .opacity = 1,
                .rect = {0, 0, 1, 1},
                .force_opaque = true});
        } else {
            // Never blit a composite that may contain a baked shell. Restore
            // only the native desktop layers; the monitor pass replays HUD.
            draw_quad({.primary = {.image = capture.wallpaper},
                .view_projection = IDENTITY,
                .model = full_screen,
                .opacity = 1,
                .rect = {0, 0, 1, 1},
                .force_opaque = true});
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            draw_quad(
                {.primary = {.image = capture.windows.image, .backdrop = capture.windows.backdrop},
                    .view_projection = IDENTITY,
                    .model = full_screen,
                    .opacity = 1});
            for (const auto &item : capture.floating) {
                auto model = IDENTITY;
                model[0] = 2 * item.rect[2];
                model[5] = 2 * item.rect[3];
                model[12] = (2 * item.rect[0]) + item.rect[2] - 1;
                model[13] =
                    (textures.top_first ? 1.F : -1.F) * ((2 * item.rect[1]) + item.rect[3] - 1);
                auto rect = item.cropped ? std::array<float, 4>{0, 0, 1, 1} : item.rect;
                if (!item.cropped && textures.top_first) {
                    rect[1] = 1 - rect[1] - rect[3];
                }
                draw_quad({.primary = {.image = item.layer.image, .backdrop = item.layer.backdrop},
                    .view_projection = IDENTITY,
                    .model = model,
                    .opacity = std::isfinite(item.opacity) ? std::clamp(item.opacity, 0.F, 1.F) : 0,
                    .rect = rect});
            }
        }
    }
    void PlanetGlRenderer::draw_sky(const DrawContext &context) const {
        const auto &frame = context.frame.get();
        const auto &settings = context.settings.get();
        const auto &inverse_projection = context.inverse_projection.get();
        const auto &sky = m_sky.uniforms;
        glUseProgram(m_sky.id);
        glUniformMatrix4fv(sky[0], 1, GL_FALSE, inverse_projection.data());
        glUniform3f(sky[1],
            static_cast<float>(frame.camera_position.x),
            static_cast<float>(frame.camera_position.y),
            static_cast<float>(frame.camera_position.z));
        glUniform1f(sky[2], static_cast<float>(frame.endpoint_envelope));
        glUniform1f(sky[3], settings.star_density);
        glBindVertexArray(m_vao);
        constexpr std::array<double, 3> STAR_DEPTHS{40, 90, 210};
        for (std::size_t i = 0; i < STAR_DEPTHS.size(); ++i) {
            const auto OFFSET = [&](double origin, double camera) {
                const double VALUE =
                    std::fmod((origin + camera) * settings.star_parallax / STAR_DEPTHS[i], 1024.0);
                return static_cast<float>(VALUE < 0 ? VALUE + 1024.0 : VALUE);
            };
            glUniform3f(sky[4 + i],
                OFFSET(frame.world_origin.x, frame.camera_position.x),
                OFFSET(frame.world_origin.y, frame.camera_position.y),
                OFFSET(frame.world_origin.z, frame.camera_position.z));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    void PlanetGlRenderer::draw_surface(
        const DrawContext &context, const SurfaceDraw &surface) const {
        const auto &textures = context.textures.get();
        const auto &frame = context.frame.get();
        const auto PROGRESS = context.progress;
        const auto &settings = context.settings.get();
        const auto &view_projection = context.view_projection.get();
        const auto &capture = surface.capture.get();
        const auto &center = surface.center;
        const auto &rotation = surface.rotation.get();
        const auto ENVELOPE = surface.envelope;
        const auto &layer = surface.layer;
        const auto KIND = surface.kind;
        const auto ELEVATION = surface.elevation;
        const auto OPACITY = surface.opacity;
        const auto RADIUS = surface.radius;
        const auto QUALITY = surface.quality;
        const auto &tint = surface.tint;
        if (layer.image == 0 || OPACITY <= 0) {
            return;
        }
        const auto &uniforms = m_planet.uniforms;
        glUseProgram(m_planet.id);
        glUniformMatrix4fv(uniforms[0], 1, GL_FALSE, view_projection.data());
        glUniformMatrix3fv(uniforms[1], 1, GL_FALSE, rotation.data());
        glUniform3f(uniforms[2],
            static_cast<float>(center.x),
            static_cast<float>(center.y),
            static_cast<float>(center.z));
        glUniform1f(uniforms[3], ENVELOPE);
        glUniform1f(uniforms[4], ENVELOPE);
        glUniform1f(uniforms[5], ELEVATION);
        glUniform1f(uniforms[6], settings.depth_strength);
        glUniform1i(uniforms[7], 2);
        glUniform1i(uniforms[8], static_cast<GLint>(m_depth_texture != 0 && !m_depth_dirty));
        glUniform1i(uniforms[9], QUALITY < 0 ? std::clamp(settings.quality, 0, 2) : QUALITY);
        glUniform1i(uniforms[10], 0);
        glUniform4f(uniforms[11], 0, textures.top_first ? 0 : 1, 1, textures.top_first ? 1 : -1);
        glUniform1i(uniforms[12], KIND);
        glUniform1f(uniforms[13], OPACITY);
        glUniform3fv(uniforms[14], 1, settings.light_direction.data());
        glUniform1f(uniforms[15], settings.night_brightness);
        glUniform1f(uniforms[16], settings.indirect_brightness);
        glUniform1f(uniforms[17], settings.cloud_coverage);
        glUniform1f(uniforms[18], settings.cloud_opacity);
        glUniform1f(uniforms[19], settings.cloud_speed);
        glUniform1f(uniforms[20], static_cast<float>(PROGRESS) * settings.duration);
        glUniform1f(uniforms[21], settings.atmosphere_intensity);
        glUniform1f(uniforms[22], settings.atmosphere_thickness);
        glUniform1i(uniforms[23], static_cast<GLint>(settings.lighting));
        glUniform1i(uniforms[24], static_cast<GLint>(settings.clouds));
        glUniform1i(uniforms[25], 1);
        glUniform1i(uniforms[26], static_cast<GLint>(layer.backdrop != 0));
        glUniform1i(uniforms[27], static_cast<GLint>(settings.depth_enabled));
        glUniform1f(uniforms[28], RADIUS > 0 ? RADIUS : static_cast<float>(frame.radius));
        glUniform1f(uniforms[29], static_cast<float>(frame.coverage));
        glUniform1f(uniforms[30],
            std::isfinite(surface.aspect) && surface.aspect > 0 ? surface.aspect
                                                                : static_cast<float>(frame.aspect));
        glUniform1f(uniforms[33], RADIUS > 0 ? RADIUS : static_cast<float>(frame.radius));
        glUniform1f(uniforms[34], 0.035F);
        glUniform1i(uniforms[35], GL_TRUE);
        glUniform1i(uniforms[36], GL_TRUE);
        glUniform1i(uniforms[31], static_cast<GLint>(settings.atmosphere_intensity > 0));
        glUniform3f(uniforms[32],
            static_cast<float>(frame.camera_position.x),
            static_cast<float>(frame.camera_position.y),
            static_cast<float>(frame.camera_position.z));
        glUniform1i(uniforms[37], 3);
        glUniform1i(uniforms[38], 4);
        glUniform1i(uniforms[39], static_cast<GLint>(layer.previous_backdrop != 0));
        glUniform1f(uniforms[40], capture_mix(layer));
        glUniform3fv(uniforms[41], 1, tint.data());
        glUniform1i(uniforms[42], static_cast<GLint>(RADIUS > 0));
        if (capture_mix(layer) < 1) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, layer.previous_image);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D,
                layer.previous_backdrop != 0 ? layer.previous_backdrop : layer.previous_image);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, layer.image);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, layer.backdrop != 0 ? layer.backdrop : capture.wallpaper);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D,
            m_depth_texture != 0 && !m_depth_dirty ? m_depth_texture : capture.wallpaper);
        glBindVertexArray(m_mesh_vao);
        glDrawElements(GL_TRIANGLES, m_index_count, GL_UNSIGNED_INT, nullptr);
    }
    PlanetGlRenderer::PreviewSamplers::~PreviewSamplers() {
        if (image) {
            glBindSampler(0, regular);
        }
        if (previous) {
            glBindSampler(3, regular);
        }
    }
    PlanetGlRenderer::NeighborView PlanetGlRenderer::neighbor_view(const DrawContext &context) {
        const auto &textures = context.textures.get();
        const auto &frame = context.frame.get();
        const auto PROGRESS = context.progress;
        const auto &settings = context.settings.get();
        const auto &view_projection = context.view_projection.get();
        NeighborView result;
        result.radius = static_cast<float>(frame.radius) * settings.solar_background_scale;
        const double BOUND =
            result.radius + (settings.depth_enabled ? std::abs(settings.depth_strength) : 0);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            for (std::size_t side = 0; side < 2; ++side) {
                const float SIGN = side == 0 ? -1.F : 1.F;
                auto &plane = result.planes[(axis * 2) + side];
                for (std::size_t column = 0; column < 4; ++column) {
                    plane[column] = view_projection[(column * 4) + 3] +
                                    (SIGN * view_projection[(column * 4) + axis]);
                }
                plane[4] = BOUND * std::hypot(plane[0], plane[1], plane[2]);
            }
        }
        result.w_radius =
            BOUND * std::hypot(view_projection[3], view_projection[7], view_projection[11]);
        const double SCALE_X =
            std::hypot(view_projection[0], view_projection[4], view_projection[8]) *
            static_cast<float>(textures.width);
        const double SCALE_Y =
            std::hypot(view_projection[1], view_projection[5], view_projection[9]) *
            static_cast<float>(textures.height);
        result.pixel_scale = BOUND * std::max(SCALE_X, SCALE_Y);
        result.envelope = static_cast<float>(std::clamp(frame.endpoint_envelope, 0., 1.));
        result.limit = settings.rotation_enabled && std::isfinite(settings.rotation_limit)
                           ? std::clamp(settings.rotation_limit, 0.F, .5F)
                           : 0.F;
        result.motion = .08F * static_cast<float>(std::sin(PROGRESS * std::numbers::pi));
        return result;
    }
    int PlanetGlRenderer::neighbor_quality(const DrawContext &context,
        const NeighborView &view,
        const PlanetNeighborTextures &neighbor) {
        const auto &textures = context.textures.get();
        const auto &settings = context.settings.get();
        const auto &view_projection = context.view_projection.get();
        if (neighbor.endpoint.identity == textures.source_identity ||
            neighbor.endpoint.identity == textures.destination_identity) {
            return -1;
        }
        const auto &center = neighbor.center;
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) {
            return -1;
        }
        if (std::ranges::any_of(view.planes, [&](const auto &plane) {
                return (plane[0] * center.x) + (plane[1] * center.y) + (plane[2] * center.z) +
                           plane[3] <
                       -plane[4];
            })) {
            return -1;
        }
        const double CLIP_W = (view_projection[3] * center.x) + (view_projection[7] * center.y) +
                              (view_projection[11] * center.z) + view_projection[15];
        const double PIXELS = view.pixel_scale / std::max(CLIP_W - view.w_radius, 0.000001);
        if (PIXELS < 2) {
            return -1;
        }
        if (PIXELS < 96) {
            return 0;
        }
        const int PIXEL_QUALITY = PIXELS < 256 ? 1 : 2;
        return std::min(std::clamp(settings.quality, 0, 2), PIXEL_QUALITY);
    }
    bool PlanetGlRenderer::draw_neighbor(const DrawContext &context,
        const NeighborView &view,
        const PlanetNeighborTextures &neighbor,
        int quality,
        PreviewSamplers &sampling) {
        if (!prepare_mesh(quality)) {
            return false;
        }
        const auto PLACEMENT = neighbor.endpoint.placement;
        const float PHASE =
            (static_cast<float>((PLACEMENT * 11400714819323198485ULL) >> 56U) / 255.F) - .5F;
        const float ANGLE = view.limit * view.envelope * ((.3F * PHASE) + view.motion);
        const float COSINE = std::cos(ANGLE);
        const float SINE = std::sin(ANGLE);
        const std::array<float, 9> ROTATION{COSINE, 0, -SINE, 0, 1, 0, SINE, 0, COSINE};
        const float MIX =
            std::isfinite(neighbor.live_mix) ? std::clamp(neighbor.live_mix, 0.F, 1.F) : 1.F;
        const float SHARED = neighbor.shared_wallpaper || neighbor.image == 0 ? 1.F : 0.F;
        const float PREVIOUS_SHARED = neighbor.previous_image == 0 ? 1.F : 0.F;
        const float IDENTITY_SHADE =
            MIX < 1 ? (PREVIOUS_SHARED * (1 - MIX)) + (SHARED * MIX) : SHARED;
        const float TINT = (.04F + (.04F * IDENTITY_SHADE)) * view.envelope;
        std::array<float, 3> tint{};
        tint[static_cast<std::size_t>(PLACEMENT % 3)] = -TINT;
        tint[static_cast<std::size_t>(((PLACEMENT / 3) + 1) % 3)] -= TINT * .5F;
        if ((neighbor.image == 0 || (MIX < 1 && neighbor.previous_image == 0)) &&
            !prepare_neutral()) {
            return false;
        }
        const GLuint IMAGE = neighbor.image != 0 ? neighbor.image : m_neutral_texture;
        const GLuint PREVIOUS_IMAGE =
            neighbor.previous_image != 0 ? neighbor.previous_image : m_neutral_texture;
        const GLuint PREVIOUS = MIX < 1 ? PREVIOUS_IMAGE : 0;
        const bool IMAGE_MIPPED = neighbor.image != 0 && neighbor.image_mipped;
        const bool PREVIOUS_MIPPED =
            PREVIOUS != 0 && neighbor.previous_image != 0 && neighbor.previous_mipped;
        if (sampling.image != IMAGE_MIPPED) {
            glBindSampler(0, IMAGE_MIPPED ? m_mip_sampler : m_sampler);
            sampling.image = IMAGE_MIPPED;
        }
        if (sampling.previous != PREVIOUS_MIPPED) {
            glBindSampler(3, PREVIOUS_MIPPED ? m_mip_sampler : m_sampler);
            sampling.previous = PREVIOUS_MIPPED;
        }
        const double OWNER_ASPECT =
            std::isfinite(neighbor.endpoint.monitor_width) &&
                    std::isfinite(neighbor.endpoint.monitor_height) &&
                    neighbor.endpoint.monitor_width > 0 && neighbor.endpoint.monitor_height > 0
                ? neighbor.endpoint.monitor_width / neighbor.endpoint.monitor_height
                : 0;
        draw_surface(context,
            {.capture = context.textures.get().source,
                .center = neighbor.center,
                .rotation = ROTATION,
                .envelope = 1,
                .layer = {.image = IMAGE,
                    .backdrop = 0,
                    .previous_image = PREVIOUS,
                    .previous_backdrop = 0,
                    .live_mix = MIX},
                .kind = 0,
                .elevation = 0,
                .opacity = 1,
                .radius = view.radius,
                .quality = quality,
                .tint = tint,
                .aspect = static_cast<float>(OWNER_ASPECT)});
        return true;
    }
    bool PlanetGlRenderer::draw_neighbors(
        const DrawContext &context, std::span<const PlanetNeighborTextures> neighbors) {
        if (neighbors.empty() || !std::isfinite(context.settings.get().solar_background_scale) ||
            context.settings.get().solar_background_scale <= 0) {
            return true;
        }
        PreviewSamplers sampling{m_sampler};
        const NeighborView VIEW = neighbor_view(context);
        for (const auto &neighbor : neighbors) {
            const int QUALITY = neighbor_quality(context, VIEW, neighbor);
            if (QUALITY >= 0 && !draw_neighbor(context, VIEW, neighbor, QUALITY, sampling)) {
                return false;
            }
        }
        return prepare_mesh(context.settings.get().quality);
    }
    void PlanetGlRenderer::draw_floating(const DrawContext &context,
        const EndpointDraw &endpoint,
        const PlanetFloatingTextures &item) {
        const auto &textures = context.textures.get();
        const auto &frame = context.frame.get();
        const auto &settings = context.settings.get();
        const auto &view_projection = context.view_projection.get();
        const auto &center = endpoint.center;
        const auto &rotation = endpoint.rotation.get();
        const auto ENVELOPE = endpoint.envelope;
        const float RELIEF = settings.depth_enabled ? std::abs(settings.depth_strength) * 0.5F : 0;
        const double COORD_U = item.rect[0] + (item.rect[2] * 0.5);
        const double COORD_V = item.rect[1] + (item.rect[3] * 0.5);
        const auto ANCHOR = planet_hop::project_surface({.u = COORD_U,
            .v = COORD_V,
            .aspect = frame.aspect,
            .radius = frame.radius,
            .coverage = frame.coverage,
            .curvature = ENVELOPE,
            .rotation = rotation,
            .center = center,
            .elevation = RELIEF + std::max(0.0F, settings.elevation)});
        const auto EAST = planet_hop::project_surface({.u = COORD_U + 0.001,
            .v = COORD_V,
            .aspect = frame.aspect,
            .radius = frame.radius,
            .coverage = frame.coverage,
            .curvature = ENVELOPE,
            .rotation = rotation,
            .center = center,
            .elevation = 0});
        const auto WEST = planet_hop::project_surface({.u = COORD_U - 0.001,
            .v = COORD_V,
            .aspect = frame.aspect,
            .radius = frame.radius,
            .coverage = frame.coverage,
            .curvature = ENVELOPE,
            .rotation = rotation,
            .center = center,
            .elevation = 0});
        planet_hop::Vec3 right{.x = EAST.position.x - WEST.position.x,
            .y = EAST.position.y - WEST.position.y,
            .z = EAST.position.z - WEST.position.z};
        const double LENGTH =
            std::sqrt((right.x * right.x) + (right.y * right.y) + (right.z * right.z));
        if (LENGTH <= 0.000001) {
            return;
        }
        right = {.x = right.x / LENGTH, .y = right.y / LENGTH, .z = right.z / LENGTH};
        const auto &normal = ANCHOR.normal;
        const planet_hop::Vec3 UP_AXIS{.x = (normal.y * right.z) - (normal.z * right.y),
            .y = (normal.z * right.x) - (normal.x * right.z),
            .z = (normal.x * right.y) - (normal.y * right.x)};
        auto model = IDENTITY;
        const float WIDTH = 2 * static_cast<float>(frame.aspect) * item.rect[2];
        const float HEIGHT = 2 * item.rect[3];
        model[0] = static_cast<float>(right.x) * WIDTH;
        model[1] = static_cast<float>(right.y) * WIDTH;
        model[2] = static_cast<float>(right.z) * WIDTH;
        model[4] = static_cast<float>(UP_AXIS.x) * HEIGHT;
        model[5] = static_cast<float>(UP_AXIS.y) * HEIGHT;
        model[6] = static_cast<float>(UP_AXIS.z) * HEIGHT;
        model[12] = static_cast<float>(ANCHOR.position.x);
        model[13] = static_cast<float>(ANCHOR.position.y);
        model[14] = static_cast<float>(ANCHOR.position.z);
        const bool PREVIOUS = capture_mix(item.layer) < 1;
        const PlanetLayerTextures PRIMARY =
            PREVIOUS ? PlanetLayerTextures{.image = item.layer.previous_image,
                           .backdrop = item.layer.previous_backdrop}
                     : item.layer;
        const PlanetLayerTextures SECONDARY{
            .image = item.layer.image, .backdrop = item.layer.backdrop};
        draw_quad({.primary = PRIMARY,
            .view_projection = view_projection,
            .model = model,
            .opacity = std::isfinite(item.opacity) ? std::clamp(item.opacity, 0.0F, 1.0F) : 0,
            .rect = item.cropped ? std::array<float, 4>{0, 0, 1, 1} : item.rect,
            .force_opaque = false,
            .secondary = SECONDARY,
            .mix = PREVIOUS ? capture_mix(item.layer) : 0,
            .top_first = textures.top_first});
    }
    void PlanetGlRenderer::draw_layers(const DrawContext &context, const EndpointDraw &endpoint) {
        const auto &textures = context.textures.get();
        const auto &settings = context.settings.get();
        const auto &capture = endpoint.capture.get();
        const auto &center = endpoint.center;
        const auto &rotation = endpoint.rotation.get();
        const auto ENVELOPE = endpoint.envelope;
        const auto SOURCE = endpoint.source;
        const float OVERLAY_OPACITY =
            settings.overlay_mode == 2 ? fade_opacity(context, SOURCE) : 1;
        if (settings.overlay_mode != 1 && !textures.native_hud) {
            draw_surface(context,
                {.capture = capture,
                    .center = center,
                    .rotation = rotation,
                    .envelope = ENVELOPE,
                    .layer = capture.bottom,
                    .kind = 2,
                    .elevation = 0.003F,
                    .opacity = OVERLAY_OPACITY});
        }
        if (settings.window_mode != 1) {
            const float OPACITY = settings.window_mode == 2 ? fade_opacity(context, SOURCE) : 1;
            draw_surface(context,
                {.capture = capture,
                    .center = center,
                    .rotation = rotation,
                    .envelope = ENVELOPE,
                    .layer = capture.windows,
                    .kind = 1,
                    .elevation = 0.006F,
                    .opacity = OPACITY});
        }
        for (const auto &item : capture.floating) {
            draw_floating(context, endpoint, item);
        }
        if (settings.overlay_mode != 1 && !textures.native_hud) {
            draw_surface(context,
                {.capture = capture,
                    .center = center,
                    .rotation = rotation,
                    .envelope = ENVELOPE,
                    .layer = capture.top,
                    .kind = 2,
                    .elevation = std::max(0.0F, settings.elevation) + 0.01F,
                    .opacity = OVERLAY_OPACITY});
        }
    }
    void PlanetGlRenderer::draw_hud(const DrawContext &context, bool top) {
        const auto &source =
            top ? context.textures.get().source.top : context.textures.get().source.bottom;
        const auto &destination = top ? context.textures.get().destination.top
                                      : context.textures.get().destination.bottom;
        glDisable(GL_DEPTH_TEST);
        draw_quad({.primary = source,
            .view_projection = IDENTITY,
            .model = context.full_screen,
            .opacity = 1,
            .rect = {0, 0, 1, 1},
            .force_opaque = false,
            .secondary = destination,
            .mix = static_cast<float>(context.progress)});
        glEnable(GL_DEPTH_TEST);
    }
    float PlanetGlRenderer::fade_opacity(const DrawContext &context, bool source) {
        const float WIDTH = std::isfinite(context.settings.get().fade)
                                ? std::clamp(context.settings.get().fade, 0.02F, 1.0F)
                                : 0.2F;
        const auto DISTANCE = static_cast<float>(source ? context.progress : 1 - context.progress);
        const float AMOUNT = std::clamp(DISTANCE / WIDTH, 0.0F, 1.0F);
        return 1.0F - (AMOUNT * AMOUNT * (3.0F - (2.0F * AMOUNT)));
    }
    bool PlanetGlRenderer::draw_raw(const PlanetFrameTextures &textures,
        const planet_hop::Frame &frame,
        double progress,
        const PlanetRenderSettings &settings,
        std::span<const PlanetNeighborTextures> neighbors) {
        std::array<float, 16> reflected_projection{};
        std::array<float, 16> reflected_inverse{};
        auto full_screen = IDENTITY;
        const DrawContext CONTEXT{.textures = textures,
            .frame = frame,
            .progress = progress,
            .settings = settings,
            .view_projection = textures.top_first ? reflected_projection : frame.view_projection,
            .inverse_projection =
                textures.top_first ? reflected_inverse : frame.inverse_view_projection,
            .full_screen = full_screen};
        if (m_planet.id == 0 || !valid_draw(CONTEXT, neighbors) || !bind_output(textures)) {
            return false;
        }
        struct DepthState {
            GLint function = 0;
            GLboolean mask = GL_TRUE;
            GLfloat clear = 1;
            DepthState() {
                glGetIntegerv(GL_DEPTH_FUNC, &function);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
                glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear);
            }
            DepthState(const DepthState &) = delete;
            DepthState &operator=(const DepthState &) = delete;
            DepthState(DepthState &&) = delete;
            DepthState &operator=(DepthState &&) = delete;
            ~DepthState() {
                glDepthFunc(static_cast<GLenum>(function));
                glDepthMask(mask);
                glClearDepthf(clear);
            }
        };
        const DepthState DEPTH_STATE;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        full_screen[0] = full_screen[5] = 2;
        if (progress == 0 || progress == 1) {
            draw_endpoint(CONTEXT);
            return true;
        }
        // Custom depth is optional: failed uploads retain pixels for retry and use
        // procedural depth, never the stale image from a previous setting.
        (void)upload_depth();
        if (!prepare_mesh(settings.quality)) {
            return false;
        }
        if (textures.top_first) {
            reflected_projection = frame.view_projection;
            reflected_inverse = frame.inverse_view_projection;
            for (std::size_t column = 0; column < 4; ++column) {
                reflected_projection[(column * 4) + 1] *= -1;
            }
            for (std::size_t row = 0; row < 4; ++row) {
                reflected_inverse[4 + row] *= -1;
            }
        }
        draw_sky(CONTEXT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glClearDepthf(1);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (!draw_neighbors(CONTEXT, neighbors)) {
            return false;
        }
        draw_surface(CONTEXT,
            {.capture = textures.source,
                .center = frame.source_center,
                .rotation = frame.source_rotation,
                .envelope = static_cast<float>(frame.source_curvature),
                .layer = {.image = textures.source.wallpaper,
                    .backdrop = 0,
                    .previous_image = textures.source.previous_wallpaper,
                    .previous_backdrop = 0,
                    .live_mix = textures.source.live_mix},
                .kind = 0,
                .elevation = 0,
                .opacity = 1});
        draw_surface(CONTEXT,
            {.capture = textures.destination,
                .center = frame.destination_center,
                .rotation = frame.destination_rotation,
                .envelope = static_cast<float>(frame.destination_curvature),
                .layer = {.image = textures.destination.wallpaper,
                    .backdrop = 0,
                    .previous_image = textures.destination.previous_wallpaper,
                    .previous_backdrop = 0,
                    .live_mix = textures.destination.live_mix},
                .kind = 0,
                .elevation = 0,
                .opacity = 1});
        glDepthMask(GL_FALSE);
        if (settings.overlay_mode == 1 && !textures.native_hud) {
            draw_hud(CONTEXT, false);
        }
        draw_layers(CONTEXT,
            {.capture = textures.source,
                .center = frame.source_center,
                .rotation = frame.source_rotation,
                .envelope = static_cast<float>(frame.source_curvature),
                .source = true});
        draw_layers(CONTEXT,
            {.capture = textures.destination,
                .center = frame.destination_center,
                .rotation = frame.destination_rotation,
                .envelope = static_cast<float>(frame.destination_curvature),
                .source = false});
        if (settings.overlay_mode == 1 && !textures.native_hud) {
            draw_hud(CONTEXT, true);
        }
        return true;
    }
}  // namespace hyprimate::workspace
