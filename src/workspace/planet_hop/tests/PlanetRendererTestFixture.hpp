#pragma once

#include <array>
#include <string_view>
#include <vector>

#include <GLES3/gl32.h>

#include "PlanetTestRenderer.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

namespace planet_renderer_tests {
    inline constexpr int WIDTH = 96, HEIGHT = 64;

    bool equal_pixels(const std::vector<unsigned char> &actual,
        const std::vector<unsigned char> &expected,
        int width,
        const char *label,
        double progress);
    GLuint texture(int width, int height, const unsigned char *pixels);
    std::array<double, 3> inverse_chart(double world_x, double world_y, double bend, double aspect);
    void dump_frame(std::string_view label,
        double progress,
        int width,
        int height,
        const std::vector<unsigned char> &pixels);

    void check_depth(PlanetTestRenderer &renderer);
    void check_allocation_retry();
    void check_native_floating(PlanetTestRenderer &renderer);
    void check_continuous_surface(PlanetTestRenderer &renderer);
    void check_landing_coverage(PlanetTestRenderer &renderer);
    void check_raw_endpoints(PlanetTestRenderer &renderer,
        const hyprimate::workspace::PlanetFrameTextures &textures,
        const hyprimate::workspace::planet_hop::Journey &journey,
        const hyprimate::workspace::PlanetRenderSettings &settings,
        int width,
        int height,
        const std::vector<unsigned char> &source,
        const std::vector<unsigned char> &destination,
        std::vector<unsigned char> &actual);
    void check_reversal_landing(PlanetTestRenderer &renderer,
        const hyprimate::workspace::PlanetFrameTextures &textures,
        const hyprimate::workspace::planet_hop::Journey &journey,
        const hyprimate::workspace::PlanetRenderSettings &settings,
        int width,
        int height,
        const std::vector<unsigned char> &source,
        const std::vector<unsigned char> &destination);
    void check_renderer_ownership(PlanetTestRenderer &renderer,
        hyprimate::workspace::PlanetFrameTextures &textures,
        const hyprimate::workspace::planet_hop::Journey &journey,
        hyprimate::workspace::PlanetRenderSettings &settings,
        int width,
        int height,
        const std::vector<unsigned char> &expected,
        GLuint source,
        GLuint destination,
        GLuint output);
    void run(bool profile, int width, int height, int frames);
}  // namespace planet_renderer_tests
