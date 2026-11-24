#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/render/Context.hpp>

#include "config_settings_generated.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/WorkspaceSpring.hpp"
#include "workspace/lava/LavaRenderer.hpp"

namespace hyprimate::workspace {
    struct LavaStyle {
        static constexpr config::WorkspaceStyle KIND = config::WorkspaceStyle::Lava;
        static constexpr std::string_view NAME = "lava";
        static constexpr bool USES_WALLPAPER = false;  // Unified mode uses complete scene RGB.
        static constexpr bool LAYERED = false;
        static constexpr bool USES_NATIVE_DIRECTION = true;

        struct Timing {
            double duration = 0;
            std::string curve;
        };
        struct Settings {
            // The turn compares this value on reload: inactive variants don't disturb a draw.
            std::optional<lava::Material> last;
            Timing prepared;
        };
        struct NativeCurves {
            SP<Hyprutils::Animation::SSpringCurve> spring;
            SP<Hyprutils::Animation::CBezierCurve> bezier;
            double spring_duration = 0;
            Transition::Curve response;
            [[nodiscard]] const Transition::Curve &resolve(
                const std::string & /*name*/, double /*seconds*/) const {
                return response;
            }
        };
        struct Resources {
            lava::Renderer renderer;
            Settings settings;
            NativeCurves curves;
            bool ready = false;
        };

        static double duration(const Resources &resources,
            const JourneyEndpoint & /*from*/,
            const JourneyEndpoint & /*to*/) {
            return resources.settings.prepared.duration;
        }
        static constexpr WorkspaceTravel travel(const Resources & /*resources*/) {
            return nullptr;
        }
        static constexpr double live_boundary(const Resources & /*resources*/) {
            return 1.0;
        }
        static bool needs_window_planes(const Resources &resources) {
            return resources.settings.last && resources.settings.last->burn_up;
        }
        static constexpr bool needs_individual_windows(const Resources & /*resources*/) {
            return false;
        }
        static constexpr bool uses_native_hud(const Resources & /*resources*/) {
            return false;
        }
        static constexpr bool uses_workspace_previews(const Resources & /*resources*/) {
            return false;
        }
        static bool draw(Resources &resources,
            Render::CRenderContext & /*context*/,
            const WorkspaceFrame &frame,
            double progress,
            Direction direction,
            bool /*matching*/) {
            return resources.settings.last &&
                   resources.renderer.draw(frame, progress, direction, *resources.settings.last);
        }

        static const auto &options(const config::SWorkspaceSettings &settings) {
            return settings.lava;
        }
        static void prepare(Resources &resources, const config::SWorkspaceSettings &settings) {
            const auto &options = settings.lava;
            const auto PACK = [](double value) { return static_cast<float>(value); };
            lava::Material material{.variant = static_cast<int>(options.variant),
                .sharedA = {PACK(options.viscosity),
                    PACK(options.coolingRate),
                    PACK(options.crustAmount),
                    PACK(options.glowIntensity)},
                .sharedB = {PACK(options.detailPreservation),
                    PACK(options.trailSmear),
                    PACK(options.edgeDrip),
                    PACK(options.ashAmount)},
                .specialA = {},
                .specialB = {},
                .burn_up = options.windowBehavior == config::LavaWindowBehavior::BurnUp};
            switch (options.variant) {
                case config::LavaVariant::CoolingFlow: {
                    const auto &flow = options.coolingFlow;
                    material.specialA = {PACK(flow.leadingBulge),
                        PACK(flow.trailingStretch),
                        PACK(flow.moltenTurbulence),
                        0};
                    break;
                }
                case config::LavaVariant::BasaltFlow: {
                    const auto &flow = options.basaltFlow;
                    material.specialA = {PACK(flow.crustThickness),
                        PACK(flow.fissureWidth),
                        PACK(flow.emberVisibility),
                        PACK(flow.massDensity)};
                    break;
                }
                case config::LavaVariant::Pahoehoe: {
                    const auto &flow = options.pahoehoe;
                    material.specialA = {PACK(flow.ropeStrength),
                        PACK(flow.foldScale),
                        PACK(flow.rippleSpeed),
                        PACK(flow.surfaceRoll)};
                    break;
                }
                case config::LavaVariant::CrustPlates: {
                    const auto &flow = options.crustPlates;
                    material.specialA = {PACK(flow.plateSize),
                        PACK(flow.plateRigidity),
                        PACK(flow.seamGlow),
                        PACK(flow.plateDrift)};
                    material.specialB = {PACK(flow.plateCompression), PACK(flow.submeltVisibility)};
                    break;
                }
            }
            resources.settings.last = material;
            refresh_timing(resources);
        }

        static bool refresh_timing(Resources &resources) {
            static const auto ENABLED = CConfigValue<Config::INTEGER>("animations:enabled");
            auto &timing = resources.settings.prepared;
            const auto &TREE = Config::animationTree();
            const auto NODE = TREE ? TREE->getAnimationPropertyConfig("workspacesIn") : nullptr;
            // Capture curve identity too: hl.curve can replace a curve under the same name.
            const auto VALUES = NODE ? NODE->pValues.lock() : nullptr;
            const std::string &name = VALUES ? VALUES->internalBezier : timing.curve;
            constexpr std::string_view PREFIX = "spring:";
            auto &curves = resources.curves;
            const bool IS_SPRING = name.starts_with(PREFIX);
            const auto SPRING = IS_SPRING && Animation::mgr()
                                    ? Animation::mgr()->getSpring(name.substr(PREFIX.size()))
                                    : nullptr;
            const auto BEZIER =
                !IS_SPRING && Animation::mgr() ? Animation::mgr()->getBezier(name) : nullptr;
            const bool REPLACED = curves.spring != SPRING || curves.bezier != BEZIER;
            if (curves.spring != SPRING) {
                curves.spring_duration = SPRING ? spring_travel_seconds(*SPRING) : 0;
                curves.response =
                    SPRING ? spring_curve(*SPRING, curves.spring_duration) : Transition::Curve{};
            }
            curves.spring = SPRING;
            curves.bezier = BEZIER;
            double seconds = 0;
            if (*ENABLED != 0 && VALUES && VALUES->internalEnabled > 0) {
                seconds = SPRING ? curves.spring_duration : VALUES->internalSpeed * .1;
            }
            const bool CHANGED = timing.duration != seconds || timing.curve != name || REPLACED;
            timing.duration = seconds;
            timing.curve = name;
            return CHANGED;
        }
    };
}  // namespace hyprimate::workspace
