#include "SheetMotion.hpp"

namespace liquid_glass {

void SheetMotion::beginPaper(Transition kind, const PaperParams &params, std::uint64_t seed) {
    m_cloth.reset();
    m_driver = Driver::Paper;
    m_opening = kind == Transition::Open;
    m_paper.begin(kind, params, seed);
}

void SheetMotion::beginCloth(Transition kind, const ClothParams &params, std::uint64_t seed, const Box &window,
                             std::span<const Box> obstacles, std::optional<ClothCloseGeometry> close) {
    m_paper.reset();
    m_driver = Driver::Cloth;
    m_opening = kind == Transition::Open;
    m_cloth.begin(kind, params, seed, window, obstacles, close);
}

void SheetMotion::beginMovement(const ClothMovementParams &params, std::uint64_t seed, const Box &window, Vec2 grabUV) {
    m_paper.reset();
    m_driver = Driver::Cloth;
    m_opening = false;
    m_cloth.beginMovement(params, seed, window, grabUV);
}

void SheetMotion::updateMovement(const Box &window, double elapsedSeconds) noexcept {
    if (moving())
        m_cloth.updateMovement(window, elapsedSeconds);
}

bool SheetMotion::advance(double elapsedSeconds) noexcept {
    return m_driver == Driver::Paper   ? m_paper.advance(elapsedSeconds)
           : m_driver == Driver::Cloth ? m_cloth.advance(elapsedSeconds)
                                       : false;
}

void SheetMotion::reset() noexcept {
    m_paper.reset();
    m_cloth.reset();
    m_driver = Driver::None;
    m_opening = false;
}

bool SheetMotion::active() const noexcept {
    return m_driver == Driver::Paper ? m_paper.active() : m_driver == Driver::Cloth && m_cloth.active();
}
bool SheetMotion::opening() const noexcept { return m_opening && active(); }
bool SheetMotion::clothOpening() const noexcept { return m_driver == Driver::Cloth && opening(); }
bool SheetMotion::moving() const noexcept { return m_driver == Driver::Cloth && m_cloth.moving(); }
bool SheetMotion::closed() const noexcept {
    return m_driver == Driver::Paper ? m_paper.closed() : m_driver == Driver::Cloth && m_cloth.closed();
}
double SheetMotion::opacity() const noexcept {
    return m_driver == Driver::Paper ? m_paper.opacity() : m_driver == Driver::Cloth ? m_cloth.opacity() : 1.0;
}
bool SheetMotion::shaded() const noexcept {
    return m_driver == Driver::Paper ? m_paper.shaded() : m_driver == Driver::Cloth && m_cloth.shaded();
}
void SheetMotion::facets(Vec2 size, PaperFacets &out) const noexcept {
    if (m_driver == Driver::Paper)
        m_paper.facets(size, out);
    else if (m_driver == Driver::Cloth)
        m_cloth.facets(size, out);
    else
        flatSheetFacets(out);
}

} // namespace liquid_glass
