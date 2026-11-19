#include "workspace/planet_hop/PlanetRenderer.hpp"
#include "workspace/planet_hop/PlanetMaterial.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <png.h>
#ifndef HYPRIMATE_PLANET_RAW_ONLY
#include "gl/RawGlBracket.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#endif

namespace hyprimate::workspace {
    namespace {
        constexpr std::array<float, 16> IDENTITY{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        constexpr std::array<const char *, 43> PLANET_UNIFORMS{
            "viewProjection", "planetRotation", "planetCenter", "curvature", "endpointEnvelope",
            "elevation", "depthStrength", "depthImage", "depthSource", "samplingQuality", "image",
            "uvTransform", "layer", "opacity", "lightDirection", "nightBrightness", "indirectBrightness",
            "cloudCoverage", "cloudOpacity", "cloudSpeed", "time", "atmosphereIntensity", "atmosphereThickness",
            "lightingEnabled", "cloudsEnabled", "backdrop", "reconstructLayer", "uDepthEnabled", "radius",
            "coverage", "aspect", "atmosphereEnabled", "cameraPosition", "planetRadius", "cloudHeight", "parametricMesh", "fullSphere",
            "previousImage", "previousBackdrop", "previousReconstruct", "liveMix", "identityTint", "opaqueCapture"};
        constexpr std::array<const char *, 21> QUAD_UNIFORMS{
            "viewProjection", "model", "image", "uvTransform", "opacity", "imageRect", "backdrop",
            "reconstructLayer", "forceOpaque", "secondaryImage", "secondaryBackdrop", "secondaryReconstruct", "mixAmount",
            "previousImage", "previousBackdrop", "previousReconstruct", "liveMix",
            "secondaryPreviousImage", "secondaryPreviousBackdrop", "secondaryPreviousReconstruct", "secondaryLiveMix"};
        constexpr std::array<const char *, 7> SKY_UNIFORMS{
            "inverseViewProjection", "cameraPosition", "endpointEnvelope", "starDensity",
            "starOffsetNear", "starOffsetMid", "starOffsetFar"};
        float capture_mix(const PlanetLayerTextures &layer) {
            return layer.previous_image != 0 && std::isfinite(layer.live_mix) ?
                std::clamp(layer.live_mix, 0.0F, 1.0F) : 1.0F;
        }

        GLuint compile(GLenum type, std::string_view text) {
            const GLuint SHADER = glCreateShader(type);
            const char *source = text.data();
            const GLint LENGTH = static_cast<GLint>(text.size());
            glShaderSource(SHADER, 1, &source, &LENGTH);
            glCompileShader(SHADER);
            GLint ok = GL_FALSE;
            glGetShaderiv(SHADER, GL_COMPILE_STATUS, &ok);
            if (ok == GL_TRUE) { return SHADER; }
            std::array<char, 2048> log{};
            glGetShaderInfoLog(SHADER, static_cast<GLsizei>(log.size()), nullptr, log.data());
            std::fprintf(stderr, "hyprimate Planet Hop shader: %s\n", log.data());
            glDeleteShader(SHADER);
            return 0;
        }
        GLuint program(std::string_view vertex, std::string_view fragment) {
            const GLuint VS = compile(GL_VERTEX_SHADER, vertex);
            const GLuint FS = compile(GL_FRAGMENT_SHADER, fragment);
            if (VS == 0 || FS == 0) {
                if (VS != 0) { glDeleteShader(VS); }
                if (FS != 0) { glDeleteShader(FS); }
                return 0;
            }
            const GLuint RESULT = glCreateProgram();
            glAttachShader(RESULT, VS);
            glAttachShader(RESULT, FS);
            glLinkProgram(RESULT);
            glDeleteShader(VS);
            glDeleteShader(FS);
            GLint ok = GL_FALSE;
            glGetProgramiv(RESULT, GL_LINK_STATUS, &ok);
            if (ok == GL_TRUE) { return RESULT; }
            std::array<char, 2048> log{};
            glGetProgramInfoLog(RESULT, static_cast<GLsizei>(log.size()), nullptr, log.data());
            std::fprintf(stderr, "hyprimate Planet Hop link: %s\n", log.data());
            glDeleteProgram(RESULT);
            return 0;
        }
    }

    bool PlanetRenderer::prepare_depth(const std::string &path) {
        if (m_depth_prepared && path == m_depth_path) { return m_depth_error.empty(); }
        m_depth_prepared = true;
        m_depth_path = path;
        m_depth_pixels.clear();
        m_depth_width = m_depth_height = 0;
        m_depth_dirty = true;
        m_depth_error.clear();
        if (path.empty()) { return true; }
        constexpr std::uintmax_t MAX_FILE = 16U * 1024U * 1024U;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            m_depth_error = "depthSource must be a regular PNG file";
            return false;
        }
        const auto SIZE = std::filesystem::file_size(path, error);
        if (error || SIZE == 0 || SIZE > MAX_FILE) {
            m_depth_error = "depthSource exceeds the 16 MiB compressed input limit";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        std::vector<unsigned char> bytes(static_cast<std::size_t>(SIZE));
        if (!input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            m_depth_error = "cannot read depthSource";
            return false;
        }
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) == 0) {
            m_depth_error = image.message;
            png_image_free(&image);
            return false;
        }
        if (image.width == 0 || image.height == 0 || image.width > 4096 || image.height > 4096) {
            m_depth_error = "depthSource dimensions must be between 1 and 4096 pixels";
            png_image_free(&image);
            return false;
        }
        image.format = PNG_FORMAT_GRAY;
        m_depth_width = static_cast<int>(image.width);
        m_depth_height = static_cast<int>(image.height);
        m_depth_pixels.resize(static_cast<std::size_t>(image.width) * image.height);
        if (png_image_finish_read(&image, nullptr, m_depth_pixels.data(), 0, nullptr) == 0) {
            m_depth_error = image.message;
            m_depth_pixels.clear();
            m_depth_width = m_depth_height = 0;
            png_image_free(&image);
            return false;
        }
        png_image_free(&image);
        return true;
    }

    bool PlanetRenderer::initialize_raw() {
        if (m_planet.id != 0) { return true; }
        m_planet.id = program(planet_hop::shader::VERTEX, planet_hop::shader::FRAGMENT);
        m_quad.id = program(planet_hop::shader::QUAD_VERTEX, planet_hop::shader::QUAD_FRAGMENT);
        m_sky.id = program(planet_hop::shader::SKY_VERTEX, planet_hop::shader::SKY_FRAGMENT);
        if (m_planet.id == 0 || m_quad.id == 0 || m_sky.id == 0) { release_raw(); return false; }
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
        for (auto &buffer : m_depth_buffers) { glGenRenderbuffers(1, &buffer.id); }
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
            std::any_of(m_depth_buffers.begin(), m_depth_buffers.end(), [](const auto &buffer) { return buffer.id == 0; }) ||
            m_sampler == 0 || m_mip_sampler == 0) {
            release_raw();
            return false;
        }
        m_depth_dirty = true;
        return true;
    }

    void PlanetRenderer::release_raw() {
        for (auto *p : {&m_planet, &m_quad, &m_sky}) {
            if (p->id != 0) { glDeleteProgram(p->id); }
            p->id = 0;
        }
        glDeleteVertexArrays(1, &m_vao);
        for (auto &mesh : m_meshes) {
            glDeleteVertexArrays(1, &mesh.vao);
            glDeleteBuffers(1, &mesh.vertices);
            glDeleteBuffers(1, &mesh.indices);
            mesh = {};
        }
        glDeleteFramebuffers(1, &m_framebuffer);
        for (auto &buffer : m_depth_buffers) { glDeleteRenderbuffers(1, &buffer.id); buffer = {}; }
        glDeleteSamplers(1, &m_sampler);
        glDeleteSamplers(1, &m_mip_sampler);
        m_mip_sampler = 0;
        glDeleteTextures(1, &m_depth_texture);
        glDeleteTextures(1, &m_neutral_texture);
        m_neutral_texture = 0;
        m_vao = m_mesh_vao = m_sampler = m_depth_texture = 0;
        m_index_count = 0;
        m_framebuffer = 0;
        m_next_depth_buffer = 0;
        m_position_caches = {};
        m_next_position_cache = 0;
        m_depth_dirty = true;
    }

    void PlanetRenderer::upload_depth() {
        if (!m_depth_dirty) { return; }
        if (m_depth_texture != 0) { glDeleteTextures(1, &m_depth_texture); m_depth_texture = 0; }
        if (!m_depth_pixels.empty()) {
            // Pixel-store/PBO state is not covered by RawGlBracket; restore every touched value.
            GLint alignment = 0, row_length = 0, skip_rows = 0, skip_pixels = 0, buffer = 0;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glActiveTexture(GL_TEXTURE2);
            glGenTextures(1, &m_depth_texture);
            glBindTexture(GL_TEXTURE_2D, m_depth_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_depth_width, m_depth_height, 0, GL_RED, GL_UNSIGNED_BYTE, m_depth_pixels.data());
            glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer));
        }
        m_depth_dirty = false;
    }
    bool PlanetRenderer::prepare_neutral() {
        if (m_neutral_texture != 0) { return true; }
        // Unknown is an opaque neutral identity material, not another workspace's
        // wallpaper. Respect compositor unpack/PBO state even on this one-pixel upload.
        GLint alignment = 0, row_length = 0, skip_rows = 0, skip_pixels = 0, buffer = 0;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glActiveTexture(GL_TEXTURE2);
        glGenTextures(1, &m_neutral_texture);
        if (m_neutral_texture != 0) {
            constexpr std::array<unsigned char, 4> NEUTRAL{72,78,86,255};
            glBindTexture(GL_TEXTURE_2D, m_neutral_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NEUTRAL.data());
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer));
        return m_neutral_texture != 0;
    }


    bool PlanetRenderer::prepare_mesh(int quality) {
        quality = std::clamp(quality, 0, 2);
        auto &mesh = m_meshes[static_cast<std::size_t>(quality)];
        if (mesh.vao != 0) {
            m_mesh_vao = mesh.vao;
            m_index_count = mesh.count;
            return true;
        }
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vertices);
        glGenBuffers(1, &mesh.indices);
        if (mesh.vao == 0 || mesh.vertices == 0 || mesh.indices == 0) {
            glDeleteVertexArrays(1, &mesh.vao);
            glDeleteBuffers(1, &mesh.vertices);
            glDeleteBuffers(1, &mesh.indices);
            mesh = {};
            return false;
        }
        m_mesh_vao = mesh.vao;
        const int COLUMNS = std::array{48, 80, 128}[static_cast<std::size_t>(quality)];
        const int ROWS = COLUMNS / 2;
        std::vector<std::array<float, 2>> vertices;
        vertices.reserve(static_cast<std::size_t>((COLUMNS + 1) * (ROWS + 1)));
        for (int y = 0; y <= ROWS; ++y) {
            for (int x = 0; x <= COLUMNS; ++x) {
                vertices.push_back({static_cast<float>(x) / COLUMNS, static_cast<float>(y) / ROWS});
            }
        }
        std::vector<GLuint> indices;
        indices.reserve(static_cast<std::size_t>(COLUMNS * ROWS * 6));
        for (int y = 0; y < ROWS; ++y) {
            for (int x = 0; x < COLUMNS; ++x) {
                const GLuint A = static_cast<GLuint>(y * (COLUMNS + 1) + x);
                const GLuint B = A + static_cast<GLuint>(COLUMNS + 1);
                indices.insert(indices.end(), {A, B, A + 1, A + 1, B, B + 1});
            }
        }
        glBindVertexArray(m_mesh_vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vertices);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(vertices[0])), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(indices[0])), indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertices[0]), nullptr);
        m_index_count = static_cast<GLsizei>(indices.size());
        mesh.count = m_index_count;
        return true;
    }

    void PlanetRenderer::draw_quad(const PlanetLayerTextures &primary,
        const std::array<float, 16> &view_projection, const std::array<float, 16> &model,
        float opacity, const std::array<float, 4> &rect, bool force_opaque,
        PlanetLayerTextures secondary, float mix, bool top_first) {
        if (primary.image == 0 || opacity <= 0) { return; }
        const auto &u = m_quad.uniforms;
        glUseProgram(m_quad.id);
        glUniformMatrix4fv(u[0], 1, GL_FALSE, view_projection.data());
        glUniformMatrix4fv(u[1], 1, GL_FALSE, model.data());
        glUniform1i(u[2], 0);
        glUniform4f(u[3], 0, top_first ? 0 : 1, 1, top_first ? 1 : -1);
        glUniform1f(u[4], opacity);
        glUniform4fv(u[5], 1, rect.data());
        glUniform1i(u[6], 1);
        glUniform1i(u[7], primary.backdrop != 0);
        glUniform1i(u[8], force_opaque);
        glUniform1i(u[9], 3);
        glUniform1i(u[10], 4);
        glUniform1i(u[11], secondary.backdrop != 0);
        glUniform1f(u[12], mix);
        glUniform1i(u[13], 5);
        glUniform1i(u[14], 6);
        glUniform1i(u[15], primary.previous_backdrop != 0);
        glUniform1f(u[16], capture_mix(primary));
        glUniform1i(u[17], 7);
        glUniform1i(u[18], 8);
        glUniform1i(u[19], secondary.previous_backdrop != 0);
        glUniform1f(u[20], capture_mix(secondary));
        const auto BIND = [](GLenum unit, GLuint texture) {
            glActiveTexture(unit);
            glBindTexture(GL_TEXTURE_2D, texture);
        };
        BIND(GL_TEXTURE0, primary.image);
        BIND(GL_TEXTURE1, primary.backdrop != 0 ? primary.backdrop : primary.image);
        if (mix > 0) {
            BIND(GL_TEXTURE3, secondary.image != 0 ? secondary.image : primary.image);
            BIND(GL_TEXTURE4, secondary.backdrop != 0 ? secondary.backdrop : primary.image);
        }
        if (capture_mix(primary) < 1) {
            glBindSampler(5, m_sampler);
            glBindSampler(6, m_sampler);
            BIND(GL_TEXTURE5, primary.previous_image);
            BIND(GL_TEXTURE6, primary.previous_backdrop != 0 ? primary.previous_backdrop : primary.previous_image);
        }
        if (mix > 0 && capture_mix(secondary) < 1) {
            glBindSampler(7, m_sampler);
            glBindSampler(8, m_sampler);
            BIND(GL_TEXTURE7, secondary.previous_image);
            BIND(GL_TEXTURE8, secondary.previous_backdrop != 0 ? secondary.previous_backdrop : secondary.previous_image);
        }
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    bool PlanetRenderer::draw_raw(const PlanetFrameTextures &textures, const planet_hop::Frame &frame,
        double progress, const PlanetRenderSettings &settings, std::span<const PlanetNeighborTextures> neighbors) {
        if (m_planet.id == 0 || textures.width <= 0 || textures.height <= 0 || textures.output == 0 ||
            !std::isfinite(progress) || progress < 0 || progress > 1 ||
            !std::isfinite(frame.radius) || !std::isfinite(frame.aspect) ||
            frame.radius <= 0 || frame.aspect <= 0) { return false; }
        const auto VALID = [&](const PlanetCaptureTextures &capture) {
            if (capture.composite == 0 || capture.wallpaper == 0 ||
                capture.composite == textures.output || capture.wallpaper == textures.output ||
                capture.previous_wallpaper == textures.output) { return false; }
            const auto LAYER_VALID = [&](const PlanetLayerTextures &layer) {
                return layer.image != textures.output && layer.backdrop != textures.output &&
                    layer.previous_image != textures.output && layer.previous_backdrop != textures.output;
            };
            if (!LAYER_VALID(capture.bottom) || !LAYER_VALID(capture.windows) || !LAYER_VALID(capture.top)) { return false; }
            return std::all_of(capture.floating.begin(), capture.floating.end(),
                [&](const auto &item) { return LAYER_VALID(item.layer); });
        };
        if (!VALID(textures.source) || !VALID(textures.destination)) { return false; }
        if (std::any_of(neighbors.begin(), neighbors.end(),
                [&](const auto &neighbor) {
                    return neighbor.image == textures.output || neighbor.previous_image == textures.output;
                })) { return false; }
        // Own the depth attachment rather than modifying Hyprland's framebuffer.
        auto depth_buffer = std::find_if(m_depth_buffers.begin(), m_depth_buffers.end(), [&](const auto &buffer) {
            return buffer.width == textures.width && buffer.height == textures.height;
        });
        if (depth_buffer == m_depth_buffers.end()) {
            GLint maximum = 0;
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximum);
            if (textures.width > maximum || textures.height > maximum) { return false; }
            depth_buffer = m_depth_buffers.begin() + static_cast<std::ptrdiff_t>(m_next_depth_buffer);
            m_next_depth_buffer = (m_next_depth_buffer + 1) % m_depth_buffers.size();
            GLint prior = 0;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &prior);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer->id);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, textures.width, textures.height);
            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prior));
            depth_buffer->width = textures.width;
            depth_buffer->height = textures.height;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures.output, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffer->id);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { return false; }
        glViewport(0, 0, textures.width, textures.height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        for (GLuint unit = 0; unit < 5; ++unit) { glBindSampler(unit, m_sampler); }
        struct DepthState {
            GLint function = 0;
            GLboolean mask = GL_TRUE;
            GLfloat clear = 1;
            DepthState() {
                glGetIntegerv(GL_DEPTH_FUNC, &function);
                glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
                glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear);
            }
            ~DepthState() { glDepthFunc(static_cast<GLenum>(function)); glDepthMask(mask); glClearDepthf(clear); }
        } depth_state;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        auto full_screen = IDENTITY;
        full_screen[0] = full_screen[5] = 2;
        if (progress == 0 || progress == 1) {
            const auto &capture = progress == 0 ? textures.source : textures.destination;
            if (!textures.native_hud) {
                draw_quad({capture.composite, 0}, IDENTITY, full_screen, 1, {0,0,1,1}, true);
            } else {
                // Never blit a composite that may contain a baked shell. Restore
                // only the native desktop layers; the monitor pass replays HUD.
                draw_quad({capture.wallpaper}, IDENTITY, full_screen, 1, {0,0,1,1}, true);
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                draw_quad({capture.windows.image, capture.windows.backdrop}, IDENTITY, full_screen, 1);
                for (const auto &item : capture.floating) {
                    auto model = IDENTITY;
                    model[0] = 2 * item.rect[2];
                    model[5] = 2 * item.rect[3];
                    model[12] = 2 * item.rect[0] + item.rect[2] - 1;
                    model[13] = (textures.top_first ? 1.F : -1.F) *
                        (2 * item.rect[1] + item.rect[3] - 1);
                    auto rect = item.cropped ? std::array<float, 4>{0,0,1,1} : item.rect;
                    if (!item.cropped && textures.top_first) { rect[1] = 1 - rect[1] - rect[3]; }
                    draw_quad({item.layer.image, item.layer.backdrop}, IDENTITY, model,
                        std::isfinite(item.opacity) ? std::clamp(item.opacity, 0.F, 1.F) : 0, rect);
                }
            }
            return true;
        }
        upload_depth();
        if (!prepare_mesh(settings.quality)) { return false; }
        std::array<float, 16> reflected_projection;
        std::array<float, 16> reflected_inverse;
        if (textures.top_first) {
            reflected_projection = frame.viewProjection;
            reflected_inverse = frame.inverseViewProjection;
            // R * VP and inverse(VP) * R, R=diag(1,-1,1,1).
            for (std::size_t column = 0; column < 4; ++column) { reflected_projection[column * 4 + 1] *= -1; }
            for (std::size_t row = 0; row < 4; ++row) { reflected_inverse[4 + row] *= -1; }
        }
        const auto &view_projection = textures.top_first ? reflected_projection : frame.viewProjection;
        const auto &inverse_projection = textures.top_first ? reflected_inverse : frame.inverseViewProjection;
        const auto &sky = m_sky.uniforms;
        glUseProgram(m_sky.id);
        glUniformMatrix4fv(sky[0], 1, GL_FALSE, inverse_projection.data());
        glUniform3f(sky[1], static_cast<float>(frame.cameraPosition.x),
            static_cast<float>(frame.cameraPosition.y), static_cast<float>(frame.cameraPosition.z));
        glUniform1f(sky[2], static_cast<float>(frame.endpointEnvelope));
        glUniform1f(sky[3], settings.star_density);
        glBindVertexArray(m_vao);
        constexpr std::array<double, 3> STAR_DEPTHS{40, 90, 210};
        for (std::size_t i = 0; i < STAR_DEPTHS.size(); ++i) {
            const auto OFFSET = [&](double origin, double camera) {
                const double VALUE = std::fmod((origin + camera) * settings.star_parallax / STAR_DEPTHS[i], 1024.0);
                return static_cast<float>(VALUE < 0 ? VALUE + 1024.0 : VALUE);
            };
            glUniform3f(sky[4 + i], OFFSET(frame.worldOrigin.x, frame.cameraPosition.x),
                OFFSET(frame.worldOrigin.y, frame.cameraPosition.y), OFFSET(frame.worldOrigin.z, frame.cameraPosition.z));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glClearDepthf(1);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        const float SOURCE_ENVELOPE = static_cast<float>(frame.sourceCurvature);
        const float DESTINATION_ENVELOPE = static_cast<float>(frame.destinationCurvature);
        const auto FADE_OPACITY = [&](bool source) {
            // A configured zero still has a short continuous handoff instead of an endpoint jump.
            const float WIDTH = std::isfinite(settings.fade) ? std::clamp(settings.fade, 0.02F, 1.0F) : 0.2F;
            const float DISTANCE = static_cast<float>(source ? progress : 1 - progress);
            const float AMOUNT = std::clamp(DISTANCE / WIDTH, 0.0F, 1.0F);
            return 1.0F - AMOUNT * AMOUNT * (3.0F - 2.0F * AMOUNT);
        };
        const float RELIEF = settings.depth_enabled ? std::abs(settings.depth_strength) * 0.5F : 0;
        const auto SURFACE = [&](const PlanetCaptureTextures &capture,
            planet_hop::Vec3 center, const std::array<float, 9> &rotation, float envelope,
            PlanetLayerTextures layer, int kind, float elevation, float opacity,
            float radius = 0, int quality = -1, std::array<float, 3> tint = {}) {
            if (layer.image == 0 || opacity <= 0) { return; }
            const auto &u = m_planet.uniforms;
            glUseProgram(m_planet.id);
            glUniformMatrix4fv(u[0], 1, GL_FALSE, view_projection.data());
            glUniformMatrix3fv(u[1], 1, GL_FALSE, rotation.data());
            glUniform3f(u[2], static_cast<float>(center.x), static_cast<float>(center.y), static_cast<float>(center.z));
            glUniform1f(u[3], envelope);
            glUniform1f(u[4], envelope);
            glUniform1f(u[5], elevation);
            glUniform1f(u[6], settings.depth_strength);
            glUniform1i(u[7], 2);
            glUniform1i(u[8], m_depth_texture != 0);
            glUniform1i(u[9], quality < 0 ? std::clamp(settings.quality, 0, 2) : quality);
            glUniform1i(u[10], 0);
            glUniform4f(u[11], 0, textures.top_first ? 0 : 1, 1, textures.top_first ? 1 : -1);
            glUniform1i(u[12], kind);
            glUniform1f(u[13], opacity);
            glUniform3fv(u[14], 1, settings.light_direction.data());
            glUniform1f(u[15], settings.night_brightness);
            glUniform1f(u[16], settings.indirect_brightness);
            glUniform1f(u[17], settings.cloud_coverage);
            glUniform1f(u[18], settings.cloud_opacity);
            glUniform1f(u[19], settings.cloud_speed);
            glUniform1f(u[20], static_cast<float>(progress) * settings.duration);
            glUniform1f(u[21], settings.atmosphere_intensity);
            glUniform1f(u[22], settings.atmosphere_thickness);
            glUniform1i(u[23], settings.lighting);
            glUniform1i(u[24], settings.clouds);
            glUniform1i(u[25], 1);
            glUniform1i(u[26], layer.backdrop != 0);
            glUniform1i(u[27], settings.depth_enabled);
            glUniform1f(u[28], radius > 0 ? radius : static_cast<float>(frame.radius));
            glUniform1f(u[29], static_cast<float>(frame.coverage));
            glUniform1f(u[30], static_cast<float>(frame.aspect));
            glUniform1f(u[33], radius > 0 ? radius : static_cast<float>(frame.radius));
            glUniform1f(u[34], 0.035F);
            glUniform1i(u[35], GL_TRUE);
            glUniform1i(u[36], GL_TRUE);
            glUniform1i(u[31], settings.atmosphere_intensity > 0);
            glUniform3f(u[32], static_cast<float>(frame.cameraPosition.x),
                static_cast<float>(frame.cameraPosition.y), static_cast<float>(frame.cameraPosition.z));
            glUniform1i(u[37], 3);
            glUniform1i(u[38], 4);
            glUniform1i(u[39], layer.previous_backdrop != 0);
            glUniform1f(u[40], capture_mix(layer));
            glUniform3fv(u[41], 1, tint.data());
            glUniform1i(u[42], radius > 0);
            if (capture_mix(layer) < 1) {
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, layer.previous_image);
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, layer.previous_backdrop != 0 ? layer.previous_backdrop : layer.previous_image);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, layer.image);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, layer.backdrop != 0 ? layer.backdrop : capture.wallpaper);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, m_depth_texture != 0 ? m_depth_texture : capture.wallpaper);
            glBindVertexArray(m_mesh_vao);
            glDrawElements(GL_TRIANGLES, m_index_count, GL_UNSIGNED_INT, nullptr);
        };
        // Inventory-only worlds share the real material and depth attachment.
        if (!neighbors.empty() && std::isfinite(settings.solar_background_scale) && settings.solar_background_scale > 0) {
            struct PreviewSamplers {
                GLuint regular;
                bool image = false, previous = false;
                ~PreviewSamplers() {
                    if (image) { glBindSampler(0, regular); }
                    if (previous) { glBindSampler(3, regular); }
                }
            } sampling{m_sampler};
            const float RADIUS = static_cast<float>(frame.radius) * settings.solar_background_scale;
            const double BOUND = RADIUS + (settings.depth_enabled ? std::abs(settings.depth_strength) : 0);
            // X/Y/Z/D and radius support per clip plane, invariant across worlds.
            std::array<std::array<double, 5>, 6> planes{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                for (std::size_t side = 0; side < 2; ++side) {
                    const float SIGN = side == 0 ? -1.F : 1.F;
                    auto &plane = planes[axis * 2 + side];
                    for (std::size_t column = 0; column < 4; ++column) {
                        plane[column] = view_projection[column * 4 + 3] + SIGN * view_projection[column * 4 + axis];
                    }
                    plane[4] = BOUND * std::hypot(plane[0], plane[1], plane[2]);
                }
            }
            const double W_RADIUS = BOUND * std::hypot(view_projection[3], view_projection[7], view_projection[11]);
            const double SCALE_X = std::hypot(view_projection[0], view_projection[4], view_projection[8]) * textures.width;
            const double SCALE_Y = std::hypot(view_projection[1], view_projection[5], view_projection[9]) * textures.height;
            const double PIXEL_SCALE = BOUND * std::max(SCALE_X, SCALE_Y);
            const float ENVELOPE = static_cast<float>(std::clamp(frame.endpointEnvelope, 0., 1.));
            const float LIMIT = settings.rotation_enabled && std::isfinite(settings.rotation_limit) ?
                std::clamp(settings.rotation_limit, 0.F, .5F) : 0.F;
            const float MOTION = .08F * static_cast<float>(std::sin(progress * 3.141592653589793));
            for (const auto &neighbor : neighbors) {
                if (neighbor.endpoint.identity == textures.source_identity ||
                    neighbor.endpoint.identity == textures.destination_identity) { continue; }
                const auto &c = neighbor.center;
                if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) { continue; }
                if (std::any_of(planes.begin(), planes.end(), [&](const auto &plane) {
                        return plane[0]*c.x + plane[1]*c.y + plane[2]*c.z + plane[3] < -plane[4];
                    })) { continue; }
                const double W = view_projection[3]*c.x + view_projection[7]*c.y +
                    view_projection[11]*c.z + view_projection[15];
                const double PIXELS = PIXEL_SCALE / std::max(W - W_RADIUS, 0.000001);
                if (PIXELS < 2) { continue; }
                const int QUALITY = std::min(std::clamp(settings.quality, 0, 2), PIXELS < 96 ? 0 : PIXELS < 256 ? 1 : 2);
                if (!prepare_mesh(QUALITY)) { return false; }
                // Placement survives workspace object reincarnation. All backgrounds
                // get restrained identity shading, without changing actual imagery.
                const auto ID = neighbor.endpoint.placement;
                const float PHASE = static_cast<float>((ID * 11400714819323198485ULL) >> 56) / 255.F - .5F;
                const float ANGLE = LIMIT * ENVELOPE * (.3F * PHASE + MOTION);
                const float C = std::cos(ANGLE), S = std::sin(ANGLE);
                const std::array<float, 9> ROTATION{C,0,-S, 0,1,0, S,0,C};
                const float MIX = std::isfinite(neighbor.live_mix) ? std::clamp(neighbor.live_mix, 0.F, 1.F) : 1.F;
                const float SHARED = neighbor.shared_wallpaper || neighbor.image == 0 ? 1.F : 0.F;
                const float PREVIOUS_SHARED = neighbor.previous_image == 0 ? 1.F : 0.F;
                const float IDENTITY_SHADE = MIX < 1 ? PREVIOUS_SHARED * (1 - MIX) + SHARED * MIX : SHARED;
                const float TINT = (.04F + .04F * IDENTITY_SHADE) * ENVELOPE;
                std::array<float, 3> tint{};
                tint[static_cast<std::size_t>(ID % 3)] = -TINT;
                tint[static_cast<std::size_t>((ID / 3 + 1) % 3)] -= TINT * .5F;
                if ((neighbor.image == 0 || (MIX < 1 && neighbor.previous_image == 0)) && !prepare_neutral()) { return false; }
                const GLuint IMAGE = neighbor.image != 0 ? neighbor.image : m_neutral_texture;
                const GLuint PREVIOUS = MIX < 1 ?
                    (neighbor.previous_image != 0 ? neighbor.previous_image : m_neutral_texture) : 0;
                const bool IMAGE_MIPPED = neighbor.image != 0 && neighbor.image_mipped;
                const bool PREVIOUS_MIPPED = PREVIOUS != 0 && neighbor.previous_image != 0 && neighbor.previous_mipped;
                if (sampling.image != IMAGE_MIPPED) {
                    glBindSampler(0, IMAGE_MIPPED ? m_mip_sampler : m_sampler);
                    sampling.image = IMAGE_MIPPED;
                }
                if (sampling.previous != PREVIOUS_MIPPED) {
                    glBindSampler(3, PREVIOUS_MIPPED ? m_mip_sampler : m_sampler);
                    sampling.previous = PREVIOUS_MIPPED;
                }
                SURFACE(textures.source, c, ROTATION, 1, {IMAGE, 0, PREVIOUS, 0, MIX}, 0, 0, 1, RADIUS, QUALITY, tint);
            }
            if (!prepare_mesh(settings.quality)) { return false; }
        }
        SURFACE(textures.source, frame.sourceCenter, frame.sourceRotation, SOURCE_ENVELOPE,
            {textures.source.wallpaper, 0, textures.source.previous_wallpaper, 0, textures.source.live_mix}, 0, 0, 1);
        SURFACE(textures.destination, frame.destinationCenter, frame.destinationRotation, DESTINATION_ENVELOPE,
            {textures.destination.wallpaper, 0, textures.destination.previous_wallpaper, 0, textures.destination.live_mix}, 0, 0, 1);
        glDepthMask(GL_FALSE);
        const auto HUD = [&](PlanetLayerTextures source, PlanetLayerTextures destination) {
            glDisable(GL_DEPTH_TEST);
            draw_quad(source, IDENTITY, full_screen, 1,
                {0,0,1,1}, false, destination, static_cast<float>(progress));
            glEnable(GL_DEPTH_TEST);
        };
        if (settings.overlay_mode == 1 && !textures.native_hud) { HUD(textures.source.bottom, textures.destination.bottom); }
        const auto LAYERS = [&](const PlanetCaptureTextures &capture, planet_hop::Vec3 center,
            const std::array<float, 9> &rotation, float envelope, bool source) {
            const float OVERLAY_OPACITY = settings.overlay_mode == 2 ? FADE_OPACITY(source) : 1;
            if (settings.overlay_mode != 1 && !textures.native_hud) {
                SURFACE(capture, center, rotation, envelope, capture.bottom, 2, 0.003F, OVERLAY_OPACITY);
            }
            if (settings.window_mode != 1) {
                const float OPACITY = settings.window_mode == 2 ? FADE_OPACITY(source) : 1;
                SURFACE(capture, center, rotation, envelope, capture.windows, 1, 0.006F, OPACITY);
            }
            for (const auto &item : capture.floating) {
                const double U = item.rect[0] + item.rect[2] * 0.5;
                const double V = item.rect[1] + item.rect[3] * 0.5;
                const auto ANCHOR = planet_hop::project_surface(U, V, frame.aspect, frame.radius,
                    frame.coverage, envelope, rotation, center,
                    RELIEF + std::max(0.0F, settings.elevation));
                const auto EAST = planet_hop::project_surface(U + 0.001, V, frame.aspect, frame.radius,
                    frame.coverage, envelope, rotation, center, 0);
                const auto WEST = planet_hop::project_surface(U - 0.001, V, frame.aspect, frame.radius,
                    frame.coverage, envelope, rotation, center, 0);
                planet_hop::Vec3 right{EAST.position.x-WEST.position.x, EAST.position.y-WEST.position.y, EAST.position.z-WEST.position.z};
                const double LENGTH = std::sqrt(right.x*right.x + right.y*right.y + right.z*right.z);
                if (LENGTH <= 0.000001) { continue; }
                right = {right.x/LENGTH,right.y/LENGTH,right.z/LENGTH};
                const auto &normal = ANCHOR.normal;
                const planet_hop::Vec3 UP{normal.y*right.z-normal.z*right.y,
                    normal.z*right.x-normal.x*right.z, normal.x*right.y-normal.y*right.x};
                auto model = IDENTITY;
                const float WIDTH = 2 * static_cast<float>(frame.aspect) * item.rect[2];
                const float HEIGHT = 2 * item.rect[3];
                model[0] = static_cast<float>(right.x) * WIDTH;
                model[1] = static_cast<float>(right.y) * WIDTH;
                model[2] = static_cast<float>(right.z) * WIDTH;
                model[4] = static_cast<float>(UP.x) * HEIGHT;
                model[5] = static_cast<float>(UP.y) * HEIGHT;
                model[6] = static_cast<float>(UP.z) * HEIGHT;
                model[12] = static_cast<float>(ANCHOR.position.x);
                model[13] = static_cast<float>(ANCHOR.position.y);
                model[14] = static_cast<float>(ANCHOR.position.z);
                const bool PREVIOUS = capture_mix(item.layer) < 1;
                const PlanetLayerTextures PRIMARY = PREVIOUS ?
                    PlanetLayerTextures{item.layer.previous_image, item.layer.previous_backdrop} : item.layer;
                const PlanetLayerTextures SECONDARY{item.layer.image, item.layer.backdrop};
                draw_quad(PRIMARY, view_projection, model,
                    std::isfinite(item.opacity) ? std::clamp(item.opacity, 0.0F, 1.0F) : 0,
                    item.cropped ? std::array<float,4>{0,0,1,1} : item.rect, false,
                    SECONDARY, PREVIOUS ? capture_mix(item.layer) : 0, textures.top_first);
            }
            if (settings.overlay_mode != 1 && !textures.native_hud) {
                SURFACE(capture, center, rotation, envelope, capture.top, 2,
                    std::max(0.0F, settings.elevation) + 0.01F, OVERLAY_OPACITY);
            }
        };
        LAYERS(textures.source, frame.sourceCenter, frame.sourceRotation, SOURCE_ENVELOPE, true);
        LAYERS(textures.destination, frame.destinationCenter, frame.destinationRotation, DESTINATION_ENVELOPE, false);
        if (settings.overlay_mode == 1 && !textures.native_hud) { HUD(textures.source.top, textures.destination.top); }
        return true;
    }

#ifndef HYPRIMATE_PLANET_RAW_ONLY
    bool PlanetRenderer::initialize() {
        if (m_planet.id != 0) { return true; }
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
            g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) { return false; }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<5> BRACKET{{}};
        return initialize_raw();
    }
    void PlanetRenderer::release() {
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL) {
            m_planet.id = m_quad.id = m_sky.id = 0;
            m_vao = m_mesh_vao = m_sampler = m_depth_texture = 0;
            m_mip_sampler = 0;
            m_neutral_texture = 0;
            m_framebuffer = 0;
            for (auto &buffer : m_depth_buffers) { buffer = {}; }
            m_meshes = {};
            m_index_count = 0;
            m_next_depth_buffer = 0;
            m_position_caches = {};
            m_next_position_cache = 0;
            return;
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<5> BRACKET{{}};
        release_raw();
    }
    bool PlanetRenderer::draw(Render::CRenderContext &ctx, const WorkspaceFrame &frame,
        const planet_hop::Journey &journey, double progress, Direction direction, bool matching,
        const PlanetRenderSettings &settings) {
        (void)ctx;
        (void)direction;
        (void)matching;
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
            g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL ||
            !frame.presentation || !frame.presentation->getTexture() ||
            !frame.source.layered || !frame.destination.layered) { return false; }
        const auto SIZE = frame.presentation->m_size;
        const auto TEXTURE = [&](const auto &buffer, bool cropped = false) -> GLuint {
            return buffer && (cropped || buffer->m_size == SIZE) && buffer->getTexture() ? buffer->getTexture()->m_texID : 0;
        };
        const auto LAYER = [&](const WorkspaceLayer &layer, const WorkspaceLayer *previous = nullptr,
            float live_mix = 1, bool cropped = false) {
            return PlanetLayerTextures{TEXTURE(layer.accumulated, cropped), TEXTURE(layer.backdrop, cropped),
                previous ? TEXTURE(previous->accumulated, cropped) : 0,
                previous ? TEXTURE(previous->backdrop, cropped) : 0, live_mix};
        };
        const auto CAPTURE = [&](const WorkspaceCapture &capture, auto &floating) {
            PlanetCaptureTextures result;
            const auto *previous = capture.previous.get();
            const float MIX = previous && std::isfinite(capture.live_mix) ?
                static_cast<float>(std::clamp(capture.live_mix, 0.0, 1.0)) : 1;
            result.composite = TEXTURE(capture.composite);
            result.wallpaper = TEXTURE(capture.wallpaper);
            result.previous_wallpaper = previous ? TEXTURE(previous->wallpaper) : 0;
            result.live_mix = MIX;
            result.bottom = LAYER(capture.bottom, previous ? &previous->bottom : nullptr, MIX);
            result.windows = LAYER(capture.windows, previous ? &previous->windows : nullptr, MIX);
            result.top = LAYER(capture.top, previous ? &previous->top : nullptr, MIX);
            floating.clear();
            if (capture.floating) {
                floating.reserve(capture.floating->size());
                for (const auto &item : *capture.floating) {
                    floating.push_back({LAYER(item.layer, &item.previous_layer, item.live_mix, item.cropped),
                        {static_cast<float>(item.rect.x), static_cast<float>(item.rect.y),
                        static_cast<float>(item.rect.w), static_cast<float>(item.rect.h)}, item.cropped, item.opacity});
                }
            }
            result.floating = floating;
            return result;
        };
        const PlanetFrameTextures TEXTURES{CAPTURE(frame.source, m_source_floating),
            CAPTURE(frame.destination, m_destination_floating), frame.presentation->getTexture()->m_texID,
            static_cast<int>(SIZE.x), static_cast<int>(SIZE.y), true, frame.native_hud,
            journey.from.identity, journey.to.identity};
        if (TEXTURES.height <= 0) { return false; }
        const auto SCENE = planet_hop::evaluate(journey, progress, static_cast<double>(SIZE.x) / SIZE.y);
        m_neighbors.clear();
        if (journey.options.planetLayout == planet_hop::Layout::SolarSystem && frame.neighbors) {
            const auto &options = journey.options;
            const std::array<double, 5> KEY{options.planetSize, options.planetSeparation,
                options.elevation, options.solarVerticalSpread, SCENE.aspect};
            auto cached = std::find_if(m_position_caches.begin(), m_position_caches.end(), [&](const auto &slot) {
                return slot.layout == options.planetLayout && slot.key == KEY &&
                    !slot.inventory.owner_before(frame.neighbors) && !frame.neighbors.owner_before(slot.inventory);
            });
            if (cached == m_position_caches.end()) {
                cached = std::find_if(m_position_caches.begin(), m_position_caches.end(),
                    [](const auto &slot) { return slot.inventory.expired(); });
                if (cached == m_position_caches.end()) {
                    cached = m_position_caches.begin() + static_cast<std::ptrdiff_t>(m_next_position_cache);
                    m_next_position_cache = (m_next_position_cache + 1) % m_position_caches.size();
                }
                cached->inventory = frame.neighbors;
                cached->layout = options.planetLayout;
                cached->key = KEY;
                cached->positions.clear();
                cached->positions.reserve(frame.neighbors->size());
                for (const auto &preview : *frame.neighbors) {
                    cached->positions.push_back(planet_hop::layout_position(preview.endpoint, options, SCENE.aspect));
                }
            }
            m_neighbors.reserve(frame.neighbors->size());
            for (std::size_t index = 0; index < frame.neighbors->size(); ++index) {
                const auto &preview = (*frame.neighbors)[index];
                if (preview.endpoint.identity == journey.from.identity ||
                    preview.endpoint.identity == journey.to.identity) { continue; }
                const auto &POSITION = cached->positions[index];
                const GLuint IMAGE = TEXTURE(preview.image, true);
                const GLuint PREVIOUS = TEXTURE(preview.previous_image, true);
                m_neighbors.push_back({preview.endpoint,
                    {POSITION.x - SCENE.worldOrigin.x, POSITION.y - SCENE.worldOrigin.y, POSITION.z - SCENE.worldOrigin.z},
                    IMAGE, IMAGE == 0, PREVIOUS, preview.live_mix, IMAGE != 0, PREVIOUS != 0});
            }
        }
        auto render_settings = settings;
        render_settings.rotation_enabled = journey.options.rotationEnabled;
        render_settings.rotation_limit = static_cast<float>(journey.options.rotationLimit);
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const auto DRAW = [&]<std::size_t Units>() {
            const gl::CRawGlBracket<Units> BRACKET{{.viewport_width = TEXTURES.width, .viewport_height = TEXTURES.height}};
            return draw_raw(TEXTURES, SCENE, progress, render_settings, m_neighbors);
        };
        // Extra units are touched only when both HUD endpoints can have capture history.
        if (!frame.native_hud && settings.overlay_mode == 1 &&
            ((frame.source.previous && frame.source.live_mix < 1) ||
                (frame.destination.previous && frame.destination.live_mix < 1))) {
            return DRAW.template operator()<9>();
        }
        return DRAW.template operator()<5>();
    }
#endif
}
