#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

#include "animations/swarm/SwarmLiveState.hpp"

// The creature sheets, each resampled into a mask atlas: its walk frames in a row of square cells
// with transparent gutters, then one solid cell for tiles that are merged into the window. Only the
// atlas's alpha is used. A sheet is decoded and measured once, on first use.
class CSwarmSprites {
  public:
    struct SSheet {
        std::vector<hyprimate::SpriteFrame> frames;  // one per walk frame, in cycle order
        double forward = 0.0;  // heading the creature faces in the sheet
        double stride = 1.0;  // body lengths walked per cycle
    };

    // The creature's measurements; null when its sheet cannot be decoded. Touches no GL state.
    const SSheet *sheet(const std::string &creature);
    // The creature's atlas; null when unavailable. Creates GL objects on first use, so call it only
    // while rendering.
    SP<Render::ITexture> texture(const std::string &creature);

  private:
    struct SEntry {
        bool tried = false;
        SSheet sheet;
        std::vector<std::uint8_t> pixels;  // premultiplied BGRA until uploaded
        SP<Render::ITexture> texture;
    };
    SEntry &entry(const std::string &creature);

    std::map<std::string, SEntry, std::less<>> m_entries;
};

// Drawing a swarm takes two steps. The layer is the window cut into shape, in two halves side by
// side, each a copy of the window's box: the left holds the walking creatures, each over its own
// tile; the right holds everything standing in the window, which is the blob amassed so far: merged
// tiles with their free corners rounded off, the notches between them filled in, and creatures
// swelling into their tiles. Silhouettes and shapes are drawn from the atlas and the window is
// multiplied into them. The pieces mesh then draws the right half where the window is, in one
// piece, and over it every walking creature's tile where its agent is, turned to its heading.

// The box either half of the layer covers: `source` grown to whole device pixels, so its texels are
// the window texture's own.
CBox swarmLayerBox(const CBox &source);
// The size of the whole layer for that box.
Vector2D swarmLayerSize(const CBox &source);

// Refills the meshes. `source` is the window's capture box inside its texture and `output` the box
// the pieces mesh is normalised to, both in device px of one space; `textureOrigin` is that space's
// position of the texture's first texel and `textureSize` the texture's size.
void fillSwarmMeshes(const hyprimate::Swarm &swarm,
    const CSwarmSprites::SSheet &sheet,
    const CBox &source,
    const CBox &output,
    hyprimate::Vec2 textureOrigin,
    hyprimate::Vec2 textureSize,
    SSwarmMeshes &meshes);

// Renders the layer into a work buffer of `size` (swarmLayerSize) and returns it, or nullptr when
// the work buffer or an input is unavailable. Must be called during rendering; it binds and
// restores the framebuffer, the draw state and the blend state itself.
SP<Render::IFramebuffer> renderSwarmLayer(Render::CRenderContext &ctx,
    const SP<Render::ITexture> &window,
    const SP<Render::ITexture> &atlas,
    const Vector2D &size,
    const SSwarmMeshes &meshes);

// Draws a swarm close, borrowing its meshes from the retained record.
class CSwarmClosePassElement final : public IPassElement {
  public:
    struct SData {
        SP<Render::ITexture> tex;  // the closing window's snapshot
        SP<Render::ITexture> atlas;  // creature masks
        Vector2D layerSize;
        CBox box;  // box the pieces mesh spans, monitor-local device px
        const SSwarmMeshes *meshes = nullptr;
        float alpha = 1.F;
        double scale = 1.0;
        CRegion damage;
    };

    explicit CSwarmClosePassElement(SData data);
    ~CSwarmClosePassElement() override = default;

    std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override;
    bool needsLiveBlur(Render::CRenderContext &ctx) override;
    bool needsPrecomputeBlur(Render::CRenderContext &ctx) override;
    const char *passName() override;
    ePassElementType type() override;
    std::optional<CBox> boundingBox(Render::CRenderContext &ctx) override;
    CRegion opaqueRegion(Render::CRenderContext &ctx) override;

  private:
    SData m_data;
};
