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
#include "animations/swarm/SwarmSurface.hpp"

// Creature sheets resampled into an atlas of walking frames, a solid tile and a disc for
// detached-piece rounding. Only alpha is used. Each sheet is decoded and measured once.
class CSwarmSprites {
  public:
    struct SSheet {
        std::vector<hyprimate::SpriteFrame> frames;  // one per walk frame, in cycle order
        double forward = 0.0;  // heading the creature faces in the sheet
        double stride = 1.0;  // body lengths walked per cycle
        double core_share = 0.0;  // solid body radius / fit, shared by every animated frame
    };

    // The creature's measurements; null when its sheet cannot be decoded. Touches no GL state.
    const SSheet *sheet(const std::string &creature);
    // The creature's atlas; null when unavailable. Creates GL objects on first use, so call it only
    // while rendering.
    SP<Render::ITexture> texture(const std::string &creature);
    CSwarmSurface &surface() {
        return m_surface;
    }

  private:
    struct SEntry {
        bool tried = false;
        SSheet sheet;
        std::vector<std::uint8_t> pixels;  // premultiplied BGRA until uploaded
        SP<Render::ITexture> texture;
    };
    SEntry &entry(const std::string &creature);

    std::map<std::string, SEntry, std::less<>> m_entries;
    CSwarmSurface m_surface;
};

// The layer has two halves: untouched walking creatures and the combined standing mass.
// Merge analytic body distance fields before applying window contents, preserving animated
// creatures over them. The final mesh places the standing mass once and each walker independently.

// The box either half of the layer covers: `source` grown to whole device pixels, so its texels are
// the window texture's own.
CBox swarm_layer_box(const CBox &source);
// The size of the whole layer for that box.
Vector2D swarm_layer_size(const CBox &source);

// Refills the meshes. `source` is the window's capture box inside its texture and `output` the box
// the pieces mesh is normalised to, both in device px of one space; `textureOrigin` is that space's
// position of the texture's first texel and `textureSize` the texture's size.
void fill_swarm_meshes(const hyprimate::Swarm &swarm,
    const CSwarmSprites::SSheet &sheet,
    const CBox &source,
    const CBox &output,
    hyprimate::Vec2 texture_origin,
    hyprimate::Vec2 texture_size,
    SSwarmMeshes &meshes);

// Renders the layer into a work buffer of `size` (swarm_layer_size) and returns it, or nullptr when
// the work buffer or an input is unavailable. Must be called during rendering; it binds and
// restores the framebuffer, the draw state and the blend state itself.
SP<Render::IFramebuffer> render_swarm_layer(Render::CRenderContext &ctx,
    const SP<Render::ITexture> &window,
    const SP<Render::ITexture> &atlas,
    const Vector2D &size,
    const SSwarmMeshes &meshes,
    CSwarmSurface &surface);

// Draws a swarm close, borrowing its meshes from the retained record.
class CSwarmClosePassElement final : public IPassElement {
  public:
    struct SData {
        SP<Render::ITexture> tex;  // the closing window's snapshot
        SP<Render::ITexture> atlas;  // creature masks
        SP<CSwarmSprites> sprites;  // retains the surface filter through the render pass
        Vector2D layer_size;
        CBox box;  // box the pieces mesh spans, monitor-local device px
        const SSwarmMeshes *meshes = nullptr;
        float alpha = 1.F;
        double scale = 1.0;
        CRegion damage;
    };

    explicit CSwarmClosePassElement(SData data);

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
