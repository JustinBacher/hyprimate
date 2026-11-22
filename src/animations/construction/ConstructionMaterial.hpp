#pragma once

#include <array>
#include <optional>

#include <GLES3/gl3.h>

#include "animations/construction/ConstructionBatch.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {

struct SConstructionTarget {
    Box canvas, source;             // Top-first device pixels in the same output coordinates.
    Box body, capture;              // Authoritative logical boxes in the same coordinate space.
    Vec2 image_origin, image_size;  // Device coordinates of the borrowed image and its texel size.
    double scale = 1;
    bool image_rgbx = false;
    bool output_top_first = true;
};

class CConstructionMaterial {
  public:
    static constexpr GLint MAX_TARGET_EDGE = 8192;
    static constexpr double MAX_TARGET_PIXELS = 16777216;

    struct View {
        GLsizei width, height;
        std::array<float, 3> pivot;
        std::array<float, 4> canvas, crop, client_map, sample_bounds;
        std::array<float, 2> inverse_image;
        std::array<GLint, 3> texel_grid;
        bool rgbx;
    };

    CConstructionMaterial() = default;
    // The caller controls owner-context lifetime and calls release() while current.
    ~CConstructionMaterial() = default;
    CConstructionMaterial(const CConstructionMaterial &) = delete;
    CConstructionMaterial &operator=(const CConstructionMaterial &) = delete;
    CConstructionMaterial(CConstructionMaterial &&) = delete;
    CConstructionMaterial &operator=(CConstructionMaterial &&) = delete;

    // Pure coordinate validation, before a compositor work lease can be acquired.
    [[nodiscard]] static std::optional<View> prepare_view(const SConstructionTarget &target) noexcept;
    [[nodiscard]] bool supports_view(const View &view) noexcept;
    [[nodiscard]] bool initialize(GLuint borrowed_program) noexcept;
    // All mutating GL operations run inside a two-unit raw state bracket. draw() expects a
    // viewport at (0, 0) sized to the view, and one alpha-capable texture at COLOR_ATTACHMENT0.
    [[nodiscard]] bool draw(const ConstructionBatch &batch, const View &view, GLuint image,
        GLuint output_texture) const noexcept;
    void release() noexcept;

  private:
    struct Uniforms {
        GLint pivot, canvas, crop, client_map, sample_bounds, inverse_image, texel_grid, rgbx, image, atlas;
    };

    [[nodiscard]] bool upload_atlas() noexcept;

    GLuint m_program = 0, m_vao = 0, m_instances = 0, m_atlas = 0, m_client_sampler = 0;
    Uniforms m_uniforms{};
    std::array<GLint, 2> m_target_limit{};
};

}  // namespace hyprimate
