#pragma once

#include <array>
#include <optional>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include "shared/Core.hpp"
#include "shader/GlesRuntime.hpp"

namespace hyprimate::shader::hyprland {
    inline constexpr unsigned MAX_EDGE = 8192, MAX_PIXELS = 16'777'216;
    using Image = SP<Render::IFramebuffer>;
    struct Extent {
        unsigned width, height;
        bool operator==(const Extent &) const = default;
    };
    using ClipList = std::array<Clip, 16>;
    [[nodiscard]] bool encloses(const Box &outer, const Box &inner) noexcept;

    [[nodiscard]] std::optional<Extent> extent(double width, double height,
        unsigned max_pixels = MAX_PIXELS) noexcept;
    [[nodiscard]] bool same_encoding(const Image &a, const Image &b);
    [[nodiscard]] DRMFormat alpha_format(DRMFormat format);
    [[nodiscard]] std::optional<Target> direct_target(Render::CRenderContext &ctx,
        const Image &encoding, const Box &box, Extent size,
        const std::array<float, 4> &domain, ClipList &clips, bool &submitted);
    [[nodiscard]] std::optional<Target> private_target(const Image &layer, Extent size,
        const std::array<float, 4> &domain, bool blur_mask = false);
}  // namespace hyprimate::shader::hyprland
