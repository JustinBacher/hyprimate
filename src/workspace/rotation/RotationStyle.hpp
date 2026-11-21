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
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/WorkspaceJourney.hpp"
#include "workspace/rotation/RotationRenderer.hpp"

namespace hyprimate::workspace {
struct RotationStyle {
    static constexpr config::WorkspaceStyle KIND = config::WorkspaceStyle::Rotation;
    static constexpr std::string_view NAME = "rotation";
    static constexpr bool USES_WALLPAPER = true;
    static constexpr bool LAYERED = false;
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
    static double duration(const Resources &resources, const JourneyEndpoint & /*from*/, const JourneyEndpoint & /*to*/) {
        return resources.settings.prepared.duration;
    }
    static constexpr double live_boundary(const Resources & /*resources*/) { return 1.0; }
    static constexpr bool needs_individual_windows(const Resources & /*resources*/) { return false; }
    static constexpr bool uses_native_hud(const Resources & /*resources*/) { return false; }
    static constexpr bool uses_workspace_previews(const Resources & /*resources*/) { return false; }
    static bool draw(Resources &resources, Render::CRenderContext &ctx, const WorkspaceFrame &frame,
        double progress, Direction direction, bool matching_wallpaper) {
        return resources.renderer.draw(ctx, frame.source.composite, frame.destination.composite, frame.presentation,
            frame.source.wallpaper, frame.destination.wallpaper, progress, direction, resources.settings.prepared.gap, matching_wallpaper);
    }
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
            const auto CHANNEL = [&](unsigned shift) {
                return static_cast<float>((value.gapFillColor >> shift) & 0xFFU) / 255.F;
            };
            const float ALPHA = CHANNEL(24);
            result.gap.color = {CHANNEL(16) * ALPHA, CHANNEL(8) * ALPHA, CHANNEL(0) * ALPHA, ALPHA};
            result.gap.blur = static_cast<float>(value.gapFillBlur);
            return result;
        });
    }
};
}
