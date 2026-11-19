#pragma once

#include <optional>
#include "StyleSettingsCache.hpp"
#include "config_settings_generated.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/planet_hop/PlanetRenderer.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

namespace hyprimate::workspace {
    struct PlanetHopStyle {
        static constexpr config::WorkspaceStyle KIND = config::WorkspaceStyle::PlanetHop;
        static constexpr bool USES_WALLPAPER = true;
        static constexpr bool LAYERED = true;
        struct Prepared {
            double duration = 0.8;
            std::string curve = "linear";
            double live_boundary = 0.22;
            PlanetRenderSettings render;
            planet_hop::SceneOptions scene;
        };
        struct Resources {
            PlanetRenderer renderer;
            SStyleSettingsCache<config::SWorkspacePlanetHop, Prepared> settings;
            bool ready = false;
            std::optional<planet_hop::Journey> journey;
        };
        static void prepare(Resources &resources, const config::SWorkspaceSettings &settings) {
            if (!resources.settings.update(settings.planetHop, [](const auto &value) {
                Prepared result;
                result.duration = value.duration;
                result.curve = value.curve;
                result.live_boundary = value.liveBoundary;
                auto &render = result.render;
                render.quality = value.samplingQuality;
                render.fade = static_cast<float>(value.fade);
                render.elevation = static_cast<float>(value.elevation);
                render.duration = static_cast<float>(value.duration);
                render.depth_enabled = value.depthEnabled;
                render.depth_strength = static_cast<float>(value.depthStrength);
                render.lighting = value.lightingEnabled;
                render.clouds = value.cloudsEnabled;
                render.cloud_coverage = static_cast<float>(value.cloudCoverage);
                render.cloud_opacity = static_cast<float>(value.cloudOpacity);
                render.cloud_speed = static_cast<float>(value.cloudSpeed);
                render.atmosphere_intensity = value.atmosphereEnabled ? static_cast<float>(value.atmosphereIntensity) : 0;
                render.atmosphere_thickness = static_cast<float>(value.atmosphereThickness);
                render.night_brightness = static_cast<float>(value.nightBrightness);
                render.indirect_brightness = static_cast<float>(value.indirectBrightness);
                render.star_density = value.starsEnabled ? static_cast<float>(value.starDensity) : 0;
                render.star_parallax = static_cast<float>(value.starParallax);
                render.light_direction = {static_cast<float>(value.lightDirectionX),
                    static_cast<float>(value.lightDirectionY), static_cast<float>(value.lightDirectionZ)};
                render.window_mode = value.windowMode == config::PlanetHopWindowMode::Floating ? 1 :
                    value.windowMode == config::PlanetHopWindowMode::Fade ? 2 : 0;
                render.overlay_mode = value.overlayMode == config::PlanetHopOverlayMode::Hud ? 1 :
                    value.overlayMode == config::PlanetHopOverlayMode::Fade ? 2 : 0;
                auto &scene = result.scene;
                scene.planetLayout = value.planetLayout == config::PlanetHopPlanetLayout::SolarSystem ? planet_hop::Layout::SolarSystem :
                    value.planetLayout == config::PlanetHopPlanetLayout::Dynamic ? planet_hop::Layout::Dynamic : planet_hop::Layout::Linear;
                scene.skipDurationMode = value.skipDurationMode == config::PlanetHopSkipDurationMode::Constant ? planet_hop::DurationMode::Constant :
                    value.skipDurationMode == config::PlanetHopSkipDurationMode::DistanceScaled ? planet_hop::DurationMode::DistanceScaled : planet_hop::DurationMode::Adaptive;
                scene.planetSize = value.planetSize;
                scene.solarVerticalSpread = value.solarVerticalSpread;
                render.solar_background_scale = static_cast<float>(value.solarBackgroundScale);
                scene.planetSeparation = value.planetSeparation;
                scene.planetCoverage = value.planetCoverage;
                scene.elevation = value.elevation;
                scene.rotationEnabled = value.rotationEnabled;
                scene.rotationLimit = value.rotationLimit;
                scene.duration = value.duration;
                scene.distanceScale = value.distanceScale;
                scene.maxDuration = value.maxDuration;
                scene.adaptiveBoundary = static_cast<std::uint32_t>(value.adaptiveBoundary);
                return result;
            })) { return; }
            resources.journey.reset();
            (void)resources.renderer.prepare_depth(settings.planetHop.depthSource);
        }
        static bool draw(Resources &resources, Render::CRenderContext &ctx, const WorkspaceFrame &frame,
            double progress, Direction direction, bool matching) {
            auto &prepared = resources.settings.prepared;
            const auto SAME = [](const JourneyEndpoint &a, const JourneyEndpoint &b) {
                return a.identity == b.identity && a.numbered == b.numbered &&
                    a.special == b.special && a.logicalOrder == b.logicalOrder && a.placement == b.placement;
            };
            if (!frame.presentation) return false;
            const auto size = frame.presentation->m_size;
            if (size.y <= 0) return false;
            const double aspect = size.x / size.y;
            if (!resources.journey || !SAME(resources.journey->from, frame.source.endpoint) ||
                !SAME(resources.journey->to, frame.destination.endpoint) || resources.journey->aspect != aspect) {
                resources.journey = planet_hop::make_journey(frame.source.endpoint, frame.destination.endpoint, prepared.scene, aspect);
                prepared.render.duration = static_cast<float>(planet_hop::duration_seconds(
                    frame.source.endpoint, frame.destination.endpoint, prepared.scene));
            }
            return resources.renderer.draw(ctx, frame, *resources.journey, progress, direction, matching, prepared.render);
        }
        static double duration(const Resources &resources, const JourneyEndpoint &from, const JourneyEndpoint &to) {
            return planet_hop::duration_seconds(from, to, resources.settings.prepared.scene);
        }
        static double live_boundary(const Resources &resources) { return resources.settings.prepared.live_boundary; }
        static bool uses_native_hud(const Resources &resources) { return resources.settings.prepared.render.overlay_mode == 1; }
        static bool uses_workspace_previews(const Resources &resources) {
            return resources.settings.prepared.scene.planetLayout == planet_hop::Layout::SolarSystem;
        }
        static bool needs_individual_windows(const Resources &resources) {
            return resources.settings.prepared.render.window_mode == 1;
        }
    };
}
