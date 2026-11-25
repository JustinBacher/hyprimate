#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <tuple>
#include <vector>

#include <GLES3/gl32.h>

#include "gl/Storage.hpp"
#include "shared/Core.hpp"
#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleMaterial.hpp"
#include "window/bubble/BubbleMaterialLayout.hpp"
#include "window/bubble/BubbleOptics.hpp"

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
            (void)std::fprintf(
                stderr, "bubble uniform name %s\n", RESULT < 0 ? "formatting failed" : "truncated");
            valid = false;
            return -1;
        }
        return glGetUniformLocation(program_id, name.data());
    }
    using bubble_material_detail::Instance;
    using bubble_material_detail::K_MICRO_INSTANCES;
    using bubble_material_detail::MicroInstance;
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
    if (m_membrane != 0U) {
        return m_membrane == programs.membrane && m_curl == programs.curl &&
               m_particles == programs.particles && m_film == programs.film &&
               m_micro == programs.micro;
    }
    struct Rollback {
        CBubbleMaterial &material;
        bool committed = false;
        ~Rollback() {
            if (!committed) {
                material.release();
            }
        }
    } rollback{*this};
    m_membrane = programs.membrane;
    m_curl = programs.curl;
    m_particles = programs.particles;
    m_film = programs.film;
    m_micro = programs.micro;
    if ((m_membrane == 0U) || (m_curl == 0U) || (m_particles == 0U) || (m_film == 0U) ||
        (m_micro == 0U)) {
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
    if ((m_vao == 0U) || (m_particle_vao == 0U) || (m_instance == 0U)) {
        return false;
    }
    glBindVertexArray(m_particle_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instance);
    if (!hyprimate::gl::allocate_buffer(GL_ARRAY_BUFFER,
            sizeof(Instance) * 2 * hyprimate::K_BUBBLE_MAX_PARTICLES,
            nullptr,
            GL_STREAM_DRAW)) {
        return false;
    }
    for (GLuint i = 0; i < 2; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Instance),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr):
            // GLES buffer byte-offset ABI.
            reinterpret_cast<void *>(static_cast<std::size_t>(i) * 4 * sizeof(float)));
        glVertexAttribDivisor(i, 1);
    }
    glGenVertexArrays(1, &m_micro_vao);
    glGenBuffers(1, &m_micro_instance);
    if ((m_micro_vao == 0U) || (m_micro_instance == 0U)) {
        return false;
    }
    glBindVertexArray(m_micro_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_micro_instance);
    if (!hyprimate::gl::allocate_buffer(
            GL_ARRAY_BUFFER, sizeof(MicroInstance) * K_MICRO_INSTANCES, nullptr, GL_STREAM_DRAW)) {
        return false;
    }
    for (GLuint i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(MicroInstance),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr):
            // GLES buffer byte-offset ABI.
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
    if (m_lut == 0U) {
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, m_lut);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!hyprimate::gl::allocate_texture_2d(
            GL_RGBA16F, TABLE.thickness_samples, TABLE.cosine_samples)) {
        return false;
    }
    {
        const hyprimate::gl::ClientPixelUnpack unpack;
        glTexSubImage2D(GL_TEXTURE_2D,
            0,
            0,
            0,
            TABLE.thickness_samples,
            TABLE.cosine_samples,
            GL_RGBA,
            GL_FLOAT,
            rgba.data());
    }
    rollback.committed = true;
    return true;
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
}
