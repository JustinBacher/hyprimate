#pragma once

#include <optional>
#include <string>

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/transformer/Transformer.hpp>

#include "HyprWindowAnimations.hpp"
#include "window/WindowAnimationEntry.hpp"

inline hyprimate::Box to_box(const CBox &box) {
    return {.x = box.x, .y = box.y, .w = box.w, .h = box.h};
}
inline CBox to_cbox(const hyprimate::Box &box) {
    return CBox{box.x, box.y, box.w, box.h};
}

class CAnimationTransformer final : public Render::IWindowTransformer {
  public:
    CAnimationTransformer(PHLWINDOWREF window,
        HyprWindowAnimations &animations,
        CHyprWindowPort &port,
        hyprimate::animation::Handle handle);
    ~CAnimationTransformer() override;
    CAnimationTransformer(const CAnimationTransformer &) = delete;
    CAnimationTransformer &operator=(const CAnimationTransformer &) = delete;
    CAnimationTransformer(CAnimationTransformer &&) = delete;
    CAnimationTransformer &operator=(CAnimationTransformer &&) = delete;

    Render::SWindowTransformBuffer transform(Render::CRenderContext &ctx,
        const Render::SWindowTransformBuffer &input,
        const Render::SWindowTransformContext &context) override;
    [[nodiscard]] int priority() const override;
    [[nodiscard]] bool active() const override;
    [[nodiscard]] bool blocksDirectScanout() const override;
    [[nodiscard]] CBox transformedExtents(const CBox &current_box) const override;
    [[nodiscard]] CBox sourceBoxForOutput(
        const CBox &output_box, const CBox &input_box) const override;
    void preWindowRender(Render::CRenderContext &ctx, CSurfacePassElement::SRenderData *) override;
    // Backdrop effects can sample in the parent pass, then put only their
    // finished window-owned paint into the nested window source.
    static void prepare_window_underlay(const PHLWINDOW &, Render::CRenderContext &);
    struct UnderlayTarget {
        SP<Render::IFramebuffer> image;
        CBox box;
    };
    [[nodiscard]] std::optional<UnderlayTarget> window_underlay_target(
        Render::CRenderContext &, const SP<Render::IFramebuffer> &parent_target);
    [[nodiscard]] hyprimate::animation::Handle handle() const noexcept {
        return m_handle;
    }
    void mark_detached() noexcept {
        m_detached = true;
    }

  private:
    class CContentSourcePass;
    struct ContentSource {
        WP<Render::IFramebuffer> framebuffer;
        Render::CRenderContext *context = nullptr;
        CContentSourcePass *passes = nullptr;
        CContentSourcePass *receipt = nullptr;
        CContentSourcePass *underlay_pass = nullptr;
        SP<Render::IFramebuffer> shader_matte;
        CBox blur_box, matte_result_box;
        double rounding = 0, rounding_power = 2;
        bool includes_underlay = false;
    };
    ContentSource m_content_source;
    Render::CRenderContext *m_underlay_context = nullptr;
    std::string m_underlay_pass_name;
    PHLWINDOWREF m_window;
    HyprWindowAnimations &m_animations;
    CHyprWindowPort &m_port;
    hyprimate::animation::Handle m_handle;
    bool m_detached = false;
};
