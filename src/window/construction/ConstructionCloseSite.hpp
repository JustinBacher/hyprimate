#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "window/WindowAnimationFacts.hpp"
#include "window/construction/ConstructionBatch.hpp"
#include "window/construction/studies/RegionalClose.hpp"

namespace hyprimate::construction {

// Geometry only. Allocate per accepted close, with draw scratch owned by the renderer.
class CloseSite {
  public:
    // Nullopt is a known native identity. An engaged span is the last presented, source-clipped
    // client faces in this capture's coordinates, possibly empty. Never substitute missing print.
    // Worker scale uses body dimensions, not capture trim; zero hides all departing crew.
    [[nodiscard]] bool start(Box body, Box capture,
        std::optional<std::span<const ConstructionQuad>> currently_visible, std::uint64_t seed,
        double seconds, double worker_size = 1) noexcept;

    // Before retention/suppression, pass the available unposed snapshot rectangle in
    // capture-local logical pixels. A moved face may need texels outside the output.
    [[nodiscard]] bool can_capture(Box local_snapshot_crop, double scale) const noexcept;

    // Once at close handoff, supply the actual last presented batch. Copies only
    // supported worker cutouts; no geometry is retained for a known blank site.
    [[nodiscard]] bool depart(const ConstructionBatch &last_presented) noexcept;

    // Bounds are conservative projected geometry + a small raster fringe, relative to capture
    // and intersected with Step.screen (in the same global coordinate space as body/capture).
    [[nodiscard]] animation::Frame advance(const animation::Step &step) noexcept;

    // Call only for a host-authorized snapshot. The crop is unposed, capture-local logical pixels.
    // Always replaces both outputs. True with empty output is intentional blank presentation;
    // false means the input was invalid or the site is no longer active.
    [[nodiscard]] bool emit(Box authorized_local_crop, studies::RegionalClose::Frame &frame,
        ConstructionBatch &effects) const noexcept;

    [[nodiscard]] Box body() const noexcept { return m_body; }
    [[nodiscard]] Box capture() const noexcept { return m_capture; }

  private:
    studies::RegionalClose m_close;
    Box m_body{}, m_capture{}, m_envelope{}, m_required_snapshot{};
    std::unique_ptr<ConstructionQuad[]> m_departing;
    std::size_t m_departing_count = 0;
    double m_departure_seconds = 0, m_departure_elapsed = 0, m_departure_distance = 0;
    bool m_blank = false, m_hide_workers = false;
};

}  // namespace hyprimate::construction
