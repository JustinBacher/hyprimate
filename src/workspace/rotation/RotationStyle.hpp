#pragma once

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "StyleSettingsCache.hpp"
#include "config_generated.hpp"
#include "workspace/rotation/RotationRenderer.hpp"

namespace hyprimate::workspace {
struct RotationStyle {
    static constexpr config::WorkspaceStyle kind = config::WorkspaceStyle::Rotation;
    static constexpr bool usesWallpaper = true;
    static constexpr std::string_view legacyName = "rotation";
    struct Prepared {
        double duration = 0.0;
        std::string curve;
        GapFill gap;
    };
    struct Resources {
        RotationRenderer renderer;
        SStyleSettingsCache<config::SWorkspaceRotation, Prepared> settings;
        bool ready = false;
    };
    static void prepare(Resources &resources, const config::SWorkspaceSettings &settings) {
        resources.settings.update(settings.rotation, [](const auto &value) {
            Prepared result;
            result.duration = value.duration;
            result.curve = value.curve;
            switch (value.gapFillStyle) {
                case config::RotationGapFillStyle::Morph: result.gap.style = GapFillStyle::Morph; break;
                case config::RotationGapFillStyle::Background: result.gap.style = GapFillStyle::Background; break;
                case config::RotationGapFillStyle::Wallpaper: result.gap.style = GapFillStyle::Wallpaper; break;
                case config::RotationGapFillStyle::Still: result.gap.style = GapFillStyle::Still; break;
            }
            const auto channel = [&](unsigned shift) {
                return static_cast<float>((value.gapFillColor >> shift) & 0xFFU) / 255.F;
            };
            const float alpha = channel(24);
            result.gap.color = {channel(16) * alpha, channel(8) * alpha, channel(0) * alpha, alpha};
            result.gap.blur = static_cast<float>(value.gapFillBlur);
            return result;
        });
    }
};
}
