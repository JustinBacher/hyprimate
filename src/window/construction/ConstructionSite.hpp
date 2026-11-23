#pragma once

#include "ConstructionBatch.hpp"
#include "ConstructionPlacement.hpp"
#include "shared/Core.hpp"

namespace hyprimate {

namespace animation { struct Step; }

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

    static constexpr std::size_t RECEIVER = 0, MAX_JOBS = 12;
    struct Station {
        Vec3 floor, ladder_base{}, ladder_top{};
        double facing, walk, deck_left, deck_right, bearing, bearing_y;
        bool guide;
    };
    struct Job {
        Box bay, source;
        double start, seating, strike, clear;
        construction::PlacementRecipe placement;
        std::array<Station, 4> stations;
        std::size_t workers = 0;
    };
    std::array<Job, MAX_JOBS> m_jobs;
    std::size_t m_job_count = 0;
    Box m_local_body, m_work_area;
    ConstructionBatch m_batch;
    Box m_body, m_capture, m_bounds;
    bool m_oversized_outsets = false;
    double m_duration = 0, m_elapsed = 0;
    bool m_active = false;
};

}  // namespace hyprimate
