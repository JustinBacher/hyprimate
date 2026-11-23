#pragma once

#include <array>
#include <memory>
#include <optional>

#include <GLES3/gl3.h>

#include "window/construction/ConstructionBatch.hpp"
#include "window/construction/studies/RegionalClose.hpp"
#include "shared/Core.hpp"

namespace hyprimate {

// Matches u_mask_mode: draw color, warp the native matte, or synthesize a body matte.
enum class ConstructionMaskMode : GLint { None = 0, Image = 1, Body = 2 };

struct SConstructionTarget {
    Box canvas, source;             // Top-first device pixels in the same output coordinates.
    Box body, capture;              // Authoritative logical boxes in the same coordinate space.
    Vec2 image_origin, image_size;  // Device coordinates of the borrowed image and its texel size.
    double scale = 1;
    bool image_rgbx = false;
    bool output_top_first = true;
    bool allow_empty_crop = false; // A valid off-output close is a successful blank publication.
    ConstructionMaskMode mask_mode = ConstructionMaskMode::None;
    double mask_rounding = 0;     // Body mask corner radius in device pixels.
    double mask_rounding_power = 2;
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
        std::optional<Box> authorized_local_crop; // Double precision for model triangle clipping.
        bool rgbx;
        ConstructionMaskMode mask_mode = ConstructionMaskMode::None;
        std::array<float, 4> mask_body{}; // Image-relative device pixel edges.
        float mask_rounding = 0;
        float mask_rounding_power = 2;
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
    [[nodiscard]] bool draw_close(const construction::studies::RegionalClose::Frame &frame,
        const ConstructionBatch &effects, const View &view, GLuint image, GLuint output_texture) noexcept;
    void release() noexcept;

  private:
    struct Uniforms {
        GLint pivot, canvas, crop, client_map, sample_bounds, inverse_image, texel_grid, rgbx;
        GLint mask_mode, mask_body, mask_rounding;
        GLint image, atlas, mesh;
    };
    struct MeshVertex {
        std::array<float, 3> position;
        std::array<float, 2> source;
    };

    [[nodiscard]] bool upload_atlas() noexcept;
    [[nodiscard]] bool initialize_close() noexcept;
    void release_close() noexcept;
    void bind_view(const View &view, GLuint image) const noexcept;
    void draw_instances(const ConstructionBatch &batch) const noexcept;

    GLuint m_program = 0, m_vao = 0, m_instances = 0, m_atlas = 0, m_client_sampler = 0;
    GLuint m_close_vao = 0, m_close_vertices = 0, m_close_indices = 0;
    std::unique_ptr<MeshVertex[]> m_close_upload;
    Uniforms m_uniforms{};
    std::array<GLint, 2> m_target_limit{};
};

}  // namespace hyprimate
