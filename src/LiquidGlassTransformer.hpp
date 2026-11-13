#pragma once

#include "ClothFlutter.hpp"
#include "Core.hpp"
#include "EdgeTransition.hpp"
#include "LiquidMesh.hpp"
#include "LiquidPose.hpp"
#include "PaperCrumple.hpp"
#include "PaperRenderer.hpp"
#include "SheetFrame.hpp"
#include "SolidFill.hpp"

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>
#include <hyprutils/memory/WeakPtr.hpp>

#include "config_generated.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

// Converts between the plugin's CBox and the core's liquid_glass::Box at the renderer boundary.
inline liquid_glass::Box toBox(const CBox &box) { return {box.x, box.y, box.w, box.h}; }
inline CBox              toCBox(const liquid_glass::Box &box) { return CBox{box.x, box.y, box.w, box.h}; }

// Liquid style: the movement mesh, the edge pop-out, and the window box the pop-out drives.
struct SLiquidStyle {
    liquid_glass::LiquidMesh     mesh{};
    liquid_glass::EdgeTransition transition{};
    // True from beginOpen until the layout box is restored: the real window box follows the pop-out.
    bool boxDriven = false;
    // The layout's box for the window (body, without decorations), tracked while the real box is driven.
    CBox goalBody{};
    // Last box written to the real box; a different goal in the animation means the layout retargeted it.
    Vector2D warpPosition{};
    Vector2D warpSize{};
    bool     warped = false;
    double   displacementGain = 1.0;
    // Pose offsets sampled from mesh and transition. Invalidated wherever a pose input changes.
    mutable liquid_glass::LiquidPoseGrid grid{};
    // Sized on first render; the render path only writes into them.
    std::vector<SMeshRenderVertex> vertices{};
    std::vector<SMeshRenderVertex> meniscusVertices{};
};

// Paper and cloth share these buffers: the displayed sheet and the GPU meshes built from it. Reused every frame. The
// frame is heap-held so liquid states, which share the variant's storage, do not reserve room for it.
struct SSheetBuffers {
    std::unique_ptr<liquid_glass::SheetFrame> frame = std::make_unique<liquid_glass::SheetFrame>();
    liquid_glass::Vec2                        captureSize{};
    std::vector<SMeshRenderVertex>            window{};
    std::vector<SMeshRenderVertex>            shade{};
};

struct SPaperStyle {
    liquid_glass::PaperCrumple crumple{};
    SSheetBuffers              sheet{};
};

struct SClothStyle {
    liquid_glass::ClothFlutter        cloth{};
    liquid_glass::ClothMovementParams movement{}; // the movement settings this state was built for
    SSheetBuffers                     sheet{};
};

// The one style a window's transformer or close record carries.
using SStyleState = std::variant<SLiquidStyle, SPaperStyle, SClothStyle>;

// The state a window starts from in `style`. `liquid`, `cloth`, and `seed` are the settings it is built for.
inline SStyleState initialStyle(liquid_glass::config::Style style, liquid_glass::MaterialParams liquid,
                                liquid_glass::ClothMovementParams cloth, std::uint64_t seed) {
    switch (style) {
    case liquid_glass::config::Style::Paper:
        return SPaperStyle{};
    case liquid_glass::config::Style::Cloth:
        return SClothStyle{.movement = cloth};
    case liquid_glass::config::Style::Liquid:
        break;
    }
    return SLiquidStyle{.mesh = liquid_glass::LiquidMesh{liquid, seed}};
}

// Paper and cloth styles: their sheet buffers, or nullptr for liquid.
SSheetBuffers       *sheetBuffersOf(SStyleState &style) noexcept;
const SSheetBuffers *sheetBuffersOf(const SStyleState &style) noexcept;
// Advances a paper or cloth style's motion. A no-op for liquid.
void advanceSheetStyle(SStyleState &style, double elapsed);
// Recomputes a paper or cloth style's displayed frame from its state. A no-op for liquid.
void refreshSheetFrame(SStyleState &style);

class CLiquidGlassTransformer final : public Render::IWindowTransformer {
  public:
    // `state` fixes the window's style for its lifetime; the runtime rebuilds the transformer when the style changes.
    CLiquidGlassTransformer(PHLWINDOWREF window, SStyleState state, SP<CSolidFill> solid, SP<CPaperShade> shade);
    ~CLiquidGlassTransformer() override = default;

    CLiquidGlassTransformer(const CLiquidGlassTransformer &) = delete;
    CLiquidGlassTransformer &operator=(const CLiquidGlassTransformer &) = delete;

    Render::SWindowTransformBuffer transform(Render::CRenderContext &ctx, const Render::SWindowTransformBuffer &in,
                                             const Render::SWindowTransformContext &context) override;
    int                            priority() const override;
    bool                           active() const override;
    bool                           blocksDirectScanout() const override;
    CBox                           transformedExtents(const CBox &currentBox) const override;
    CBox                           sourceBoxForOutput(const CBox &outputBox, const CBox &inputBox) const override;

    // True when the state was built for these settings; a mismatch means the runtime rebuilds the transformer.
    bool builtFor(liquid_glass::config::Style style, liquid_glass::MaterialParams liquid,
                  liquid_glass::ClothMovementParams cloth) const noexcept;

    // Liquid style. The runtime only calls these while the configured style is Liquid.
    liquid_glass::LiquidMesh           &mesh() noexcept { return liquid().mesh; }
    const liquid_glass::LiquidMesh     &mesh() const noexcept { return liquid().mesh; }
    liquid_glass::EdgeTransition       &transition() noexcept { return liquid().transition; }
    const liquid_glass::EdgeTransition &transition() const noexcept { return liquid().transition; }
    // `captureSize` is the decorated capture size the physics advances with; it also refreshes the cached gain.
    void beginOpen(const liquid_glass::EdgeFrame &frame, const liquid_glass::TransitionParams &params, double strength,
                   liquid_glass::Vec2 captureSize);
    // The pose the transformer draws with.
    liquid_glass::LiquidPose renderPose() const;
    // The same pop-out for a closing snapshot taken now: the window as drawn at its driven box, mapped to the rest box.
    liquid_glass::LiquidPose closePose() const;
    // Follows the pop-out with the window's real box, so decorations that draw at that box (borders, HyprGlass) grow
    // out of the bead with the window. Called once per physics advance after the transition has stepped; restores the
    // layout box when the pop-out ends.
    void driveBox();
    // Ends the pop-out and returns the real box to the layout's.
    void cancelOpening();
    void cancelMovement();
    // Recomputes the cached movement-field gain. Called once per physics advance, never from the render path.
    void refreshPose(liquid_glass::Vec2 captureSize);
    // Advances the edge pop-out and the movement field one physics step; invalidates the cached pose grid.
    void advanceLiquid(double elapsed, liquid_glass::Vec2 size, double strength, double drive);

    // Paper style. Sheet animations stay at the capture box and do not drive the window's layout box.
    void beginPaperOpen(const liquid_glass::PaperParams &params, std::uint64_t seed, liquid_glass::Vec2 captureSize);

    // Cloth style.
    void beginClothOpen(const liquid_glass::ClothParams &params, std::uint64_t seed, const liquid_glass::Box &window,
                        std::span<const liquid_glass::Box> obstacles);
    void beginClothMovement(std::uint64_t seed, const liquid_glass::Box &window, liquid_glass::Vec2 grabUV);
    void updateClothMovement(const liquid_glass::Box &window, double elapsedSeconds);
    // Whether the active style is liquid. The liquid accessors above require it.
    bool liquidStyle() const noexcept { return std::holds_alternative<SLiquidStyle>(m_style); }
    // Paper or cloth sheet is still animating.
    bool sheetActive() const noexcept;
    // Cloth is following a window move, or opening (not moving).
    bool clothMoving() const noexcept;
    bool clothOpening() const noexcept;

    // Paper and cloth: advances the sheet and refreshes its frame. A no-op for other styles or an idle sheet.
    void advanceSheet(double elapsed, liquid_glass::Vec2 captureSize);

    // Moves the whole state out for a close record. The transformer keeps a fresh liquid state; it is detached next.
    SStyleState takeStyle() noexcept;

    // Set by the runtime right before this transformer is removed from its owner list.
    void markDetached();

  private:
    SLiquidStyle       &liquid() noexcept { return std::get<SLiquidStyle>(m_style); }
    const SLiquidStyle &liquid() const noexcept { return std::get<SLiquidStyle>(m_style); }
    // Whether the active style is still deforming the window.
    bool   deforming() const noexcept;
    double sheetOpacity() const noexcept;
    bool   sheetShaded() const noexcept;
    void   restoreBox();

    PHLWINDOWREF    m_window;
    SStyleState     m_style;
    SP<CSolidFill>  m_solid;
    SP<CPaperShade> m_shade;
    bool            m_detached = false;
};
