#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "workspace/planet_hop/PlanetGlPrivate.hpp"

namespace hyprimate::workspace {
    namespace {
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
    }  // namespace
    namespace detail {
        void cache_program_uniforms(GLuint planet,
            std::array<GLint, 48> &planet_uniforms,
            GLuint quad,
            std::array<GLint, 48> &quad_uniforms,
            GLuint sky,
            std::array<GLint, 48> &sky_uniforms) {
            const auto CACHE = [](GLuint id, auto &target, const auto &names) {
                target.fill(-1);
                for (std::size_t i = 0; i < names.size(); ++i) {
                    target[i] = glGetUniformLocation(id, names[i]);
                }
            };
            CACHE(planet, planet_uniforms, PLANET_UNIFORMS);
            CACHE(quad, quad_uniforms, QUAD_UNIFORMS);
            CACHE(sky, sky_uniforms, SKY_UNIFORMS);
        }
    }  // namespace detail

    using detail::capture_mix;
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
}  // namespace hyprimate::workspace
