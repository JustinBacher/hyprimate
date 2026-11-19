#pragma once

#include "GlHarness.hpp"
#include "workspace/planet_hop/PlanetGlRenderer.hpp"
#include "workspace/planet_hop/PlanetMaterial.hpp"

// Offscreen tests own their programs; production owns CShader instances instead.
// Both supply the same GLSL to the compositor-free renderer.
class PlanetTestRenderer final {
  public:
    PlanetTestRenderer() = default;
    ~PlanetTestRenderer() {
        release_raw();
    }
    PlanetTestRenderer(const PlanetTestRenderer &) = delete;
    PlanetTestRenderer &operator=(const PlanetTestRenderer &) = delete;
    PlanetTestRenderer(PlanetTestRenderer &&) = delete;
    PlanetTestRenderer &operator=(PlanetTestRenderer &&) = delete;

    bool initialize_raw() {
        namespace shader = hyprimate::workspace::planet_hop::shader;
        if (m_programs.planet == 0) {
            m_programs.planet = gl_harness::build_program(shader::VERTEX, shader::FRAGMENT);
            m_programs.quad = gl_harness::build_program(shader::QUAD_VERTEX, shader::QUAD_FRAGMENT);
            m_programs.sky = gl_harness::build_program(shader::SKY_VERTEX, shader::SKY_FRAGMENT);
        }
        return m_renderer.initialize_raw(m_programs);
    }

    void release_raw() {
        m_renderer.release_raw();
        glDeleteProgram(m_programs.planet);
        glDeleteProgram(m_programs.quad);
        glDeleteProgram(m_programs.sky);
        m_programs = {};
    }

    bool prepare_depth(const std::string &path) {
        return m_renderer.prepare_depth(path);
    }
    [[nodiscard]] bool has_depth_image() const {
        return m_renderer.has_depth_image();
    }
    [[nodiscard]] const std::string &depth_error() const {
        return m_renderer.depth_error();
    }

    bool draw_raw(const hyprimate::workspace::PlanetFrameTextures &textures,
        const hyprimate::workspace::planet_hop::Frame &frame,
        double progress,
        const hyprimate::workspace::PlanetRenderSettings &settings,
        std::span<const hyprimate::workspace::PlanetNeighborTextures> neighbors = {}) {
        return m_renderer.draw_raw(textures, frame, progress, settings, neighbors);
    }

  private:
    hyprimate::workspace::PlanetGlRenderer m_renderer;
    hyprimate::workspace::PlanetPrograms m_programs{};
};
