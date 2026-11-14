#pragma once

#include "animations/cloth/ClothFlutter.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/paper/PaperCrumple.hpp"
#include "animations/shared/PaperRenderer.hpp"
#include "animations/shared/SheetFrame.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/liquid/LiquidLiveState.hpp"
#include "animations/paper/PaperLiveState.hpp"
#include "animations/cloth/ClothLiveState.hpp"
#include "animations/swarm/SwarmLiveState.hpp"
#include "animations/swarm/SwarmRenderer.hpp"

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
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


// The one style a window's transformer or close record carries.
using SStyleState = std::variant<SLiquidStyle, SPaperStyle, SClothStyle, SSwarmStyle>;

// The state a window starts from in `style`. `liquid`, `cloth`, and `seed` are the settings it is built for.
SStyleState initialStyle(liquid_glass::config::Style style, liquid_glass::MaterialParams liquid,
                         liquid_glass::ClothMovementParams cloth, std::uint64_t seed);


class CAnimationTransformer final : public Render::IWindowTransformer {
  public:
    // `state` fixes the window's style for its lifetime; the runtime rebuilds the transformer when the style changes.
    CAnimationTransformer(PHLWINDOWREF window, SStyleState state, SP<CSolidFill> solid, SP<CPaperShade> shade,
                          SP<CSwarmSprites> sprites);
    ~CAnimationTransformer() override = default;

    CAnimationTransformer(const CAnimationTransformer &) = delete;
    CAnimationTransformer &operator=(const CAnimationTransformer &) = delete;

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

    void cancelOpening();
    void cancelMovement();

    // Close staging inspects or transfers the state before detaching the transformer.
    SStyleState &styleState() noexcept { return m_style; }
    const SStyleState &styleState() const noexcept { return m_style; }

    // Set by the runtime right before this transformer is removed from its owner list.
    void markDetached();

  private:
    friend struct SLiquidLiveStyle;
    friend struct SPaperLiveStyle;
    friend struct SClothLiveStyle;
    friend struct SSwarmLiveStyle;

    // Whether the active style is still deforming the window.
    bool   deforming() const noexcept;

    PHLWINDOWREF      m_window;
    SStyleState       m_style;
    SP<CSolidFill>    m_solid;
    SP<CPaperShade>   m_shade;
    SP<CSwarmSprites> m_sprites;
    bool              m_detached = false;
};
