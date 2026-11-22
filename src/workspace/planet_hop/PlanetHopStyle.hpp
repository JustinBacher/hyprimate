#pragma once

#include <optional>
#include "StyleSettingsCache.hpp"
#include "config_settings_generated.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/planet_hop/PlanetCurves.hpp"
#include "workspace/planet_hop/PlanetRenderer.hpp"
#include "workspace/planet_hop/PlanetScene.hpp"

namespace hyprimate::workspace {
    struct PlanetHopStyle {
        static constexpr config::WorkspaceStyle KIND = config::WorkspaceStyle::PlanetHop;
        static constexpr std::string_view NAME = "planet_hop";
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
            planet_hop::PlanetCurves curves;
            SStyleSettingsCache<config::SWorkspacePlanetHop, Prepared> settings;
            bool ready = false;
            std::optional<planet_hop::Journey> journey;
        };
        static void prepare(Resources &resources, const config::SWorkspaceSettings &settings) {
            const bool CHANGED = resources.settings.update(settings.planetHop, [](const auto &value) {
                Prepared result;
                result.duration = value.duration / 1000.0;
                result.curve = value.curve;
                result.live_boundary = value.liveBoundary;
                auto &render = result.render;
                render.quality = value.samplingQuality;
                render.fade = static_cast<float>(value.fade);
                render.elevation = static_cast<float>(value.elevation);
                render.duration = static_cast<float>(result.duration);
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
                render.window_mode = 0;
                if (value.windowMode == config::PlanetHopWindowMode::Floating) {
                    render.window_mode = 1;
                } else if (value.windowMode == config::PlanetHopWindowMode::Fade) {
                    render.window_mode = 2;
                }
                render.overlay_mode = 0;
                if (value.overlayMode == config::PlanetHopOverlayMode::Hud) {
                    render.overlay_mode = 1;
                } else if (value.overlayMode == config::PlanetHopOverlayMode::Fade) {
                    render.overlay_mode = 2;
                }
                auto &scene = result.scene;
                scene.planet_layout = planet_hop::Layout::Focused;
                if (value.planetLayout == config::PlanetHopPlanetLayout::SolarSystem) {
                    scene.planet_layout = planet_hop::Layout::SolarSystem;
                } else if (value.planetLayout == config::PlanetHopPlanetLayout::Monitor) {
                    scene.planet_layout = planet_hop::Layout::Monitor;
                } else if (value.planetLayout == config::PlanetHopPlanetLayout::All) {
                    scene.planet_layout = planet_hop::Layout::All;
                }
                scene.planet_size = value.planetSize;
                scene.solar_vertical_spread = value.solarVerticalSpread;
                render.solar_background_scale = static_cast<float>(value.solarBackgroundScale);
                scene.planet_separation = value.planetSeparation;
                scene.planet_coverage = value.planetCoverage;
                scene.elevation = value.elevation;
                scene.rotation_enabled = value.rotationEnabled;
                scene.rotation_limit = value.rotationLimit;
                return result;
            });
            if (CHANGED) {
                resources.journey.reset();
            }
            // Cached successful/invalid paths are cheap; transient CPU allocation
            // failure must get another attempt even when settings are unchanged.
            (void)resources.renderer.prepare_depth(settings.planetHop.depthSource);
        }
        static bool draw(Resources &resources, Render::CRenderContext &ctx, const WorkspaceFrame &frame,
            double progress, Direction direction, bool matching) {
            const auto &prepared = resources.settings.prepared;
            if (!frame.presentation) {
                return false;
            }
            const auto SIZE = frame.presentation->m_size;
            if (SIZE.y <= 0) {
                return false;
            }
            const double ASPECT = SIZE.x / SIZE.y;
            if (!resources.journey || resources.journey->from != frame.source.endpoint ||
                resources.journey->to != frame.destination.endpoint ||
                resources.journey->aspect != ASPECT) {
                resources.journey = planet_hop::make_journey(
                    frame.source.endpoint, frame.destination.endpoint, prepared.scene, ASPECT);
            }
            return resources.renderer.draw(ctx, frame, *resources.journey, progress, direction, matching, prepared.render);
        }
        static double duration(const Resources &resources,
            const JourneyEndpoint & /*from*/, const JourneyEndpoint & /*destination*/) {
            return resources.settings.prepared.duration;
        }
        static WorkspaceTravel travel(const Resources &resources) {
            if (resources.settings.prepared.scene.planet_layout ==
                planet_hop::Layout::SolarSystem) {
                return +[](double position, bool destination) {
                    return planet_hop::remaining_travel(position, destination, true);
                };
            }
            return +[](double position, bool destination) {
                return planet_hop::remaining_travel(position, destination, false);
            };
        }
        // A missed refresh samples the same wall-clock journey, not a reason
        // to hand a long flight back to native 250 ms into its duration.
        static double max_frame_gap(const Resources &resources) { return resources.settings.prepared.duration; }
        static double live_boundary(const Resources &resources) { return resources.settings.prepared.live_boundary; }
        static bool uses_native_hud(const Resources &resources) { return resources.settings.prepared.render.overlay_mode == 1; }
        static bool uses_workspace_previews(const Resources &resources) {
            return resources.settings.prepared.scene.planet_layout !=
                   planet_hop::Layout::Focused;
        }
        static bool includes_workspace(const Resources &resources,
            const JourneyEndpoint &candidate, const JourneyEndpoint &source,
            const JourneyEndpoint &destination) {
            return planet_hop::scene_member(candidate, source, destination,
                resources.settings.prepared.scene.planet_layout);
        }
        static bool needs_individual_windows(const Resources &resources) {
            return resources.settings.prepared.render.window_mode == 1;
        }
    };
}
