// Generated from config/settings.json; do not edit.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace hyprimate::config {

template <class E> struct Flags {
    std::uint8_t bits = 0;
    [[nodiscard]] constexpr bool has(E value) const noexcept { return (bits & static_cast<std::uint8_t>(value)) != 0; }
    constexpr bool operator==(const Flags &) const = default;
};

enum class Shape : std::uint8_t { Pillow, Ellipse, Circle };
enum class DefaultAnimationEdge : std::uint8_t { Bottom, Top, Left, Right };
enum class EdgeBounds : std::uint8_t { Screen, WorkArea };
enum class CrumpleOrigin : std::uint8_t { Center, Random, Cursor, Left, Right, Top, Bottom };
enum class ForceDirection : std::uint8_t { Inward, Directional, Rotational };
enum class SimulationQuality : std::uint8_t { Adaptive, Low, Medium, High };
enum class LoneWindowSides : std::uint8_t { Top = 1, Bottom = 2, Left = 4, Right = 8 };
enum class Direction : std::uint8_t { Inside, Top, Bottom, Left, Right };
enum class Side : std::uint8_t { Auto, Nearest, Origin, Top, Bottom, Left, Right };
enum class WorkspaceStyle : std::uint8_t { Rotation, PlanetHop, Cube, Lava };
inline constexpr std::array WORKSPACE_STYLES{WorkspaceStyle::Rotation, WorkspaceStyle::PlanetHop, WorkspaceStyle::Cube, WorkspaceStyle::Lava};
enum class RotationGapFillStyle : std::uint8_t { Morph, Background, Wallpaper, Still };
enum class PlanetHopWindowMode : std::uint8_t { Surface, Floating, Fade };
enum class PlanetHopOverlayMode : std::uint8_t { World, Hud, Fade };
enum class PlanetHopPlanetLayout : std::uint8_t { Focused, Monitor, All, SolarSystem };
enum class CubeVariant : std::uint8_t { Solid, InsideOut };
enum class LavaVariant : std::uint8_t { CoolingFlow, BasaltFlow, Pahoehoe, CrustPlates };
enum class LavaWindowBehavior : std::uint8_t { MeltWithWorkspace, BurnUp };
enum class Style : std::uint8_t { Liquid, Paper, Cloth, Swarm, Bubble, Smoke, Burn, Construction, Shader };
enum class ClosingStyle : std::uint8_t { Consume, Curl };
enum class DebrisDirection : std::uint8_t { Natural, Fall, Rise, Wind };
enum class PaperMoveStyle : std::uint8_t { Flutter, Glide };
enum class Variant : std::uint8_t { Native, CrewCarry, Crane, PieceByPiece };

struct SConstructionShared {
    double duration{};
    double workerSize{};
    bool operator==(const SConstructionShared &) const = default;
};

struct SConstructionTransition {
    double duration{};
    bool operator==(const SConstructionTransition &) const = default;
};

struct SConstructionMovement {
    Variant variant{};
    bool operator==(const SConstructionMovement &) const = default;
};

struct SConstructionSettings {
    SConstructionShared shared{};
    SConstructionTransition open{};
    SConstructionTransition close{};
    SConstructionMovement movement{};
    bool operator==(const SConstructionSettings &) const = default;
};

struct SBurnShared {
    double duration{};
    double variation{};
    double fragmentDensity{};
    double emberDensity{};
    double windDirection{};
    double windStrength{};
    double gravityStrength{};
    double scorchLifetime{};
    double healingDuration{};
    bool operator==(const SBurnShared &) const = default;
};

struct SBurnMovement {
    double smokeDensity{};
    bool operator==(const SBurnMovement &) const = default;
};

struct SBurnTransition {
    double duration{};
    ClosingStyle closingStyle{};
    DebrisDirection debrisDirection{};
    double curlIntensity{};
    bool operator==(const SBurnTransition &) const = default;
};

struct SBurnSettings {
    SBurnShared shared{};
    SBurnMovement movement{};
    SBurnTransition open{};
    SBurnTransition close{};
    bool operator==(const SBurnSettings &) const = default;
};

struct SLiquidShared {
    double strength{};
    double viscosity{};
    double elasticity{};
    double momentum{};
    double variation{};
    double duration{};
    std::uint32_t color{};
    DefaultAnimationEdge defaultAnimationEdge{};
    EdgeBounds edgeBounds{};
    double emergeDrag{};
    Side side{};
    bool splash{};
    bool operator==(const SLiquidShared &) const = default;
};

struct SLiquidMovement {
    double strength{};
    double viscosity{};
    double elasticity{};
    double momentum{};
    double variation{};
    bool operator==(const SLiquidMovement &) const = default;
};

struct SLiquidTransition {
    double duration{};
    double strength{};
    std::uint32_t color{};
    DefaultAnimationEdge defaultAnimationEdge{};
    EdgeBounds edgeBounds{};
    double emergeDrag{};
    double elasticity{};
    double momentum{};
    Side side{};
    bool splash{};
    bool operator==(const SLiquidTransition &) const = default;
};

struct SLiquidSettings {
    SLiquidShared shared{};
    SLiquidMovement movement{};
    SLiquidTransition open{};
    SLiquidTransition close{};
    bool operator==(const SLiquidSettings &) const = default;
};

struct SClothShared {
    double strength{};
    double springiness{};
    double gravity{};
    double flutter{};
    double shading{};
    double shine{};
    double variation{};
    double duration{};
    DefaultAnimationEdge defaultAnimationEdge{};
    EdgeBounds edgeBounds{};
    Side side{};
    bool operator==(const SClothShared &) const = default;
};

struct SClothMovement {
    double strength{};
    double springiness{};
    double gravity{};
    double flutter{};
    double shading{};
    double shine{};
    double variation{};
    bool operator==(const SClothMovement &) const = default;
};

struct SClothTransition {
    double duration{};
    DefaultAnimationEdge defaultAnimationEdge{};
    EdgeBounds edgeBounds{};
    double flutter{};
    double shading{};
    double shine{};
    double variation{};
    Side side{};
    bool operator==(const SClothTransition &) const = default;
};

struct SClothSettings {
    SClothShared shared{};
    SClothMovement movement{};
    SClothTransition open{};
    SClothTransition close{};
    bool operator==(const SClothSettings &) const = default;
};

struct SBubbleShared {
    double strength{};
    double microbubbles{};
    double trail{};
    Shape shape{};
    double roundness{};
    double shapeSpeed{};
    double transparency{};
    double duration{};
    bool operator==(const SBubbleShared &) const = default;
};

struct SBubbleMovement {
    double strength{};
    double microbubbles{};
    double trail{};
    Shape shape{};
    double roundness{};
    double shapeSpeed{};
    double transparency{};
    bool operator==(const SBubbleMovement &) const = default;
};

struct SBubbleTransition {
    double duration{};
    int count{};
    double birthRate{};
    double sizeVariation{};
    double growthVariation{};
    double interaction{};
    double damping{};
    double buoyancy{};
    double linger{};
    bool operator==(const SBubbleTransition &) const = default;
};

struct SBubbleSettings {
    SBubbleShared shared{};
    SBubbleMovement movement{};
    SBubbleTransition open{};
    SBubbleTransition close{};
    bool operator==(const SBubbleSettings &) const = default;
};

struct SSmokeShared {
    double strength{};
    double turbulence{};
    double trail{};
    double duration{};
    Direction direction{};
    std::uint32_t color{};
    double density{};
    double variation{};
    bool operator==(const SSmokeShared &) const = default;
};

struct SSmokeMovement {
    double strength{};
    double turbulence{};
    double trail{};
    double dragFollow{};
    double trailLength{};
    double trailLifetime{};
    double trailThickness{};
    double reform{};
    double settleSpeed{};
    double mass{};
    double roundness{};
    bool operator==(const SSmokeMovement &) const = default;
};

struct SSmokeTransition {
    double duration{};
    double turbulence{};
    Direction direction{};
    bool operator==(const SSmokeTransition &) const = default;
};

struct SSmokeSettings {
    SSmokeShared shared{};
    SSmokeMovement movement{};
    SSmokeTransition open{};
    SSmokeTransition close{};
    bool operator==(const SSmokeSettings &) const = default;
};

struct SPaperShared {
    double duration{};
    double crumpliness{};
    double shading{};
    double variation{};
    double foldDensity{};
    double paperStiffness{};
    double crumpleSize{};
    CrumpleOrigin crumpleOrigin{};
    ForceDirection forceDirection{};
    double rotationIntensity{};
    double settling{};
    SimulationQuality simulationQuality{};
    double stiffness{};
    double inertia{};
    double settleDamping{};
    bool operator==(const SPaperShared &) const = default;
};

struct SPaperMoveFlutter {
    double airResistance{};
    double intensity{};
    double frequency{};
    double lift{};
    double torsion{};
    bool operator==(const SPaperMoveFlutter &) const = default;
};

struct SPaperMoveGlide {
    double friction{};
    double cornerLift{};
    double edgeCurl{};
    double shadowSeparation{};
    bool operator==(const SPaperMoveGlide &) const = default;
};

struct SPaperMove {
    PaperMoveStyle style{};
    SPaperMoveFlutter flutter{};
    SPaperMoveGlide glide{};
    bool operator==(const SPaperMove &) const = default;
};

struct SPaperResize {
    double foldIntensity{};
    double curlIntensity{};
    double creaseDensity{};
    bool operator==(const SPaperResize &) const = default;
};

struct SPaperTransition {
    double duration{};
    double crumpliness{};
    double shading{};
    double variation{};
    double foldDensity{};
    double paperStiffness{};
    double crumpleSize{};
    CrumpleOrigin crumpleOrigin{};
    ForceDirection forceDirection{};
    double rotationIntensity{};
    double unfoldTension{};
    double settling{};
    double creasePersistence{};
    SimulationQuality simulationQuality{};
    bool operator==(const SPaperTransition &) const = default;
};

struct SPaperSettings {
    SPaperShared shared{};
    SPaperMove move{};
    SPaperResize resize{};
    SPaperTransition open{};
    SPaperTransition close{};
    bool operator==(const SPaperSettings &) const = default;
};

struct SSwarmShared {
    double duration{};
    Flags<LoneWindowSides> loneWindowSides{};
    std::string sprite;
    int count{};
    double speed{};
    double stagger{};
    double cohesion{};
    double separation{};
    double agility{};
    double settling{};
    double wander{};
    double variation{};
    bool operator==(const SSwarmShared &) const = default;
};

struct SSwarmTransition {
    double duration{};
    Flags<LoneWindowSides> loneWindowSides{};
    std::string sprite;
    int count{};
    double speed{};
    double stagger{};
    double cohesion{};
    double separation{};
    double agility{};
    double settling{};
    double wander{};
    double variation{};
    bool operator==(const SSwarmTransition &) const = default;
};

struct SSwarmMovement {
    std::string sprite;
    int count{};
    double speed{};
    double stagger{};
    double cohesion{};
    double separation{};
    double agility{};
    double settling{};
    double wander{};
    double variation{};
    bool operator==(const SSwarmMovement &) const = default;
};

struct SSwarmSettings {
    SSwarmShared shared{};
    SSwarmTransition open{};
    SSwarmTransition close{};
    SSwarmMovement movement{};
    bool operator==(const SSwarmSettings &) const = default;
};

struct SWorkspaceRotation {
    double duration{};
    std::string curve;
    RotationGapFillStyle gapFillStyle{};
    std::uint32_t gapFillColor{};
    double gapFillBlur{};
    bool keepOverlaysStatic{};
    bool operator==(const SWorkspaceRotation &) const = default;
};

struct SWorkspacePlanetHop {
    PlanetHopWindowMode windowMode{};
    PlanetHopOverlayMode overlayMode{};
    PlanetHopPlanetLayout planetLayout{};
    double duration{};
    std::string curve;
    double liveBoundary{};
    int samplingQuality{};
    double planetSize{};
    double planetSeparation{};
    double solarVerticalSpread{};
    double solarBackgroundScale{};
    double planetCoverage{};
    double indirectBrightness{};
    double elevation{};
    double fade{};
    bool rotationEnabled{};
    double rotationLimit{};
    bool starsEnabled{};
    double starDensity{};
    double starParallax{};
    bool atmosphereEnabled{};
    double atmosphereIntensity{};
    double atmosphereThickness{};
    bool lightingEnabled{};
    double lightDirectionX{};
    double lightDirectionY{};
    double lightDirectionZ{};
    double nightBrightness{};
    bool cloudsEnabled{};
    double cloudCoverage{};
    double cloudOpacity{};
    double cloudSpeed{};
    bool depthEnabled{};
    double depthStrength{};
    std::string depthSource;
    bool operator==(const SWorkspacePlanetHop &) const = default;
};

struct SWorkspaceCubeSolid {
    double cubeTilt{};
    double zoom{};
    bool operator==(const SWorkspaceCubeSolid &) const = default;
};

struct SWorkspaceCubeInsideOut {
    double cubeTilt{};
    double zoom{};
    bool operator==(const SWorkspaceCubeInsideOut &) const = default;
};

struct SWorkspaceCube {
    CubeVariant variant{};
    double duration{};
    std::string curve;
    SWorkspaceCubeSolid solid{};
    SWorkspaceCubeInsideOut insideOut{};
    bool operator==(const SWorkspaceCube &) const = default;
};

struct SWorkspaceLavaCoolingFlow {
    double leadingBulge{};
    double trailingStretch{};
    double moltenTurbulence{};
    bool operator==(const SWorkspaceLavaCoolingFlow &) const = default;
};

struct SWorkspaceLavaBasaltFlow {
    double crustThickness{};
    double fissureWidth{};
    double emberVisibility{};
    double massDensity{};
    bool operator==(const SWorkspaceLavaBasaltFlow &) const = default;
};

struct SWorkspaceLavaPahoehoe {
    double ropeStrength{};
    double foldScale{};
    double rippleSpeed{};
    double surfaceRoll{};
    bool operator==(const SWorkspaceLavaPahoehoe &) const = default;
};

struct SWorkspaceLavaCrustPlates {
    double plateSize{};
    double plateRigidity{};
    double seamGlow{};
    double plateDrift{};
    double plateCompression{};
    double submeltVisibility{};
    bool operator==(const SWorkspaceLavaCrustPlates &) const = default;
};

struct SWorkspaceLava {
    LavaVariant variant{};
    LavaWindowBehavior windowBehavior{};
    double viscosity{};
    double coolingRate{};
    double crustAmount{};
    double glowIntensity{};
    double detailPreservation{};
    double trailSmear{};
    double edgeDrip{};
    double ashAmount{};
    SWorkspaceLavaCoolingFlow coolingFlow{};
    SWorkspaceLavaBasaltFlow basaltFlow{};
    SWorkspaceLavaPahoehoe pahoehoe{};
    SWorkspaceLavaCrustPlates crustPlates{};
    bool operator==(const SWorkspaceLava &) const = default;
};

struct SWorkspaceSettings {
    bool enabled{};
    WorkspaceStyle style{};
    std::string effect;
    std::string effectParameters;
    double effectDuration{};
    SWorkspaceRotation rotation{};
    SWorkspacePlanetHop planetHop{};
    SWorkspaceCube cube{};
    SWorkspaceLava lava{};
    bool operator==(const SWorkspaceSettings &) const = default;
};

struct SWallpaperSettings {
    std::string effect;
    std::string effectParameters;
    int effectDuration{};
    std::string sourceNamespace;
    bool operator==(const SWallpaperSettings &) const = default;
};

struct STransitionConfig {
    std::string effect;
    std::string effectParameters;
    double effectDuration{};
    bool enabled{};
    bool operator==(const STransitionConfig &) const = default;
};

struct SMoveShared {
    std::string effect;
    std::string effectParameters;
    double effectDuration{};
    bool operator==(const SMoveShared &) const = default;
};

struct SMoveSettings {
    SMoveShared shared{};
    bool operator==(const SMoveSettings &) const = default;
};

struct SResizeShared {
    std::string effect;
    std::string effectParameters;
    double effectDuration{};
    bool operator==(const SResizeShared &) const = default;
};

struct SResizeSettings {
    SResizeShared shared{};
    bool operator==(const SResizeSettings &) const = default;
};

struct SMovement {
    bool enabled{};
    bool workspaceMovement{};
    bool operator==(const SMovement &) const = default;
};

struct SConfig {
    SConstructionSettings construction{};
    SBurnSettings burn{};
    SLiquidSettings liquid{};
    SClothSettings cloth{};
    SBubbleSettings bubble{};
    SSmokeSettings smoke{};
    SPaperSettings paper{};
    SSwarmSettings swarm{};
    SWorkspaceSettings workspace{};
    SWallpaperSettings wallpaper{};
    bool enabled{};
    Style style{};
    STransitionConfig open{};
    STransitionConfig close{};
    SMoveSettings move{};
    SResizeSettings resize{};
    SMovement movement{};
    bool operator==(const SConfig &) const = default;
};

} // namespace hyprimate::config
