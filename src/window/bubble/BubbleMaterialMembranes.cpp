#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include <GLES3/gl32.h>

#include "window/bubble/BubbleMaterial.hpp"
#include "window/bubble/BubbleMaterialLayout.hpp"
#include "window/bubble/BubbleSheets.hpp"

namespace {
    using bubble_material_detail::vector;
    template <std::size_t N>
    void surface(const std::array<GLint, N> &uniforms, const hyprimate::BubbleSurface &shape) {
        static_assert(N == 13 + (4 * 6) + 9);
        vector(std::get<0>(uniforms), shape.half_size);
        vector(std::get<1>(uniforms), shape.core);
        glUniform1f(std::get<2>(uniforms), static_cast<float>(shape.corner));
        glUniform1f(std::get<3>(uniforms), static_cast<float>(shape.depth));
        glUniform1f(std::get<4>(uniforms), static_cast<float>(shape.content));
        glUniform1f(std::get<5>(uniforms), static_cast<float>(shape.film));
        glUniform1f(std::get<6>(uniforms), static_cast<float>(shape.visibility));
        glUniform1f(std::get<7>(uniforms), static_cast<float>(shape.ripple));
        glUniform1f(std::get<8>(uniforms), static_cast<float>(shape.thickness));
        glUniform1f(std::get<9>(uniforms), static_cast<float>(shape.flow));
        glUniform1f(std::get<10>(uniforms), static_cast<float>(shape.aureole));
        glUniform1i(std::get<11>(uniforms), shape.spherical);
        glUniform1i(std::get<12>(uniforms),
            static_cast<int>(std::min(shape.hole_count, hyprimate::K_BUBBLE_HOLES)));
        auto locations = std::span{uniforms}.subspan(13, 4 * 6);
        for (const auto &hole :
            std::span{shape.holes}.first(std::min(shape.hole_count, hyprimate::K_BUBBLE_HOLES))) {
            const auto LOCATIONS = locations.template first<6>();
            locations = locations.template subspan<6>();
            auto location = LOCATIONS.begin();
            glUniform3f(*location++,
                static_cast<float>(hole.centre.x),
                static_cast<float>(hole.centre.y),
                static_cast<float>(hole.centre.z));
            glUniform1f(*location++, static_cast<float>(hole.radius));
            glUniform1f(*location++, static_cast<float>(hole.rim));
            glUniform1f(*location++, static_cast<float>(hole.precursor));
            glUniform4f(*location++,
                static_cast<float>(std::get<0>(hole.amplitude)),
                static_cast<float>(std::get<1>(hole.amplitude)),
                static_cast<float>(std::get<2>(hole.amplitude)),
                static_cast<float>(std::get<3>(hole.amplitude)));
            glUniform4f(*location,
                static_cast<float>(std::get<0>(hole.phase)),
                static_cast<float>(std::get<1>(hole.phase)),
                static_cast<float>(std::get<2>(hole.phase)),
                static_cast<float>(std::get<3>(hole.phase)));
        }
        const auto &deformation = shape.motion;
        vector(std::get<37>(uniforms), deformation.drag);
        glUniform4f(std::get<38>(uniforms),
            static_cast<float>(std::get<0>(deformation.wave_cos)),
            static_cast<float>(std::get<1>(deformation.wave_cos)),
            static_cast<float>(std::get<2>(deformation.wave_cos)),
            static_cast<float>(std::get<3>(deformation.wave_cos)));
        glUniform4f(std::get<39>(uniforms),
            static_cast<float>(std::get<0>(deformation.wave_sin)),
            static_cast<float>(std::get<1>(deformation.wave_sin)),
            static_cast<float>(std::get<2>(deformation.wave_sin)),
            static_cast<float>(std::get<3>(deformation.wave_sin)));
        glUniform1f(std::get<40>(uniforms), static_cast<float>(deformation.sheen));
        glUniform1f(std::get<41>(uniforms), static_cast<float>(deformation.round));
        vector(std::get<42>(uniforms), deformation.extent);
        vector(std::get<43>(uniforms), deformation.carry);
        glUniform1f(std::get<44>(uniforms), static_cast<float>(shape.transparency));
        std::array<float, 4 * hyprimate::K_BUBBLE_BUDS> buds{};
        auto out = buds.begin();
        for (const auto &bud : deformation.buds) {
            *out++ = static_cast<float>(bud.foot.x);
            *out++ = static_cast<float>(bud.foot.y);
            *out++ = static_cast<float>(bud.height);
            *out++ = static_cast<float>(bud.width);
        }
        glUniform4fv(
            std::get<45>(uniforms), static_cast<GLsizei>(hyprimate::K_BUBBLE_BUDS), buds.data());
    }

}  // namespace
void CBubbleMaterial::draw_membranes(
    const hyprimate::BubbleFrame &frame, const SBubbleTarget &target, const SBubbleImages &images) {
    if (m_membrane == 0U) {
        return;
    }
    glUseProgram(m_membrane);
    glBindVertexArray(m_vao);
    const auto &uniforms = m_membrane_uniforms;
    vector(std::get<0>(uniforms), frame.size);
    vector(std::get<1>(uniforms), target.source_pos);
    vector(std::get<2>(uniforms), target.source_size);
    vector(std::get<3>(uniforms), target.origin);
    vector(std::get<4>(uniforms), images.texture_origin);
    vector(std::get<5>(uniforms), images.texture_size);
    vector(std::get<6>(uniforms), images.wallpaper_origin);
    vector(std::get<7>(uniforms), images.wallpaper_x);
    vector(std::get<8>(uniforms), images.wallpaper_y);
    vector(std::get<9>(uniforms), images.wallpaper_pixel);
    glUniform1i(std::get<10>(uniforms), static_cast<GLint>(images.wallpaper != 0));
    glUniform1i(std::get<11>(uniforms), 0);
    glUniform1i(std::get<12>(uniforms), 1);
    glUniform1i(std::get<13>(uniforms), 2);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, images.window);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, images.wallpaper);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_lut);
    surface(std::get<0>(m_surface_uniforms), frame.window);
    surface(std::get<1>(m_surface_uniforms), frame.shell);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // Draw companions behind the parent using destination alpha.
    if (frame.formation_count > 0) {
        GLint src_rgb = 0, dst_rgb = 0, src_alpha = 0, dst_alpha = 0;
        const GLboolean BLENDING = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
        draw_microbubbles(std::span(frame.formation)
                              .first(std::min(frame.formation_count, frame.formation.size())),
            frame.size,
            target);
        glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
        if (BLENDING == 0U) {
            glDisable(GL_BLEND);
        }
        glUseProgram(m_membrane);
        glBindVertexArray(m_vao);
    }
    const auto ACTIVE = [](const hyprimate::BubbleSurface &current) {
        if (current.visibility <= 0 || current.aureole <= 0) {
            return false;
        }
        for (const auto &hole : std::span(current.holes)
                 .first(std::min(current.hole_count, hyprimate::K_BUBBLE_HOLES))) {
            if (hole.radius > 0) {
                return true;
            }
        }
        return false;
    };
    const std::array<const hyprimate::BubbleSurface *, 2> SURFACES{&frame.window, &frame.shell};
    std::array<std::array<hyprimate::BubbleSheet, hyprimate::K_BUBBLE_MAX_SHEETS>, 2> sheets;
    std::array<std::size_t, 2> counts{};
    std::array<bool, 2> visible{};
    for (std::size_t surface_index = 0; surface_index < SURFACES.size(); ++surface_index) {
        counts.at(surface_index) =
            hyprimate::bubble_sheets(*SURFACES.at(surface_index), sheets.at(surface_index));
        visible.at(surface_index) =
            ACTIVE(*SURFACES.at(surface_index)) ||
            (SURFACES.at(surface_index)->rupture_visibility > 0 &&
                std::any_of(sheets.at(surface_index).begin(),
                    sheets.at(surface_index).begin() + counts.at(surface_index),
                    [](const hyprimate::BubbleSheet &sheet) { return sheet.scale > 0; }));
    }
    if (!std::get<0>(visible) && !std::get<1>(visible)) {
        return;
    }
    GLint src_rgb = 0;
    GLint dst_rgb = 0;
    GLint src_alpha = 0;
    GLint dst_alpha = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
    const GLboolean BLENDING = glIsEnabled(GL_BLEND);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_curl);
    const auto &curl_uniforms = m_curl_uniforms;
    vector(std::get<0>(curl_uniforms), frame.size);
    vector(std::get<1>(curl_uniforms), target.canvas);
    vector(std::get<2>(curl_uniforms), target.origin);
    vector(std::get<3>(curl_uniforms), target.source_pos);
    vector(std::get<4>(curl_uniforms), target.source_size);
    vector(std::get<5>(curl_uniforms), images.texture_origin);
    vector(std::get<6>(curl_uniforms), images.texture_size);
    vector(std::get<7>(curl_uniforms), images.wallpaper_origin);
    vector(std::get<8>(curl_uniforms), images.wallpaper_x);
    vector(std::get<9>(curl_uniforms), images.wallpaper_y);
    vector(std::get<10>(curl_uniforms), images.wallpaper_pixel);
    glUniform1i(std::get<11>(curl_uniforms), static_cast<GLint>(images.wallpaper != 0));
    glUniform1i(std::get<12>(curl_uniforms), 0);
    glUniform1i(std::get<13>(curl_uniforms), 1);
    glUniform1i(std::get<14>(curl_uniforms), 2);
    for (std::size_t surface_index = 0; surface_index < SURFACES.size(); ++surface_index) {
        if (!visible.at(surface_index)) {
            continue;
        }
        const auto *current_surface = SURFACES.at(surface_index);
        const auto COUNT = counts.at(surface_index);
        for (std::size_t sheet_index = 0; sheet_index < COUNT; ++sheet_index) {
            const auto &piece = sheets.at(surface_index)[sheet_index];
            const auto &mask_uniforms = m_sheet_mask_uniforms[sheet_index];
            glUniform4f(std::get<0>(mask_uniforms),
                static_cast<float>(piece.hole),
                static_cast<float>(piece.phi),
                static_cast<float>(piece.half_angle),
                0);
            glUniform4f(std::get<1>(mask_uniforms),
                static_cast<float>(std::get<0>(piece.radii)),
                static_cast<float>(std::get<1>(piece.radii)),
                static_cast<float>(std::get<2>(piece.radii)),
                static_cast<float>(std::get<3>(piece.radii)));
            glUniform4f(std::get<2>(mask_uniforms),
                static_cast<float>(std::get<0>(piece.onsets)),
                static_cast<float>(std::get<1>(piece.onsets)),
                static_cast<float>(std::get<2>(piece.onsets)),
                static_cast<float>(std::get<3>(piece.onsets)));
        }
        glUniform1i(std::get<0>(m_sheet_uniforms), 0);
        glUniform1i(std::get<1>(m_sheet_uniforms), static_cast<int>(COUNT));
        surface(m_curl_surface_uniforms, *current_surface);
        if (ACTIVE(*current_surface)) {
            glDrawArraysInstanced(GL_TRIANGLES,
                0,
                96 * 12 * 6,
                static_cast<GLsizei>(
                    std::min(current_surface->hole_count, hyprimate::K_BUBBLE_HOLES)));
        }
        // Detached material outlives its consumed parent; only the sheet's own contraction ends it.
        glUniform1f(std::get<6>(m_curl_surface_uniforms),
            static_cast<float>(current_surface->rupture_visibility));
        for (std::size_t sheet_index = 0; sheet_index < COUNT; ++sheet_index) {
            const auto &piece = sheets.at(surface_index)[sheet_index];
            if (piece.scale <= 0) {
                continue;
            }
            for (std::size_t hole_index = 0; hole_index < hyprimate::K_BUBBLE_HOLES; ++hole_index) {
                glUniform1f(m_curl_surface_uniforms.at(14 + (hole_index * 6)),
                    static_cast<float>(piece.radii.at(hole_index)));
                const auto ONSET = static_cast<float>(piece.onsets.at(hole_index));
                glUniform4f(m_curl_surface_uniforms.at(17 + (hole_index * 6)),
                    ONSET * .014F,
                    ONSET * .012F,
                    ONSET * .010F,
                    ONSET * .008F);
            }
            glUniform1i(std::get<0>(m_sheet_uniforms), 1);
            glUniform1i(std::get<1>(m_sheet_uniforms), static_cast<int>(sheet_index));
            glUniform1i(std::get<2>(m_sheet_uniforms), static_cast<int>(piece.hole));
            glUniform2f(std::get<3>(m_sheet_uniforms),
                static_cast<float>(piece.phi),
                static_cast<float>(piece.half_angle));
            glUniform3f(std::get<4>(m_sheet_uniforms),
                static_cast<float>(piece.pivot.x),
                static_cast<float>(piece.pivot.y),
                static_cast<float>(piece.pivot.z));
            glUniform3f(std::get<5>(m_sheet_uniforms),
                static_cast<float>(piece.axis.x),
                static_cast<float>(piece.axis.y),
                static_cast<float>(piece.axis.z));
            glUniform3f(std::get<6>(m_sheet_uniforms),
                static_cast<float>(piece.translation.x),
                static_cast<float>(piece.translation.y),
                static_cast<float>(piece.translation.z));
            glUniform1f(std::get<7>(m_sheet_uniforms), static_cast<float>(piece.rotation));
            glUniform1f(std::get<8>(m_sheet_uniforms), static_cast<float>(piece.scale));
            glUniform1f(std::get<9>(m_sheet_uniforms),
                static_cast<float>(std::clamp(1.0 - (piece.age / .05), 0.0, 1.0)));
            // Preserve the attached mesh's vertices when a sector becomes a free sheet.
            constexpr double TWO_PI = 6.283185307179586;
            constexpr int SLICES = 96;
            constexpr int VERTICES_PER_SLICE = 12 * 6;
            const double PHI = std::fmod(piece.phi + TWO_PI, TWO_PI);
            const int FIRST =
                static_cast<int>(std::floor((PHI - piece.half_angle) * SLICES / TWO_PI));
            const int LAST =
                static_cast<int>(std::ceil((PHI + piece.half_angle) * SLICES / TWO_PI));
            const int START = (FIRST + SLICES) % SLICES;
            const int LENGTH = LAST - FIRST;
            const int HEAD = std::min(LENGTH, SLICES - START);
            glDrawArrays(GL_TRIANGLES, START * VERTICES_PER_SLICE, HEAD * VERTICES_PER_SLICE);
            if (HEAD < LENGTH) {
                glDrawArrays(GL_TRIANGLES, 0, (LENGTH - HEAD) * VERTICES_PER_SLICE);
            }
        }
    }
    glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
    if (BLENDING == 0U) {
        glDisable(GL_BLEND);
    }
}
