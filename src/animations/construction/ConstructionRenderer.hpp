#pragma once

#include <memory>

#include "WindowAnimationFacts.hpp"

class CHyprAnimationCanvas;
namespace Render { class IFramebuffer; }
namespace hyprimate {
    struct ConstructionBatch;
    namespace construction { class CloseSite; }
}

class CConstructionRenderer {
  public:
    CConstructionRenderer();
    ~CConstructionRenderer();
    CConstructionRenderer(const CConstructionRenderer &) = delete;
    CConstructionRenderer &operator=(const CConstructionRenderer &) = delete;
    CConstructionRenderer(CConstructionRenderer &&) = delete;
    CConstructionRenderer &operator=(CConstructionRenderer &&) = delete;

    // Metadata only: before retain/suppress, the host separately proves identity and placement.
    [[nodiscard]] static bool supports_snapshot(Render::IFramebuffer &snapshot);

    [[nodiscard]] bool draw(const hyprimate::ConstructionBatch &batch, hyprimate::Box body,
        hyprimate::Box capture, const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);
    [[nodiscard]] bool draw(const hyprimate::construction::CloseSite &site,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
