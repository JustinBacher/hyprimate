#include <algorithm>
#include <array>
#include <cstddef>
#include <mdspan>
#include <span>

#include <GLES3/gl32.h>

#include "window/bubble/BubbleMaterial.hpp"
#include "window/bubble/BubbleMaterialLayout.hpp"

namespace {
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

}  // namespace
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
