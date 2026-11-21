#pragma once

#include <memory>

class CHyprAnimationCanvas;
namespace hyprimate {
    struct PaperSurface;
    namespace animation {
        template <class Canvas>
        struct Draw;
    }
}  // namespace hyprimate

class CPaperRenderer {
  public:
    CPaperRenderer();
    ~CPaperRenderer();
    CPaperRenderer(const CPaperRenderer &) = delete;
    CPaperRenderer &operator=(const CPaperRenderer &) = delete;
    CPaperRenderer(CPaperRenderer &&) = delete;
    CPaperRenderer &operator=(CPaperRenderer &&) = delete;

    bool draw(const hyprimate::PaperSurface &surface,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw);
    bool warm_up();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
