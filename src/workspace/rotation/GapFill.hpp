#pragma once

#include <array>

namespace hyprimate::workspace {
    enum class GapFillStyle { Morph, Background, Wallpaper, Still };
    struct GapFill {
        GapFillStyle style = GapFillStyle::Still;
        // Premultiplied RGBA; blur is measured in physical pixels.
        std::array<float, 4> color{0, 0, 0, 1};
        float blur = 0;
    };
}  // namespace hyprimate::workspace
