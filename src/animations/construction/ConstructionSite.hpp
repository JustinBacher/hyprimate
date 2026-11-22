#pragma once

#include "ConstructionBatch.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {

namespace animation { struct Step; }
namespace construction {
struct Layout {
    Box body, capture, opening, moving;
    Vec3 floor;
    double scale = 0, travel = 0, deck_left = 0, deck_right = 0;
    double guide_y = 0, bolt_y = 0;
};
}  // namespace construction

class ConstructionSite {
public:
    void build(Box body, Box capture, Box work_area, std::uint64_t seed, double duration);
    void step(const animation::Step &step) noexcept;

    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] Box bounds() const noexcept { return m_bounds; }
    [[nodiscard]] const ConstructionBatch &batch() const noexcept { return m_batch; }
    [[nodiscard]] Box body() const noexcept { return m_body; }
    [[nodiscard]] Box capture() const noexcept { return m_capture; }

private:
    void publish(double time) noexcept;

    ConstructionBatch m_batch;
    Box m_body, m_capture, m_bounds;
    construction::Layout m_layout;
    double m_duration = 0, m_elapsed = 0;
    bool m_active = false, m_delayed = false;
};

}  // namespace hyprimate
