#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mdspan>
#include <span>
#include <tuple>
#include <vector>

#include <GLES3/gl32.h>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMaterial.hpp"
#include "animations/bubble/BubbleMotion.hpp"
#include "animations/bubble/BubbleOptics.hpp"
#include "animations/bubble/BubbleSheets.hpp"
#include "animations/bubble/BubbleWake.hpp"
#include "animations/shared/Core.hpp"

namespace {
    template <typename... Args>
    GLint formatted_uniform(GLuint program_id, bool &valid, const char *format, Args... args) {
        if (!valid) {
            return -1;
        }
        std::array<char, 80> name{};
        const int RESULT = std::snprintf(name.data(), name.size(), format, args...);
        if (RESULT < 0 || static_cast<std::size_t>(RESULT) >= name.size()) {
            name.front() = '\0';
            (void)std::fprintf(stderr, "bubble uniform name %s\n",
                RESULT < 0 ? "formatting failed" : "truncated");
            valid = false;
            return -1;
        }
        return glGetUniformLocation(program_id, name.data());
    }
    void vector(GLint location, hyprimate::Vec2 value) {
        glUniform2f(location, static_cast<float>(value.x), static_cast<float>(value.y));
    }
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
        glUniform4fv(std::get<45>(uniforms), static_cast<GLsizei>(hyprimate::K_BUBBLE_BUDS), buds.data());
    }
    struct Instance {
        std::array<float, 8> values;
    };
    // One small soap bubble: shape, neck, film (thickness, seed, flow, visibility), outline.
    struct MicroInstance {
        std::array<float, 16> values;
    };
    constexpr std::size_t K_MICRO_INSTANCES = std::max(
        {hyprimate::K_BUBBLE_MICRO_CAPACITY, hyprimate::K_BUBBLE_WAKE_BUBBLES,
            hyprimate::kBubbleFormationCapacity});
    MicroInstance micro_instance(const hyprimate::BubbleMicro &bubble) {
        return {{
            static_cast<float>(bubble.centre.x), static_cast<float>(bubble.centre.y),
            static_cast<float>(bubble.radius), static_cast<float>(bubble.growth),
            static_cast<float>(bubble.anchor.x), static_cast<float>(bubble.anchor.y),
            static_cast<float>(bubble.attach), static_cast<float>(bubble.collapse),
            static_cast<float>(bubble.thickness), static_cast<float>(bubble.seed),
            static_cast<float>(bubble.flow), static_cast<float>(bubble.visibility),
            static_cast<float>(bubble.squash.x), static_cast<float>(bubble.squash.y), 0.F, 0.F,
        }};
    }
    // Movement uniforms follow the 13 fields and 4 holes of every membrane.
    constexpr std::array K_MOTION_FIELDS{
        "drag",
        "waveCos",
        "waveSin",
        "sheen",
        "round",
        "extent",
        "carry",
        "transparency",
        "buds[0]",
    };
}  // namespace

bool CBubbleMaterial::initialize(const SBubblePrograms &programs) {
    if (m_tried) {
        return (m_membrane != 0U) && (m_curl != 0U) && (m_particles != 0U) && (m_film != 0U) &&
               (m_micro != 0U) && (m_vao != 0U) && (m_particle_vao != 0U) && (m_micro_vao != 0U) &&
               (m_instance != 0U) && (m_micro_instance != 0U) && (m_lut != 0U);
    }
    m_tried = true;
    m_membrane = programs.membrane;
    m_curl = programs.curl;
    m_particles = programs.particles;
    m_film = programs.film;
    m_micro = programs.micro;
    if ((m_membrane == 0U) || (m_curl == 0U) || (m_particles == 0U) || (m_film == 0U) ||
        (m_micro == 0U)) {
        release();
        m_tried = true;
        return false;
    }
    constexpr std::array MEMBRANE_NAMES{
        "size",
        "sourcePos",
        "sourceSize",
        "outputOrigin",
        "textureOrigin",
        "imageSize",
        "wallpaperOrigin",
        "wallpaperX",
        "wallpaperY",
        "wallpaperPixel",
        "wallpaperReady",
        "image",
        "wallpaperImage",
        "filmLut",
    };
    constexpr std::array PARTICLE_NAMES{
        "canvas",
        "origin",
        "sourcePos",
        "sourceSize",
        "size",
        "onGlass",
    };
    constexpr std::array CURL_NAMES{
        "size",
        "canvas",
        "origin",
        "sourcePos",
        "sourceSize",
        "textureOrigin",
        "imageSize",
        "wallpaperOrigin",
        "wallpaperX",
        "wallpaperY",
        "wallpaperPixel",
        "wallpaperReady",
        "image",
        "wallpaperImage",
        "filmLut",
    };
    constexpr std::array SURFACE_NAMES{"windowSurface", "shellSurface"};
    constexpr std::array FIELDS{
        "halfSize",
        "core",
        "corner",
        "depth",
        "content",
        "film",
        "visibility",
        "ripple",
        "thickness",
        "flow",
        "aureole",
        "spherical",
        "holeCount",
    };
    constexpr std::array HOLE_FIELDS{
        "centre",
        "radius",
        "rim",
        "precursor",
        "amplitude",
        "phase",
    };
    static_assert(std::size(MEMBRANE_NAMES) == std::tuple_size_v<decltype(m_membrane_uniforms)>);
    static_assert(std::size(PARTICLE_NAMES) == std::tuple_size_v<decltype(m_particle_uniforms)>);
    static_assert(std::size(CURL_NAMES) == std::tuple_size_v<decltype(m_curl_uniforms)>);
    static_assert(std::size(SURFACE_NAMES) == std::tuple_size_v<decltype(m_surface_uniforms)>);
    static_assert(std::size(FIELDS) == 13 && std::size(HOLE_FIELDS) == 6 &&
                  std::size(K_MOTION_FIELDS) == 9 && hyprimate::K_BUBBLE_HOLES == 4);
    std::ranges::transform(MEMBRANE_NAMES, m_membrane_uniforms.begin(), [this](const char *name) {
        return glGetUniformLocation(m_membrane, name);
    });
    std::ranges::transform(PARTICLE_NAMES, m_particle_uniforms.begin(), [this](const char *name) {
        return glGetUniformLocation(m_particles, name);
    });
    std::ranges::transform(CURL_NAMES, m_curl_uniforms.begin(), [this](const char *name) {
        return glGetUniformLocation(m_curl, name);
    });
    constexpr std::array SHEET_NAMES{
        "sheetMode",
        "maskCount",
        "sheetHole",
        "sector",
        "pivot",
        "axis",
        "translation",
        "rotation",
        "shrink",
        "imageFade",
    };
    static_assert(std::size(SHEET_NAMES) == std::tuple_size_v<decltype(m_sheet_uniforms)>);
    std::ranges::transform(SHEET_NAMES, m_sheet_uniforms.begin(), [this](const char *name) {
        return glGetUniformLocation(m_curl, name);
    });
    constexpr std::array MASK_FIELDS{"domain", "radii", "onsets"};
    static_assert(
        std::size(MASK_FIELDS) == std::tuple_size_v<decltype(m_sheet_mask_uniforms)::value_type>);
    bool valid_names = true;
    std::size_t sheet_index = 0;
    for (auto &mask_uniforms : m_sheet_mask_uniforms) {
        auto *location = mask_uniforms.begin();
        for (const auto *field : MASK_FIELDS) {
            *location = formatted_uniform(m_curl, valid_names, "masks[%zu].%s", sheet_index, field);
            ++location;
        }
        ++sheet_index;
    }
    const auto *surface_name = SURFACE_NAMES.begin();
    for (auto &uniforms : m_surface_uniforms) {
        auto *location = uniforms.begin();
        for (const auto *field : FIELDS) {
            *location = formatted_uniform(m_membrane, valid_names, "%s.%s", *surface_name, field);
            ++location;
        }
        for (std::size_t hole_slot = 0; hole_slot < hyprimate::K_BUBBLE_HOLES; ++hole_slot) {
            for (const auto *field : HOLE_FIELDS) {
                *location = formatted_uniform(
                    m_membrane, valid_names, "%s.holes[%zu].%s", *surface_name, hole_slot, field);
                ++location;
            }
        }
        for (const auto *field : K_MOTION_FIELDS) {
            *location = formatted_uniform(m_membrane, valid_names, "%s.%s", *surface_name, field);
            ++location;
        }
        ++surface_name;
    }
    {
        auto *location = m_curl_surface_uniforms.begin();
        for (const auto *field : FIELDS) {
            *location = formatted_uniform(m_curl, valid_names, "sheet.%s", field);
            ++location;
        }
        for (std::size_t hole_index = 0; hole_index < hyprimate::K_BUBBLE_HOLES; ++hole_index) {
            for (const auto *field : HOLE_FIELDS) {
                *location = formatted_uniform(
                    m_curl, valid_names, "sheet.holes[%zu].%s", hole_index, field);
                ++location;
            }
        }
        for (const auto *field : K_MOTION_FIELDS) {
            *location = formatted_uniform(m_curl, valid_names, "sheet.%s", field);
            ++location;
        }
    }
    if (!valid_names) {
        release();
        m_tried = true;
        return false;
    }
    constexpr std::array FILM_NAMES{
        "canvas",
        "origin",
        "sourcePos",
        "sourceSize",
        "size",
        "nodes",
        "nodeState",
        "seed",
        "filmLut",
        "arcRange",
    };
    static_assert(std::size(FILM_NAMES) == std::tuple_size_v<decltype(m_film_uniforms)>);
    std::ranges::transform(FILM_NAMES, m_film_uniforms.begin(), [this](const char *name) {
        return glGetUniformLocation(m_film, name);
    });
    constexpr std::array MICRO_NAMES{
        "canvas",
        "origin",
        "sourcePos",
        "sourceSize",
        "size",
        "filmLut",
    };
    static_assert(std::size(MICRO_NAMES) == std::tuple_size_v<decltype(m_micro_uniforms)>);
    std::ranges::transform(MICRO_NAMES, m_micro_uniforms.begin(), [this](const char *name) {
        return glGetUniformLocation(m_micro, name);
    });
    glGenVertexArrays(1, &m_vao);
    glGenVertexArrays(1, &m_particle_vao);
    glGenBuffers(1, &m_instance);
    glBindVertexArray(m_particle_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instance);
    for (GLuint i = 0; i < 2; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Instance),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): GLES buffer byte-offset ABI.
            reinterpret_cast<void *>(static_cast<std::size_t>(i) * 4 * sizeof(float)));
        glVertexAttribDivisor(i, 1);
    }
    glGenVertexArrays(1, &m_micro_vao);
    glGenBuffers(1, &m_micro_instance);
    glBindVertexArray(m_micro_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_micro_instance);
    glBufferData(
        GL_ARRAY_BUFFER, sizeof(MicroInstance) * K_MICRO_INSTANCES, nullptr, GL_STREAM_DRAW);
    for (GLuint i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(MicroInstance),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): GLES buffer byte-offset ABI.
            reinterpret_cast<void *>(static_cast<std::size_t>(i) * 4 * sizeof(float)));
        glVertexAttribDivisor(i, 1);
    }
    const auto TABLE = hyprimate::bubble_thin_film_table();
    std::vector<float> rgba(
        static_cast<std::size_t>(TABLE.thickness_samples) * TABLE.cosine_samples * 4, 1.0F);
    auto rgb = std::span{TABLE.rgb};
    auto pixels = std::span{rgba};
    for (std::size_t i = 0; i < TABLE.rgb.size() / 3; ++i) {
        std::ranges::copy(rgb.first<3>(), pixels.begin());
        rgb = rgb.subspan<3>();
        pixels = pixels.subspan<4>();
    }
    glActiveTexture(GL_TEXTURE2);
    glGenTextures(1, &m_lut);
    glBindTexture(GL_TEXTURE_2D, m_lut);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_RGBA16F,
        TABLE.thickness_samples,
        TABLE.cosine_samples,
        0,
        GL_RGBA,
        GL_FLOAT,
        rgba.data());
    return (m_vao != 0U) && (m_particle_vao != 0U) && (m_micro_vao != 0U) && (m_instance != 0U) &&
           (m_micro_instance != 0U) && (m_lut != 0U);
}
void CBubbleMaterial::release() {
    if (m_vao != 0U) {
        glDeleteVertexArrays(1, &m_vao);
    }
    if (m_particle_vao != 0U) {
        glDeleteVertexArrays(1, &m_particle_vao);
    }
    if (m_micro_vao != 0U) {
        glDeleteVertexArrays(1, &m_micro_vao);
    }
    if (m_instance != 0U) {
        glDeleteBuffers(1, &m_instance);
    }
    if (m_micro_instance != 0U) {
        glDeleteBuffers(1, &m_micro_instance);
    }
    if (m_lut != 0U) {
        glDeleteTextures(1, &m_lut);
    }
    m_membrane = m_curl = m_particles = m_film = m_micro = m_vao = m_particle_vao = m_micro_vao =
        m_instance = m_micro_instance = m_lut = 0;
    m_tried = false;
}
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
        draw_microbubbles(std::span(frame.formation).first(std::min(frame.formation_count, frame.formation.size())),
            frame.size, target);
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
        for (const auto &hole : std::span(current.holes).first(std::min(current.hole_count, hyprimate::K_BUBBLE_HOLES))) {
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
void CBubbleMaterial::draw_particles(std::span<const hyprimate::BubbleParticle> particles,
    hyprimate::Vec2 size,
    const SBubbleTarget &target,
    bool on_glass) {
    if ((m_particles == 0U) || particles.empty()) {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): Every uploaded instance is fully assigned; the unused suffix is never read or uploaded.
    [[indeterminate]] std::array<Instance, 2 * hyprimate::K_BUBBLE_MAX_PARTICLES> data;
    const auto COUNT = std::min(particles.size(), data.size());
    const auto INPUT = particles.first(COUNT);
    auto input_it = INPUT.begin();
    for (auto &instance : std::span{data}.first(COUNT)) {
        const auto &particle = *input_it++;
        instance = {
            {
                static_cast<float>(particle.centre.x),
                static_cast<float>(particle.centre.y),
                static_cast<float>(particle.tail.x),
                static_cast<float>(particle.tail.y),
                static_cast<float>(particle.radius),
                static_cast<float>(particle.tail_radius),
                static_cast<float>(particle.depth),
                static_cast<float>(particle.blur),
            },
        };
    }
    glUseProgram(m_particles);
    glBindVertexArray(m_particle_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instance);
    glBufferData(GL_ARRAY_BUFFER, sizeof(data), nullptr, GL_STREAM_DRAW);
    glBufferSubData(
        GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(COUNT * sizeof(Instance)), data.data());
    const auto &uniforms = m_particle_uniforms;
    vector(std::get<0>(uniforms), target.canvas);
    vector(std::get<1>(uniforms), target.origin);
    vector(std::get<2>(uniforms), target.source_pos);
    vector(std::get<3>(uniforms), target.source_size);
    vector(std::get<4>(uniforms), size);
    glUniform1i(std::get<5>(uniforms), static_cast<GLint>(on_glass));
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, static_cast<GLsizei>(COUNT));
}
void CBubbleMaterial::draw_microbubbles(std::span<const hyprimate::BubbleMicro> bubbles,
    hyprimate::Vec2 size,
    const SBubbleTarget &target) {
    if ((m_micro == 0U) || bubbles.empty()) {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): Every uploaded instance is fully assigned; the unused suffix is never read or uploaded.
    [[indeterminate]] std::array<MicroInstance, K_MICRO_INSTANCES> data;
    std::size_t count = 0;
    for (const auto &bubble : bubbles) {
        if (count == data.size()) {
            break;
        }
        if (bubble.visibility > 0 && bubble.radius > 0) {
            data[count++] = micro_instance(bubble);
        }
    }
    if (count == 0) {
        return;
    }
    glUseProgram(m_micro);
    glBindVertexArray(m_micro_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_micro_instance);
    glBufferSubData(
        GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(MicroInstance)), data.data());
    const auto &uniforms = m_micro_uniforms;
    vector(std::get<0>(uniforms), target.canvas);
    vector(std::get<1>(uniforms), target.origin);
    vector(std::get<2>(uniforms), target.source_pos);
    vector(std::get<3>(uniforms), target.source_size);
    vector(std::get<4>(uniforms), size);
    glUniform1i(std::get<5>(uniforms), 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_lut);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, static_cast<GLsizei>(count));
}
void CBubbleMaterial::draw_wake(
    const hyprimate::BubbleWakeView &wake, const SBubbleTarget &target) {
    if (m_film == 0U) {
        return;
    }
    if (wake.segment_count > 0) {
        glUseProgram(m_film);
        glBindVertexArray(m_vao);
        const auto &uniforms = m_film_uniforms;
        vector(std::get<0>(uniforms), target.canvas);
        vector(std::get<1>(uniforms), target.origin);
        vector(std::get<2>(uniforms), target.source_pos);
        vector(std::get<3>(uniforms), target.source_size);
        vector(std::get<4>(uniforms), wake.size);
        glUniform1f(std::get<7>(uniforms), static_cast<float>(wake.seed));
        glUniform1i(std::get<8>(uniforms), 2);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_lut);
        // The film program holds 48 nodes; a strand never has more.
        static_assert(hyprimate::K_BUBBLE_WAKE_STRAND_NODES <= 48);
        std::array<float, 4 * 48> nodes{}, state{};
        for (const auto &segment : std::span(wake.segments).first(std::min(wake.segment_count, wake.segments.size()))) {
            if (segment.count < 2 || segment.first + segment.count > wake.node_count) {
                continue;
            }
            const auto COUNT = std::min<std::size_t>(segment.count, 48);
            using PackedNodes = std::mdspan<float, std::extents<std::size_t, 48, 4>>;
            const PackedNodes NODE_DATA{nodes.data()};
            const PackedNodes STATE_DATA{state.data()};
            std::size_t node_index = 0;
            for (const auto &node : std::span(wake.nodes).subspan(segment.first, COUNT)) {
                NODE_DATA[node_index, 0] = static_cast<float>(node.position.x);
                NODE_DATA[node_index, 1] = static_cast<float>(node.position.y);
                NODE_DATA[node_index, 2] = static_cast<float>(node.half_width);
                NODE_DATA[node_index, 3] = static_cast<float>(node.arc);
                STATE_DATA[node_index, 0] = static_cast<float>(node.direction.x);
                STATE_DATA[node_index, 1] = static_cast<float>(node.direction.y);
                STATE_DATA[node_index, 2] = static_cast<float>(node.thickness);
                STATE_DATA[node_index, 3] = static_cast<float>(node.evaporated);
                ++node_index;
            }
            glUniform4fv(std::get<5>(uniforms), static_cast<GLsizei>(COUNT), nodes.data());
            glUniform4fv(std::get<6>(uniforms), static_cast<GLsizei>(COUNT), state.data());
            glUniform2f(std::get<9>(uniforms), static_cast<float>(segment.from), static_cast<float>(segment.to));
            glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(2 * COUNT));
        }
    }
    draw_microbubbles(
        std::span(wake.bubbles).first(std::min(wake.bubble_count, wake.bubbles.size())),
        wake.size,
        target);
}
