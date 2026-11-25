#include <algorithm>
#include <array>
#include <cstdio>
#include <mdspan>
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
    using bubble_material_detail::vector;
    MicroInstance micro_instance(const hyprimate::BubbleMicro &bubble) {
        return {{
            static_cast<float>(bubble.centre.x),
            static_cast<float>(bubble.centre.y),
            static_cast<float>(bubble.radius),
            static_cast<float>(bubble.growth),
            static_cast<float>(bubble.anchor.x),
            static_cast<float>(bubble.anchor.y),
            static_cast<float>(bubble.attach),
            static_cast<float>(bubble.collapse),
            static_cast<float>(bubble.thickness),
            static_cast<float>(bubble.seed),
            static_cast<float>(bubble.flow),
            static_cast<float>(bubble.visibility),
            static_cast<float>(bubble.squash.x),
            static_cast<float>(bubble.squash.y),
            0.F,
            0.F,
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
void CBubbleMaterial::draw_particles(std::span<const hyprimate::BubbleParticle> particles,
    hyprimate::Vec2 size,
    const SBubbleTarget &target,
    bool on_glass) {
    if ((m_particles == 0U) || particles.empty()) {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): Every uploaded instance is fully
    // assigned; the unused suffix is never read or uploaded.
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
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): Every uploaded instance is fully
    // assigned; the unused suffix is never read or uploaded.
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
        constexpr std::size_t MAX_NODES = 48;
        static_assert(hyprimate::K_BUBBLE_WAKE_STRAND_NODES <= MAX_NODES);
        std::array<float, 4 * MAX_NODES> nodes{}, state{};
        const auto NODE_COUNT = std::min(wake.node_count, wake.nodes.size());
        for (const auto &segment :
            std::span(wake.segments).first(std::min(wake.segment_count, wake.segments.size()))) {
            const auto COUNT = segment.count;
            if (COUNT < 2 || COUNT > MAX_NODES || segment.first > NODE_COUNT ||
                COUNT > NODE_COUNT - segment.first) {
                continue;
            }
            using PackedNodes = std::mdspan<float, std::extents<std::size_t, MAX_NODES, 4>>;
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
            glUniform2f(std::get<9>(uniforms),
                static_cast<float>(segment.from),
                static_cast<float>(segment.to));
            glDrawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(2 * COUNT));
        }
    }
    draw_microbubbles(
        std::span(wake.bubbles).first(std::min(wake.bubble_count, wake.bubbles.size())),
        wake.size,
        target);
}
