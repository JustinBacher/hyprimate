// Generated from config/settings.json; do not edit.
#pragma once

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <stdexcept>

#include "config_settings_generated.hpp"

namespace hyprimate::config {

namespace detail {
// Non-finite values read as the schema minimum, which turns a strength or drain off.
inline double clamp_float(float value, double low, double high) noexcept {
    return std::isfinite(value) ? std::clamp(static_cast<double>(value), low, high) : low;
}

inline double clamp_float(float value, double low) noexcept {
    return std::isfinite(value) ? std::max(static_cast<double>(value), low) : low;
}

inline int clamp_int(long long value, long long low, long long high) noexcept {
    return static_cast<int>(std::clamp(value, low, high));
}

inline Shape parse_shape(const std::string& text) noexcept {
    if (text == "pillow") {
        return Shape::Pillow;
    }
    if (text == "ellipse") {
        return Shape::Ellipse;
    }
    if (text == "circle") {
        return Shape::Circle;
    }
    return Shape::Pillow;
}

inline DefaultAnimationEdge parse_default_animation_edge(const std::string& text) noexcept {
    if (text == "bottom") {
        return DefaultAnimationEdge::Bottom;
    }
    if (text == "top") {
        return DefaultAnimationEdge::Top;
    }
    if (text == "left") {
        return DefaultAnimationEdge::Left;
    }
    if (text == "right") {
        return DefaultAnimationEdge::Right;
    }
    return DefaultAnimationEdge::Bottom;
}

inline EdgeBounds parse_edge_bounds(const std::string& text) noexcept {
    if (text == "screen") {
        return EdgeBounds::Screen;
    }
    if (text == "work_area") {
        return EdgeBounds::WorkArea;
    }
    return EdgeBounds::Screen;
}

inline CrumpleOrigin parse_crumple_origin(const std::string& text) noexcept {
    if (text == "center") {
        return CrumpleOrigin::Center;
    }
    if (text == "random") {
        return CrumpleOrigin::Random;
    }
    if (text == "cursor") {
        return CrumpleOrigin::Cursor;
    }
    if (text == "left") {
        return CrumpleOrigin::Left;
    }
    if (text == "right") {
        return CrumpleOrigin::Right;
    }
    if (text == "top") {
        return CrumpleOrigin::Top;
    }
    if (text == "bottom") {
        return CrumpleOrigin::Bottom;
    }
    return CrumpleOrigin::Center;
}

inline ForceDirection parse_force_direction(const std::string& text) noexcept {
    if (text == "inward") {
        return ForceDirection::Inward;
    }
    if (text == "directional") {
        return ForceDirection::Directional;
    }
    if (text == "rotational") {
        return ForceDirection::Rotational;
    }
    return ForceDirection::Inward;
}

inline SimulationQuality parse_simulation_quality(const std::string& text) noexcept {
    if (text == "adaptive") {
        return SimulationQuality::Adaptive;
    }
    if (text == "low") {
        return SimulationQuality::Low;
    }
    if (text == "medium") {
        return SimulationQuality::Medium;
    }
    if (text == "high") {
        return SimulationQuality::High;
    }
    return SimulationQuality::Adaptive;
}

inline std::optional<Flags<LoneWindowSides>> parse_lone_window_sides(const std::string& text) noexcept {
    Flags<LoneWindowSides> result;
    std::size_t start = 0;
    while (true) {
        const auto END = text.find(',', start);
        const auto FIRST = text.find_first_not_of(" \t", start);
        if (FIRST == std::string::npos || (END != std::string::npos && FIRST >= END)) {
            return std::nullopt;
        }
        const auto LAST = text.find_last_not_of(" \t", END == std::string::npos ? END : END - 1);
        const auto LENGTH = (LAST - FIRST) + 1;
        if (text.compare(FIRST, LENGTH, "all") == 0) {
            result.bits = 15;
        } else if (text.compare(FIRST, LENGTH, "top") == 0) {
            result.bits |= 1U;
        } else if (text.compare(FIRST, LENGTH, "bottom") == 0) {
            result.bits |= 2U;
        } else if (text.compare(FIRST, LENGTH, "left") == 0) {
            result.bits |= 4U;
        } else if (text.compare(FIRST, LENGTH, "right") == 0) {
            result.bits |= 8U;
        } else {
            return std::nullopt;
        }
        if (END == std::string::npos) {
            break;
        }
        start = END + 1;
    }
    return result;
}

inline Direction parse_direction(const std::string& text) noexcept {
    if (text == "inside") {
        return Direction::Inside;
    }
    if (text == "top") {
        return Direction::Top;
    }
    if (text == "bottom") {
        return Direction::Bottom;
    }
    if (text == "left") {
        return Direction::Left;
    }
    if (text == "right") {
        return Direction::Right;
    }
    return Direction::Inside;
}

inline Side parse_side(const std::string& text) noexcept {
    if (text == "auto") {
        return Side::Auto;
    }
    if (text == "nearest") {
        return Side::Nearest;
    }
    if (text == "origin") {
        return Side::Origin;
    }
    if (text == "top") {
        return Side::Top;
    }
    if (text == "bottom") {
        return Side::Bottom;
    }
    if (text == "left") {
        return Side::Left;
    }
    if (text == "right") {
        return Side::Right;
    }
    return Side::Auto;
}

inline WorkspaceStyle parse_workspace_style(const std::string& text) noexcept {
    if (text == "rotation") {
        return WorkspaceStyle::Rotation;
    }
    if (text == "planet_hop") {
        return WorkspaceStyle::PlanetHop;
    }
    if (text == "cube") {
        return WorkspaceStyle::Cube;
    }
    if (text == "lava") {
        return WorkspaceStyle::Lava;
    }
    return WorkspaceStyle::Rotation;
}

inline RotationGapFillStyle parse_rotation_gap_fill_style(const std::string& text) noexcept {
    if (text == "morph") {
        return RotationGapFillStyle::Morph;
    }
    if (text == "background") {
        return RotationGapFillStyle::Background;
    }
    if (text == "wallpaper") {
        return RotationGapFillStyle::Wallpaper;
    }
    if (text == "still") {
        return RotationGapFillStyle::Still;
    }
    return RotationGapFillStyle::Still;
}

inline PlanetHopWindowMode parse_planet_hop_window_mode(const std::string& text) noexcept {
    if (text == "surface") {
        return PlanetHopWindowMode::Surface;
    }
    if (text == "floating") {
        return PlanetHopWindowMode::Floating;
    }
    if (text == "fade") {
        return PlanetHopWindowMode::Fade;
    }
    return PlanetHopWindowMode::Surface;
}

inline PlanetHopOverlayMode parse_planet_hop_overlay_mode(const std::string& text) noexcept {
    if (text == "world") {
        return PlanetHopOverlayMode::World;
    }
    if (text == "hud") {
        return PlanetHopOverlayMode::Hud;
    }
    if (text == "fade") {
        return PlanetHopOverlayMode::Fade;
    }
    return PlanetHopOverlayMode::Fade;
}

inline PlanetHopPlanetLayout parse_planet_hop_planet_layout(const std::string& text) noexcept {
    if (text == "focused") {
        return PlanetHopPlanetLayout::Focused;
    }
    if (text == "monitor") {
        return PlanetHopPlanetLayout::Monitor;
    }
    if (text == "all") {
        return PlanetHopPlanetLayout::All;
    }
    if (text == "solar_system") {
        return PlanetHopPlanetLayout::SolarSystem;
    }
    return PlanetHopPlanetLayout::Focused;
}

inline CubeVariant parse_cube_variant(const std::string& text) noexcept {
    if (text == "solid") {
        return CubeVariant::Solid;
    }
    if (text == "inside_out") {
        return CubeVariant::InsideOut;
    }
    return CubeVariant::Solid;
}

inline LavaVariant parse_lava_variant(const std::string& text) noexcept {
    if (text == "cooling_flow") {
        return LavaVariant::CoolingFlow;
    }
    if (text == "basalt_flow") {
        return LavaVariant::BasaltFlow;
    }
    if (text == "pahoehoe") {
        return LavaVariant::Pahoehoe;
    }
    if (text == "crust_plates") {
        return LavaVariant::CrustPlates;
    }
    return LavaVariant::CrustPlates;
}

inline LavaWindowBehavior parse_lava_window_behavior(const std::string& text) noexcept {
    if (text == "melt_with_workspace") {
        return LavaWindowBehavior::MeltWithWorkspace;
    }
    if (text == "burn_up") {
        return LavaWindowBehavior::BurnUp;
    }
    return LavaWindowBehavior::MeltWithWorkspace;
}

inline Style parse_style(const std::string& text) noexcept {
    if (text == "liquid") {
        return Style::Liquid;
    }
    if (text == "paper") {
        return Style::Paper;
    }
    if (text == "cloth") {
        return Style::Cloth;
    }
    if (text == "swarm") {
        return Style::Swarm;
    }
    if (text == "bubble") {
        return Style::Bubble;
    }
    if (text == "smoke") {
        return Style::Smoke;
    }
    if (text == "burn") {
        return Style::Burn;
    }
    if (text == "construction") {
        return Style::Construction;
    }
    return Style::Liquid;
}

inline ClosingStyle parse_closing_style(const std::string& text) noexcept {
    if (text == "consume") {
        return ClosingStyle::Consume;
    }
    if (text == "curl") {
        return ClosingStyle::Curl;
    }
    return ClosingStyle::Curl;
}

inline DebrisDirection parse_debris_direction(const std::string& text) noexcept {
    if (text == "natural") {
        return DebrisDirection::Natural;
    }
    if (text == "fall") {
        return DebrisDirection::Fall;
    }
    if (text == "rise") {
        return DebrisDirection::Rise;
    }
    if (text == "wind") {
        return DebrisDirection::Wind;
    }
    return DebrisDirection::Natural;
}

inline PaperMoveStyle parse_paper_move_style(const std::string& text) noexcept {
    if (text == "flutter") {
        return PaperMoveStyle::Flutter;
    }
    if (text == "glide") {
        return PaperMoveStyle::Glide;
    }
    return PaperMoveStyle::Flutter;
}

inline Variant parse_variant(const std::string& text) noexcept {
    if (text == "native") {
        return Variant::Native;
    }
    if (text == "crew_carry") {
        return Variant::CrewCarry;
    }
    if (text == "crane") {
        return Variant::Crane;
    }
    if (text == "piece_by_piece") {
        return Variant::PieceByPiece;
    }
    return Variant::CrewCarry;
}

} // namespace detail

struct SConfigValues {
    SP<Config::Values::CFloatValue> m_globalConstructionDuration;
    CConfigValue<Config::FLOAT> m_globalConstructionDurationSet;
    SP<Config::Values::CFloatValue> m_globalBurnDuration;
    CConfigValue<Config::FLOAT> m_globalBurnDurationSet;
    SP<Config::Values::CFloatValue> m_globalLiquidStrength;
    CConfigValue<Config::FLOAT> m_globalLiquidStrengthSet;
    SP<Config::Values::CFloatValue> m_globalLiquidViscosity;
    CConfigValue<Config::FLOAT> m_globalLiquidViscositySet;
    SP<Config::Values::CFloatValue> m_globalLiquidElasticity;
    CConfigValue<Config::FLOAT> m_globalLiquidElasticitySet;
    SP<Config::Values::CFloatValue> m_globalLiquidMomentum;
    CConfigValue<Config::FLOAT> m_globalLiquidMomentumSet;
    SP<Config::Values::CFloatValue> m_globalLiquidVariation;
    CConfigValue<Config::FLOAT> m_globalLiquidVariationSet;
    SP<Config::Values::CFloatValue> m_globalClothStrength;
    CConfigValue<Config::FLOAT> m_globalClothStrengthSet;
    SP<Config::Values::CFloatValue> m_globalClothSpringiness;
    CConfigValue<Config::FLOAT> m_globalClothSpringinessSet;
    SP<Config::Values::CFloatValue> m_globalClothGravity;
    CConfigValue<Config::FLOAT> m_globalClothGravitySet;
    SP<Config::Values::CFloatValue> m_globalClothFlutter;
    CConfigValue<Config::FLOAT> m_globalClothFlutterSet;
    SP<Config::Values::CFloatValue> m_globalClothShading;
    CConfigValue<Config::FLOAT> m_globalClothShadingSet;
    SP<Config::Values::CFloatValue> m_globalClothShine;
    CConfigValue<Config::FLOAT> m_globalClothShineSet;
    SP<Config::Values::CFloatValue> m_globalClothVariation;
    CConfigValue<Config::FLOAT> m_globalClothVariationSet;
    SP<Config::Values::CFloatValue> m_globalBubbleStrength;
    CConfigValue<Config::FLOAT> m_globalBubbleStrengthSet;
    SP<Config::Values::CFloatValue> m_globalBubbleMicrobubbles;
    CConfigValue<Config::FLOAT> m_globalBubbleMicrobubblesSet;
    SP<Config::Values::CFloatValue> m_globalBubbleTrail;
    CConfigValue<Config::FLOAT> m_globalBubbleTrailSet;
    SP<Config::Values::CStringValue> m_globalBubbleShape;
    CConfigValue<Config::STRING> m_globalBubbleShapeSet;
    SP<Config::Values::CFloatValue> m_globalBubbleRoundness;
    CConfigValue<Config::FLOAT> m_globalBubbleRoundnessSet;
    SP<Config::Values::CFloatValue> m_globalBubbleShapeSpeed;
    CConfigValue<Config::FLOAT> m_globalBubbleShapeSpeedSet;
    SP<Config::Values::CFloatValue> m_globalBubbleTransparency;
    CConfigValue<Config::FLOAT> m_globalBubbleTransparencySet;
    SP<Config::Values::CFloatValue> m_globalSmokeStrength;
    CConfigValue<Config::FLOAT> m_globalSmokeStrengthSet;
    SP<Config::Values::CFloatValue> m_globalSmokeTurbulence;
    CConfigValue<Config::FLOAT> m_globalSmokeTurbulenceSet;
    SP<Config::Values::CFloatValue> m_globalSmokeTrail;
    CConfigValue<Config::FLOAT> m_globalSmokeTrailSet;
    SP<Config::Values::CFloatValue> m_globalLiquidDuration;
    CConfigValue<Config::FLOAT> m_globalLiquidDurationSet;
    SP<Config::Values::CColorValue> m_globalLiquidColor;
    CConfigValue<Config::INTEGER> m_globalLiquidColorSet;
    SP<Config::Values::CStringValue> m_globalLiquidDefaultAnimationEdge;
    CConfigValue<Config::STRING> m_globalLiquidDefaultAnimationEdgeSet;
    SP<Config::Values::CStringValue> m_globalLiquidEdgeBounds;
    CConfigValue<Config::STRING> m_globalLiquidEdgeBoundsSet;
    SP<Config::Values::CFloatValue> m_globalLiquidEmergeDrag;
    CConfigValue<Config::FLOAT> m_globalLiquidEmergeDragSet;
    SP<Config::Values::CFloatValue> m_globalPaperDuration;
    CConfigValue<Config::FLOAT> m_globalPaperDurationSet;
    SP<Config::Values::CFloatValue> m_globalPaperCrumpliness;
    CConfigValue<Config::FLOAT> m_globalPaperCrumplinessSet;
    SP<Config::Values::CFloatValue> m_globalPaperShading;
    CConfigValue<Config::FLOAT> m_globalPaperShadingSet;
    SP<Config::Values::CFloatValue> m_globalPaperVariation;
    CConfigValue<Config::FLOAT> m_globalPaperVariationSet;
    SP<Config::Values::CFloatValue> m_globalPaperFoldDensity;
    CConfigValue<Config::FLOAT> m_globalPaperFoldDensitySet;
    SP<Config::Values::CFloatValue> m_globalPaperPaperStiffness;
    CConfigValue<Config::FLOAT> m_globalPaperPaperStiffnessSet;
    SP<Config::Values::CFloatValue> m_globalPaperCrumpleSize;
    CConfigValue<Config::FLOAT> m_globalPaperCrumpleSizeSet;
    SP<Config::Values::CStringValue> m_globalPaperCrumpleOrigin;
    CConfigValue<Config::STRING> m_globalPaperCrumpleOriginSet;
    SP<Config::Values::CStringValue> m_globalPaperForceDirection;
    CConfigValue<Config::STRING> m_globalPaperForceDirectionSet;
    SP<Config::Values::CFloatValue> m_globalPaperRotationIntensity;
    CConfigValue<Config::FLOAT> m_globalPaperRotationIntensitySet;
    SP<Config::Values::CFloatValue> m_globalPaperSettling;
    CConfigValue<Config::FLOAT> m_globalPaperSettlingSet;
    SP<Config::Values::CStringValue> m_globalPaperSimulationQuality;
    CConfigValue<Config::STRING> m_globalPaperSimulationQualitySet;
    SP<Config::Values::CFloatValue> m_globalClothDuration;
    CConfigValue<Config::FLOAT> m_globalClothDurationSet;
    SP<Config::Values::CStringValue> m_globalClothDefaultAnimationEdge;
    CConfigValue<Config::STRING> m_globalClothDefaultAnimationEdgeSet;
    SP<Config::Values::CStringValue> m_globalClothEdgeBounds;
    CConfigValue<Config::STRING> m_globalClothEdgeBoundsSet;
    SP<Config::Values::CFloatValue> m_globalSwarmDuration;
    CConfigValue<Config::FLOAT> m_globalSwarmDurationSet;
    SP<Config::Values::CStringValue> m_globalSwarmLoneWindowSides;
    CConfigValue<Config::STRING> m_globalSwarmLoneWindowSidesSet;
    SP<Config::Values::CFloatValue> m_globalBubbleDuration;
    CConfigValue<Config::FLOAT> m_globalBubbleDurationSet;
    SP<Config::Values::CFloatValue> m_globalSmokeDuration;
    CConfigValue<Config::FLOAT> m_globalSmokeDurationSet;
    SP<Config::Values::CStringValue> m_globalSmokeDirection;
    CConfigValue<Config::STRING> m_globalSmokeDirectionSet;
    SP<Config::Values::CStringValue> m_globalLiquidSide;
    CConfigValue<Config::STRING> m_globalLiquidSideSet;
    SP<Config::Values::CBoolValue> m_globalLiquidSplash;
    CConfigValue<Config::BOOL> m_globalLiquidSplashSet;
    SP<Config::Values::CStringValue> m_globalClothSide;
    CConfigValue<Config::STRING> m_globalClothSideSet;
    SP<Config::Values::CBoolValue> m_workspaceEnabled;
    SP<Config::Values::CStringValue> m_workspaceStyle;
    SP<Config::Values::CStringValue> m_workspaceEffect;
    SP<Config::Values::CStringValue> m_workspaceEffectParameters;
    SP<Config::Values::CFloatValue> m_workspaceEffectDuration;
    SP<Config::Values::CFloatValue> m_workspaceRotationDuration;
    SP<Config::Values::CStringValue> m_workspaceRotationCurve;
    SP<Config::Values::CStringValue> m_workspaceRotationGapFillStyle;
    SP<Config::Values::CColorValue> m_workspaceRotationGapFillColor;
    SP<Config::Values::CFloatValue> m_workspaceRotationGapFillBlur;
    SP<Config::Values::CBoolValue> m_workspaceRotationKeepOverlaysStatic;
    SP<Config::Values::CStringValue> m_workspacePlanetHopWindowMode;
    SP<Config::Values::CStringValue> m_workspacePlanetHopOverlayMode;
    SP<Config::Values::CStringValue> m_workspacePlanetHopPlanetLayout;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopDuration;
    SP<Config::Values::CStringValue> m_workspacePlanetHopCurve;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopLiveBoundary;
    SP<Config::Values::CIntValue> m_workspacePlanetHopSamplingQuality;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopPlanetSize;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopPlanetSeparation;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopSolarVerticalSpread;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopSolarBackgroundScale;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopPlanetCoverage;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopIndirectBrightness;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopElevation;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopFade;
    SP<Config::Values::CBoolValue> m_workspacePlanetHopRotationEnabled;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopRotationLimit;
    SP<Config::Values::CBoolValue> m_workspacePlanetHopStarsEnabled;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopStarDensity;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopStarParallax;
    SP<Config::Values::CBoolValue> m_workspacePlanetHopAtmosphereEnabled;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopAtmosphereIntensity;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopAtmosphereThickness;
    SP<Config::Values::CBoolValue> m_workspacePlanetHopLightingEnabled;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopLightDirectionX;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopLightDirectionY;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopLightDirectionZ;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopNightBrightness;
    SP<Config::Values::CBoolValue> m_workspacePlanetHopCloudsEnabled;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopCloudCoverage;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopCloudOpacity;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopCloudSpeed;
    SP<Config::Values::CBoolValue> m_workspacePlanetHopDepthEnabled;
    SP<Config::Values::CFloatValue> m_workspacePlanetHopDepthStrength;
    SP<Config::Values::CStringValue> m_workspacePlanetHopDepthSource;
    SP<Config::Values::CStringValue> m_workspaceCubeVariant;
    SP<Config::Values::CFloatValue> m_workspaceCubeDuration;
    SP<Config::Values::CStringValue> m_workspaceCubeCurve;
    SP<Config::Values::CFloatValue> m_workspaceCubeSolidCubeTilt;
    SP<Config::Values::CFloatValue> m_workspaceCubeSolidZoom;
    SP<Config::Values::CFloatValue> m_workspaceCubeInsideOutCubeTilt;
    SP<Config::Values::CFloatValue> m_workspaceCubeInsideOutZoom;
    SP<Config::Values::CStringValue> m_workspaceLavaVariant;
    SP<Config::Values::CStringValue> m_workspaceLavaWindowBehavior;
    SP<Config::Values::CFloatValue> m_workspaceLavaViscosity;
    SP<Config::Values::CFloatValue> m_workspaceLavaCoolingRate;
    SP<Config::Values::CFloatValue> m_workspaceLavaCrustAmount;
    SP<Config::Values::CFloatValue> m_workspaceLavaGlowIntensity;
    SP<Config::Values::CFloatValue> m_workspaceLavaDetailPreservation;
    SP<Config::Values::CFloatValue> m_workspaceLavaTrailSmear;
    SP<Config::Values::CFloatValue> m_workspaceLavaEdgeDrip;
    SP<Config::Values::CFloatValue> m_workspaceLavaAshAmount;
    SP<Config::Values::CFloatValue> m_workspaceLavaCoolingFlowLeadingBulge;
    SP<Config::Values::CFloatValue> m_workspaceLavaCoolingFlowTrailingStretch;
    SP<Config::Values::CFloatValue> m_workspaceLavaCoolingFlowMoltenTurbulence;
    SP<Config::Values::CFloatValue> m_workspaceLavaBasaltFlowCrustThickness;
    SP<Config::Values::CFloatValue> m_workspaceLavaBasaltFlowFissureWidth;
    SP<Config::Values::CFloatValue> m_workspaceLavaBasaltFlowEmberVisibility;
    SP<Config::Values::CFloatValue> m_workspaceLavaBasaltFlowMassDensity;
    SP<Config::Values::CFloatValue> m_workspaceLavaPahoehoeRopeStrength;
    SP<Config::Values::CFloatValue> m_workspaceLavaPahoehoeFoldScale;
    SP<Config::Values::CFloatValue> m_workspaceLavaPahoehoeRippleSpeed;
    SP<Config::Values::CFloatValue> m_workspaceLavaPahoehoeSurfaceRoll;
    SP<Config::Values::CFloatValue> m_workspaceLavaCrustPlatesPlateSize;
    SP<Config::Values::CFloatValue> m_workspaceLavaCrustPlatesPlateRigidity;
    SP<Config::Values::CFloatValue> m_workspaceLavaCrustPlatesSeamGlow;
    SP<Config::Values::CFloatValue> m_workspaceLavaCrustPlatesPlateDrift;
    SP<Config::Values::CFloatValue> m_workspaceLavaCrustPlatesPlateCompression;
    SP<Config::Values::CFloatValue> m_workspaceLavaCrustPlatesSubmeltVisibility;
    SP<Config::Values::CStringValue> m_wallpaperEffect;
    SP<Config::Values::CStringValue> m_wallpaperEffectParameters;
    SP<Config::Values::CIntValue> m_wallpaperEffectDuration;
    SP<Config::Values::CStringValue> m_wallpaperSourceNamespace;
    SP<Config::Values::CBoolValue> m_enabled;
    SP<Config::Values::CBoolValue> m_duringWorkspaceSwitch;
    SP<Config::Values::CStringValue> m_style;
    SP<Config::Values::CStringValue> m_openEffect;
    SP<Config::Values::CStringValue> m_closeEffect;
    SP<Config::Values::CStringValue> m_openEffectParameters;
    SP<Config::Values::CStringValue> m_closeEffectParameters;
    SP<Config::Values::CFloatValue> m_openEffectDuration;
    SP<Config::Values::CFloatValue> m_closeEffectDuration;
    SP<Config::Values::CStringValue> m_moveEffect;
    SP<Config::Values::CStringValue> m_moveEffectParameters;
    SP<Config::Values::CFloatValue> m_moveEffectDuration;
    SP<Config::Values::CStringValue> m_resizeEffect;
    SP<Config::Values::CStringValue> m_resizeEffectParameters;
    SP<Config::Values::CFloatValue> m_resizeEffectDuration;
    SP<Config::Values::CFloatValue> m_openConstructionDuration;
    CConfigValue<Config::FLOAT> m_openConstructionDurationSet;
    SP<Config::Values::CFloatValue> m_closeConstructionDuration;
    CConfigValue<Config::FLOAT> m_closeConstructionDurationSet;
    SP<Config::Values::CFloatValue> m_constructionWorkerSize;
    SP<Config::Values::CFloatValue> m_burnVariation;
    SP<Config::Values::CFloatValue> m_burnFragmentDensity;
    SP<Config::Values::CFloatValue> m_burnEmberDensity;
    SP<Config::Values::CFloatValue> m_burnWindDirection;
    SP<Config::Values::CFloatValue> m_burnWindStrength;
    SP<Config::Values::CFloatValue> m_burnGravityStrength;
    SP<Config::Values::CFloatValue> m_burnScorchLifetime;
    SP<Config::Values::CFloatValue> m_burnHealingDuration;
    SP<Config::Values::CFloatValue> m_burnMovementSmokeDensity;
    SP<Config::Values::CFloatValue> m_openBurnDuration;
    CConfigValue<Config::FLOAT> m_openBurnDurationSet;
    SP<Config::Values::CFloatValue> m_closeBurnDuration;
    CConfigValue<Config::FLOAT> m_closeBurnDurationSet;
    SP<Config::Values::CStringValue> m_burnClosingStyle;
    SP<Config::Values::CStringValue> m_burnDebrisDirection;
    SP<Config::Values::CFloatValue> m_burnCurlIntensity;
    SP<Config::Values::CFloatValue> m_paperStiffness;
    CConfigValue<Config::FLOAT> m_paperStiffnessSet;
    SP<Config::Values::CFloatValue> m_paperInertia;
    SP<Config::Values::CFloatValue> m_paperSettleDamping;
    CConfigValue<Config::FLOAT> m_paperSettleDampingSet;
    SP<Config::Values::CStringValue> m_paperMoveStyle;
    SP<Config::Values::CFloatValue> m_paperFlutterAirResistance;
    SP<Config::Values::CFloatValue> m_paperFlutterIntensity;
    SP<Config::Values::CFloatValue> m_paperFlutterFrequency;
    SP<Config::Values::CFloatValue> m_paperFlutterLift;
    SP<Config::Values::CFloatValue> m_paperFlutterTorsion;
    SP<Config::Values::CFloatValue> m_paperGlideFriction;
    SP<Config::Values::CFloatValue> m_paperGlideCornerLift;
    SP<Config::Values::CFloatValue> m_paperGlideEdgeCurl;
    SP<Config::Values::CFloatValue> m_paperGlideShadowSeparation;
    SP<Config::Values::CFloatValue> m_paperResizeFoldIntensity;
    SP<Config::Values::CFloatValue> m_paperResizeCurlIntensity;
    SP<Config::Values::CFloatValue> m_paperResizeCreaseDensity;
    SP<Config::Values::CStringValue> m_swarmSprite;
    SP<Config::Values::CIntValue> m_swarmCount;
    SP<Config::Values::CFloatValue> m_swarmSpeed;
    SP<Config::Values::CFloatValue> m_swarmStagger;
    SP<Config::Values::CFloatValue> m_swarmCohesion;
    SP<Config::Values::CFloatValue> m_swarmSeparation;
    SP<Config::Values::CFloatValue> m_swarmAgility;
    SP<Config::Values::CFloatValue> m_swarmSettling;
    SP<Config::Values::CFloatValue> m_swarmWander;
    SP<Config::Values::CFloatValue> m_swarmVariation;
    SP<Config::Values::CColorValue> m_smokeColor;
    SP<Config::Values::CFloatValue> m_smokeDensity;
    SP<Config::Values::CFloatValue> m_smokeVariation;
    SP<Config::Values::CBoolValue> m_movementEnabled;
    SP<Config::Values::CBoolValue> m_workspaceMovement;
    SP<Config::Values::CStringValue> m_movementConstructionVariant;
    SP<Config::Values::CFloatValue> m_movementLiquidStrength;
    CConfigValue<Config::FLOAT> m_movementLiquidStrengthSet;
    SP<Config::Values::CFloatValue> m_viscosity;
    CConfigValue<Config::FLOAT> m_viscositySet;
    SP<Config::Values::CFloatValue> m_elasticity;
    CConfigValue<Config::FLOAT> m_elasticitySet;
    SP<Config::Values::CFloatValue> m_momentum;
    CConfigValue<Config::FLOAT> m_momentumSet;
    SP<Config::Values::CFloatValue> m_variation;
    CConfigValue<Config::FLOAT> m_variationSet;
    SP<Config::Values::CFloatValue> m_movementClothStrength;
    CConfigValue<Config::FLOAT> m_movementClothStrengthSet;
    SP<Config::Values::CFloatValue> m_movementClothSpringiness;
    CConfigValue<Config::FLOAT> m_movementClothSpringinessSet;
    SP<Config::Values::CFloatValue> m_movementClothGravity;
    CConfigValue<Config::FLOAT> m_movementClothGravitySet;
    SP<Config::Values::CFloatValue> m_movementClothFlutter;
    CConfigValue<Config::FLOAT> m_movementClothFlutterSet;
    SP<Config::Values::CFloatValue> m_movementClothShading;
    CConfigValue<Config::FLOAT> m_movementClothShadingSet;
    SP<Config::Values::CFloatValue> m_movementClothShine;
    CConfigValue<Config::FLOAT> m_movementClothShineSet;
    SP<Config::Values::CFloatValue> m_movementClothVariation;
    CConfigValue<Config::FLOAT> m_movementClothVariationSet;
    SP<Config::Values::CFloatValue> m_movementBubbleStrength;
    CConfigValue<Config::FLOAT> m_movementBubbleStrengthSet;
    SP<Config::Values::CFloatValue> m_movementBubbleMicrobubbles;
    CConfigValue<Config::FLOAT> m_movementBubbleMicrobubblesSet;
    SP<Config::Values::CFloatValue> m_movementBubbleTrail;
    CConfigValue<Config::FLOAT> m_movementBubbleTrailSet;
    SP<Config::Values::CStringValue> m_movementBubbleShape;
    CConfigValue<Config::STRING> m_movementBubbleShapeSet;
    SP<Config::Values::CFloatValue> m_movementBubbleRoundness;
    CConfigValue<Config::FLOAT> m_movementBubbleRoundnessSet;
    SP<Config::Values::CFloatValue> m_movementBubbleShapeSpeed;
    CConfigValue<Config::FLOAT> m_movementBubbleShapeSpeedSet;
    SP<Config::Values::CFloatValue> m_movementBubbleTransparency;
    CConfigValue<Config::FLOAT> m_movementBubbleTransparencySet;
    SP<Config::Values::CFloatValue> m_movementSmokeStrength;
    CConfigValue<Config::FLOAT> m_movementSmokeStrengthSet;
    SP<Config::Values::CFloatValue> m_movementSmokeTurbulence;
    CConfigValue<Config::FLOAT> m_movementSmokeTurbulenceSet;
    SP<Config::Values::CFloatValue> m_movementSmokeTrail;
    CConfigValue<Config::FLOAT> m_movementSmokeTrailSet;
    SP<Config::Values::CFloatValue> m_movementSmokeDragFollow;
    SP<Config::Values::CFloatValue> m_movementSmokeTrailLength;
    SP<Config::Values::CFloatValue> m_movementSmokeTrailLifetime;
    SP<Config::Values::CFloatValue> m_movementSmokeTrailThickness;
    SP<Config::Values::CFloatValue> m_movementSmokeReform;
    SP<Config::Values::CFloatValue> m_movementSmokeSettleSpeed;
    SP<Config::Values::CFloatValue> m_movementSmokeMass;
    SP<Config::Values::CFloatValue> m_movementSmokeRoundness;
    SP<Config::Values::CBoolValue> m_openEnabled;
    SP<Config::Values::CFloatValue> m_openLiquidDuration;
    CConfigValue<Config::FLOAT> m_openLiquidDurationSet;
    SP<Config::Values::CFloatValue> m_openLiquidStrength;
    CConfigValue<Config::FLOAT> m_openLiquidStrengthSet;
    SP<Config::Values::CColorValue> m_openLiquidColor;
    CConfigValue<Config::INTEGER> m_openLiquidColorSet;
    SP<Config::Values::CStringValue> m_openDefaultAnimationEdge;
    CConfigValue<Config::STRING> m_openDefaultAnimationEdgeSet;
    SP<Config::Values::CStringValue> m_edgeBounds;
    CConfigValue<Config::STRING> m_edgeBoundsSet;
    SP<Config::Values::CFloatValue> m_emergeDrag;
    CConfigValue<Config::FLOAT> m_emergeDragSet;
    SP<Config::Values::CFloatValue> m_openElasticity;
    CConfigValue<Config::FLOAT> m_openElasticitySet;
    SP<Config::Values::CFloatValue> m_openMomentum;
    CConfigValue<Config::FLOAT> m_openMomentumSet;
    SP<Config::Values::CFloatValue> m_openPaperDuration;
    CConfigValue<Config::FLOAT> m_openPaperDurationSet;
    SP<Config::Values::CFloatValue> m_openPaperCrumpliness;
    CConfigValue<Config::FLOAT> m_openPaperCrumplinessSet;
    SP<Config::Values::CFloatValue> m_openPaperShading;
    CConfigValue<Config::FLOAT> m_openPaperShadingSet;
    SP<Config::Values::CFloatValue> m_openPaperVariation;
    CConfigValue<Config::FLOAT> m_openPaperVariationSet;
    SP<Config::Values::CFloatValue> m_openPaperFoldDensity;
    CConfigValue<Config::FLOAT> m_openPaperFoldDensitySet;
    SP<Config::Values::CFloatValue> m_openPaperStiffness;
    CConfigValue<Config::FLOAT> m_openPaperStiffnessSet;
    SP<Config::Values::CFloatValue> m_openPaperCrumpleSize;
    CConfigValue<Config::FLOAT> m_openPaperCrumpleSizeSet;
    SP<Config::Values::CStringValue> m_openPaperCrumpleOrigin;
    CConfigValue<Config::STRING> m_openPaperCrumpleOriginSet;
    SP<Config::Values::CStringValue> m_openPaperForceDirection;
    CConfigValue<Config::STRING> m_openPaperForceDirectionSet;
    SP<Config::Values::CFloatValue> m_openPaperRotationIntensity;
    CConfigValue<Config::FLOAT> m_openPaperRotationIntensitySet;
    SP<Config::Values::CFloatValue> m_openPaperUnfoldTension;
    SP<Config::Values::CFloatValue> m_openPaperSettling;
    CConfigValue<Config::FLOAT> m_openPaperSettlingSet;
    SP<Config::Values::CFloatValue> m_openPaperCreasePersistence;
    SP<Config::Values::CStringValue> m_openPaperSimulationQuality;
    CConfigValue<Config::STRING> m_openPaperSimulationQualitySet;
    SP<Config::Values::CFloatValue> m_openClothDuration;
    CConfigValue<Config::FLOAT> m_openClothDurationSet;
    SP<Config::Values::CStringValue> m_openClothDefaultAnimationEdge;
    CConfigValue<Config::STRING> m_openClothDefaultAnimationEdgeSet;
    SP<Config::Values::CStringValue> m_openClothEdgeBounds;
    CConfigValue<Config::STRING> m_openClothEdgeBoundsSet;
    SP<Config::Values::CFloatValue> m_openClothFlutter;
    CConfigValue<Config::FLOAT> m_openClothFlutterSet;
    SP<Config::Values::CFloatValue> m_openClothShading;
    CConfigValue<Config::FLOAT> m_openClothShadingSet;
    SP<Config::Values::CFloatValue> m_openClothShine;
    CConfigValue<Config::FLOAT> m_openClothShineSet;
    SP<Config::Values::CFloatValue> m_openClothVariation;
    CConfigValue<Config::FLOAT> m_openClothVariationSet;
    SP<Config::Values::CFloatValue> m_openSwarmDuration;
    CConfigValue<Config::FLOAT> m_openSwarmDurationSet;
    SP<Config::Values::CStringValue> m_openSwarmLoneWindowSides;
    CConfigValue<Config::STRING> m_openSwarmLoneWindowSidesSet;
    SP<Config::Values::CFloatValue> m_openBubbleDuration;
    CConfigValue<Config::FLOAT> m_openBubbleDurationSet;
    SP<Config::Values::CIntValue> m_openBubbleCount;
    SP<Config::Values::CFloatValue> m_openBubbleBirthRate;
    SP<Config::Values::CFloatValue> m_openBubbleSizeVariation;
    SP<Config::Values::CFloatValue> m_openBubbleGrowthVariation;
    SP<Config::Values::CFloatValue> m_openBubbleInteraction;
    SP<Config::Values::CFloatValue> m_openBubbleDamping;
    SP<Config::Values::CFloatValue> m_openBubbleBuoyancy;
    SP<Config::Values::CFloatValue> m_openBubbleLinger;
    SP<Config::Values::CFloatValue> m_openSmokeDuration;
    CConfigValue<Config::FLOAT> m_openSmokeDurationSet;
    SP<Config::Values::CFloatValue> m_openSmokeTurbulence;
    CConfigValue<Config::FLOAT> m_openSmokeTurbulenceSet;
    SP<Config::Values::CStringValue> m_openSmokeDirection;
    CConfigValue<Config::STRING> m_openSmokeDirectionSet;
    SP<Config::Values::CBoolValue> m_closeEnabled;
    SP<Config::Values::CFloatValue> m_closeLiquidDuration;
    CConfigValue<Config::FLOAT> m_closeLiquidDurationSet;
    SP<Config::Values::CFloatValue> m_closeLiquidStrength;
    CConfigValue<Config::FLOAT> m_closeLiquidStrengthSet;
    SP<Config::Values::CColorValue> m_closeLiquidColor;
    CConfigValue<Config::INTEGER> m_closeLiquidColorSet;
    SP<Config::Values::CStringValue> m_closeDefaultAnimationEdge;
    CConfigValue<Config::STRING> m_closeDefaultAnimationEdgeSet;
    SP<Config::Values::CStringValue> m_closeSide;
    CConfigValue<Config::STRING> m_closeSideSet;
    SP<Config::Values::CStringValue> m_closeEdgeBounds;
    CConfigValue<Config::STRING> m_closeEdgeBoundsSet;
    SP<Config::Values::CBoolValue> m_closeSplash;
    CConfigValue<Config::BOOL> m_closeSplashSet;
    SP<Config::Values::CFloatValue> m_closeElasticity;
    CConfigValue<Config::FLOAT> m_closeElasticitySet;
    SP<Config::Values::CFloatValue> m_closeMomentum;
    CConfigValue<Config::FLOAT> m_closeMomentumSet;
    SP<Config::Values::CFloatValue> m_closePaperDuration;
    CConfigValue<Config::FLOAT> m_closePaperDurationSet;
    SP<Config::Values::CFloatValue> m_closePaperCrumpliness;
    CConfigValue<Config::FLOAT> m_closePaperCrumplinessSet;
    SP<Config::Values::CFloatValue> m_closePaperShading;
    CConfigValue<Config::FLOAT> m_closePaperShadingSet;
    SP<Config::Values::CFloatValue> m_closePaperVariation;
    CConfigValue<Config::FLOAT> m_closePaperVariationSet;
    SP<Config::Values::CFloatValue> m_closePaperFoldDensity;
    CConfigValue<Config::FLOAT> m_closePaperFoldDensitySet;
    SP<Config::Values::CFloatValue> m_closePaperStiffness;
    CConfigValue<Config::FLOAT> m_closePaperStiffnessSet;
    SP<Config::Values::CFloatValue> m_closePaperCrumpleSize;
    CConfigValue<Config::FLOAT> m_closePaperCrumpleSizeSet;
    SP<Config::Values::CStringValue> m_closePaperCrumpleOrigin;
    CConfigValue<Config::STRING> m_closePaperCrumpleOriginSet;
    SP<Config::Values::CStringValue> m_closePaperForceDirection;
    CConfigValue<Config::STRING> m_closePaperForceDirectionSet;
    SP<Config::Values::CFloatValue> m_closePaperRotationIntensity;
    CConfigValue<Config::FLOAT> m_closePaperRotationIntensitySet;
    SP<Config::Values::CFloatValue> m_closePaperSettling;
    CConfigValue<Config::FLOAT> m_closePaperSettlingSet;
    SP<Config::Values::CStringValue> m_closePaperSimulationQuality;
    CConfigValue<Config::STRING> m_closePaperSimulationQualitySet;
    SP<Config::Values::CFloatValue> m_closeClothDuration;
    CConfigValue<Config::FLOAT> m_closeClothDurationSet;
    SP<Config::Values::CStringValue> m_closeClothDefaultAnimationEdge;
    CConfigValue<Config::STRING> m_closeClothDefaultAnimationEdgeSet;
    SP<Config::Values::CStringValue> m_closeClothSide;
    CConfigValue<Config::STRING> m_closeClothSideSet;
    SP<Config::Values::CStringValue> m_closeClothEdgeBounds;
    CConfigValue<Config::STRING> m_closeClothEdgeBoundsSet;
    SP<Config::Values::CFloatValue> m_closeClothFlutter;
    CConfigValue<Config::FLOAT> m_closeClothFlutterSet;
    SP<Config::Values::CFloatValue> m_closeClothShading;
    CConfigValue<Config::FLOAT> m_closeClothShadingSet;
    SP<Config::Values::CFloatValue> m_closeClothShine;
    CConfigValue<Config::FLOAT> m_closeClothShineSet;
    SP<Config::Values::CFloatValue> m_closeClothVariation;
    CConfigValue<Config::FLOAT> m_closeClothVariationSet;
    SP<Config::Values::CFloatValue> m_closeSwarmDuration;
    CConfigValue<Config::FLOAT> m_closeSwarmDurationSet;
    SP<Config::Values::CStringValue> m_closeSwarmLoneWindowSides;
    CConfigValue<Config::STRING> m_closeSwarmLoneWindowSidesSet;
    SP<Config::Values::CFloatValue> m_closeBubbleDuration;
    CConfigValue<Config::FLOAT> m_closeBubbleDurationSet;
    SP<Config::Values::CFloatValue> m_closeSmokeDuration;
    CConfigValue<Config::FLOAT> m_closeSmokeDurationSet;
    SP<Config::Values::CFloatValue> m_closeSmokeTurbulence;
    CConfigValue<Config::FLOAT> m_closeSmokeTurbulenceSet;
    SP<Config::Values::CStringValue> m_closeSmokeDirection;
    CConfigValue<Config::STRING> m_closeSmokeDirectionSet;
    SP<Config::Values::CStringValue> m_movementSwarmSprite;
    CConfigValue<Config::STRING> m_movementSwarmSpriteSet;
    SP<Config::Values::CIntValue> m_movementSwarmCount;
    CConfigValue<Config::INTEGER> m_movementSwarmCountSet;
    SP<Config::Values::CFloatValue> m_movementSwarmSpeed;
    CConfigValue<Config::FLOAT> m_movementSwarmSpeedSet;
    SP<Config::Values::CFloatValue> m_movementSwarmStagger;
    CConfigValue<Config::FLOAT> m_movementSwarmStaggerSet;
    SP<Config::Values::CFloatValue> m_movementSwarmCohesion;
    CConfigValue<Config::FLOAT> m_movementSwarmCohesionSet;
    SP<Config::Values::CFloatValue> m_movementSwarmSeparation;
    CConfigValue<Config::FLOAT> m_movementSwarmSeparationSet;
    SP<Config::Values::CFloatValue> m_movementSwarmAgility;
    CConfigValue<Config::FLOAT> m_movementSwarmAgilitySet;
    SP<Config::Values::CFloatValue> m_movementSwarmSettling;
    CConfigValue<Config::FLOAT> m_movementSwarmSettlingSet;
    SP<Config::Values::CFloatValue> m_movementSwarmWander;
    CConfigValue<Config::FLOAT> m_movementSwarmWanderSet;
    SP<Config::Values::CFloatValue> m_movementSwarmVariation;
    CConfigValue<Config::FLOAT> m_movementSwarmVariationSet;
    SP<Config::Values::CStringValue> m_openSwarmSprite;
    CConfigValue<Config::STRING> m_openSwarmSpriteSet;
    SP<Config::Values::CIntValue> m_openSwarmCount;
    CConfigValue<Config::INTEGER> m_openSwarmCountSet;
    SP<Config::Values::CFloatValue> m_openSwarmSpeed;
    CConfigValue<Config::FLOAT> m_openSwarmSpeedSet;
    SP<Config::Values::CFloatValue> m_openSwarmStagger;
    CConfigValue<Config::FLOAT> m_openSwarmStaggerSet;
    SP<Config::Values::CFloatValue> m_openSwarmCohesion;
    CConfigValue<Config::FLOAT> m_openSwarmCohesionSet;
    SP<Config::Values::CFloatValue> m_openSwarmSeparation;
    CConfigValue<Config::FLOAT> m_openSwarmSeparationSet;
    SP<Config::Values::CFloatValue> m_openSwarmAgility;
    CConfigValue<Config::FLOAT> m_openSwarmAgilitySet;
    SP<Config::Values::CFloatValue> m_openSwarmSettling;
    CConfigValue<Config::FLOAT> m_openSwarmSettlingSet;
    SP<Config::Values::CFloatValue> m_openSwarmWander;
    CConfigValue<Config::FLOAT> m_openSwarmWanderSet;
    SP<Config::Values::CFloatValue> m_openSwarmVariation;
    CConfigValue<Config::FLOAT> m_openSwarmVariationSet;
    SP<Config::Values::CStringValue> m_closeSwarmSprite;
    CConfigValue<Config::STRING> m_closeSwarmSpriteSet;
    SP<Config::Values::CIntValue> m_closeSwarmCount;
    CConfigValue<Config::INTEGER> m_closeSwarmCountSet;
    SP<Config::Values::CFloatValue> m_closeSwarmSpeed;
    CConfigValue<Config::FLOAT> m_closeSwarmSpeedSet;
    SP<Config::Values::CFloatValue> m_closeSwarmStagger;
    CConfigValue<Config::FLOAT> m_closeSwarmStaggerSet;
    SP<Config::Values::CFloatValue> m_closeSwarmCohesion;
    CConfigValue<Config::FLOAT> m_closeSwarmCohesionSet;
    SP<Config::Values::CFloatValue> m_closeSwarmSeparation;
    CConfigValue<Config::FLOAT> m_closeSwarmSeparationSet;
    SP<Config::Values::CFloatValue> m_closeSwarmAgility;
    CConfigValue<Config::FLOAT> m_closeSwarmAgilitySet;
    SP<Config::Values::CFloatValue> m_closeSwarmSettling;
    CConfigValue<Config::FLOAT> m_closeSwarmSettlingSet;
    SP<Config::Values::CFloatValue> m_closeSwarmWander;
    CConfigValue<Config::FLOAT> m_closeSwarmWanderSet;
    SP<Config::Values::CFloatValue> m_closeSwarmVariation;
    CConfigValue<Config::FLOAT> m_closeSwarmVariationSet;
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity): Schema-generated registration keeps each value and validator together.
inline void registerConfigValues(HANDLE handle, SConfigValues& values) {
    values.m_globalConstructionDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:construction:duration", "Shared construction duration setting.", 620.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_globalBurnDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:duration", "Shared burn duration setting.", 1000.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_globalLiquidStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:liquid:strength", "Shared liquid strength setting.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_globalLiquidViscosity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:liquid:viscosity", "Shared liquid viscosity setting.", 0.75F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalLiquidElasticity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:liquid:elasticity", "Shared liquid elasticity setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalLiquidMomentum = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:liquid:momentum", "Shared liquid momentum setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalLiquidVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:liquid:variation", "Shared liquid variation setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalClothStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:cloth:strength", "Shared cloth strength setting.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_globalClothSpringiness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:cloth:springiness", "Shared cloth springiness setting.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalClothGravity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:cloth:gravity", "Shared cloth gravity setting.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 3.0F});
    values.m_globalClothFlutter = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:cloth:flutter", "Shared cloth flutter setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalClothShading = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:cloth:shading", "Shared cloth shading setting.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalClothShine = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:cloth:shine", "Shared cloth shine setting.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalClothVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:cloth:variation", "Shared cloth variation setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalBubbleStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:bubble:strength", "Shared bubble strength setting.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_globalBubbleMicrobubbles = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:bubble:microbubbles", "Shared bubble microbubbles setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalBubbleTrail = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:bubble:trail", "Shared bubble trail setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalBubbleShape = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:bubble:shape", "Shared bubble shape setting.", "pillow",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "pillow" || value == "ellipse" || value == "circle") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:bubble:shape must be one of: pillow, ellipse, circle"));
        },});
    values.m_globalBubbleRoundness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:bubble:roundness", "Shared bubble roundness setting.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalBubbleShapeSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:bubble:shape_speed", "Shared bubble shape speed setting.", 1200.0F,
        Config::Values::SFloatValueOptions{.min = 100.0F, .max = 6000.0F});
    values.m_globalBubbleTransparency = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:bubble:transparency", "Shared bubble transparency setting.", 0.88F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalSmokeStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:smoke:strength", "Shared smoke strength setting.", 0.85F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_globalSmokeTurbulence = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:smoke:turbulence", "Shared smoke turbulence setting.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalSmokeTrail = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:smoke:trail", "Shared smoke trail setting.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalLiquidDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:liquid:duration", "Shared liquid duration setting.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_globalLiquidColor = makeShared<Config::Values::CColorValue>("plugin:hyprimate:window:options:liquid:color", "Shared liquid color setting.", 0xff000000);
    values.m_globalLiquidDefaultAnimationEdge = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:liquid:default_animation_edge", "Shared liquid default animation edge setting.", "bottom",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "bottom" || value == "top" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:liquid:default_animation_edge must be one of: bottom, top, left, right"));
        },});
    values.m_globalLiquidEdgeBounds = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:liquid:edge_bounds", "Shared liquid edge bounds setting.", "screen",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "screen" || value == "work_area") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:liquid:edge_bounds must be one of: screen, work_area"));
        },});
    values.m_globalLiquidEmergeDrag = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:liquid:emerge_drag", "Shared liquid emerge drag setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalPaperDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:duration", "Shared paper duration setting.", 600.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_globalPaperCrumpliness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:crumpliness", "Shared paper crumpliness setting.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalPaperShading = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:shading", "Shared paper shading setting.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalPaperVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:variation", "Shared paper variation setting.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalPaperFoldDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:fold_density", "Shared paper fold density setting.", 0.55F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalPaperPaperStiffness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:paper_stiffness", "Shared paper paper stiffness setting.", 0.72F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalPaperCrumpleSize = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:crumple_size", "Shared paper crumple size setting.", 0.18F,
        Config::Values::SFloatValueOptions{.min = 0.1F, .max = 0.45F});
    values.m_globalPaperCrumpleOrigin = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:paper:crumple_origin", "Shared paper crumple origin setting.", "center",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "center" || value == "random" || value == "cursor" || value == "left" || value == "right" || value == "top" || value == "bottom") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:paper:crumple_origin must be one of: center, random, cursor, left, right, top, bottom"));
        },});
    values.m_globalPaperForceDirection = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:paper:force_direction", "Shared paper force direction setting.", "inward",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "inward" || value == "directional" || value == "rotational") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:paper:force_direction must be one of: inward, directional, rotational"));
        },});
    values.m_globalPaperRotationIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:rotation_intensity", "Shared paper rotation intensity setting.", 0.22F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalPaperSettling = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:settling", "Shared paper settling setting.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_globalPaperSimulationQuality = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:paper:simulation_quality", "Shared paper simulation quality setting.", "adaptive",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "adaptive" || value == "low" || value == "medium" || value == "high") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:paper:simulation_quality must be one of: adaptive, low, medium, high"));
        },});
    values.m_globalClothDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:cloth:duration", "Shared cloth duration setting.", 1000.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_globalClothDefaultAnimationEdge = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:cloth:default_animation_edge", "Shared cloth default animation edge setting.", "bottom",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "bottom" || value == "top" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:cloth:default_animation_edge must be one of: bottom, top, left, right"));
        },});
    values.m_globalClothEdgeBounds = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:cloth:edge_bounds", "Shared cloth edge bounds setting.", "screen",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "screen" || value == "work_area") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:cloth:edge_bounds must be one of: screen, work_area"));
        },});
    values.m_globalSwarmDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:duration", "Shared swarm duration setting.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_globalSwarmLoneWindowSides = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:swarm:lone_window_sides", "Shared swarm lone window sides setting.", "all",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (detail::parse_lone_window_sides(value)) {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:swarm:lone_window_sides must be a comma-separated list of: top, bottom, left, right, all"));
        },});
    values.m_globalBubbleDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:bubble:duration", "Shared bubble duration setting.", 1000.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_globalSmokeDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:smoke:duration", "Shared smoke duration setting.", 1400.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_globalSmokeDirection = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:smoke:direction", "Shared smoke direction setting.", "inside",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "inside" || value == "top" || value == "bottom" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:smoke:direction must be one of: inside, top, bottom, left, right"));
        },});
    values.m_globalLiquidSide = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:liquid:side", "Shared liquid side setting.", "auto",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "auto" || value == "nearest" || value == "origin" || value == "top" || value == "bottom" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:liquid:side must be one of: auto, nearest, origin, top, bottom, left, right"));
        },});
    values.m_globalLiquidSplash = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:window:options:liquid:splash", "Shared liquid splash setting.", true);
    values.m_globalClothSide = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:cloth:side", "Shared cloth side setting.", "auto",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "auto" || value == "nearest" || value == "origin" || value == "top" || value == "bottom" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:cloth:side must be one of: auto, nearest, origin, top, bottom, left, right"));
        },});
    values.m_workspaceEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:workspace:enabled", "Enable workspace transition effects independently of window effects.", false);
    values.m_workspaceStyle = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:style", "Choose the workspace transition style. Invalid names are rejected.", "rotation",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "rotation" || value == "planet_hop" || value == "cube" || value == "lava") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:workspace:style must be one of: rotation, planet_hop, cube, lava"));
        },});
    values.m_workspaceEffect = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:effect", "Path to a native GLES shader transition manifest. Empty uses workspace.style.", "");
    values.m_workspaceEffectParameters = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:effect_parameters", "JSON object overriding declared workspace shader parameters.", "{}");
    values.m_workspaceEffectDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:effect_duration", "External workspace transition time in milliseconds. Zero uses the built-in workspace style.", 600.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 60000.0F});
    values.m_workspaceRotationDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:rotation:duration", "Time in milliseconds for the workspace rotation.", 600.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_workspaceRotationCurve = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:rotation:curve", "Name of a Hyprland Bezier animation curve. Unknown names are rejected and the previous valid curve is retained. Curve overshoot is clipped to the wheel's rotation range.", "default");
    values.m_workspaceRotationGapFillStyle = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:rotation:gap_fill_style", "Gap treatment: morph stretches an auxiliary incoming-wallpaper bridge; background uses gap_fill_color; wallpaper blends enlarged, optionally blurred wallpaper copies; still (default) holds the outgoing wallpaper stationary while windows rotate. In still mode a different incoming wallpaper rotates in with its windows; matching texture resources with unchanged content stay stationary. No mode changes the rigid circular workspace motion. Configuration uses underscores: gap_fill_style, gap_fill_color, gap_fill_blur. Invalid styles are rejected.", "still",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "morph" || value == "background" || value == "wallpaper" || value == "still") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:workspace:options:rotation:gap_fill_style must be one of: morph, background, wallpaper, still"));
        },});
    values.m_workspaceRotationGapFillColor = makeShared<Config::Values::CColorValue>("plugin:hyprimate:workspace:options:rotation:gap_fill_color", "Gap colour for background mode, in ARGB; defaults to opaque black.", 0xff000000);
    values.m_workspaceRotationGapFillBlur = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:rotation:gap_fill_blur", "Blur radius in physical pixels for the duplicated background imagery in wallpaper mode only; zero disables blur. Original wallpapers, windows and rotation geometry are unaffected.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 64.0F});
    values.m_workspaceRotationKeepOverlaysStatic = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:workspace:options:rotation:keep_overlays_static", "Keep layer-shell bars, overlays and their popups fixed on screen while the workspace rotates. When false, they rotate with the workspace as before.", false);
    values.m_workspacePlanetHopWindowMode = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:planet_hop:window_mode", "Place windows on the planet surface, float them above it, or fade them during the hop.", "surface",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "surface" || value == "floating" || value == "fade") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:workspace:options:planet_hop:window_mode must be one of: surface, floating, fade"));
        },});
    values.m_workspacePlanetHopOverlayMode = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:planet_hop:overlay_mode", "Render overlays in the world, as a HUD, or fade them during the hop.", "fade",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "world" || value == "hud" || value == "fade") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:workspace:options:planet_hop:overlay_mode must be one of: world, hud, fade"));
        },});
    values.m_workspacePlanetHopPlanetLayout = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:planet_hop:planet_layout", "Show the two journey endpoints, this monitor's workspaces, every monitor's workspaces, or every monitor's workspaces in a spatial solar system, respectively. Replaces linear and dynamic; use focused for endpoint-only travel.", "focused",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "focused" || value == "monitor" || value == "all" || value == "solar_system") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:workspace:options:planet_hop:planet_layout must be one of: focused, monitor, all, solar_system"));
        },});
    values.m_workspacePlanetHopDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:duration", "Total departure-to-landing time in milliseconds, including skipped workspaces.", 800.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_workspacePlanetHopCurve = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:planet_hop:curve", "Named Hyprland Bezier or spring for the complete journey. Planet Hop also accepts style-local spring definitions.", "linear");
    values.m_workspacePlanetHopLiveBoundary = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:live_boundary", "Fraction adjoining each endpoint during which live workspace imagery is refreshed.", 0.22F,
        Config::Values::SFloatValueOptions{.min = 0.08F, .max = 0.35F});
    values.m_workspacePlanetHopSamplingQuality = makeShared<Config::Values::CIntValue>("plugin:hyprimate:workspace:options:planet_hop:sampling_quality", "Material sampling quality: 0 low, 1 medium, 2 high.", 2,
        Config::Values::SIntValueOptions{.min = 0, .max = 2});
    values.m_workspacePlanetHopPlanetSize = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:planet_size", "Relative planet size.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.1F, .max = 4.0F});
    values.m_workspacePlanetHopPlanetSeparation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:planet_separation", "Separation between planets in planet-radius units.", 4.0F,
        Config::Values::SFloatValueOptions{.min = 1.0F, .max = 20.0F});
    values.m_workspacePlanetHopSolarVerticalSpread = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:solar_vertical_spread", "Vertical spread of solar-system workspace positions, relative to planet spacing and the output aspect.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 2.0F});
    values.m_workspacePlanetHopSolarBackgroundScale = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:solar_background_scale", "Radius of inactive workspace planets relative to the source and destination, before perspective projection. Applies to monitor, all and solar_system.", 0.75F,
        Config::Values::SFloatValueOptions{.min = 0.25F, .max = 1.0F});
    values.m_workspacePlanetHopPlanetCoverage = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:planet_coverage", "Spread of central desktop imagery on the front hemisphere. Larger values give the centre more surface area; the desktop edges always reach the limb.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.1F, .max = 1.0F});
    values.m_workspacePlanetHopIndirectBrightness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:indirect_brightness", "Indirect illumination of the planet.", 0.18F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopElevation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:elevation", "Elevation of floating windows above the planet surface in scene units; world overlays use the same elevation.", 0.2F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopFade = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:fade", "Fraction of the hop used for fading windows and overlays.", 0.2F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopRotationEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:workspace:options:planet_hop:rotation_enabled", "Enable planet rotation during the hop.", true);
    values.m_workspacePlanetHopRotationLimit = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:rotation_limit", "Maximum planet rotation in radians.", 0.35F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 3.14F});
    values.m_workspacePlanetHopStarsEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:workspace:options:planet_hop:stars_enabled", "Enable the star field.", true);
    values.m_workspacePlanetHopStarDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:star_density", "Density of background stars.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopStarParallax = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:star_parallax", "Strength of background star parallax.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 2.0F});
    values.m_workspacePlanetHopAtmosphereEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:workspace:options:planet_hop:atmosphere_enabled", "Enable the planet atmosphere.", true);
    values.m_workspacePlanetHopAtmosphereIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:atmosphere_intensity", "Atmospheric glow intensity.", 0.3F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopAtmosphereThickness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:atmosphere_thickness", "Atmospheric thickness relative to planet radius.", 0.035F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopLightingEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:workspace:options:planet_hop:lighting_enabled", "Enable directional planet lighting.", true);
    values.m_workspacePlanetHopLightDirectionX = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:light_direction_x", "Horizontal component of the light direction.", -0.6F,
        Config::Values::SFloatValueOptions{.min = -1.0F, .max = 1.0F});
    values.m_workspacePlanetHopLightDirectionY = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:light_direction_y", "Vertical component of the light direction.", -0.3F,
        Config::Values::SFloatValueOptions{.min = -1.0F, .max = 1.0F});
    values.m_workspacePlanetHopLightDirectionZ = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:light_direction_z", "Depth component of the light direction.", 1.0F,
        Config::Values::SFloatValueOptions{.min = -1.0F, .max = 1.0F});
    values.m_workspacePlanetHopNightBrightness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:night_brightness", "Brightness of the planet's night side.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopCloudsEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:workspace:options:planet_hop:clouds_enabled", "Enable the cloud layer.", true);
    values.m_workspacePlanetHopCloudCoverage = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:cloud_coverage", "Fraction of the planet covered by clouds.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopCloudOpacity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:cloud_opacity", "Cloud layer opacity.", 0.18F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopCloudSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:cloud_speed", "Cloud drift speed in radians per second.", 0.025F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspacePlanetHopDepthEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:workspace:options:planet_hop:depth_enabled", "Enable planet surface depth.", true);
    values.m_workspacePlanetHopDepthStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:planet_hop:depth_strength", "Surface relief strength in planet-radius units.", 0.018F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 0.5F});
    values.m_workspacePlanetHopDepthSource = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:planet_hop:depth_source", "Optional PNG depth image path; empty uses procedural depth. PNG images are limited to 4096 pixels per dimension and 16 MiB encoded.", "");
    values.m_workspaceCubeVariant = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:cube:variant", "Turn between full workspaces on the outer faces of a solid cube or the inner walls of a room.", "solid",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "solid" || value == "inside_out") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:workspace:options:cube:variant must be one of: solid, inside_out"));
        },});
    values.m_workspaceCubeDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:cube:duration", "Time in milliseconds for the cube turn. Zero skips the transition.", 600.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_workspaceCubeCurve = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:cube:curve", "Name of a native Hyprland Bezier curve for the cube turn. Unknown names retain the previous valid curve, or fall back to the native default.", "default");
    values.m_workspaceCubeSolidCubeTilt = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:cube:solid:cube_tilt", "Mid-turn tilt in degrees. Positive angles tip the workspace face up; zero keeps it level.", 0.0F,
        Config::Values::SFloatValueOptions{.min = -20.0F, .max = 20.0F});
    values.m_workspaceCubeSolidZoom = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:cube:solid:zoom", "Extra camera retreat at mid-turn, measured in workspace widths.", 0.25F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceCubeInsideOutCubeTilt = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:cube:inside_out:cube_tilt", "Mid-turn tilt in degrees. Positive angles tip the workspace face up; zero keeps it level.", 0.0F,
        Config::Values::SFloatValueOptions{.min = -20.0F, .max = 20.0F});
    values.m_workspaceCubeInsideOutZoom = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:cube:inside_out:zoom", "Fraction of the safe camera retreat inside the room at mid-turn; 1 uses the full safe distance.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaVariant = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:lava:variant", "Choose one Lava surface model. Only its matching subtable affects the transition.", "crust_plates",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "cooling_flow" || value == "basalt_flow" || value == "pahoehoe" || value == "crust_plates") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:workspace:options:lava:variant must be one of: cooling_flow, basalt_flow, pahoehoe, crust_plates"));
        },});
    values.m_workspaceLavaWindowBehavior = makeShared<Config::Values::CStringValue>("plugin:hyprimate:workspace:options:lava:window_behavior", "Melt the complete scene as one slab, or burn away visible app-window pixels while the rest of the desktop melts.", "melt_with_workspace",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "melt_with_workspace" || value == "burn_up") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:workspace:options:lava:window_behavior must be one of: melt_with_workspace, burn_up"));
        },});
    values.m_workspaceLavaViscosity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:viscosity", "Make the flowing workspace look thicker and more cohesive.", 0.72F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCoolingRate = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:cooling_rate", "Control how quickly the visible surface cools over the course of travel.", 0.8F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCrustAmount = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:crust_amount", "Increase the coverage of cooled crust relative to exposed melt.", 0.68F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaGlowIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:glow_intensity", "Brighten the hot material visible between cooler regions.", 0.55F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaDetailPreservation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:detail_preservation", "Keep desktop details more legible through the molten surface.", 0.75F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaTrailSmear = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:trail_smear", "Draw desktop detail into longer streaks behind the flow.", 0.35F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaEdgeDrip = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:edge_drip", "Increase the sag and dripping of the molten edge.", 0.15F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaAshAmount = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:ash_amount", "Add dark ash flecks to the cooling surface.", 0.08F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCoolingFlowLeadingBulge = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:cooling_flow:leading_bulge", "Round out the advancing front of the cooling flow.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCoolingFlowTrailingStretch = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:cooling_flow:trailing_stretch", "Pull the rear of the cooling flow into a longer tail.", 0.4F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCoolingFlowMoltenTurbulence = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:cooling_flow:molten_turbulence", "Increase local warping within the molten flow.", 0.3F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaBasaltFlowCrustThickness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:basalt_flow:crust_thickness", "Thicken the dark basalt surface around fissures.", 0.75F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaBasaltFlowFissureWidth = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:basalt_flow:fissure_width", "Widen cracks in the basalt crust.", 0.28F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaBasaltFlowEmberVisibility = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:basalt_flow:ember_visibility", "Make hot embers more visible in the basalt.", 0.12F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaBasaltFlowMassDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:basalt_flow:mass_density", "Give the basalt flow a denser, heavier appearance.", 0.8F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaPahoehoeRopeStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:pahoehoe:rope_strength", "Emphasize the rope-like ridges on the flowing surface.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaPahoehoeFoldScale = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:pahoehoe:fold_scale", "Set the size of individual rope folds.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaPahoehoeRippleSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:pahoehoe:ripple_speed", "Advance the surface ripples further during the same workspace travel.", 0.4F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaPahoehoeSurfaceRoll = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:pahoehoe:surface_roll", "Increase the rolling deformation of the folded surface.", 0.55F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCrustPlatesPlateSize = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:crust_plates:plate_size", "Set the size of individual crust plates.", 0.55F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCrustPlatesPlateRigidity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:crust_plates:plate_rigidity", "Keep imagery on each moving plate more rigid.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCrustPlatesSeamGlow = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:crust_plates:seam_glow", "Brighten the hot seams between plates.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCrustPlatesPlateDrift = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:crust_plates:plate_drift", "Increase the relative displacement of neighboring plates.", 0.35F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCrustPlatesPlateCompression = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:crust_plates:plate_compression", "Increase how tightly the plates press together during flow.", 0.25F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_workspaceLavaCrustPlatesSubmeltVisibility = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:workspace:options:lava:crust_plates:submelt_visibility", "Reveal more of the molten layer beneath the plates.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_wallpaperEffect = makeShared<Config::Values::CStringValue>("plugin:hyprimate:wallpaper:effect", "Path to a native GLES wallpaper manifest. Empty keeps the native background.", "");
    values.m_wallpaperEffectParameters = makeShared<Config::Values::CStringValue>("plugin:hyprimate:wallpaper:effect_parameters", "JSON object overriding declared wallpaper shader parameters.", "{}");
    values.m_wallpaperEffectDuration = makeShared<Config::Values::CIntValue>("plugin:hyprimate:wallpaper:effect_duration", "Milliseconds of wallpaper simulation before holding the final frame. Zero runs continuously.", 0,
        Config::Values::SIntValueOptions{.min = 0, .max = 60000});
    values.m_wallpaperSourceNamespace = makeShared<Config::Values::CStringValue>("plugin:hyprimate:wallpaper:source_namespace", "Exact layer-shell BACKGROUND namespace allowed as a shader wallpaper source. Empty allows only the unobscured stock wallpaper.", "");
    values.m_enabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:window:enabled", "Enable window movement, opening and closing effects. When false, none of these effects run.", true);
    values.m_duringWorkspaceSwitch = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:window:during_workspace_switch", "Show window effects during workspace switches. When false, use native window presentation where available until the switch ends.", false);
    values.m_style = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:style", "Choose the built-in style for movement and for transitions without an external effect override.", "liquid",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "liquid" || value == "paper" || value == "cloth" || value == "swarm" || value == "bubble" || value == "smoke" || value == "burn" || value == "construction") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:style must be one of: liquid, paper, cloth, swarm, bubble, smoke, burn, construction"));
        },});
    values.m_openEffect = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:effect", "Path to a native GLES shader effect manifest for window opening. Empty uses window.style.", "");
    values.m_closeEffect = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:effect", "Path to a native GLES shader effect manifest for window closing. Empty uses window.style.", "");
    values.m_openEffectParameters = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:effect_parameters", "JSON object overriding the opening effect's declared parameters.", "{}");
    values.m_closeEffectParameters = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:effect_parameters", "JSON object overriding the closing effect's declared parameters.", "{}");
    values.m_openEffectDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:effect_duration", "Opening effect duration in milliseconds. Zero uses the compositor's native opening.", 600.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 60000.0F});
    values.m_closeEffectDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:effect_duration", "Closing effect duration in milliseconds. Zero uses the compositor's native closing.", 600.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 60000.0F});
    values.m_moveEffect = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:move:effect", "Path to a native GLES shader manifest for movement. Empty uses window.style.", "");
    values.m_moveEffectParameters = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:move:effect_parameters", "JSON object overriding declared movement shader parameters.", "{}");
    values.m_moveEffectDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:move:effect_duration", "Movement settling time in milliseconds; zero disables the external effect.", 350.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 60000.0F});
    values.m_resizeEffect = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:resize:effect", "Path to a native GLES shader manifest for resizing. Empty uses window.style.", "");
    values.m_resizeEffectParameters = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:resize:effect_parameters", "JSON object overriding declared resizing shader parameters.", "{}");
    values.m_resizeEffectDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:resize:effect_duration", "Resize settling time in milliseconds; zero disables the external effect.", 350.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 60000.0F});
    values.m_openConstructionDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:construction:duration", "Time in milliseconds for the builders to raise and fasten the window panels.", 620.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_closeConstructionDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:construction:duration", "Time in milliseconds for the crew to clear the window and its printed facade to collapse.", 850.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_constructionWorkerSize = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:construction:worker_size", "Worker size multiplier for opening, movement, resizing and closing. Workers scale with the window's shorter dimension; 1 is normal size, 0 hides the people, and 2 doubles their size.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 2.0F});
    values.m_burnVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:variation", "Irregularity of the combustion front, from even propagation to strongly uneven burning.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_burnFragmentDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:fragment_density", "Relative emission of detached printed fragments. Zero disables them.", 0.55F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_burnEmberDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:ember_density", "Relative emission of luminous embers. Zero disables them.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_burnWindDirection = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:wind_direction", "Environmental airflow heading in degrees clockwise from screen right (90 is down, 270 is up). Does not steer the combustion front.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 360.0F});
    values.m_burnWindStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:wind_strength", "Wind speed in logical pixels per second. Light ash follows airflow more readily than paper.", 100.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1000.0F});
    values.m_burnGravityStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:gravity_strength", "Downward gravitational acceleration in logical pixels per second squared.", 300.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1800.0F});
    values.m_burnScorchLifetime = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:scorch_lifetime", "Seconds a fresh movement or closing scorch cools on the wallpaper before regeneration.", 1.4F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 10.0F});
    values.m_burnHealingDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:burn:healing_duration", "Seconds of local wallpaper recovery after the scorch lifetime. No wallpaper file is changed.", 1.8F,
        Config::Values::SFloatValueOptions{.min = 0.1F, .max = 10.0F});
    values.m_burnMovementSmokeDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:burn:smoke_density", "Faint smoke released by newly charred wallpaper. Zero leaves the damaged paper and falling fragments visible without smoke.", 0.22F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openBurnDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:burn:duration", "Combustion time in milliseconds for the hole to reveal the window.", 1000.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_closeBurnDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:burn:duration", "Combustion time in milliseconds for the closing sheet. Detached material cools on its own clock.", 1500.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_burnClosingStyle = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:burn:closing_style", "Consume burns the printed sheet in place; curl also bends the brittle edges and exposed paper.", "curl",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "consume" || value == "curl") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:burn:closing_style must be one of: consume, curl"));
        },});
    values.m_burnDebrisDirection = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:burn:debris_direction", "Motion of close debris: material-dependent natural forces, falling debris, rising debris or directional wind. Fire propagation is independent.", "natural",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "natural" || value == "fall" || value == "rise" || value == "wind") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:burn:debris_direction must be one of: natural, fall, rise, wind"));
        },});
    values.m_burnCurlIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:burn:curl_intensity", "Strength of local bending at the combustion front in the curl closing style.", 0.8F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperStiffness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:stiffness", "Shared resistance to bending; higher values keep the sheet more rigid. Explicit values also supply opening and closing paper_stiffness unless a legacy shared or per-animation override is set.", 0.75F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperInertia = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:inertia", "Shared retention of bending momentum across paper movement, resizing and transitions. Zero minimizes residual motion; higher values prolong directional lag without stretching the sheet.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperSettleDamping = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:settle_damping", "Shared dissipation of residual bending motion; higher values settle more quickly. Explicit values also supply opening and closing settling unless a legacy shared or per-animation override is set.", 0.8F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperMoveStyle = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:paper:move:style", "Movement behavior: flutter lifts the sheet into airflow; glide keeps it close to the desktop. Inactive movement styles do not simulate.", "flutter",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "flutter" || value == "glide") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:options:paper:move:style must be one of: flutter, glide"));
        },});
    values.m_paperFlutterAirResistance = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:flutter:air_resistance", "Flutter-only airflow coupling to movement velocity and acceleration. Zero removes airflow-driven bending.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperFlutterIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:flutter:intensity", "Flutter-only strength of movement-induced bends and ripples. Zero disables these bends, not pickup lift.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperFlutterFrequency = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:flutter:frequency", "Flutter-only ripple frequency multiplier; higher values produce faster, more closely spaced transient ripples.", 1.2F,
        Config::Values::SFloatValueOptions{.min = 0.1F, .max = 4.0F});
    values.m_paperFlutterLift = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:flutter:lift", "Flutter-only apparent pickup height and shadow separation. Zero keeps the body at the desktop surface.", 0.3F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperFlutterTorsion = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:flutter:torsion", "Flutter-only twisting from acceleration and direction changes. Zero disables the torsional response.", 0.4F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperGlideFriction = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:glide:friction", "Glide-only surface friction damping local bends; higher values flatten residual curls sooner.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperGlideCornerLift = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:glide:corner_lift", "Glide-only corner lifting in response to acceleration. Zero keeps the corners grounded.", 0.25F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperGlideEdgeCurl = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:glide:edge_curl", "Glide-only localized leading-edge curling from movement speed. Zero removes edge curling.", 0.3F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperGlideShadowSeparation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:move:glide:shadow_separation", "Glide-only apparent contact-shadow separation under locally lifted edges and corners. Zero removes separation.", 0.2F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperResizeFoldIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:resize:fold_intensity", "Resize-only strength of localized compression buckles and folds. Zero disables compression folding.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperResizeCurlIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:resize:curl_intensity", "Resize-only strength of edge curls released during expansion. Zero disables expansion curling.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_paperResizeCreaseDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:paper:resize:crease_density", "Resize-only spacing of compression creases: lower values make broad folds, higher values make finer pleats.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_swarmSprite = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:options:swarm:sprite", "Swarm sprite name in the sprites directory, with or without .png, or an explicit PNG path. The sheet has four columns and two rows of frames; transparency defines the silhouette.", "spider");
    values.m_swarmCount = makeShared<Config::Values::CIntValue>("plugin:hyprimate:window:options:swarm:count", "Number of swarm creatures per window. Each carries a piece of the window, so fewer creatures mean larger pieces.", 250,
        Config::Values::SIntValueOptions{.min = 1, .max = 400});
    values.m_swarmSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:speed", "Swarm creature travel speed in logical pixels per second.", 1400.0F,
        Config::Values::SFloatValueOptions{.min = 100.0F, .max = 6000.0F});
    values.m_swarmStagger = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:stagger", "Time in seconds over which swarm creatures arrive or leave in random order when a window opens or closes. Movement releases them over half this time. Zero makes them arrive or leave together.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 3.0F});
    values.m_swarmCohesion = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:cohesion", "How strongly nearby swarm creatures travel together. Zero lets each find its own way.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_swarmSeparation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:separation", "How much swarm creatures avoid and bump off each other while travelling. Zero lets them pass through each other.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_swarmAgility = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:agility", "Swarm acceleration and grip. Higher values hold formation during faster movement and turn more sharply; lower values break apart sooner and trail farther.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_swarmSettling = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:settling", "How quickly swarm creatures regroup and merge back into the window. Lower values make them mill about longer.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_swarmWander = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:wander", "How much swarm creatures wander and hesitate while travelling. Zero makes them follow their routes steadily.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_swarmVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:swarm:variation", "Variation in each swarm creature's speed, grip and route curvature. Zero makes them travel straight and alike.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_smokeColor = makeShared<Config::Values::CColorValue>("plugin:hyprimate:window:options:smoke:color", "Base tint for the smoke body and wisps when moving, opening and closing, in ARGB. Opaque tint supplies 80% of the colour; the window supplies the rest. Alpha scales tint strength, not smoke opacity. Zero alpha keeps the window-derived colour.", 0x00000000);
    values.m_smokeDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:smoke:density", "Optical thickness of the smoke body when moving, opening and closing. Higher values build a denser, more layered interior while thin outer smoke stays translucent; lower values give a lighter, wispier body.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.25F, .max = 2.0F});
    values.m_smokeVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:options:smoke:variation", "Randomness of smoke between windows and animations: where smoke gathers or leaves, how it turns and swirls, and how its density and wisps vary. Zero makes every animation alike while the smoke still moves naturally. Configured directions are always respected.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:window:movement:enabled", "Deform windows while they move. This does not affect opening or closing effects.", true);
    values.m_workspaceMovement = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:window:movement:workspace_movement", "Let workspace slides trigger movement effects. When false, ordinary movement, opening and closing are unaffected.", true);
    values.m_movementConstructionVariant = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:movement:options:construction:variant", "Choose how Construction moves a window: carry it with workers (default), lift it with a crane, or relocate its pieces. All three use workers to resize the intact window. Native explicitly opts out of movement and resize effects.", "crew_carry",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "native" || value == "crew_carry" || value == "crane" || value == "piece_by_piece") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:movement:options:construction:variant must be one of: native, crew_carry, crane, piece_by_piece"));
        },});
    values.m_movementLiquidStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:liquid:strength", "Liquid movement deformation strength. Zero moves windows without deformation.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_viscosity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:liquid:viscosity", "Liquid resistance to flow during movement. Higher values make it thicker and more cohesive.", 0.75F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_elasticity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:liquid:elasticity", "Liquid stiffness and recovery speed during movement. Higher values snap back faster.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_momentum = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:liquid:momentum", "Liquid inertia after movement stops. Zero settles without overshoot.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_variation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:liquid:variation", "Random variation between windows and movements. Zero makes liquid movement identical.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementClothStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:cloth:strength", "Cloth movement deformation strength. Zero moves windows without deformation.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_movementClothSpringiness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:cloth:springiness", "Horizontal springiness of the curtain's top edge during movement. Zero keeps it stiff; higher values allow more gathering and rebound. The lower curtain remains free-flowing.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementClothGravity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:cloth:gravity", "Cloth movement gravity multiplier. One is normal gravity; zero disables gravity. Lower values let fabric lift more easily. Opening and closing are unaffected.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 3.0F});
    values.m_movementClothFlutter = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:cloth:flutter", "Cloth movement ripple strength. Zero removes varying gusts, but movement still bends the sheet.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementClothShading = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:cloth:shading", "Light and shadow on cloth movement folds. Zero draws them unlit.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementClothShine = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:cloth:shine", "Highlight strength on cloth movement folds. Zero removes highlights.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementClothVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:cloth:variation", "Variation in cloth movement between windows. Zero gives every window the same motion.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementBubbleStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:bubble:strength", "How much the trailing bubble film lags, ripples and overshoots during movement. Zero moves windows without deformation.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_movementBubbleMicrobubbles = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:bubble:microbubbles", "Rate at which small bubbles swell out of the moving film, mostly on its trailing side and during acceleration, ride its outline, and then pinch off into the trail, sink back into the film or pop. Zero forms none.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementBubbleTrail = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:bubble:trail", "Amount of soap film a fast-moving window draws out of its trailing side as thin streaks, which dry from their edges inward and break up. Zero leaves none.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementBubbleShape = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:movement:options:bubble:shape", "Shape surface tension pulls a moving bubble toward. Pillow draws the window in slightly and rounds its corners while keeping its proportions and straight sides; ellipse keeps the window's proportions; circle makes a disc regardless of its proportions. Invalid names are rejected.", "pillow",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "pillow" || value == "ellipse" || value == "circle") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:movement:options:bubble:shape must be one of: pillow, ellipse, circle"));
        },});
    values.m_movementBubbleRoundness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:bubble:roundness", "How far a window moving at shape_speed contracts toward its bubble shape before returning to a rectangle when stopped. Zero keeps it rectangular.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementBubbleShapeSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:bubble:shape_speed", "Window speed in logical pixels per second at which bubble shape contraction reaches its full amount. Slower windows contract proportionally less.", 1200.0F,
        Config::Values::SFloatValueOptions{.min = 100.0F, .max = 6000.0F});
    values.m_movementBubbleTransparency = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:bubble:transparency", "Window transparency while in bubble form. Zero keeps the window imagery opaque; one shows only the refracted desktop through the film. Highlights and iridescence remain visible. The normal window is unchanged when movement settles.", 0.88F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSmokeStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:strength", "How readily motion unravels the window into flowing smoke. Zero disables movement transformation without affecting opening or closing.", 0.85F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_movementSmokeTurbulence = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:turbulence", "Strength of evolving smoke folds and smaller curling structures. Shared with opening and closing unless overridden.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSmokeTrail = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:trail", "Amount of content-coloured smoke shed into the wake. Zero stops new wake emission; existing wisps still dissipate over their remaining lifetime.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSmokeDragFollow = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:drag_follow", "How closely the main smoke core follows the dragged window: 0 preserves its original lag and coast, 1 tracks the pointer exactly, and intermediate values partially attach the core. The loose tail remains unaffected.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSmokeTrailLength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:trail_length", "Maximum wake distance beyond the smoke body in logical pixels. Zero uses the automatic reach.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 2400.0F});
    values.m_movementSmokeTrailLifetime = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:trail_lifetime", "Lifetime of emitted wake smoke in milliseconds. Zero restores the original amount-based lifetime (550–1150 ms, 940 ms with the default trail amount). Positive overrides have a 100 ms minimum; configured values from 1 to 99 ms are clamped to 100 ms.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 4000.0F});
    values.m_movementSmokeTrailThickness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:trail_thickness", "Wake width multiplier, not the solid main smoke body. One is neutral.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.25F, .max = 2.5F});
    values.m_movementSmokeReform = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:reform", "Time scale in seconds for moving smoke to condense back into the exact window after movement stops.", 0.48F,
        Config::Values::SFloatValueOptions{.min = 0.15F, .max = 1.5F});
    values.m_movementSmokeSettleSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:settle_speed", "Speed multiplier for the smoke body's expansion and condensation after movement or resizing stops. One preserves the reform time scale; higher values reform faster and lower values linger. Opening and closing durations are unchanged.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.1F, .max = 2.0F});
    values.m_movementSmokeMass = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:mass", "Size of the moving smoke body as a fraction of the window's width and height. One keeps roughly the window's footprint; smaller values contract a moving window into a denser mass that expands back when it stops.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.15F, .max = 1.0F});
    values.m_movementSmokeRoundness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:smoke:roundness", "Shape of the moving smoke body, independent of its size. Zero keeps the window's rectangular proportions; higher values round its corners and even out its proportions toward a turbulent, roughly circular mass.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:window:open:enabled", "Animate windows as they open using the selected style. When false, windows open without this effect; movement is unaffected.", true);
    values.m_openLiquidDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:liquid:duration", "Time in milliseconds for the liquid pop-out.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_openLiquidStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:liquid:strength", "Liquid pop-out size and deformation strength when opening. Zero removes the pop-out.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_openLiquidColor = makeShared<Config::Values::CColorValue>("plugin:hyprimate:window:open:options:liquid:color", "Colour of the liquid at the opening edge, in ARGB. Zero alpha draws no liquid.", 0xff000000);
    values.m_openDefaultAnimationEdge = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:liquid:default_animation_edge", "Preferred opening edge for the first window on a workspace. Later windows use their placement. Invalid edges fall back to a valid edge.", "bottom",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "bottom" || value == "top" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:liquid:default_animation_edge must be one of: bottom, top, left, right"));
        },});
    values.m_edgeBounds = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:liquid:edge_bounds", "Choose monitor edges (screen) or edges inside reserved areas (work_area) for the liquid pop-out.", "screen",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "screen" || value == "work_area") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:liquid:edge_bounds must be one of: screen, work_area"));
        },});
    values.m_emergeDrag = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:liquid:emerge_drag", "Share of liquid pop-out travel applied as drag to the movement field. Zero makes the pop rigid.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openElasticity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:liquid:elasticity", "Liquid pop-out stiffness and recovery speed. Higher values snap back faster.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openMomentum = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:liquid:momentum", "Liquid pop-out inertia. Zero settles without overshoot.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:duration", "Time in milliseconds for the paper ball to uncrumple into the window.", 600.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_openPaperCrumpliness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:crumpliness", "Strength of the distributed pressure that gathers and creases the sheet.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperShading = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:shading", "Strength of lighting and fold shadows; zero leaves the geometry unlit.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:variation", "Variation in grip locations, loads and timing. Zero retains subtle differences between events.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperFoldDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:fold_density", "Frequency of folds: low gives broad flaps, high gives more closely spaced creases.", 0.55F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperStiffness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:paper_stiffness", "Elastic resistance to bending before the sheet takes a crease. Overrides shared stiffness when explicitly set.", 0.72F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperCrumpleSize = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:crumple_size", "Target extent of the packed paper relative to the window material length scale.", 0.18F,
        Config::Values::SFloatValueOptions{.min = 0.1F, .max = 0.45F});
    values.m_openPaperCrumpleOrigin = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:paper:crumple_origin", "Where the packed sheet converges. Cursor samples the nearest point on the capture at event time.", "center",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "center" || value == "random" || value == "cursor" || value == "left" || value == "right" || value == "top" || value == "bottom") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:paper:crumple_origin must be one of: center, random, cursor, left, right, top, bottom"));
        },});
    values.m_openPaperForceDirection = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:paper:force_direction", "Gather around the origin, press along a preferred axis, or twist the grips.", "inward",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "inward" || value == "directional" || value == "rotational") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:paper:force_direction must be one of: inward, directional, rotational"));
        },});
    values.m_openPaperRotationIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:rotation_intensity", "Rotational impulse applied by the grips.", 0.22F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperUnfoldTension = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:unfold_tension", "Strength of pulling forces as folded sections emerge.", 0.62F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperSettling = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:settling", "Higher values damp residual motion and increase friction between folded layers. Overrides shared settle_damping when explicitly set.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperCreasePersistence = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:paper:crease_persistence", "How long released bends retain their preferred angle before flattening.", 0.62F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openPaperSimulationQuality = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:paper:simulation_quality", "Physical mesh detail. Adaptive selects detail from capture size and active workload.", "adaptive",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "adaptive" || value == "low" || value == "medium" || value == "high") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:paper:simulation_quality must be one of: adaptive, low, medium, high"));
        },});
    values.m_openClothDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:cloth:duration", "Time in milliseconds for the gathered curtain to spread from its top edge and settle flat.", 1000.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_openClothDefaultAnimationEdge = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:cloth:default_animation_edge", "Opening origin for the first cloth window on a workspace, used when the closing side is origin. Later windows use their placement. Cloth still opens in place.", "bottom",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "bottom" || value == "top" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:cloth:default_animation_edge must be one of: bottom, top, left, right"));
        },});
    values.m_openClothEdgeBounds = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:cloth:edge_bounds", "Choose monitor edges (screen) or edges inside reserved areas (work_area) for the cloth opening origin.", "screen",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "screen" || value == "work_area") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:cloth:edge_bounds must be one of: screen, work_area"));
        },});
    values.m_openClothFlutter = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:cloth:flutter", "Cloth gust strength when opening. Zero removes flutter, but gravity still moves the sheet.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openClothShading = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:cloth:shading", "Light and shadow on cloth folds when opening. Zero draws them unlit.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openClothShine = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:cloth:shine", "Highlight strength on cloth folds when opening. Zero removes highlights.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openClothVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:cloth:variation", "Variation in cloth opening motion between windows. Zero gives every window the same motion.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSwarmDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:duration", "Time in milliseconds for the swarm to arrive and form the window.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_openSwarmLoneWindowSides = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:swarm:lone_window_sides", "Screen sides swarm creatures enter from for the first window on a workspace. Later windows use the side where they appear (see Edge choice).", "all",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (detail::parse_lone_window_sides(value)) {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:swarm:lone_window_sides must be a comma-separated list of: top, bottom, left, right, all"));
        },});
    values.m_openBubbleDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:bubble:duration", "Time in milliseconds for the opening: small bubbles surface and interact for about the first half, the window bubble grows out of them, then inflates into the window's shape and settles.", 1000.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_openBubbleCount = makeShared<Config::Values::CIntValue>("plugin:hyprimate:window:open:options:bubble:count", "Opening-only budget for simultaneously living small bubbles, not a fixed population. Slots are reused as bubbles merge and burst. Zero disables them and the window bubble forms on its own.", 140,
        Config::Values::SIntValueOptions{.min = 0, .max = 160});
    values.m_openBubbleBirthRate = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:bubble:birth_rate", "Opening-only multiplier for how often bubbles surface. Higher values produce a busier raft within the count budget.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.25F, .max = 2.0F});
    values.m_openBubbleSizeVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:bubble:size_variation", "Opening-only spread of surfacing bubble sizes. Coalescence still grows larger bubbles at zero.", 0.75F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openBubbleGrowthVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:bubble:growth_variation", "Opening-only variation in air supply and in how quickly bubbles surface. Even at zero, coalescence and boundary pressure change inflation speed.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openBubbleInteraction = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:bubble:interaction", "Opening-only strength of bubble contact and adhesion forces, including their push on the window bubble.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.25F, .max = 2.0F});
    values.m_openBubbleDamping = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:bubble:damping", "Opening-only drag of the liquid on bubbles. Higher values calm the raft faster; lower values retain more inertia.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.5F, .max = 2.0F});
    values.m_openBubbleBuoyancy = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:bubble:buoyancy", "Opening-only upward drift of small bubbles. Zero removes it, not the liquid's flow or contact-driven motion.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 2.0F});
    values.m_openBubbleLinger = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:bubble:linger", "Opening-only lifetime of small bubbles before their films drain and burst. Higher values keep bubbles around the landed window longer; all burst within duration.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSmokeDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:smoke:duration", "Time in milliseconds for smoke to gather, grow into a mass and condense into the window's full-resolution contents.", 1400.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_openSmokeTurbulence = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:smoke:turbulence", "Strength of rolling folds and curling filaments during smoke materialization.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSmokeDirection = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:smoke:direction", "Where opening smoke comes from. Inside grows the window from a faint wisp at a random point within it; an edge streams the smoke in from that screen edge. An edge the window does not face falls back to the opening-edge choice of liquid. Invalid names are rejected.", "inside",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "inside" || value == "top" || value == "bottom" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:open:options:smoke:direction must be one of: inside, top, bottom, left, right"));
        },});
    values.m_closeEnabled = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:window:close:enabled", "Animate windows as they close using the selected style. When false, windows close natively; movement is unaffected.", true);
    values.m_closeLiquidDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:liquid:duration", "Time in milliseconds for the window to drain to the screen edge.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_closeLiquidStrength = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:liquid:strength", "Liquid drain deformation strength when closing. Zero closes the window natively.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.5F});
    values.m_closeLiquidColor = makeShared<Config::Values::CColorValue>("plugin:hyprimate:window:close:options:liquid:color", "Colour of the liquid at the closing edge, in ARGB. Zero alpha draws no liquid.", 0xff000000);
    values.m_closeDefaultAnimationEdge = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:liquid:default_animation_edge", "Preferred closing edge for the last window on a workspace when side is auto. Other windows use their placement. Invalid edges fall back to a valid edge.", "bottom",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "bottom" || value == "top" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:liquid:default_animation_edge must be one of: bottom, top, left, right"));
        },});
    values.m_closeSide = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:liquid:side", "Liquid exit edge: the side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal edge selection.", "auto",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "auto" || value == "nearest" || value == "origin" || value == "top" || value == "bottom" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:liquid:side must be one of: auto, nearest, origin, top, bottom, left, right"));
        },});
    values.m_closeEdgeBounds = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:liquid:edge_bounds", "Choose monitor edges (screen) or edges inside reserved areas (work_area) for the liquid exit.", "screen",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "screen" || value == "work_area") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:liquid:edge_bounds must be one of: screen, work_area"));
        },});
    values.m_closeSplash = makeShared<Config::Values::CBoolValue>("plugin:hyprimate:window:close:options:liquid:splash", "Show a small edge-liquid splash where a window closes. When false, it closes without liquid.", true);
    values.m_closeElasticity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:liquid:elasticity", "Liquid drain stiffness and recovery speed. Higher values snap back faster.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeMomentum = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:liquid:momentum", "Liquid closing dive acceleration. Higher values make the dive advance faster.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closePaperDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:duration", "Time in milliseconds for the window to crumple into a paper ball and vanish.", 500.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_closePaperCrumpliness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:crumpliness", "Strength of the distributed pressure that gathers and creases the sheet.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closePaperShading = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:shading", "Strength of lighting and fold shadows; zero leaves the geometry unlit.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closePaperVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:variation", "Variation in grip locations, loads and timing. Zero retains subtle differences between events.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closePaperFoldDensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:fold_density", "Frequency of folds: low gives broad flaps, high gives more closely spaced creases.", 0.55F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closePaperStiffness = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:paper_stiffness", "Elastic resistance to bending before the sheet takes a crease. Overrides shared stiffness when explicitly set.", 0.72F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closePaperCrumpleSize = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:crumple_size", "Target extent of the packed paper relative to the window material length scale.", 0.18F,
        Config::Values::SFloatValueOptions{.min = 0.1F, .max = 0.45F});
    values.m_closePaperCrumpleOrigin = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:paper:crumple_origin", "Where the packed sheet converges. Cursor samples the nearest point on the capture at event time.", "center",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "center" || value == "random" || value == "cursor" || value == "left" || value == "right" || value == "top" || value == "bottom") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:paper:crumple_origin must be one of: center, random, cursor, left, right, top, bottom"));
        },});
    values.m_closePaperForceDirection = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:paper:force_direction", "Gather around the origin, press along a preferred axis, or twist the grips.", "inward",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "inward" || value == "directional" || value == "rotational") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:paper:force_direction must be one of: inward, directional, rotational"));
        },});
    values.m_closePaperRotationIntensity = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:rotation_intensity", "Rotational impulse applied by the grips.", 0.22F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closePaperSettling = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:paper:settling", "Higher values damp residual motion and increase friction between folded layers. Overrides shared settle_damping when explicitly set.", 0.45F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closePaperSimulationQuality = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:paper:simulation_quality", "Physical mesh detail. Adaptive selects detail from capture size and active workload.", "adaptive",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "adaptive" || value == "low" || value == "medium" || value == "high") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:paper:simulation_quality must be one of: adaptive, low, medium, high"));
        },});
    values.m_closeClothDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:cloth:duration", "Time in milliseconds for the top rod to carry the curtain beyond the selected screen edge.", 900.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_closeClothDefaultAnimationEdge = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:cloth:default_animation_edge", "Preferred cloth closing edge for the last window on a workspace when side is auto. Invalid edges fall back to a valid edge.", "bottom",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "bottom" || value == "top" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:cloth:default_animation_edge must be one of: bottom, top, left, right"));
        },});
    values.m_closeClothSide = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:cloth:side", "Cloth exit edge: the side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal edge selection.", "auto",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "auto" || value == "nearest" || value == "origin" || value == "top" || value == "bottom" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:cloth:side must be one of: auto, nearest, origin, top, bottom, left, right"));
        },});
    values.m_closeClothEdgeBounds = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:cloth:edge_bounds", "Choose monitor edges (screen) or edges inside reserved areas (work_area) for cloth edge selection. Cloth travels beyond the actual screen before disappearing.", "screen",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "screen" || value == "work_area") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:cloth:edge_bounds must be one of: screen, work_area"));
        },});
    values.m_closeClothFlutter = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:cloth:flutter", "Cloth gust strength when closing. Zero removes flutter, but gravity still moves the sheet.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeClothShading = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:cloth:shading", "Light and shadow on cloth folds when closing. Zero draws them unlit.", 0.7F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeClothShine = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:cloth:shine", "Highlight strength on cloth folds when closing. Zero removes highlights.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeClothVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:cloth:variation", "Variation in cloth closing motion between windows. Zero gives every window the same motion.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeSwarmDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:duration", "Time in milliseconds for the swarm to break up and leave the window.", 0.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_closeSwarmLoneWindowSides = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:swarm:lone_window_sides", "Screen sides swarm creatures leave through for the last window on a workspace. Otherwise they use the side where the window sits (see Edge choice).", "all",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (detail::parse_lone_window_sides(value)) {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:swarm:lone_window_sides must be a comma-separated list of: top, bottom, left, right, all"));
        },});
    values.m_closeBubbleDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:bubble:duration", "Time in milliseconds for the bubble film to thin, tear, retract into strands and droplets, and for the droplets to evaporate.", 420.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_closeSmokeDuration = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:smoke:duration", "Time in milliseconds for the window to gather into a smoke mass and leave. Trailing wisps may decay briefly afterward.", 1600.0F,
        Config::Values::SFloatValueOptions{.min = 0.0F});
    values.m_closeSmokeTurbulence = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:smoke:turbulence", "Strength of rolling folds and curling filaments during smoke disintegration.", 0.65F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeSmokeDirection = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:smoke:direction", "Where closing smoke leaves. Inside collapses the window into a swirling mass at a random point within it that dissipates; an edge gathers a mass on the opposite side, rears and turns it toward that screen edge and pulls the rest of the window after it. An edge the window does not face falls back to the closing-edge choice of liquid. Invalid names are rejected.", "bottom",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "inside" || value == "top" || value == "bottom" || value == "left" || value == "right") {
                return {};
            }
            return std::unexpected(std::string("plugin:hyprimate:window:close:options:smoke:direction must be one of: inside, top, bottom, left, right"));
        },});
    values.m_movementSwarmSprite = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:movement:options:swarm:sprite", "Swarm sprite name in the sprites directory, with or without .png, or an explicit PNG path. The sheet has four columns and two rows of frames; transparency defines the silhouette.", "spider");
    values.m_movementSwarmCount = makeShared<Config::Values::CIntValue>("plugin:hyprimate:window:movement:options:swarm:count", "Number of swarm creatures per window. Each carries a piece of the window, so fewer creatures mean larger pieces.", 250,
        Config::Values::SIntValueOptions{.min = 1, .max = 400});
    values.m_movementSwarmSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:swarm:speed", "Swarm creature travel speed in logical pixels per second.", 1400.0F,
        Config::Values::SFloatValueOptions{.min = 100.0F, .max = 6000.0F});
    values.m_movementSwarmStagger = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:swarm:stagger", "Time in seconds over which swarm creatures arrive or leave in random order when a window opens or closes. Movement releases them over half this time. Zero makes them arrive or leave together.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 3.0F});
    values.m_movementSwarmCohesion = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:swarm:cohesion", "How strongly nearby swarm creatures travel together. Zero lets each find its own way.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSwarmSeparation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:swarm:separation", "How much swarm creatures avoid and bump off each other while travelling. Zero lets them pass through each other.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSwarmAgility = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:swarm:agility", "Swarm acceleration and grip. Higher values hold formation during faster movement and turn more sharply; lower values break apart sooner and trail farther.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSwarmSettling = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:swarm:settling", "How quickly swarm creatures regroup and merge back into the window. Lower values make them mill about longer.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSwarmWander = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:swarm:wander", "How much swarm creatures wander and hesitate while travelling. Zero makes them follow their routes steadily.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_movementSwarmVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:movement:options:swarm:variation", "Variation in each swarm creature's speed, grip and route curvature. Zero makes them travel straight and alike.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSwarmSprite = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:open:options:swarm:sprite", "Swarm sprite name in the sprites directory, with or without .png, or an explicit PNG path. The sheet has four columns and two rows of frames; transparency defines the silhouette.", "spider");
    values.m_openSwarmCount = makeShared<Config::Values::CIntValue>("plugin:hyprimate:window:open:options:swarm:count", "Number of swarm creatures per window. Each carries a piece of the window, so fewer creatures mean larger pieces.", 250,
        Config::Values::SIntValueOptions{.min = 1, .max = 400});
    values.m_openSwarmSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:speed", "Swarm creature travel speed in logical pixels per second.", 1400.0F,
        Config::Values::SFloatValueOptions{.min = 100.0F, .max = 6000.0F});
    values.m_openSwarmStagger = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:stagger", "Time in seconds over which swarm creatures arrive or leave in random order when a window opens or closes. Movement releases them over half this time. Zero makes them arrive or leave together.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 3.0F});
    values.m_openSwarmCohesion = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:cohesion", "How strongly nearby swarm creatures travel together. Zero lets each find its own way.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSwarmSeparation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:separation", "How much swarm creatures avoid and bump off each other while travelling. Zero lets them pass through each other.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSwarmAgility = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:agility", "Swarm acceleration and grip. Higher values hold formation during faster movement and turn more sharply; lower values break apart sooner and trail farther.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSwarmSettling = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:settling", "How quickly swarm creatures regroup and merge back into the window. Lower values make them mill about longer.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSwarmWander = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:wander", "How much swarm creatures wander and hesitate while travelling. Zero makes them follow their routes steadily.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_openSwarmVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:open:options:swarm:variation", "Variation in each swarm creature's speed, grip and route curvature. Zero makes them travel straight and alike.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeSwarmSprite = makeShared<Config::Values::CStringValue>("plugin:hyprimate:window:close:options:swarm:sprite", "Swarm sprite name in the sprites directory, with or without .png, or an explicit PNG path. The sheet has four columns and two rows of frames; transparency defines the silhouette.", "spider");
    values.m_closeSwarmCount = makeShared<Config::Values::CIntValue>("plugin:hyprimate:window:close:options:swarm:count", "Number of swarm creatures per window. Each carries a piece of the window, so fewer creatures mean larger pieces.", 250,
        Config::Values::SIntValueOptions{.min = 1, .max = 400});
    values.m_closeSwarmSpeed = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:speed", "Swarm creature travel speed in logical pixels per second.", 1400.0F,
        Config::Values::SFloatValueOptions{.min = 100.0F, .max = 6000.0F});
    values.m_closeSwarmStagger = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:stagger", "Time in seconds over which swarm creatures arrive or leave in random order when a window opens or closes. Movement releases them over half this time. Zero makes them arrive or leave together.", 0.6F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 3.0F});
    values.m_closeSwarmCohesion = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:cohesion", "How strongly nearby swarm creatures travel together. Zero lets each find its own way.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeSwarmSeparation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:separation", "How much swarm creatures avoid and bump off each other while travelling. Zero lets them pass through each other.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeSwarmAgility = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:agility", "Swarm acceleration and grip. Higher values hold formation during faster movement and turn more sharply; lower values break apart sooner and trail farther.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeSwarmSettling = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:settling", "How quickly swarm creatures regroup and merge back into the window. Lower values make them mill about longer.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeSwarmWander = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:wander", "How much swarm creatures wander and hesitate while travelling. Zero makes them follow their routes steadily.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    values.m_closeSwarmVariation = makeShared<Config::Values::CFloatValue>("plugin:hyprimate:window:close:options:swarm:variation", "Variation in each swarm creature's speed, grip and route curvature. Zero makes them travel straight and alike.", 0.5F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});
    if (!HyprlandAPI::addConfigValueV2(handle, values.m_globalConstructionDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBurnDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidViscosity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidElasticity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidMomentum)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothSpringiness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothGravity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothFlutter)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothShading)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothShine)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBubbleStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBubbleMicrobubbles)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBubbleTrail)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBubbleShape)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBubbleRoundness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBubbleShapeSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBubbleTransparency)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalSmokeStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalSmokeTurbulence)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalSmokeTrail)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidColor)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidDefaultAnimationEdge)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidEdgeBounds)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidEmergeDrag)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperCrumpliness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperShading)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperFoldDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperPaperStiffness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperCrumpleSize)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperCrumpleOrigin)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperForceDirection)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperRotationIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperSettling)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalPaperSimulationQuality)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothDefaultAnimationEdge)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothEdgeBounds)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalSwarmDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalSwarmLoneWindowSides)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalBubbleDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalSmokeDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalSmokeDirection)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidSide)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalLiquidSplash)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_globalClothSide)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceStyle)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceEffect)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceEffectParameters)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceEffectDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceRotationDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceRotationCurve)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceRotationGapFillStyle)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceRotationGapFillColor)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceRotationGapFillBlur)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceRotationKeepOverlaysStatic)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopWindowMode)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopOverlayMode)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopPlanetLayout)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopCurve)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopLiveBoundary)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopSamplingQuality)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopPlanetSize)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopPlanetSeparation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopSolarVerticalSpread)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopSolarBackgroundScale)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopPlanetCoverage)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopIndirectBrightness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopElevation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopFade)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopRotationEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopRotationLimit)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopStarsEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopStarDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopStarParallax)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopAtmosphereEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopAtmosphereIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopAtmosphereThickness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopLightingEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopLightDirectionX)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopLightDirectionY)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopLightDirectionZ)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopNightBrightness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopCloudsEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopCloudCoverage)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopCloudOpacity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopCloudSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopDepthEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopDepthStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspacePlanetHopDepthSource)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceCubeVariant)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceCubeDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceCubeCurve)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceCubeSolidCubeTilt)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceCubeSolidZoom)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceCubeInsideOutCubeTilt)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceCubeInsideOutZoom)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaVariant)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaWindowBehavior)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaViscosity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCoolingRate)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCrustAmount)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaGlowIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaDetailPreservation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaTrailSmear)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaEdgeDrip)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaAshAmount)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCoolingFlowLeadingBulge)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCoolingFlowTrailingStretch)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCoolingFlowMoltenTurbulence)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaBasaltFlowCrustThickness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaBasaltFlowFissureWidth)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaBasaltFlowEmberVisibility)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaBasaltFlowMassDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaPahoehoeRopeStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaPahoehoeFoldScale)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaPahoehoeRippleSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaPahoehoeSurfaceRoll)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCrustPlatesPlateSize)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCrustPlatesPlateRigidity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCrustPlatesSeamGlow)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCrustPlatesPlateDrift)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCrustPlatesPlateCompression)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceLavaCrustPlatesSubmeltVisibility)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_wallpaperEffect)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_wallpaperEffectParameters)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_wallpaperEffectDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_wallpaperSourceNamespace)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_enabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_duringWorkspaceSwitch)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_style)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openEffect)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeEffect)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openEffectParameters)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeEffectParameters)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openEffectDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeEffectDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_moveEffect)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_moveEffectParameters)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_moveEffectDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_resizeEffect)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_resizeEffectParameters)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_resizeEffectDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openConstructionDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeConstructionDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_constructionWorkerSize)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnFragmentDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnEmberDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnWindDirection)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnWindStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnGravityStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnScorchLifetime)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnHealingDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnMovementSmokeDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBurnDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeBurnDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnClosingStyle)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnDebrisDirection)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_burnCurlIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperStiffness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperInertia)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperSettleDamping)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperMoveStyle)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperFlutterAirResistance)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperFlutterIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperFlutterFrequency)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperFlutterLift)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperFlutterTorsion)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperGlideFriction)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperGlideCornerLift)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperGlideEdgeCurl)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperGlideShadowSeparation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperResizeFoldIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperResizeCurlIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_paperResizeCreaseDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmSprite)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmCount)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmStagger)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmCohesion)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmSeparation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmAgility)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmSettling)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmWander)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_swarmVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_smokeColor)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_smokeDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_smokeVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_workspaceMovement)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementConstructionVariant)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementLiquidStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_viscosity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_elasticity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_momentum)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_variation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementClothStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementClothSpringiness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementClothGravity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementClothFlutter)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementClothShading)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementClothShine)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementClothVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementBubbleStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementBubbleMicrobubbles)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementBubbleTrail)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementBubbleShape)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementBubbleRoundness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementBubbleShapeSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementBubbleTransparency)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeTurbulence)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeTrail)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeDragFollow)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeTrailLength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeTrailLifetime)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeTrailThickness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeReform)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeSettleSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeMass)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSmokeRoundness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openLiquidDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openLiquidStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openLiquidColor)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openDefaultAnimationEdge)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_edgeBounds)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_emergeDrag)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openElasticity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openMomentum)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperCrumpliness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperShading)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperFoldDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperStiffness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperCrumpleSize)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperCrumpleOrigin)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperForceDirection)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperRotationIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperUnfoldTension)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperSettling)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperCreasePersistence)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openPaperSimulationQuality)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openClothDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openClothDefaultAnimationEdge)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openClothEdgeBounds)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openClothFlutter)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openClothShading)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openClothShine)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openClothVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmLoneWindowSides)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleCount)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleBirthRate)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleSizeVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleGrowthVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleInteraction)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleDamping)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleBuoyancy)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openBubbleLinger)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSmokeDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSmokeTurbulence)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSmokeDirection)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeEnabled)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeLiquidDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeLiquidStrength)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeLiquidColor)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeDefaultAnimationEdge)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSide)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeEdgeBounds)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSplash)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeElasticity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeMomentum)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperCrumpliness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperShading)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperFoldDensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperStiffness)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperCrumpleSize)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperCrumpleOrigin)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperForceDirection)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperRotationIntensity)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperSettling)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closePaperSimulationQuality)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeClothDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeClothDefaultAnimationEdge)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeClothSide)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeClothEdgeBounds)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeClothFlutter)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeClothShading)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeClothShine)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeClothVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmLoneWindowSides)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeBubbleDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSmokeDuration)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSmokeTurbulence)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSmokeDirection)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmSprite)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmCount)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmStagger)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmCohesion)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmSeparation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmAgility)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmSettling)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmWander)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_movementSwarmVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmSprite)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmCount)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmStagger)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmCohesion)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmSeparation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmAgility)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmSettling)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmWander)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_openSwarmVariation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmSprite)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmCount)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmSpeed)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmStagger)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmCohesion)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmSeparation)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmAgility)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmSettling)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmWander)
     || !HyprlandAPI::addConfigValueV2(handle, values.m_closeSwarmVariation)) {
        throw std::runtime_error("[hyprimate] failed to register configuration values");
    }
    values.m_globalConstructionDurationSet.bind("plugin:hyprimate:window:options:construction:duration");
    values.m_globalBurnDurationSet.bind("plugin:hyprimate:window:options:burn:duration");
    values.m_globalLiquidStrengthSet.bind("plugin:hyprimate:window:options:liquid:strength");
    values.m_globalLiquidViscositySet.bind("plugin:hyprimate:window:options:liquid:viscosity");
    values.m_globalLiquidElasticitySet.bind("plugin:hyprimate:window:options:liquid:elasticity");
    values.m_globalLiquidMomentumSet.bind("plugin:hyprimate:window:options:liquid:momentum");
    values.m_globalLiquidVariationSet.bind("plugin:hyprimate:window:options:liquid:variation");
    values.m_globalClothStrengthSet.bind("plugin:hyprimate:window:options:cloth:strength");
    values.m_globalClothSpringinessSet.bind("plugin:hyprimate:window:options:cloth:springiness");
    values.m_globalClothGravitySet.bind("plugin:hyprimate:window:options:cloth:gravity");
    values.m_globalClothFlutterSet.bind("plugin:hyprimate:window:options:cloth:flutter");
    values.m_globalClothShadingSet.bind("plugin:hyprimate:window:options:cloth:shading");
    values.m_globalClothShineSet.bind("plugin:hyprimate:window:options:cloth:shine");
    values.m_globalClothVariationSet.bind("plugin:hyprimate:window:options:cloth:variation");
    values.m_globalBubbleStrengthSet.bind("plugin:hyprimate:window:options:bubble:strength");
    values.m_globalBubbleMicrobubblesSet.bind("plugin:hyprimate:window:options:bubble:microbubbles");
    values.m_globalBubbleTrailSet.bind("plugin:hyprimate:window:options:bubble:trail");
    values.m_globalBubbleShapeSet.bind("plugin:hyprimate:window:options:bubble:shape");
    values.m_globalBubbleRoundnessSet.bind("plugin:hyprimate:window:options:bubble:roundness");
    values.m_globalBubbleShapeSpeedSet.bind("plugin:hyprimate:window:options:bubble:shape_speed");
    values.m_globalBubbleTransparencySet.bind("plugin:hyprimate:window:options:bubble:transparency");
    values.m_globalSmokeStrengthSet.bind("plugin:hyprimate:window:options:smoke:strength");
    values.m_globalSmokeTurbulenceSet.bind("plugin:hyprimate:window:options:smoke:turbulence");
    values.m_globalSmokeTrailSet.bind("plugin:hyprimate:window:options:smoke:trail");
    values.m_globalLiquidDurationSet.bind("plugin:hyprimate:window:options:liquid:duration");
    values.m_globalLiquidColorSet.bind("plugin:hyprimate:window:options:liquid:color");
    values.m_globalLiquidDefaultAnimationEdgeSet.bind("plugin:hyprimate:window:options:liquid:default_animation_edge");
    values.m_globalLiquidEdgeBoundsSet.bind("plugin:hyprimate:window:options:liquid:edge_bounds");
    values.m_globalLiquidEmergeDragSet.bind("plugin:hyprimate:window:options:liquid:emerge_drag");
    values.m_globalPaperDurationSet.bind("plugin:hyprimate:window:options:paper:duration");
    values.m_globalPaperCrumplinessSet.bind("plugin:hyprimate:window:options:paper:crumpliness");
    values.m_globalPaperShadingSet.bind("plugin:hyprimate:window:options:paper:shading");
    values.m_globalPaperVariationSet.bind("plugin:hyprimate:window:options:paper:variation");
    values.m_globalPaperFoldDensitySet.bind("plugin:hyprimate:window:options:paper:fold_density");
    values.m_globalPaperPaperStiffnessSet.bind("plugin:hyprimate:window:options:paper:paper_stiffness");
    values.m_globalPaperCrumpleSizeSet.bind("plugin:hyprimate:window:options:paper:crumple_size");
    values.m_globalPaperCrumpleOriginSet.bind("plugin:hyprimate:window:options:paper:crumple_origin");
    values.m_globalPaperForceDirectionSet.bind("plugin:hyprimate:window:options:paper:force_direction");
    values.m_globalPaperRotationIntensitySet.bind("plugin:hyprimate:window:options:paper:rotation_intensity");
    values.m_globalPaperSettlingSet.bind("plugin:hyprimate:window:options:paper:settling");
    values.m_globalPaperSimulationQualitySet.bind("plugin:hyprimate:window:options:paper:simulation_quality");
    values.m_globalClothDurationSet.bind("plugin:hyprimate:window:options:cloth:duration");
    values.m_globalClothDefaultAnimationEdgeSet.bind("plugin:hyprimate:window:options:cloth:default_animation_edge");
    values.m_globalClothEdgeBoundsSet.bind("plugin:hyprimate:window:options:cloth:edge_bounds");
    values.m_globalSwarmDurationSet.bind("plugin:hyprimate:window:options:swarm:duration");
    values.m_globalSwarmLoneWindowSidesSet.bind("plugin:hyprimate:window:options:swarm:lone_window_sides");
    values.m_globalBubbleDurationSet.bind("plugin:hyprimate:window:options:bubble:duration");
    values.m_globalSmokeDurationSet.bind("plugin:hyprimate:window:options:smoke:duration");
    values.m_globalSmokeDirectionSet.bind("plugin:hyprimate:window:options:smoke:direction");
    values.m_globalLiquidSideSet.bind("plugin:hyprimate:window:options:liquid:side");
    values.m_globalLiquidSplashSet.bind("plugin:hyprimate:window:options:liquid:splash");
    values.m_globalClothSideSet.bind("plugin:hyprimate:window:options:cloth:side");
    values.m_openConstructionDurationSet.bind("plugin:hyprimate:window:open:options:construction:duration");
    values.m_closeConstructionDurationSet.bind("plugin:hyprimate:window:close:options:construction:duration");
    values.m_openBurnDurationSet.bind("plugin:hyprimate:window:open:options:burn:duration");
    values.m_closeBurnDurationSet.bind("plugin:hyprimate:window:close:options:burn:duration");
    values.m_paperStiffnessSet.bind("plugin:hyprimate:window:options:paper:stiffness");
    values.m_paperSettleDampingSet.bind("plugin:hyprimate:window:options:paper:settle_damping");
    values.m_movementLiquidStrengthSet.bind("plugin:hyprimate:window:movement:options:liquid:strength");
    values.m_viscositySet.bind("plugin:hyprimate:window:movement:options:liquid:viscosity");
    values.m_elasticitySet.bind("plugin:hyprimate:window:movement:options:liquid:elasticity");
    values.m_momentumSet.bind("plugin:hyprimate:window:movement:options:liquid:momentum");
    values.m_variationSet.bind("plugin:hyprimate:window:movement:options:liquid:variation");
    values.m_movementClothStrengthSet.bind("plugin:hyprimate:window:movement:options:cloth:strength");
    values.m_movementClothSpringinessSet.bind("plugin:hyprimate:window:movement:options:cloth:springiness");
    values.m_movementClothGravitySet.bind("plugin:hyprimate:window:movement:options:cloth:gravity");
    values.m_movementClothFlutterSet.bind("plugin:hyprimate:window:movement:options:cloth:flutter");
    values.m_movementClothShadingSet.bind("plugin:hyprimate:window:movement:options:cloth:shading");
    values.m_movementClothShineSet.bind("plugin:hyprimate:window:movement:options:cloth:shine");
    values.m_movementClothVariationSet.bind("plugin:hyprimate:window:movement:options:cloth:variation");
    values.m_movementBubbleStrengthSet.bind("plugin:hyprimate:window:movement:options:bubble:strength");
    values.m_movementBubbleMicrobubblesSet.bind("plugin:hyprimate:window:movement:options:bubble:microbubbles");
    values.m_movementBubbleTrailSet.bind("plugin:hyprimate:window:movement:options:bubble:trail");
    values.m_movementBubbleShapeSet.bind("plugin:hyprimate:window:movement:options:bubble:shape");
    values.m_movementBubbleRoundnessSet.bind("plugin:hyprimate:window:movement:options:bubble:roundness");
    values.m_movementBubbleShapeSpeedSet.bind("plugin:hyprimate:window:movement:options:bubble:shape_speed");
    values.m_movementBubbleTransparencySet.bind("plugin:hyprimate:window:movement:options:bubble:transparency");
    values.m_movementSmokeStrengthSet.bind("plugin:hyprimate:window:movement:options:smoke:strength");
    values.m_movementSmokeTurbulenceSet.bind("plugin:hyprimate:window:movement:options:smoke:turbulence");
    values.m_movementSmokeTrailSet.bind("plugin:hyprimate:window:movement:options:smoke:trail");
    values.m_openLiquidDurationSet.bind("plugin:hyprimate:window:open:options:liquid:duration");
    values.m_openLiquidStrengthSet.bind("plugin:hyprimate:window:open:options:liquid:strength");
    values.m_openLiquidColorSet.bind("plugin:hyprimate:window:open:options:liquid:color");
    values.m_openDefaultAnimationEdgeSet.bind("plugin:hyprimate:window:open:options:liquid:default_animation_edge");
    values.m_edgeBoundsSet.bind("plugin:hyprimate:window:open:options:liquid:edge_bounds");
    values.m_emergeDragSet.bind("plugin:hyprimate:window:open:options:liquid:emerge_drag");
    values.m_openElasticitySet.bind("plugin:hyprimate:window:open:options:liquid:elasticity");
    values.m_openMomentumSet.bind("plugin:hyprimate:window:open:options:liquid:momentum");
    values.m_openPaperDurationSet.bind("plugin:hyprimate:window:open:options:paper:duration");
    values.m_openPaperCrumplinessSet.bind("plugin:hyprimate:window:open:options:paper:crumpliness");
    values.m_openPaperShadingSet.bind("plugin:hyprimate:window:open:options:paper:shading");
    values.m_openPaperVariationSet.bind("plugin:hyprimate:window:open:options:paper:variation");
    values.m_openPaperFoldDensitySet.bind("plugin:hyprimate:window:open:options:paper:fold_density");
    values.m_openPaperStiffnessSet.bind("plugin:hyprimate:window:open:options:paper:paper_stiffness");
    values.m_openPaperCrumpleSizeSet.bind("plugin:hyprimate:window:open:options:paper:crumple_size");
    values.m_openPaperCrumpleOriginSet.bind("plugin:hyprimate:window:open:options:paper:crumple_origin");
    values.m_openPaperForceDirectionSet.bind("plugin:hyprimate:window:open:options:paper:force_direction");
    values.m_openPaperRotationIntensitySet.bind("plugin:hyprimate:window:open:options:paper:rotation_intensity");
    values.m_openPaperSettlingSet.bind("plugin:hyprimate:window:open:options:paper:settling");
    values.m_openPaperSimulationQualitySet.bind("plugin:hyprimate:window:open:options:paper:simulation_quality");
    values.m_openClothDurationSet.bind("plugin:hyprimate:window:open:options:cloth:duration");
    values.m_openClothDefaultAnimationEdgeSet.bind("plugin:hyprimate:window:open:options:cloth:default_animation_edge");
    values.m_openClothEdgeBoundsSet.bind("plugin:hyprimate:window:open:options:cloth:edge_bounds");
    values.m_openClothFlutterSet.bind("plugin:hyprimate:window:open:options:cloth:flutter");
    values.m_openClothShadingSet.bind("plugin:hyprimate:window:open:options:cloth:shading");
    values.m_openClothShineSet.bind("plugin:hyprimate:window:open:options:cloth:shine");
    values.m_openClothVariationSet.bind("plugin:hyprimate:window:open:options:cloth:variation");
    values.m_openSwarmDurationSet.bind("plugin:hyprimate:window:open:options:swarm:duration");
    values.m_openSwarmLoneWindowSidesSet.bind("plugin:hyprimate:window:open:options:swarm:lone_window_sides");
    values.m_openBubbleDurationSet.bind("plugin:hyprimate:window:open:options:bubble:duration");
    values.m_openSmokeDurationSet.bind("plugin:hyprimate:window:open:options:smoke:duration");
    values.m_openSmokeTurbulenceSet.bind("plugin:hyprimate:window:open:options:smoke:turbulence");
    values.m_openSmokeDirectionSet.bind("plugin:hyprimate:window:open:options:smoke:direction");
    values.m_closeLiquidDurationSet.bind("plugin:hyprimate:window:close:options:liquid:duration");
    values.m_closeLiquidStrengthSet.bind("plugin:hyprimate:window:close:options:liquid:strength");
    values.m_closeLiquidColorSet.bind("plugin:hyprimate:window:close:options:liquid:color");
    values.m_closeDefaultAnimationEdgeSet.bind("plugin:hyprimate:window:close:options:liquid:default_animation_edge");
    values.m_closeSideSet.bind("plugin:hyprimate:window:close:options:liquid:side");
    values.m_closeEdgeBoundsSet.bind("plugin:hyprimate:window:close:options:liquid:edge_bounds");
    values.m_closeSplashSet.bind("plugin:hyprimate:window:close:options:liquid:splash");
    values.m_closeElasticitySet.bind("plugin:hyprimate:window:close:options:liquid:elasticity");
    values.m_closeMomentumSet.bind("plugin:hyprimate:window:close:options:liquid:momentum");
    values.m_closePaperDurationSet.bind("plugin:hyprimate:window:close:options:paper:duration");
    values.m_closePaperCrumplinessSet.bind("plugin:hyprimate:window:close:options:paper:crumpliness");
    values.m_closePaperShadingSet.bind("plugin:hyprimate:window:close:options:paper:shading");
    values.m_closePaperVariationSet.bind("plugin:hyprimate:window:close:options:paper:variation");
    values.m_closePaperFoldDensitySet.bind("plugin:hyprimate:window:close:options:paper:fold_density");
    values.m_closePaperStiffnessSet.bind("plugin:hyprimate:window:close:options:paper:paper_stiffness");
    values.m_closePaperCrumpleSizeSet.bind("plugin:hyprimate:window:close:options:paper:crumple_size");
    values.m_closePaperCrumpleOriginSet.bind("plugin:hyprimate:window:close:options:paper:crumple_origin");
    values.m_closePaperForceDirectionSet.bind("plugin:hyprimate:window:close:options:paper:force_direction");
    values.m_closePaperRotationIntensitySet.bind("plugin:hyprimate:window:close:options:paper:rotation_intensity");
    values.m_closePaperSettlingSet.bind("plugin:hyprimate:window:close:options:paper:settling");
    values.m_closePaperSimulationQualitySet.bind("plugin:hyprimate:window:close:options:paper:simulation_quality");
    values.m_closeClothDurationSet.bind("plugin:hyprimate:window:close:options:cloth:duration");
    values.m_closeClothDefaultAnimationEdgeSet.bind("plugin:hyprimate:window:close:options:cloth:default_animation_edge");
    values.m_closeClothSideSet.bind("plugin:hyprimate:window:close:options:cloth:side");
    values.m_closeClothEdgeBoundsSet.bind("plugin:hyprimate:window:close:options:cloth:edge_bounds");
    values.m_closeClothFlutterSet.bind("plugin:hyprimate:window:close:options:cloth:flutter");
    values.m_closeClothShadingSet.bind("plugin:hyprimate:window:close:options:cloth:shading");
    values.m_closeClothShineSet.bind("plugin:hyprimate:window:close:options:cloth:shine");
    values.m_closeClothVariationSet.bind("plugin:hyprimate:window:close:options:cloth:variation");
    values.m_closeSwarmDurationSet.bind("plugin:hyprimate:window:close:options:swarm:duration");
    values.m_closeSwarmLoneWindowSidesSet.bind("plugin:hyprimate:window:close:options:swarm:lone_window_sides");
    values.m_closeBubbleDurationSet.bind("plugin:hyprimate:window:close:options:bubble:duration");
    values.m_closeSmokeDurationSet.bind("plugin:hyprimate:window:close:options:smoke:duration");
    values.m_closeSmokeTurbulenceSet.bind("plugin:hyprimate:window:close:options:smoke:turbulence");
    values.m_closeSmokeDirectionSet.bind("plugin:hyprimate:window:close:options:smoke:direction");
    values.m_movementSwarmSpriteSet.bind("plugin:hyprimate:window:movement:options:swarm:sprite");
    values.m_movementSwarmCountSet.bind("plugin:hyprimate:window:movement:options:swarm:count");
    values.m_movementSwarmSpeedSet.bind("plugin:hyprimate:window:movement:options:swarm:speed");
    values.m_movementSwarmStaggerSet.bind("plugin:hyprimate:window:movement:options:swarm:stagger");
    values.m_movementSwarmCohesionSet.bind("plugin:hyprimate:window:movement:options:swarm:cohesion");
    values.m_movementSwarmSeparationSet.bind("plugin:hyprimate:window:movement:options:swarm:separation");
    values.m_movementSwarmAgilitySet.bind("plugin:hyprimate:window:movement:options:swarm:agility");
    values.m_movementSwarmSettlingSet.bind("plugin:hyprimate:window:movement:options:swarm:settling");
    values.m_movementSwarmWanderSet.bind("plugin:hyprimate:window:movement:options:swarm:wander");
    values.m_movementSwarmVariationSet.bind("plugin:hyprimate:window:movement:options:swarm:variation");
    values.m_openSwarmSpriteSet.bind("plugin:hyprimate:window:open:options:swarm:sprite");
    values.m_openSwarmCountSet.bind("plugin:hyprimate:window:open:options:swarm:count");
    values.m_openSwarmSpeedSet.bind("plugin:hyprimate:window:open:options:swarm:speed");
    values.m_openSwarmStaggerSet.bind("plugin:hyprimate:window:open:options:swarm:stagger");
    values.m_openSwarmCohesionSet.bind("plugin:hyprimate:window:open:options:swarm:cohesion");
    values.m_openSwarmSeparationSet.bind("plugin:hyprimate:window:open:options:swarm:separation");
    values.m_openSwarmAgilitySet.bind("plugin:hyprimate:window:open:options:swarm:agility");
    values.m_openSwarmSettlingSet.bind("plugin:hyprimate:window:open:options:swarm:settling");
    values.m_openSwarmWanderSet.bind("plugin:hyprimate:window:open:options:swarm:wander");
    values.m_openSwarmVariationSet.bind("plugin:hyprimate:window:open:options:swarm:variation");
    values.m_closeSwarmSpriteSet.bind("plugin:hyprimate:window:close:options:swarm:sprite");
    values.m_closeSwarmCountSet.bind("plugin:hyprimate:window:close:options:swarm:count");
    values.m_closeSwarmSpeedSet.bind("plugin:hyprimate:window:close:options:swarm:speed");
    values.m_closeSwarmStaggerSet.bind("plugin:hyprimate:window:close:options:swarm:stagger");
    values.m_closeSwarmCohesionSet.bind("plugin:hyprimate:window:close:options:swarm:cohesion");
    values.m_closeSwarmSeparationSet.bind("plugin:hyprimate:window:close:options:swarm:separation");
    values.m_closeSwarmAgilitySet.bind("plugin:hyprimate:window:close:options:swarm:agility");
    values.m_closeSwarmSettlingSet.bind("plugin:hyprimate:window:close:options:swarm:settling");
    values.m_closeSwarmWanderSet.bind("plugin:hyprimate:window:close:options:swarm:wander");
    values.m_closeSwarmVariationSet.bind("plugin:hyprimate:window:close:options:swarm:variation");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): Schema-generated reads include per-setting inheritance decisions.
inline SConfig readConfig(const SConfigValues& values) {
    SConfig config;
    config.construction.shared.duration = detail::clamp_float(values.m_globalConstructionDuration->value(), 0.0F);
    config.burn.shared.duration = detail::clamp_float(values.m_globalBurnDuration->value(), 0.0F);
    config.liquid.shared.strength = detail::clamp_float(values.m_globalLiquidStrength->value(), 0.0F, 1.5F);
    config.liquid.shared.viscosity = detail::clamp_float(values.m_globalLiquidViscosity->value(), 0.0F, 1.0F);
    config.liquid.shared.elasticity = detail::clamp_float(values.m_globalLiquidElasticity->value(), 0.0F, 1.0F);
    config.liquid.shared.momentum = detail::clamp_float(values.m_globalLiquidMomentum->value(), 0.0F, 1.0F);
    config.liquid.shared.variation = detail::clamp_float(values.m_globalLiquidVariation->value(), 0.0F, 1.0F);
    config.cloth.shared.strength = detail::clamp_float(values.m_globalClothStrength->value(), 0.0F, 1.5F);
    config.cloth.shared.springiness = detail::clamp_float(values.m_globalClothSpringiness->value(), 0.0F, 1.0F);
    config.cloth.shared.gravity = detail::clamp_float(values.m_globalClothGravity->value(), 0.0F, 3.0F);
    config.cloth.shared.flutter = detail::clamp_float(values.m_globalClothFlutter->value(), 0.0F, 1.0F);
    config.cloth.shared.shading = detail::clamp_float(values.m_globalClothShading->value(), 0.0F, 1.0F);
    config.cloth.shared.shine = detail::clamp_float(values.m_globalClothShine->value(), 0.0F, 1.0F);
    config.cloth.shared.variation = detail::clamp_float(values.m_globalClothVariation->value(), 0.0F, 1.0F);
    config.bubble.shared.strength = detail::clamp_float(values.m_globalBubbleStrength->value(), 0.0F, 1.5F);
    config.bubble.shared.microbubbles = detail::clamp_float(values.m_globalBubbleMicrobubbles->value(), 0.0F, 1.0F);
    config.bubble.shared.trail = detail::clamp_float(values.m_globalBubbleTrail->value(), 0.0F, 1.0F);
    config.bubble.shared.shape = detail::parse_shape(values.m_globalBubbleShape->value());
    config.bubble.shared.roundness = detail::clamp_float(values.m_globalBubbleRoundness->value(), 0.0F, 1.0F);
    config.bubble.shared.shapeSpeed = detail::clamp_float(values.m_globalBubbleShapeSpeed->value(), 100.0F, 6000.0F);
    config.bubble.shared.transparency = detail::clamp_float(values.m_globalBubbleTransparency->value(), 0.0F, 1.0F);
    config.smoke.shared.strength = detail::clamp_float(values.m_globalSmokeStrength->value(), 0.0F, 1.5F);
    config.smoke.shared.turbulence = detail::clamp_float(values.m_globalSmokeTurbulence->value(), 0.0F, 1.0F);
    config.smoke.shared.trail = detail::clamp_float(values.m_globalSmokeTrail->value(), 0.0F, 1.0F);
    config.liquid.shared.duration = detail::clamp_float(values.m_globalLiquidDuration->value(), 0.0F);
    config.liquid.shared.color = static_cast<std::uint32_t>(values.m_globalLiquidColor->value());
    config.liquid.shared.defaultAnimationEdge = detail::parse_default_animation_edge(values.m_globalLiquidDefaultAnimationEdge->value());
    config.liquid.shared.edgeBounds = detail::parse_edge_bounds(values.m_globalLiquidEdgeBounds->value());
    config.liquid.shared.emergeDrag = detail::clamp_float(values.m_globalLiquidEmergeDrag->value(), 0.0F, 1.0F);
    config.paper.shared.duration = detail::clamp_float(values.m_globalPaperDuration->value(), 0.0F);
    config.paper.shared.crumpliness = detail::clamp_float(values.m_globalPaperCrumpliness->value(), 0.0F, 1.0F);
    config.paper.shared.shading = detail::clamp_float(values.m_globalPaperShading->value(), 0.0F, 1.0F);
    config.paper.shared.variation = detail::clamp_float(values.m_globalPaperVariation->value(), 0.0F, 1.0F);
    config.paper.shared.foldDensity = detail::clamp_float(values.m_globalPaperFoldDensity->value(), 0.0F, 1.0F);
    config.paper.shared.paperStiffness = detail::clamp_float(values.m_globalPaperPaperStiffness->value(), 0.0F, 1.0F);
    config.paper.shared.crumpleSize = detail::clamp_float(values.m_globalPaperCrumpleSize->value(), 0.1F, 0.45F);
    config.paper.shared.crumpleOrigin = detail::parse_crumple_origin(values.m_globalPaperCrumpleOrigin->value());
    config.paper.shared.forceDirection = detail::parse_force_direction(values.m_globalPaperForceDirection->value());
    config.paper.shared.rotationIntensity = detail::clamp_float(values.m_globalPaperRotationIntensity->value(), 0.0F, 1.0F);
    config.paper.shared.settling = detail::clamp_float(values.m_globalPaperSettling->value(), 0.0F, 1.0F);
    config.paper.shared.simulationQuality = detail::parse_simulation_quality(values.m_globalPaperSimulationQuality->value());
    config.cloth.shared.duration = detail::clamp_float(values.m_globalClothDuration->value(), 0.0F);
    config.cloth.shared.defaultAnimationEdge = detail::parse_default_animation_edge(values.m_globalClothDefaultAnimationEdge->value());
    config.cloth.shared.edgeBounds = detail::parse_edge_bounds(values.m_globalClothEdgeBounds->value());
    config.swarm.shared.duration = detail::clamp_float(values.m_globalSwarmDuration->value(), 0.0F);
    config.swarm.shared.loneWindowSides = detail::parse_lone_window_sides(values.m_globalSwarmLoneWindowSides->value()).value_or(Flags<LoneWindowSides>{15});
    config.bubble.shared.duration = detail::clamp_float(values.m_globalBubbleDuration->value(), 0.0F);
    config.smoke.shared.duration = detail::clamp_float(values.m_globalSmokeDuration->value(), 0.0F);
    config.smoke.shared.direction = detail::parse_direction(values.m_globalSmokeDirection->value());
    config.liquid.shared.side = detail::parse_side(values.m_globalLiquidSide->value());
    config.liquid.shared.splash = values.m_globalLiquidSplash->value();
    config.cloth.shared.side = detail::parse_side(values.m_globalClothSide->value());
    config.workspace.enabled = values.m_workspaceEnabled->value();
    config.workspace.style = detail::parse_workspace_style(values.m_workspaceStyle->value());
    config.workspace.effect = values.m_workspaceEffect->value();
    config.workspace.effectParameters = values.m_workspaceEffectParameters->value();
    config.workspace.effectDuration = detail::clamp_float(values.m_workspaceEffectDuration->value(), 0.0F, 60000.0F);
    config.workspace.rotation.duration = detail::clamp_float(values.m_workspaceRotationDuration->value(), 0.0F);
    config.workspace.rotation.curve = values.m_workspaceRotationCurve->value();
    config.workspace.rotation.gapFillStyle = detail::parse_rotation_gap_fill_style(values.m_workspaceRotationGapFillStyle->value());
    config.workspace.rotation.gapFillColor = static_cast<std::uint32_t>(values.m_workspaceRotationGapFillColor->value());
    config.workspace.rotation.gapFillBlur = detail::clamp_float(values.m_workspaceRotationGapFillBlur->value(), 0.0F, 64.0F);
    config.workspace.rotation.keepOverlaysStatic = values.m_workspaceRotationKeepOverlaysStatic->value();
    config.workspace.planetHop.windowMode = detail::parse_planet_hop_window_mode(values.m_workspacePlanetHopWindowMode->value());
    config.workspace.planetHop.overlayMode = detail::parse_planet_hop_overlay_mode(values.m_workspacePlanetHopOverlayMode->value());
    config.workspace.planetHop.planetLayout = detail::parse_planet_hop_planet_layout(values.m_workspacePlanetHopPlanetLayout->value());
    config.workspace.planetHop.duration = detail::clamp_float(values.m_workspacePlanetHopDuration->value(), 0.0F);
    config.workspace.planetHop.curve = values.m_workspacePlanetHopCurve->value();
    config.workspace.planetHop.liveBoundary = detail::clamp_float(values.m_workspacePlanetHopLiveBoundary->value(), 0.08F, 0.35F);
    config.workspace.planetHop.samplingQuality = detail::clamp_int(values.m_workspacePlanetHopSamplingQuality->value(), 0, 2);
    config.workspace.planetHop.planetSize = detail::clamp_float(values.m_workspacePlanetHopPlanetSize->value(), 0.1F, 4.0F);
    config.workspace.planetHop.planetSeparation = detail::clamp_float(values.m_workspacePlanetHopPlanetSeparation->value(), 1.0F, 20.0F);
    config.workspace.planetHop.solarVerticalSpread = detail::clamp_float(values.m_workspacePlanetHopSolarVerticalSpread->value(), 0.0F, 2.0F);
    config.workspace.planetHop.solarBackgroundScale = detail::clamp_float(values.m_workspacePlanetHopSolarBackgroundScale->value(), 0.25F, 1.0F);
    config.workspace.planetHop.planetCoverage = detail::clamp_float(values.m_workspacePlanetHopPlanetCoverage->value(), 0.1F, 1.0F);
    config.workspace.planetHop.indirectBrightness = detail::clamp_float(values.m_workspacePlanetHopIndirectBrightness->value(), 0.0F, 1.0F);
    config.workspace.planetHop.elevation = detail::clamp_float(values.m_workspacePlanetHopElevation->value(), 0.0F, 1.0F);
    config.workspace.planetHop.fade = detail::clamp_float(values.m_workspacePlanetHopFade->value(), 0.0F, 1.0F);
    config.workspace.planetHop.rotationEnabled = values.m_workspacePlanetHopRotationEnabled->value();
    config.workspace.planetHop.rotationLimit = detail::clamp_float(values.m_workspacePlanetHopRotationLimit->value(), 0.0F, 3.14F);
    config.workspace.planetHop.starsEnabled = values.m_workspacePlanetHopStarsEnabled->value();
    config.workspace.planetHop.starDensity = detail::clamp_float(values.m_workspacePlanetHopStarDensity->value(), 0.0F, 1.0F);
    config.workspace.planetHop.starParallax = detail::clamp_float(values.m_workspacePlanetHopStarParallax->value(), 0.0F, 2.0F);
    config.workspace.planetHop.atmosphereEnabled = values.m_workspacePlanetHopAtmosphereEnabled->value();
    config.workspace.planetHop.atmosphereIntensity = detail::clamp_float(values.m_workspacePlanetHopAtmosphereIntensity->value(), 0.0F, 1.0F);
    config.workspace.planetHop.atmosphereThickness = detail::clamp_float(values.m_workspacePlanetHopAtmosphereThickness->value(), 0.0F, 1.0F);
    config.workspace.planetHop.lightingEnabled = values.m_workspacePlanetHopLightingEnabled->value();
    config.workspace.planetHop.lightDirectionX = detail::clamp_float(values.m_workspacePlanetHopLightDirectionX->value(), -1.0F, 1.0F);
    config.workspace.planetHop.lightDirectionY = detail::clamp_float(values.m_workspacePlanetHopLightDirectionY->value(), -1.0F, 1.0F);
    config.workspace.planetHop.lightDirectionZ = detail::clamp_float(values.m_workspacePlanetHopLightDirectionZ->value(), -1.0F, 1.0F);
    config.workspace.planetHop.nightBrightness = detail::clamp_float(values.m_workspacePlanetHopNightBrightness->value(), 0.0F, 1.0F);
    config.workspace.planetHop.cloudsEnabled = values.m_workspacePlanetHopCloudsEnabled->value();
    config.workspace.planetHop.cloudCoverage = detail::clamp_float(values.m_workspacePlanetHopCloudCoverage->value(), 0.0F, 1.0F);
    config.workspace.planetHop.cloudOpacity = detail::clamp_float(values.m_workspacePlanetHopCloudOpacity->value(), 0.0F, 1.0F);
    config.workspace.planetHop.cloudSpeed = detail::clamp_float(values.m_workspacePlanetHopCloudSpeed->value(), 0.0F, 1.0F);
    config.workspace.planetHop.depthEnabled = values.m_workspacePlanetHopDepthEnabled->value();
    config.workspace.planetHop.depthStrength = detail::clamp_float(values.m_workspacePlanetHopDepthStrength->value(), 0.0F, 0.5F);
    config.workspace.planetHop.depthSource = values.m_workspacePlanetHopDepthSource->value();
    config.workspace.cube.variant = detail::parse_cube_variant(values.m_workspaceCubeVariant->value());
    config.workspace.cube.duration = detail::clamp_float(values.m_workspaceCubeDuration->value(), 0.0F);
    config.workspace.cube.curve = values.m_workspaceCubeCurve->value();
    config.workspace.cube.solid.cubeTilt = detail::clamp_float(values.m_workspaceCubeSolidCubeTilt->value(), -20.0F, 20.0F);
    config.workspace.cube.solid.zoom = detail::clamp_float(values.m_workspaceCubeSolidZoom->value(), 0.0F, 1.0F);
    config.workspace.cube.insideOut.cubeTilt = detail::clamp_float(values.m_workspaceCubeInsideOutCubeTilt->value(), -20.0F, 20.0F);
    config.workspace.cube.insideOut.zoom = detail::clamp_float(values.m_workspaceCubeInsideOutZoom->value(), 0.0F, 1.0F);
    config.workspace.lava.variant = detail::parse_lava_variant(values.m_workspaceLavaVariant->value());
    config.workspace.lava.windowBehavior = detail::parse_lava_window_behavior(values.m_workspaceLavaWindowBehavior->value());
    config.workspace.lava.viscosity = detail::clamp_float(values.m_workspaceLavaViscosity->value(), 0.0F, 1.0F);
    config.workspace.lava.coolingRate = detail::clamp_float(values.m_workspaceLavaCoolingRate->value(), 0.0F, 1.0F);
    config.workspace.lava.crustAmount = detail::clamp_float(values.m_workspaceLavaCrustAmount->value(), 0.0F, 1.0F);
    config.workspace.lava.glowIntensity = detail::clamp_float(values.m_workspaceLavaGlowIntensity->value(), 0.0F, 1.0F);
    config.workspace.lava.detailPreservation = detail::clamp_float(values.m_workspaceLavaDetailPreservation->value(), 0.0F, 1.0F);
    config.workspace.lava.trailSmear = detail::clamp_float(values.m_workspaceLavaTrailSmear->value(), 0.0F, 1.0F);
    config.workspace.lava.edgeDrip = detail::clamp_float(values.m_workspaceLavaEdgeDrip->value(), 0.0F, 1.0F);
    config.workspace.lava.ashAmount = detail::clamp_float(values.m_workspaceLavaAshAmount->value(), 0.0F, 1.0F);
    config.workspace.lava.coolingFlow.leadingBulge = detail::clamp_float(values.m_workspaceLavaCoolingFlowLeadingBulge->value(), 0.0F, 1.0F);
    config.workspace.lava.coolingFlow.trailingStretch = detail::clamp_float(values.m_workspaceLavaCoolingFlowTrailingStretch->value(), 0.0F, 1.0F);
    config.workspace.lava.coolingFlow.moltenTurbulence = detail::clamp_float(values.m_workspaceLavaCoolingFlowMoltenTurbulence->value(), 0.0F, 1.0F);
    config.workspace.lava.basaltFlow.crustThickness = detail::clamp_float(values.m_workspaceLavaBasaltFlowCrustThickness->value(), 0.0F, 1.0F);
    config.workspace.lava.basaltFlow.fissureWidth = detail::clamp_float(values.m_workspaceLavaBasaltFlowFissureWidth->value(), 0.0F, 1.0F);
    config.workspace.lava.basaltFlow.emberVisibility = detail::clamp_float(values.m_workspaceLavaBasaltFlowEmberVisibility->value(), 0.0F, 1.0F);
    config.workspace.lava.basaltFlow.massDensity = detail::clamp_float(values.m_workspaceLavaBasaltFlowMassDensity->value(), 0.0F, 1.0F);
    config.workspace.lava.pahoehoe.ropeStrength = detail::clamp_float(values.m_workspaceLavaPahoehoeRopeStrength->value(), 0.0F, 1.0F);
    config.workspace.lava.pahoehoe.foldScale = detail::clamp_float(values.m_workspaceLavaPahoehoeFoldScale->value(), 0.0F, 1.0F);
    config.workspace.lava.pahoehoe.rippleSpeed = detail::clamp_float(values.m_workspaceLavaPahoehoeRippleSpeed->value(), 0.0F, 1.0F);
    config.workspace.lava.pahoehoe.surfaceRoll = detail::clamp_float(values.m_workspaceLavaPahoehoeSurfaceRoll->value(), 0.0F, 1.0F);
    config.workspace.lava.crustPlates.plateSize = detail::clamp_float(values.m_workspaceLavaCrustPlatesPlateSize->value(), 0.0F, 1.0F);
    config.workspace.lava.crustPlates.plateRigidity = detail::clamp_float(values.m_workspaceLavaCrustPlatesPlateRigidity->value(), 0.0F, 1.0F);
    config.workspace.lava.crustPlates.seamGlow = detail::clamp_float(values.m_workspaceLavaCrustPlatesSeamGlow->value(), 0.0F, 1.0F);
    config.workspace.lava.crustPlates.plateDrift = detail::clamp_float(values.m_workspaceLavaCrustPlatesPlateDrift->value(), 0.0F, 1.0F);
    config.workspace.lava.crustPlates.plateCompression = detail::clamp_float(values.m_workspaceLavaCrustPlatesPlateCompression->value(), 0.0F, 1.0F);
    config.workspace.lava.crustPlates.submeltVisibility = detail::clamp_float(values.m_workspaceLavaCrustPlatesSubmeltVisibility->value(), 0.0F, 1.0F);
    config.wallpaper.effect = values.m_wallpaperEffect->value();
    config.wallpaper.effectParameters = values.m_wallpaperEffectParameters->value();
    config.wallpaper.effectDuration = detail::clamp_int(values.m_wallpaperEffectDuration->value(), 0, 60000);
    config.wallpaper.sourceNamespace = values.m_wallpaperSourceNamespace->value();
    config.enabled = values.m_enabled->value();
    config.duringWorkspaceSwitch = values.m_duringWorkspaceSwitch->value();
    config.style = detail::parse_style(values.m_style->value());
    config.open.effect = values.m_openEffect->value();
    config.close.effect = values.m_closeEffect->value();
    config.open.effectParameters = values.m_openEffectParameters->value();
    config.close.effectParameters = values.m_closeEffectParameters->value();
    config.open.effectDuration = detail::clamp_float(values.m_openEffectDuration->value(), 0.0F, 60000.0F);
    config.close.effectDuration = detail::clamp_float(values.m_closeEffectDuration->value(), 0.0F, 60000.0F);
    config.move.shared.effect = values.m_moveEffect->value();
    config.move.shared.effectParameters = values.m_moveEffectParameters->value();
    config.move.shared.effectDuration = detail::clamp_float(values.m_moveEffectDuration->value(), 0.0F, 60000.0F);
    config.resize.shared.effect = values.m_resizeEffect->value();
    config.resize.shared.effectParameters = values.m_resizeEffectParameters->value();
    config.resize.shared.effectDuration = detail::clamp_float(values.m_resizeEffectDuration->value(), 0.0F, 60000.0F);
    config.construction.open.duration = detail::clamp_float((values.m_openConstructionDurationSet.setByUser() || !values.m_globalConstructionDurationSet.setByUser() ? values.m_openConstructionDuration->value() : values.m_globalConstructionDuration->value()), 0.0F);
    config.construction.close.duration = detail::clamp_float((values.m_closeConstructionDurationSet.setByUser() || !values.m_globalConstructionDurationSet.setByUser() ? values.m_closeConstructionDuration->value() : values.m_globalConstructionDuration->value()), 0.0F);
    config.construction.shared.workerSize = detail::clamp_float(values.m_constructionWorkerSize->value(), 0.0F, 2.0F);
    config.burn.shared.variation = detail::clamp_float(values.m_burnVariation->value(), 0.0F, 1.0F);
    config.burn.shared.fragmentDensity = detail::clamp_float(values.m_burnFragmentDensity->value(), 0.0F, 1.0F);
    config.burn.shared.emberDensity = detail::clamp_float(values.m_burnEmberDensity->value(), 0.0F, 1.0F);
    config.burn.shared.windDirection = detail::clamp_float(values.m_burnWindDirection->value(), 0.0F, 360.0F);
    config.burn.shared.windStrength = detail::clamp_float(values.m_burnWindStrength->value(), 0.0F, 1000.0F);
    config.burn.shared.gravityStrength = detail::clamp_float(values.m_burnGravityStrength->value(), 0.0F, 1800.0F);
    config.burn.shared.scorchLifetime = detail::clamp_float(values.m_burnScorchLifetime->value(), 0.0F, 10.0F);
    config.burn.shared.healingDuration = detail::clamp_float(values.m_burnHealingDuration->value(), 0.1F, 10.0F);
    config.burn.movement.smokeDensity = detail::clamp_float(values.m_burnMovementSmokeDensity->value(), 0.0F, 1.0F);
    config.burn.open.duration = detail::clamp_float((values.m_openBurnDurationSet.setByUser() || !values.m_globalBurnDurationSet.setByUser() ? values.m_openBurnDuration->value() : values.m_globalBurnDuration->value()), 0.0F);
    config.burn.close.duration = detail::clamp_float((values.m_closeBurnDurationSet.setByUser() || !values.m_globalBurnDurationSet.setByUser() ? values.m_closeBurnDuration->value() : values.m_globalBurnDuration->value()), 0.0F);
    config.burn.close.closingStyle = detail::parse_closing_style(values.m_burnClosingStyle->value());
    config.burn.close.debrisDirection = detail::parse_debris_direction(values.m_burnDebrisDirection->value());
    config.burn.close.curlIntensity = detail::clamp_float(values.m_burnCurlIntensity->value(), 0.0F, 1.0F);
    config.paper.shared.stiffness = detail::clamp_float(values.m_paperStiffness->value(), 0.0F, 1.0F);
    config.paper.shared.inertia = detail::clamp_float(values.m_paperInertia->value(), 0.0F, 1.0F);
    config.paper.shared.settleDamping = detail::clamp_float(values.m_paperSettleDamping->value(), 0.0F, 1.0F);
    config.paper.move.style = detail::parse_paper_move_style(values.m_paperMoveStyle->value());
    config.paper.move.flutter.airResistance = detail::clamp_float(values.m_paperFlutterAirResistance->value(), 0.0F, 1.0F);
    config.paper.move.flutter.intensity = detail::clamp_float(values.m_paperFlutterIntensity->value(), 0.0F, 1.0F);
    config.paper.move.flutter.frequency = detail::clamp_float(values.m_paperFlutterFrequency->value(), 0.1F, 4.0F);
    config.paper.move.flutter.lift = detail::clamp_float(values.m_paperFlutterLift->value(), 0.0F, 1.0F);
    config.paper.move.flutter.torsion = detail::clamp_float(values.m_paperFlutterTorsion->value(), 0.0F, 1.0F);
    config.paper.move.glide.friction = detail::clamp_float(values.m_paperGlideFriction->value(), 0.0F, 1.0F);
    config.paper.move.glide.cornerLift = detail::clamp_float(values.m_paperGlideCornerLift->value(), 0.0F, 1.0F);
    config.paper.move.glide.edgeCurl = detail::clamp_float(values.m_paperGlideEdgeCurl->value(), 0.0F, 1.0F);
    config.paper.move.glide.shadowSeparation = detail::clamp_float(values.m_paperGlideShadowSeparation->value(), 0.0F, 1.0F);
    config.paper.resize.foldIntensity = detail::clamp_float(values.m_paperResizeFoldIntensity->value(), 0.0F, 1.0F);
    config.paper.resize.curlIntensity = detail::clamp_float(values.m_paperResizeCurlIntensity->value(), 0.0F, 1.0F);
    config.paper.resize.creaseDensity = detail::clamp_float(values.m_paperResizeCreaseDensity->value(), 0.0F, 1.0F);
    config.swarm.shared.sprite = values.m_swarmSprite->value();
    config.swarm.shared.count = detail::clamp_int(values.m_swarmCount->value(), 1, 400);
    config.swarm.shared.speed = detail::clamp_float(values.m_swarmSpeed->value(), 100.0F, 6000.0F);
    config.swarm.shared.stagger = detail::clamp_float(values.m_swarmStagger->value(), 0.0F, 3.0F);
    config.swarm.shared.cohesion = detail::clamp_float(values.m_swarmCohesion->value(), 0.0F, 1.0F);
    config.swarm.shared.separation = detail::clamp_float(values.m_swarmSeparation->value(), 0.0F, 1.0F);
    config.swarm.shared.agility = detail::clamp_float(values.m_swarmAgility->value(), 0.0F, 1.0F);
    config.swarm.shared.settling = detail::clamp_float(values.m_swarmSettling->value(), 0.0F, 1.0F);
    config.swarm.shared.wander = detail::clamp_float(values.m_swarmWander->value(), 0.0F, 1.0F);
    config.swarm.shared.variation = detail::clamp_float(values.m_swarmVariation->value(), 0.0F, 1.0F);
    config.smoke.shared.color = static_cast<std::uint32_t>(values.m_smokeColor->value());
    config.smoke.shared.density = detail::clamp_float(values.m_smokeDensity->value(), 0.25F, 2.0F);
    config.smoke.shared.variation = detail::clamp_float(values.m_smokeVariation->value(), 0.0F, 1.0F);
    config.movement.enabled = values.m_movementEnabled->value();
    config.movement.workspaceMovement = values.m_workspaceMovement->value();
    config.construction.movement.variant = detail::parse_variant(values.m_movementConstructionVariant->value());
    config.liquid.movement.strength = detail::clamp_float((values.m_movementLiquidStrengthSet.setByUser() || !values.m_globalLiquidStrengthSet.setByUser() ? values.m_movementLiquidStrength->value() : values.m_globalLiquidStrength->value()), 0.0F, 1.5F);
    config.liquid.movement.viscosity = detail::clamp_float((values.m_viscositySet.setByUser() || !values.m_globalLiquidViscositySet.setByUser() ? values.m_viscosity->value() : values.m_globalLiquidViscosity->value()), 0.0F, 1.0F);
    config.liquid.movement.elasticity = detail::clamp_float((values.m_elasticitySet.setByUser() || !values.m_globalLiquidElasticitySet.setByUser() ? values.m_elasticity->value() : values.m_globalLiquidElasticity->value()), 0.0F, 1.0F);
    config.liquid.movement.momentum = detail::clamp_float((values.m_momentumSet.setByUser() || !values.m_globalLiquidMomentumSet.setByUser() ? values.m_momentum->value() : values.m_globalLiquidMomentum->value()), 0.0F, 1.0F);
    config.liquid.movement.variation = detail::clamp_float((values.m_variationSet.setByUser() || !values.m_globalLiquidVariationSet.setByUser() ? values.m_variation->value() : values.m_globalLiquidVariation->value()), 0.0F, 1.0F);
    config.cloth.movement.strength = detail::clamp_float((values.m_movementClothStrengthSet.setByUser() || !values.m_globalClothStrengthSet.setByUser() ? values.m_movementClothStrength->value() : values.m_globalClothStrength->value()), 0.0F, 1.5F);
    config.cloth.movement.springiness = detail::clamp_float((values.m_movementClothSpringinessSet.setByUser() || !values.m_globalClothSpringinessSet.setByUser() ? values.m_movementClothSpringiness->value() : values.m_globalClothSpringiness->value()), 0.0F, 1.0F);
    config.cloth.movement.gravity = detail::clamp_float((values.m_movementClothGravitySet.setByUser() || !values.m_globalClothGravitySet.setByUser() ? values.m_movementClothGravity->value() : values.m_globalClothGravity->value()), 0.0F, 3.0F);
    config.cloth.movement.flutter = detail::clamp_float((values.m_movementClothFlutterSet.setByUser() || !values.m_globalClothFlutterSet.setByUser() ? values.m_movementClothFlutter->value() : values.m_globalClothFlutter->value()), 0.0F, 1.0F);
    config.cloth.movement.shading = detail::clamp_float((values.m_movementClothShadingSet.setByUser() || !values.m_globalClothShadingSet.setByUser() ? values.m_movementClothShading->value() : values.m_globalClothShading->value()), 0.0F, 1.0F);
    config.cloth.movement.shine = detail::clamp_float((values.m_movementClothShineSet.setByUser() || !values.m_globalClothShineSet.setByUser() ? values.m_movementClothShine->value() : values.m_globalClothShine->value()), 0.0F, 1.0F);
    config.cloth.movement.variation = detail::clamp_float((values.m_movementClothVariationSet.setByUser() || !values.m_globalClothVariationSet.setByUser() ? values.m_movementClothVariation->value() : values.m_globalClothVariation->value()), 0.0F, 1.0F);
    config.bubble.movement.strength = detail::clamp_float((values.m_movementBubbleStrengthSet.setByUser() || !values.m_globalBubbleStrengthSet.setByUser() ? values.m_movementBubbleStrength->value() : values.m_globalBubbleStrength->value()), 0.0F, 1.5F);
    config.bubble.movement.microbubbles = detail::clamp_float((values.m_movementBubbleMicrobubblesSet.setByUser() || !values.m_globalBubbleMicrobubblesSet.setByUser() ? values.m_movementBubbleMicrobubbles->value() : values.m_globalBubbleMicrobubbles->value()), 0.0F, 1.0F);
    config.bubble.movement.trail = detail::clamp_float((values.m_movementBubbleTrailSet.setByUser() || !values.m_globalBubbleTrailSet.setByUser() ? values.m_movementBubbleTrail->value() : values.m_globalBubbleTrail->value()), 0.0F, 1.0F);
    config.bubble.movement.shape = detail::parse_shape((values.m_movementBubbleShapeSet.setByUser() || !values.m_globalBubbleShapeSet.setByUser() ? values.m_movementBubbleShape->value() : values.m_globalBubbleShape->value()));
    config.bubble.movement.roundness = detail::clamp_float((values.m_movementBubbleRoundnessSet.setByUser() || !values.m_globalBubbleRoundnessSet.setByUser() ? values.m_movementBubbleRoundness->value() : values.m_globalBubbleRoundness->value()), 0.0F, 1.0F);
    config.bubble.movement.shapeSpeed = detail::clamp_float((values.m_movementBubbleShapeSpeedSet.setByUser() || !values.m_globalBubbleShapeSpeedSet.setByUser() ? values.m_movementBubbleShapeSpeed->value() : values.m_globalBubbleShapeSpeed->value()), 100.0F, 6000.0F);
    config.bubble.movement.transparency = detail::clamp_float((values.m_movementBubbleTransparencySet.setByUser() || !values.m_globalBubbleTransparencySet.setByUser() ? values.m_movementBubbleTransparency->value() : values.m_globalBubbleTransparency->value()), 0.0F, 1.0F);
    config.smoke.movement.strength = detail::clamp_float((values.m_movementSmokeStrengthSet.setByUser() || !values.m_globalSmokeStrengthSet.setByUser() ? values.m_movementSmokeStrength->value() : values.m_globalSmokeStrength->value()), 0.0F, 1.5F);
    config.smoke.movement.turbulence = detail::clamp_float((values.m_movementSmokeTurbulenceSet.setByUser() || !values.m_globalSmokeTurbulenceSet.setByUser() ? values.m_movementSmokeTurbulence->value() : values.m_globalSmokeTurbulence->value()), 0.0F, 1.0F);
    config.smoke.movement.trail = detail::clamp_float((values.m_movementSmokeTrailSet.setByUser() || !values.m_globalSmokeTrailSet.setByUser() ? values.m_movementSmokeTrail->value() : values.m_globalSmokeTrail->value()), 0.0F, 1.0F);
    config.smoke.movement.dragFollow = detail::clamp_float(values.m_movementSmokeDragFollow->value(), 0.0F, 1.0F);
    config.smoke.movement.trailLength = detail::clamp_float(values.m_movementSmokeTrailLength->value(), 0.0F, 2400.0F);
    config.smoke.movement.trailLifetime = detail::clamp_float(values.m_movementSmokeTrailLifetime->value(), 0.0F, 4000.0F);
    config.smoke.movement.trailThickness = detail::clamp_float(values.m_movementSmokeTrailThickness->value(), 0.25F, 2.5F);
    config.smoke.movement.reform = detail::clamp_float(values.m_movementSmokeReform->value(), 0.15F, 1.5F);
    config.smoke.movement.settleSpeed = detail::clamp_float(values.m_movementSmokeSettleSpeed->value(), 0.1F, 2.0F);
    config.smoke.movement.mass = detail::clamp_float(values.m_movementSmokeMass->value(), 0.15F, 1.0F);
    config.smoke.movement.roundness = detail::clamp_float(values.m_movementSmokeRoundness->value(), 0.0F, 1.0F);
    config.open.enabled = values.m_openEnabled->value();
    config.liquid.open.duration = (values.m_openLiquidDurationSet.setByUser() || values.m_globalLiquidDurationSet.setByUser() ? detail::clamp_float((values.m_openLiquidDurationSet.setByUser() || !values.m_globalLiquidDurationSet.setByUser() ? values.m_openLiquidDuration->value() : values.m_globalLiquidDuration->value()), 0.0F) : -1.0);
    config.liquid.open.strength = detail::clamp_float((values.m_openLiquidStrengthSet.setByUser() || !values.m_globalLiquidStrengthSet.setByUser() ? values.m_openLiquidStrength->value() : values.m_globalLiquidStrength->value()), 0.0F, 1.5F);
    config.liquid.open.color = static_cast<std::uint32_t>((values.m_openLiquidColorSet.setByUser() || !values.m_globalLiquidColorSet.setByUser() ? values.m_openLiquidColor->value() : values.m_globalLiquidColor->value()));
    config.liquid.open.defaultAnimationEdge = detail::parse_default_animation_edge((values.m_openDefaultAnimationEdgeSet.setByUser() || !values.m_globalLiquidDefaultAnimationEdgeSet.setByUser() ? values.m_openDefaultAnimationEdge->value() : values.m_globalLiquidDefaultAnimationEdge->value()));
    config.liquid.open.edgeBounds = detail::parse_edge_bounds((values.m_edgeBoundsSet.setByUser() || !values.m_globalLiquidEdgeBoundsSet.setByUser() ? values.m_edgeBounds->value() : values.m_globalLiquidEdgeBounds->value()));
    config.liquid.open.emergeDrag = detail::clamp_float((values.m_emergeDragSet.setByUser() || !values.m_globalLiquidEmergeDragSet.setByUser() ? values.m_emergeDrag->value() : values.m_globalLiquidEmergeDrag->value()), 0.0F, 1.0F);
    config.liquid.open.elasticity = detail::clamp_float((values.m_openElasticitySet.setByUser() || !values.m_globalLiquidElasticitySet.setByUser() ? values.m_openElasticity->value() : values.m_globalLiquidElasticity->value()), 0.0F, 1.0F);
    config.liquid.open.momentum = detail::clamp_float((values.m_openMomentumSet.setByUser() || !values.m_globalLiquidMomentumSet.setByUser() ? values.m_openMomentum->value() : values.m_globalLiquidMomentum->value()), 0.0F, 1.0F);
    config.paper.open.duration = detail::clamp_float((values.m_openPaperDurationSet.setByUser() || !values.m_globalPaperDurationSet.setByUser() ? values.m_openPaperDuration->value() : values.m_globalPaperDuration->value()), 0.0F);
    config.paper.open.crumpliness = detail::clamp_float((values.m_openPaperCrumplinessSet.setByUser() || !values.m_globalPaperCrumplinessSet.setByUser() ? values.m_openPaperCrumpliness->value() : values.m_globalPaperCrumpliness->value()), 0.0F, 1.0F);
    config.paper.open.shading = detail::clamp_float((values.m_openPaperShadingSet.setByUser() || !values.m_globalPaperShadingSet.setByUser() ? values.m_openPaperShading->value() : values.m_globalPaperShading->value()), 0.0F, 1.0F);
    config.paper.open.variation = detail::clamp_float((values.m_openPaperVariationSet.setByUser() || !values.m_globalPaperVariationSet.setByUser() ? values.m_openPaperVariation->value() : values.m_globalPaperVariation->value()), 0.0F, 1.0F);
    config.paper.open.foldDensity = detail::clamp_float((values.m_openPaperFoldDensitySet.setByUser() || !values.m_globalPaperFoldDensitySet.setByUser() ? values.m_openPaperFoldDensity->value() : values.m_globalPaperFoldDensity->value()), 0.0F, 1.0F);
    config.paper.open.paperStiffness = detail::clamp_float((!values.m_openPaperStiffnessSet.setByUser() && !values.m_globalPaperPaperStiffnessSet.setByUser() && values.m_paperStiffnessSet.setByUser() ? values.m_paperStiffness->value() : (values.m_openPaperStiffnessSet.setByUser() || !values.m_globalPaperPaperStiffnessSet.setByUser() ? values.m_openPaperStiffness->value() : values.m_globalPaperPaperStiffness->value())), 0.0F, 1.0F);
    config.paper.open.crumpleSize = detail::clamp_float((values.m_openPaperCrumpleSizeSet.setByUser() || !values.m_globalPaperCrumpleSizeSet.setByUser() ? values.m_openPaperCrumpleSize->value() : values.m_globalPaperCrumpleSize->value()), 0.1F, 0.45F);
    config.paper.open.crumpleOrigin = detail::parse_crumple_origin((values.m_openPaperCrumpleOriginSet.setByUser() || !values.m_globalPaperCrumpleOriginSet.setByUser() ? values.m_openPaperCrumpleOrigin->value() : values.m_globalPaperCrumpleOrigin->value()));
    config.paper.open.forceDirection = detail::parse_force_direction((values.m_openPaperForceDirectionSet.setByUser() || !values.m_globalPaperForceDirectionSet.setByUser() ? values.m_openPaperForceDirection->value() : values.m_globalPaperForceDirection->value()));
    config.paper.open.rotationIntensity = detail::clamp_float((values.m_openPaperRotationIntensitySet.setByUser() || !values.m_globalPaperRotationIntensitySet.setByUser() ? values.m_openPaperRotationIntensity->value() : values.m_globalPaperRotationIntensity->value()), 0.0F, 1.0F);
    config.paper.open.unfoldTension = detail::clamp_float(values.m_openPaperUnfoldTension->value(), 0.0F, 1.0F);
    config.paper.open.settling = detail::clamp_float((!values.m_openPaperSettlingSet.setByUser() && !values.m_globalPaperSettlingSet.setByUser() && values.m_paperSettleDampingSet.setByUser() ? values.m_paperSettleDamping->value() : (values.m_openPaperSettlingSet.setByUser() || !values.m_globalPaperSettlingSet.setByUser() ? values.m_openPaperSettling->value() : values.m_globalPaperSettling->value())), 0.0F, 1.0F);
    config.paper.open.creasePersistence = detail::clamp_float(values.m_openPaperCreasePersistence->value(), 0.0F, 1.0F);
    config.paper.open.simulationQuality = detail::parse_simulation_quality((values.m_openPaperSimulationQualitySet.setByUser() || !values.m_globalPaperSimulationQualitySet.setByUser() ? values.m_openPaperSimulationQuality->value() : values.m_globalPaperSimulationQuality->value()));
    config.cloth.open.duration = detail::clamp_float((values.m_openClothDurationSet.setByUser() || !values.m_globalClothDurationSet.setByUser() ? values.m_openClothDuration->value() : values.m_globalClothDuration->value()), 0.0F);
    config.cloth.open.defaultAnimationEdge = detail::parse_default_animation_edge((values.m_openClothDefaultAnimationEdgeSet.setByUser() || !values.m_globalClothDefaultAnimationEdgeSet.setByUser() ? values.m_openClothDefaultAnimationEdge->value() : values.m_globalClothDefaultAnimationEdge->value()));
    config.cloth.open.edgeBounds = detail::parse_edge_bounds((values.m_openClothEdgeBoundsSet.setByUser() || !values.m_globalClothEdgeBoundsSet.setByUser() ? values.m_openClothEdgeBounds->value() : values.m_globalClothEdgeBounds->value()));
    config.cloth.open.flutter = detail::clamp_float((values.m_openClothFlutterSet.setByUser() || !values.m_globalClothFlutterSet.setByUser() ? values.m_openClothFlutter->value() : values.m_globalClothFlutter->value()), 0.0F, 1.0F);
    config.cloth.open.shading = detail::clamp_float((values.m_openClothShadingSet.setByUser() || !values.m_globalClothShadingSet.setByUser() ? values.m_openClothShading->value() : values.m_globalClothShading->value()), 0.0F, 1.0F);
    config.cloth.open.shine = detail::clamp_float((values.m_openClothShineSet.setByUser() || !values.m_globalClothShineSet.setByUser() ? values.m_openClothShine->value() : values.m_globalClothShine->value()), 0.0F, 1.0F);
    config.cloth.open.variation = detail::clamp_float((values.m_openClothVariationSet.setByUser() || !values.m_globalClothVariationSet.setByUser() ? values.m_openClothVariation->value() : values.m_globalClothVariation->value()), 0.0F, 1.0F);
    config.swarm.open.duration = (values.m_openSwarmDurationSet.setByUser() || values.m_globalSwarmDurationSet.setByUser() ? detail::clamp_float((values.m_openSwarmDurationSet.setByUser() || !values.m_globalSwarmDurationSet.setByUser() ? values.m_openSwarmDuration->value() : values.m_globalSwarmDuration->value()), 0.0F) : -1.0);
    config.swarm.open.loneWindowSides = detail::parse_lone_window_sides((values.m_openSwarmLoneWindowSidesSet.setByUser() || !values.m_globalSwarmLoneWindowSidesSet.setByUser() ? values.m_openSwarmLoneWindowSides->value() : values.m_globalSwarmLoneWindowSides->value())).value_or(Flags<LoneWindowSides>{15});
    config.bubble.open.duration = detail::clamp_float((values.m_openBubbleDurationSet.setByUser() || !values.m_globalBubbleDurationSet.setByUser() ? values.m_openBubbleDuration->value() : values.m_globalBubbleDuration->value()), 0.0F);
    config.bubble.open.count = detail::clamp_int(values.m_openBubbleCount->value(), 0, 160);
    config.bubble.open.birthRate = detail::clamp_float(values.m_openBubbleBirthRate->value(), 0.25F, 2.0F);
    config.bubble.open.sizeVariation = detail::clamp_float(values.m_openBubbleSizeVariation->value(), 0.0F, 1.0F);
    config.bubble.open.growthVariation = detail::clamp_float(values.m_openBubbleGrowthVariation->value(), 0.0F, 1.0F);
    config.bubble.open.interaction = detail::clamp_float(values.m_openBubbleInteraction->value(), 0.25F, 2.0F);
    config.bubble.open.damping = detail::clamp_float(values.m_openBubbleDamping->value(), 0.5F, 2.0F);
    config.bubble.open.buoyancy = detail::clamp_float(values.m_openBubbleBuoyancy->value(), 0.0F, 2.0F);
    config.bubble.open.linger = detail::clamp_float(values.m_openBubbleLinger->value(), 0.0F, 1.0F);
    config.smoke.open.duration = detail::clamp_float((values.m_openSmokeDurationSet.setByUser() || !values.m_globalSmokeDurationSet.setByUser() ? values.m_openSmokeDuration->value() : values.m_globalSmokeDuration->value()), 0.0F);
    config.smoke.open.turbulence = detail::clamp_float((values.m_openSmokeTurbulenceSet.setByUser() || !values.m_globalSmokeTurbulenceSet.setByUser() ? values.m_openSmokeTurbulence->value() : values.m_globalSmokeTurbulence->value()), 0.0F, 1.0F);
    config.smoke.open.direction = detail::parse_direction((values.m_openSmokeDirectionSet.setByUser() || !values.m_globalSmokeDirectionSet.setByUser() ? values.m_openSmokeDirection->value() : values.m_globalSmokeDirection->value()));
    config.close.enabled = values.m_closeEnabled->value();
    config.liquid.close.duration = (values.m_closeLiquidDurationSet.setByUser() || values.m_globalLiquidDurationSet.setByUser() ? detail::clamp_float((values.m_closeLiquidDurationSet.setByUser() || !values.m_globalLiquidDurationSet.setByUser() ? values.m_closeLiquidDuration->value() : values.m_globalLiquidDuration->value()), 0.0F) : -1.0);
    config.liquid.close.strength = detail::clamp_float((values.m_closeLiquidStrengthSet.setByUser() || !values.m_globalLiquidStrengthSet.setByUser() ? values.m_closeLiquidStrength->value() : values.m_globalLiquidStrength->value()), 0.0F, 1.5F);
    config.liquid.close.color = static_cast<std::uint32_t>((values.m_closeLiquidColorSet.setByUser() || !values.m_globalLiquidColorSet.setByUser() ? values.m_closeLiquidColor->value() : values.m_globalLiquidColor->value()));
    config.liquid.close.defaultAnimationEdge = detail::parse_default_animation_edge((values.m_closeDefaultAnimationEdgeSet.setByUser() || !values.m_globalLiquidDefaultAnimationEdgeSet.setByUser() ? values.m_closeDefaultAnimationEdge->value() : values.m_globalLiquidDefaultAnimationEdge->value()));
    config.liquid.close.side = detail::parse_side((values.m_closeSideSet.setByUser() || !values.m_globalLiquidSideSet.setByUser() ? values.m_closeSide->value() : values.m_globalLiquidSide->value()));
    config.liquid.close.edgeBounds = detail::parse_edge_bounds((values.m_closeEdgeBoundsSet.setByUser() || !values.m_globalLiquidEdgeBoundsSet.setByUser() ? values.m_closeEdgeBounds->value() : values.m_globalLiquidEdgeBounds->value()));
    config.liquid.close.splash = (values.m_closeSplashSet.setByUser() || !values.m_globalLiquidSplashSet.setByUser() ? values.m_closeSplash->value() : values.m_globalLiquidSplash->value());
    config.liquid.close.elasticity = detail::clamp_float((values.m_closeElasticitySet.setByUser() || !values.m_globalLiquidElasticitySet.setByUser() ? values.m_closeElasticity->value() : values.m_globalLiquidElasticity->value()), 0.0F, 1.0F);
    config.liquid.close.momentum = detail::clamp_float((values.m_closeMomentumSet.setByUser() || !values.m_globalLiquidMomentumSet.setByUser() ? values.m_closeMomentum->value() : values.m_globalLiquidMomentum->value()), 0.0F, 1.0F);
    config.paper.close.duration = detail::clamp_float((values.m_closePaperDurationSet.setByUser() || !values.m_globalPaperDurationSet.setByUser() ? values.m_closePaperDuration->value() : values.m_globalPaperDuration->value()), 0.0F);
    config.paper.close.crumpliness = detail::clamp_float((values.m_closePaperCrumplinessSet.setByUser() || !values.m_globalPaperCrumplinessSet.setByUser() ? values.m_closePaperCrumpliness->value() : values.m_globalPaperCrumpliness->value()), 0.0F, 1.0F);
    config.paper.close.shading = detail::clamp_float((values.m_closePaperShadingSet.setByUser() || !values.m_globalPaperShadingSet.setByUser() ? values.m_closePaperShading->value() : values.m_globalPaperShading->value()), 0.0F, 1.0F);
    config.paper.close.variation = detail::clamp_float((values.m_closePaperVariationSet.setByUser() || !values.m_globalPaperVariationSet.setByUser() ? values.m_closePaperVariation->value() : values.m_globalPaperVariation->value()), 0.0F, 1.0F);
    config.paper.close.foldDensity = detail::clamp_float((values.m_closePaperFoldDensitySet.setByUser() || !values.m_globalPaperFoldDensitySet.setByUser() ? values.m_closePaperFoldDensity->value() : values.m_globalPaperFoldDensity->value()), 0.0F, 1.0F);
    config.paper.close.paperStiffness = detail::clamp_float((!values.m_closePaperStiffnessSet.setByUser() && !values.m_globalPaperPaperStiffnessSet.setByUser() && values.m_paperStiffnessSet.setByUser() ? values.m_paperStiffness->value() : (values.m_closePaperStiffnessSet.setByUser() || !values.m_globalPaperPaperStiffnessSet.setByUser() ? values.m_closePaperStiffness->value() : values.m_globalPaperPaperStiffness->value())), 0.0F, 1.0F);
    config.paper.close.crumpleSize = detail::clamp_float((values.m_closePaperCrumpleSizeSet.setByUser() || !values.m_globalPaperCrumpleSizeSet.setByUser() ? values.m_closePaperCrumpleSize->value() : values.m_globalPaperCrumpleSize->value()), 0.1F, 0.45F);
    config.paper.close.crumpleOrigin = detail::parse_crumple_origin((values.m_closePaperCrumpleOriginSet.setByUser() || !values.m_globalPaperCrumpleOriginSet.setByUser() ? values.m_closePaperCrumpleOrigin->value() : values.m_globalPaperCrumpleOrigin->value()));
    config.paper.close.forceDirection = detail::parse_force_direction((values.m_closePaperForceDirectionSet.setByUser() || !values.m_globalPaperForceDirectionSet.setByUser() ? values.m_closePaperForceDirection->value() : values.m_globalPaperForceDirection->value()));
    config.paper.close.rotationIntensity = detail::clamp_float((values.m_closePaperRotationIntensitySet.setByUser() || !values.m_globalPaperRotationIntensitySet.setByUser() ? values.m_closePaperRotationIntensity->value() : values.m_globalPaperRotationIntensity->value()), 0.0F, 1.0F);
    config.paper.close.settling = detail::clamp_float((!values.m_closePaperSettlingSet.setByUser() && !values.m_globalPaperSettlingSet.setByUser() && values.m_paperSettleDampingSet.setByUser() ? values.m_paperSettleDamping->value() : (values.m_closePaperSettlingSet.setByUser() || !values.m_globalPaperSettlingSet.setByUser() ? values.m_closePaperSettling->value() : values.m_globalPaperSettling->value())), 0.0F, 1.0F);
    config.paper.close.simulationQuality = detail::parse_simulation_quality((values.m_closePaperSimulationQualitySet.setByUser() || !values.m_globalPaperSimulationQualitySet.setByUser() ? values.m_closePaperSimulationQuality->value() : values.m_globalPaperSimulationQuality->value()));
    config.cloth.close.duration = detail::clamp_float((values.m_closeClothDurationSet.setByUser() || !values.m_globalClothDurationSet.setByUser() ? values.m_closeClothDuration->value() : values.m_globalClothDuration->value()), 0.0F);
    config.cloth.close.defaultAnimationEdge = detail::parse_default_animation_edge((values.m_closeClothDefaultAnimationEdgeSet.setByUser() || !values.m_globalClothDefaultAnimationEdgeSet.setByUser() ? values.m_closeClothDefaultAnimationEdge->value() : values.m_globalClothDefaultAnimationEdge->value()));
    config.cloth.close.side = detail::parse_side((values.m_closeClothSideSet.setByUser() || !values.m_globalClothSideSet.setByUser() ? values.m_closeClothSide->value() : values.m_globalClothSide->value()));
    config.cloth.close.edgeBounds = detail::parse_edge_bounds((values.m_closeClothEdgeBoundsSet.setByUser() || !values.m_globalClothEdgeBoundsSet.setByUser() ? values.m_closeClothEdgeBounds->value() : values.m_globalClothEdgeBounds->value()));
    config.cloth.close.flutter = detail::clamp_float((values.m_closeClothFlutterSet.setByUser() || !values.m_globalClothFlutterSet.setByUser() ? values.m_closeClothFlutter->value() : values.m_globalClothFlutter->value()), 0.0F, 1.0F);
    config.cloth.close.shading = detail::clamp_float((values.m_closeClothShadingSet.setByUser() || !values.m_globalClothShadingSet.setByUser() ? values.m_closeClothShading->value() : values.m_globalClothShading->value()), 0.0F, 1.0F);
    config.cloth.close.shine = detail::clamp_float((values.m_closeClothShineSet.setByUser() || !values.m_globalClothShineSet.setByUser() ? values.m_closeClothShine->value() : values.m_globalClothShine->value()), 0.0F, 1.0F);
    config.cloth.close.variation = detail::clamp_float((values.m_closeClothVariationSet.setByUser() || !values.m_globalClothVariationSet.setByUser() ? values.m_closeClothVariation->value() : values.m_globalClothVariation->value()), 0.0F, 1.0F);
    config.swarm.close.duration = (values.m_closeSwarmDurationSet.setByUser() || values.m_globalSwarmDurationSet.setByUser() ? detail::clamp_float((values.m_closeSwarmDurationSet.setByUser() || !values.m_globalSwarmDurationSet.setByUser() ? values.m_closeSwarmDuration->value() : values.m_globalSwarmDuration->value()), 0.0F) : -1.0);
    config.swarm.close.loneWindowSides = detail::parse_lone_window_sides((values.m_closeSwarmLoneWindowSidesSet.setByUser() || !values.m_globalSwarmLoneWindowSidesSet.setByUser() ? values.m_closeSwarmLoneWindowSides->value() : values.m_globalSwarmLoneWindowSides->value())).value_or(Flags<LoneWindowSides>{15});
    config.bubble.close.duration = detail::clamp_float((values.m_closeBubbleDurationSet.setByUser() || !values.m_globalBubbleDurationSet.setByUser() ? values.m_closeBubbleDuration->value() : values.m_globalBubbleDuration->value()), 0.0F);
    config.smoke.close.duration = detail::clamp_float((values.m_closeSmokeDurationSet.setByUser() || !values.m_globalSmokeDurationSet.setByUser() ? values.m_closeSmokeDuration->value() : values.m_globalSmokeDuration->value()), 0.0F);
    config.smoke.close.turbulence = detail::clamp_float((values.m_closeSmokeTurbulenceSet.setByUser() || !values.m_globalSmokeTurbulenceSet.setByUser() ? values.m_closeSmokeTurbulence->value() : values.m_globalSmokeTurbulence->value()), 0.0F, 1.0F);
    config.smoke.close.direction = detail::parse_direction((values.m_closeSmokeDirectionSet.setByUser() || !values.m_globalSmokeDirectionSet.setByUser() ? values.m_closeSmokeDirection->value() : values.m_globalSmokeDirection->value()));
    config.swarm.movement.sprite = (values.m_movementSwarmSpriteSet.setByUser() ? values.m_movementSwarmSprite->value() : values.m_swarmSprite->value());
    config.swarm.movement.count = detail::clamp_int((values.m_movementSwarmCountSet.setByUser() ? values.m_movementSwarmCount->value() : values.m_swarmCount->value()), 1, 400);
    config.swarm.movement.speed = detail::clamp_float((values.m_movementSwarmSpeedSet.setByUser() ? values.m_movementSwarmSpeed->value() : values.m_swarmSpeed->value()), 100.0F, 6000.0F);
    config.swarm.movement.stagger = detail::clamp_float((values.m_movementSwarmStaggerSet.setByUser() ? values.m_movementSwarmStagger->value() : values.m_swarmStagger->value()), 0.0F, 3.0F);
    config.swarm.movement.cohesion = detail::clamp_float((values.m_movementSwarmCohesionSet.setByUser() ? values.m_movementSwarmCohesion->value() : values.m_swarmCohesion->value()), 0.0F, 1.0F);
    config.swarm.movement.separation = detail::clamp_float((values.m_movementSwarmSeparationSet.setByUser() ? values.m_movementSwarmSeparation->value() : values.m_swarmSeparation->value()), 0.0F, 1.0F);
    config.swarm.movement.agility = detail::clamp_float((values.m_movementSwarmAgilitySet.setByUser() ? values.m_movementSwarmAgility->value() : values.m_swarmAgility->value()), 0.0F, 1.0F);
    config.swarm.movement.settling = detail::clamp_float((values.m_movementSwarmSettlingSet.setByUser() ? values.m_movementSwarmSettling->value() : values.m_swarmSettling->value()), 0.0F, 1.0F);
    config.swarm.movement.wander = detail::clamp_float((values.m_movementSwarmWanderSet.setByUser() ? values.m_movementSwarmWander->value() : values.m_swarmWander->value()), 0.0F, 1.0F);
    config.swarm.movement.variation = detail::clamp_float((values.m_movementSwarmVariationSet.setByUser() ? values.m_movementSwarmVariation->value() : values.m_swarmVariation->value()), 0.0F, 1.0F);
    config.swarm.open.sprite = (values.m_openSwarmSpriteSet.setByUser() ? values.m_openSwarmSprite->value() : values.m_swarmSprite->value());
    config.swarm.open.count = detail::clamp_int((values.m_openSwarmCountSet.setByUser() ? values.m_openSwarmCount->value() : values.m_swarmCount->value()), 1, 400);
    config.swarm.open.speed = detail::clamp_float((values.m_openSwarmSpeedSet.setByUser() ? values.m_openSwarmSpeed->value() : values.m_swarmSpeed->value()), 100.0F, 6000.0F);
    config.swarm.open.stagger = detail::clamp_float((values.m_openSwarmStaggerSet.setByUser() ? values.m_openSwarmStagger->value() : values.m_swarmStagger->value()), 0.0F, 3.0F);
    config.swarm.open.cohesion = detail::clamp_float((values.m_openSwarmCohesionSet.setByUser() ? values.m_openSwarmCohesion->value() : values.m_swarmCohesion->value()), 0.0F, 1.0F);
    config.swarm.open.separation = detail::clamp_float((values.m_openSwarmSeparationSet.setByUser() ? values.m_openSwarmSeparation->value() : values.m_swarmSeparation->value()), 0.0F, 1.0F);
    config.swarm.open.agility = detail::clamp_float((values.m_openSwarmAgilitySet.setByUser() ? values.m_openSwarmAgility->value() : values.m_swarmAgility->value()), 0.0F, 1.0F);
    config.swarm.open.settling = detail::clamp_float((values.m_openSwarmSettlingSet.setByUser() ? values.m_openSwarmSettling->value() : values.m_swarmSettling->value()), 0.0F, 1.0F);
    config.swarm.open.wander = detail::clamp_float((values.m_openSwarmWanderSet.setByUser() ? values.m_openSwarmWander->value() : values.m_swarmWander->value()), 0.0F, 1.0F);
    config.swarm.open.variation = detail::clamp_float((values.m_openSwarmVariationSet.setByUser() ? values.m_openSwarmVariation->value() : values.m_swarmVariation->value()), 0.0F, 1.0F);
    config.swarm.close.sprite = (values.m_closeSwarmSpriteSet.setByUser() ? values.m_closeSwarmSprite->value() : values.m_swarmSprite->value());
    config.swarm.close.count = detail::clamp_int((values.m_closeSwarmCountSet.setByUser() ? values.m_closeSwarmCount->value() : values.m_swarmCount->value()), 1, 400);
    config.swarm.close.speed = detail::clamp_float((values.m_closeSwarmSpeedSet.setByUser() ? values.m_closeSwarmSpeed->value() : values.m_swarmSpeed->value()), 100.0F, 6000.0F);
    config.swarm.close.stagger = detail::clamp_float((values.m_closeSwarmStaggerSet.setByUser() ? values.m_closeSwarmStagger->value() : values.m_swarmStagger->value()), 0.0F, 3.0F);
    config.swarm.close.cohesion = detail::clamp_float((values.m_closeSwarmCohesionSet.setByUser() ? values.m_closeSwarmCohesion->value() : values.m_swarmCohesion->value()), 0.0F, 1.0F);
    config.swarm.close.separation = detail::clamp_float((values.m_closeSwarmSeparationSet.setByUser() ? values.m_closeSwarmSeparation->value() : values.m_swarmSeparation->value()), 0.0F, 1.0F);
    config.swarm.close.agility = detail::clamp_float((values.m_closeSwarmAgilitySet.setByUser() ? values.m_closeSwarmAgility->value() : values.m_swarmAgility->value()), 0.0F, 1.0F);
    config.swarm.close.settling = detail::clamp_float((values.m_closeSwarmSettlingSet.setByUser() ? values.m_closeSwarmSettling->value() : values.m_swarmSettling->value()), 0.0F, 1.0F);
    config.swarm.close.wander = detail::clamp_float((values.m_closeSwarmWanderSet.setByUser() ? values.m_closeSwarmWander->value() : values.m_swarmWander->value()), 0.0F, 1.0F);
    config.swarm.close.variation = detail::clamp_float((values.m_closeSwarmVariationSet.setByUser() ? values.m_closeSwarmVariation->value() : values.m_swarmVariation->value()), 0.0F, 1.0F);
    return config;
}

} // namespace hyprimate::config
