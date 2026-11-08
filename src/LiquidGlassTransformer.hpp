#pragma once

#include "EdgeTransition.hpp"
#include "LiquidMesh.hpp"
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
inline constexpr int LIQUID_MENISCUS_COLUMNS = 96;
inline constexpr std::size_t LIQUID_MENISCUS_VERTEX_COUNT = static_cast<std::size_t>(LIQUID_MENISCUS_COLUMNS) * 6;

// One pose for both live-window transforms and retained closing snapshots.
struct SLiquidPose {
    const liquid_glass::LiquidMesh&       mesh;
    const liquid_glass::EdgeTransition&   transition;
    liquid_glass::Vec2                    compensation{1.0, 1.0};
    liquid_glass::Vec2                    openOffset{0.0, 0.0};
    // Cached movement-field guard (EdgeTransition::displacementGain); 1 when idle.
    double                                displacementGain = 1.0;
};

// Advances the edge pop-out and the movement field one frame. `drive` (0-1) is the share of pop-out travel fed to the
// mesh as drag; 0 is a rigid pop.
void advanceLiquidPose(liquid_glass::LiquidMesh& mesh, liquid_glass::EdgeTransition& transition, double elapsed, liquid_glass::Vec2 size,
                       double strength, double drive);

// Union of every displayed window sample and the meniscus, in logical pixels (unpadded). Clipped to the visible
// half-plane while the transition is active; never empty.
CBox liquidRenderExtents(const SLiquidPose& pose, const CBox& logicalBox);

// The half-plane inside the screen edge in logical pixels, or nullopt when no transition is active.
std::optional<CBox> liquidVisibleBox(const SLiquidPose& pose, const CBox& logicalBox);

// Bounding box of the edge liquid in logical pixels, or nullopt when there is no meniscus.
std::optional<CBox> liquidMeniscusExtents(const SLiquidPose& pose, const CBox& logicalBox);

// Fills `out` with the smooth render mesh. `sourceDevice` is the undeformed window box in device pixels,
// `scale` is the monitor scale (displacements are logical pixels), `outputDevice` is the output box in device
// pixels, and `textureOrigin`/`textureSize` describe the source texture in device pixels.
// `out` is resized to LIQUID_RENDER_VERTEX_COUNT only when its size differs, so the steady state does not allocate.
void fillLiquidRenderMesh(const SLiquidPose& pose, const CBox& sourceDevice, double scale, const CBox& outputDevice,
                          liquid_glass::Vec2 textureOrigin, liquid_glass::Vec2 textureSize, std::vector<SMeshRenderVertex>& out);

// Fills `out` with the edge liquid strip for `pose`. Geometry is in the same device space as fillLiquidRenderMesh.
void fillLiquidMeniscusMesh(const SLiquidPose& pose, const CBox& sourceDevice, double scale, const CBox& outputDevice,
                            std::vector<SMeshRenderVertex>& out);

class CLiquidGlassTransformer final : public Render::IWindowTransformer {
  public:
    explicit CLiquidGlassTransformer(PHLWINDOWREF window, liquid_glass::MotionMode mode, liquid_glass::MaterialParams params, std::uint64_t seed,
                                     SP<CSolidFill> solid);
    ~CLiquidGlassTransformer() override = default;

    CLiquidGlassTransformer(const CLiquidGlassTransformer&)            = delete;
    CLiquidGlassTransformer& operator=(const CLiquidGlassTransformer&) = delete;

    Render::SWindowTransformBuffer transform(Render::CRenderContext& ctx, const Render::SWindowTransformBuffer& in, const Render::SWindowTransformContext& context) override;
    int                            priority() const override;
    bool                           active() const override;
    bool                           blocksDirectScanout() const override;
    CBox                           transformedExtents(const CBox& currentBox) const override;
    CBox                           sourceBoxForOutput(const CBox& outputBox, const CBox& inputBox) const override;

    liquid_glass::LiquidMesh&       mesh() noexcept { return m_mesh; }
    const liquid_glass::LiquidMesh& mesh() const noexcept { return m_mesh; }
    liquid_glass::EdgeTransition&       transition() noexcept { return m_transition; }
    const liquid_glass::EdgeTransition& transition() const noexcept { return m_transition; }
    // `captureSize` is the decorated capture size the physics advances with; it also refreshes the cached gain.
    void beginOpen(const liquid_glass::EdgeFrame& frame, const liquid_glass::MaterialParams& params, double strength, liquid_glass::Vec2 captureSize);
    SLiquidPose renderPose() const;
    void cancelOpening() noexcept { m_transition.reset(); m_openGeometry = false; m_displacementGain = 1.0; m_compensation = {1.0, 1.0}; }
    // Recomputes the cached movement-field gain. Called once per physics advance, never from the render path.
    void refreshPose(liquid_glass::Vec2 captureSize);

    // Set by the runtime right before this transformer is removed from its owner list.
    void markDetached() noexcept { m_detached = true; m_mesh.reset(); cancelOpening(); }

  private:
    liquid_glass::Vec2 openCompensation(const PHLWINDOW& window) const;
    liquid_glass::Vec2 openOffset(const PHLWINDOW& window) const;
    PHLWINDOWREF                    m_window;
    liquid_glass::LiquidMesh        m_mesh;
    liquid_glass::EdgeTransition    m_transition;
    SP<CSolidFill>                  m_solid;
    bool                            m_openGeometry = false;
    double                          m_displacementGain = 1.0;
    // Open scale the cached gain was computed with; the render path uses this exact value so gain and map agree.
    liquid_glass::Vec2              m_compensation{1.0, 1.0};
    bool                            m_detached     = false;
    // Fixed size, sized once in the constructor; the render path only writes into them.
    std::vector<SMeshRenderVertex>  m_vertices;
    std::vector<SMeshRenderVertex>  m_meniscusVertices;
};
