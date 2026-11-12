#pragma once

#include "ClothFlutter.hpp"
#include "PaperCrumple.hpp"

namespace liquid_glass {

// One sheet driver; switching materials discards the previous shape.
class SheetMotion {
  public:
    void   beginPaper(Transition kind, const PaperParams &params, std::uint64_t seed);
    void   beginCloth(Transition kind, const ClothParams &params, std::uint64_t seed, const Box &window,
                      std::span<const Box> obstacles, std::optional<ClothCloseGeometry> close = std::nullopt);
    bool   advance(double elapsedSeconds) noexcept;
    void   reset() noexcept;
    bool   active() const noexcept;
    bool   closed() const noexcept;
    double opacity() const noexcept;
    bool   shaded() const noexcept;
    void   facets(Vec2 size, PaperFacets &out) const noexcept;

  private:
    enum class Driver { None, Paper, Cloth };
    Driver       m_driver = Driver::None;
    PaperCrumple m_paper;
    ClothFlutter m_cloth;
};

} // namespace liquid_glass
