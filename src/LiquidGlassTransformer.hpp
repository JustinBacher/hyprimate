#pragma once

#include "EdgeTransition.hpp"
#include "LiquidMesh.hpp"
#include "PaperCrumple.hpp"
#include "PaperRenderer.hpp"
#include "SolidFill.hpp"

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>
#include <hyprutils/memory/WeakPtr.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Render mesh resolution: LIQUID_RENDER_CELLS quads per side, sampled from the lattice by sampleDisplacement.
inline constexpr int LIQUID_RENDER_CELLS = 32;
// Two triangles (six vertices) per cell.
inline constexpr std::size_t LIQUID_RENDER_VERTEX_COUNT =
    static_cast<std::size_t>(LIQUID_RENDER_CELLS) * LIQUID_RENDER_CELLS * 6;
// Meniscus strip resolution along the edge; two triangles (six vertices) per column.
inline constexpr int         LIQUID_MENISCUS_COLUMNS = 96;
inline constexpr std::size_t LIQUID_MENISCUS_VERTEX_COUNT = static_cast<std::size_t>(LIQUID_MENISCUS_COLUMNS) * 6;

// One pose for both live-window transforms and retained closing snapshots. With `boxPose` the real window box is driven
// to follow the pop-out, so the renderer applies only the movement field to the window and `openOffset` only places the
// edge liquid. Without it (closing snapshots) `compensation` and `openOffset` carry the window from its drawn box to
// the rest box.
struct SLiquidPose {
    const liquid_glass::LiquidMesh     &mesh;
    const liquid_glass::EdgeTransition &transition;
    liquid_glass::Vec2                  compensation{1.0, 1.0};
    liquid_glass::Vec2                  openOffset{0.0, 0.0};
    // Cached movement-field guard (EdgeTransition::displacementGain); 1 when idle.
    double displacementGain = 1.0;
    bool   boxPose = false;
};

// Advances the edge pop-out and the movement field one frame. `drive` (0-1) is the share of pop-out travel fed to the
// mesh as drag; 0 is a rigid pop.
void advanceLiquidPose(liquid_glass::LiquidMesh &mesh, liquid_glass::EdgeTransition &transition, double elapsed,
                       liquid_glass::Vec2 size, double strength, double drive);

// Union of every displayed window sample and the meniscus, in logical pixels (unpadded). Clipped to the visible
// half-plane while the transition is active; never empty.
CBox liquidRenderExtents(const SLiquidPose &pose, const CBox &logicalBox);

// The half-plane inside the screen edge in logical pixels, or nullopt when no transition is active.
std::optional<CBox> liquidVisibleBox(const SLiquidPose &pose, const CBox &logicalBox);

// Bounding box of the edge liquid in logical pixels, or nullopt when there is no meniscus.
std::optional<CBox> liquidMeniscusExtents(const SLiquidPose &pose, const CBox &logicalBox);

// Fills `out` with the smooth render mesh. `sourceDevice` is the undeformed window box in device pixels,
// `scale` is the monitor scale (displacements are logical pixels), `outputDevice` is the output box in device
// pixels, and `textureOrigin`/`textureSize` describe the source texture in device pixels.
// `out` is resized to LIQUID_RENDER_VERTEX_COUNT only when its size differs, so the steady state does not allocate.
void fillLiquidRenderMesh(const SLiquidPose &pose, const CBox &sourceDevice, double scale, const CBox &outputDevice,
                          liquid_glass::Vec2 textureOrigin, liquid_glass::Vec2 textureSize,
                          std::vector<SMeshRenderVertex> &out);

// Fills `out` with the edge liquid strip for `pose`. Geometry is in the same device space as fillLiquidRenderMesh.
void fillLiquidMeniscusMesh(const SLiquidPose &pose, const CBox &sourceDevice, double scale, const CBox &outputDevice,
                            std::vector<SMeshRenderVertex> &out);

class CLiquidGlassTransformer final : public Render::IWindowTransformer {
  public:
    explicit CLiquidGlassTransformer(PHLWINDOWREF window, liquid_glass::MotionMode mode,
                                     liquid_glass::MaterialParams params, std::uint64_t seed, SP<CSolidFill> solid,
                                     SP<CPaperShade> shade);
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

    liquid_glass::LiquidMesh           &mesh() noexcept { return m_mesh; }
    const liquid_glass::LiquidMesh     &mesh() const noexcept { return m_mesh; }
    liquid_glass::EdgeTransition       &transition() noexcept { return m_transition; }
    const liquid_glass::EdgeTransition &transition() const noexcept { return m_transition; }
    // `captureSize` is the decorated capture size the physics advances with; it also refreshes the cached gain.
    void beginOpen(const liquid_glass::EdgeFrame &frame, const liquid_glass::MaterialParams &params, double strength,
                   liquid_glass::Vec2 captureSize);
    // The pose the transformer draws with.
    SLiquidPose renderPose() const;
    // The same pop-out for a closing snapshot taken now: the window as drawn at its driven box, mapped to the rest box.
    SLiquidPose closePose() const;
    // Follows the pop-out with the window's real box, so decorations that draw at that box (borders, HyprGlass) grow
    // out of the bead with the window. Called once per physics advance after the transition has stepped; restores the
    // layout box when the pop-out ends.
    void driveBox();
    // Ends the pop-out and returns the real box to the layout's.
    void cancelOpening();
    // Recomputes the cached movement-field gain. Called once per physics advance, never from the render path.
    void refreshPose(liquid_glass::Vec2 captureSize);
    // Starts the paper crumple at the window's box. `captureSize` is the decorated capture size; the transformer
    // cancels any liquid pop-out first, so the box drive is never shared with the paper.
    void beginPaperOpen(const liquid_glass::PaperParams &params, std::uint64_t seed, liquid_glass::Vec2 captureSize);
    // Advances the paper model. Facets are recomputed lazily by the render path, once per drawn frame: physics ticks
    // run several times per frame, and refreshing the facets on each tick multiplied their cost for nothing.
    void                              advancePaper(double elapsed, liquid_glass::Vec2 captureSize);
    liquid_glass::PaperCrumple       &paper() noexcept { return m_paper; }
    const liquid_glass::PaperCrumple &paper() const noexcept { return m_paper; }

    // Set by the runtime right before this transformer is removed from its owner list.
    void markDetached() {
        m_detached = true;
        m_mesh.reset();
        cancelOpening();
    }

  private:
    // The paper's facets for its current state, recomputed only after the model advanced.
    const liquid_glass::PaperFacets &paperFacets() const noexcept;
    void                             restoreBox();
    PHLWINDOWREF                     m_window;
    liquid_glass::LiquidMesh         m_mesh;
    liquid_glass::EdgeTransition     m_transition;
    SP<CSolidFill>                   m_solid;
    SP<CPaperShade>                  m_shade;
    // True from beginOpen until the layout box is restored: the real window box follows the pop-out.
    bool m_boxDriven = false;
    // The layout's box for the window (body, without decorations), tracked while the real box is driven.
    CBox m_goalBody{};
    // Last box written to the real box; a different goal in the animation means the layout retargeted it.
    Vector2D m_warpPosition{};
    Vector2D m_warpSize{};
    bool     m_warped = false;
    double   m_displacementGain = 1.0;
    bool     m_detached = false;
    // Fixed size, sized once in the constructor; the render path only writes into them.
    std::vector<SMeshRenderVertex> m_vertices;
    std::vector<SMeshRenderVertex> m_meniscusVertices;
    // Paper crumple: the model, its facets, and the window and shade meshes. Each is sized once; the render path only
    // writes.
    liquid_glass::PaperCrumple        m_paper;
    mutable liquid_glass::PaperFacets m_paperFacets{};
    mutable bool                      m_paperFacetsStale = false;
    liquid_glass::Vec2                m_paperCaptureSize{};
    std::vector<SMeshRenderVertex>    m_paperVertices;
    std::vector<SMeshRenderVertex>    m_paperShadeVertices;
};
