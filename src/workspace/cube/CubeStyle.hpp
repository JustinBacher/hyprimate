#pragma once

#include <string>
#include <string_view>

#include "StyleSettingsCache.hpp"
#include "config_settings_generated.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/WorkspaceTransition.hpp"
#include "workspace/cube/CubeMaterial.hpp"
#include "workspace/cube/CubeRenderer.hpp"

namespace hyprimate::workspace {
struct CubeStyle {
    static constexpr config::WorkspaceStyle KIND = config::WorkspaceStyle::Cube;
    static constexpr std::string_view NAME = "cube";
    static constexpr bool USES_WALLPAPER = false;
    static constexpr bool LAYERED = false;

    struct Prepared {
        double duration = 0.0;
        std::string curve;
        cube::Settings material{};
    };
    struct Resources {
        CubeRenderer renderer;
        SStyleSettingsCache<config::SWorkspaceCube, Prepared> settings;
        bool ready = false;
    };

    static double duration(const Resources &resources, const JourneyEndpoint & /*from*/, const JourneyEndpoint & /*to*/) {
        return resources.settings.prepared.duration;
    }
    static constexpr WorkspaceTravel travel(const Resources & /*resources*/) { return nullptr; }
    static constexpr double live_boundary(const Resources & /*resources*/) { return 1.0; }
    static constexpr bool needs_individual_windows(const Resources & /*resources*/) { return false; }
    static constexpr bool uses_native_hud(const Resources & /*resources*/) { return false; }
    static constexpr bool uses_workspace_previews(const Resources & /*resources*/) { return false; }

    static void prepare(Resources &resources, const config::SWorkspaceSettings &settings) {
        resources.settings.update(settings.cube, [](const auto &value) {
            const bool SOLID = value.variant == config::CubeVariant::Solid;
            return Prepared{
                .duration = value.duration,
                .curve = value.curve,
                .material = {
                    .variant = SOLID ? cube::Variant::Solid : cube::Variant::InsideOut,
                    .tilt_degrees = static_cast<float>(SOLID ? value.solid.cubeTilt : value.insideOut.cubeTilt),
                    .zoom = static_cast<float>(SOLID ? value.solid.zoom : value.insideOut.zoom),
                },
            };
        });
    }

    static bool draw(Resources &resources, Render::CRenderContext &ctx, const WorkspaceFrame &frame,
        double progress, Direction direction, bool /*matching_wallpaper*/) {
        return resources.renderer.draw(ctx, frame, progress, direction, resources.settings.prepared.material);
    }
};
}
