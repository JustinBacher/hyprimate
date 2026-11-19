#pragma once

#include <cstdint>
#include <variant>

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>
#include <hyprutils/memory/WeakPtr.hpp>

#include "StyleTypes.hpp"
#include "StyleResources.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetRenderer.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "config_generated.hpp"

// Converts between the plugin's CBox and the core's hyprimate::Box at the renderer boundary.
inline hyprimate::Box to_box(const CBox &box) {
    return {.x = box.x, .y = box.y, .w = box.w, .h = box.h};
}
inline CBox to_cbox(const hyprimate::Box &box) {
    return CBox{box.x, box.y, box.w, box.h};
}

class CPluginRuntime;


class CAnimationTransformer final : public Render::IWindowTransformer {
  public:
    // `state` fixes the window's style for its lifetime; the runtime rebuilds the transformer when
    // the style changes.
    CAnimationTransformer(PHLWINDOWREF window, SStyleState state, SStyleResources<StyleModules> &resources);
    ~CAnimationTransformer() override = default;

    CAnimationTransformer(const CAnimationTransformer &) = delete;
    CAnimationTransformer &operator=(const CAnimationTransformer &) = delete;
    CAnimationTransformer(CAnimationTransformer &&) = delete;
    CAnimationTransformer &operator=(CAnimationTransformer &&) = delete;

    Render::SWindowTransformBuffer transform(Render::CRenderContext &ctx,
        const Render::SWindowTransformBuffer &input,
        const Render::SWindowTransformContext &context) override;
    int priority() const override;
    bool active() const override;
    bool blocksDirectScanout() const override;
    CBox transformedExtents(const CBox &current_box) const override;
    CBox sourceBoxForOutput(const CBox &output_box, const CBox &input_box) const override;

    // True when the state was built for these settings; a mismatch means the runtime rebuilds the
    // transformer.
    bool built_for(hyprimate::config::Style style, const CPluginRuntime &runtime) const;

    void cancel_opening();
    void cancel_movement();

    // Close staging inspects or transfers the state before detaching the transformer.
    SStyleState &style_state() noexcept {
        return m_style;
    }
    const SStyleState &style_state() const noexcept {
        return m_style;
    }

    // Set by the runtime right before this transformer is removed from its owner list.
    void mark_detached();

  private:
#define HYPRIMATE_FRIEND(kind, tag, module) friend struct tag;
    HYPRIMATE_STYLE_LIST(HYPRIMATE_FRIEND, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_FRIEND

    // Whether the active style is still deforming the window.
    bool deforming() const;

    PHLWINDOWREF m_window;
    SStyleState m_style;
    SStyleResources<StyleModules> &m_resources;
    bool m_detached = false;
};
