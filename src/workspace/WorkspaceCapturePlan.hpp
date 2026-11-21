#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace hyprimate::workspace {

    struct CaptureFeatures {
        bool wallpaper = false;
        bool layered = false;
        bool individual = false;
        bool native_hud = false;
    };

    struct CaptureSafetyFacts {
        bool session_available = true;
        bool locked = false;
        bool input_available = true;
        bool exclusive_input = false;
        bool input_method = false;
        bool non_window_focus = false;
        bool protected_content = false;
        bool invalid_fadeout = false;
    };

    constexpr bool capture_scene_safe(const CaptureSafetyFacts &facts) {
        return facts.session_available && !facts.locked && facts.input_available &&
               !facts.exclusive_input && !facts.input_method && !facts.non_window_focus &&
               !facts.protected_content && !facts.invalid_fadeout;
    }

    // beginRender has transferred a full-fake session to us. Native callbacks
    // may invalidate it: never submit or abort a replacement on the reusable context.
    template<class Owns, class Build, class Submit, class Abort>
    bool run_capture_pass(const Owns &owns, const Build &build_authorized,
        const Submit &submit, const Abort &abort) {
        if (!owns()) {
            return false;
        }
        try {
            if (build_authorized() && owns() && submit()) {
                return true;
            }
        } catch (...) {
            if (owns()) {
                abort();
            }
            throw;
        }
        if (owns()) {
            abort();
        }
        return false;
    }

    constexpr bool capture_hud_safe(bool retiring_shell) {
        return !retiring_shell;
    }

    enum class CaptureBufferAction { Reject, Allocate, Reuse };
    struct CaptureBufferFacts {
        bool native_ready = false;
        bool compatible = true;
        bool scene_safe = false;
        bool hud_safe = true;
        bool active = false;
        std::array<bool, 6> base{};
        std::array<bool, 6> planes{};
    };

    inline bool capture_base_ready(const CaptureBufferFacts &facts, CaptureFeatures features) {
        const auto BASE = std::span{facts.base}.first(features.wallpaper ? 6U : 3U);
        return std::ranges::all_of(BASE, [](bool usable) { return usable; });
    }

    inline CaptureBufferAction capture_buffer_plan(
        const CaptureBufferFacts &facts, CaptureFeatures features) {
        if (!facts.native_ready || !facts.compatible || !facts.scene_safe ||
            (features.native_hud && !facts.hud_safe)) {
            return CaptureBufferAction::Reject;
        }
        if (!capture_base_ready(facts, features)) {
            return facts.active ? CaptureBufferAction::Reject : CaptureBufferAction::Allocate;
        }
        if (features.layered &&
            !std::ranges::all_of(facts.planes, [](bool usable) { return usable; })) {
            return CaptureBufferAction::Reject;
        }
        return CaptureBufferAction::Reuse;
    }
    struct CaptureAllocationFacts {
        std::uint64_t bytes = 0;
        std::uint64_t monitor_bytes = 0;
        std::uint64_t total_bytes = 0;
        bool wide_color = false;
    };

    constexpr bool capture_allocation_allowed(const CaptureAllocationFacts &facts) {
        const std::uint64_t MONITOR_LIMIT = (facts.wide_color ? 1024ULL : 512ULL) * 1024 * 1024;
        constexpr std::uint64_t GLOBAL_LIMIT = 2048ULL * 1024 * 1024;
        return facts.bytes != 0 && facts.bytes <= MONITOR_LIMIT &&
               facts.monitor_bytes <= MONITOR_LIMIT - facts.bytes &&
               facts.total_bytes <= GLOBAL_LIMIT - facts.bytes;
    }
    struct CaptureTargetFacts {
        bool present = false;
        bool matching_extent = false;
    };
    struct CaptureTargetPlan {
        bool release = false;
        bool allocate = false;
    };
    constexpr CaptureTargetPlan capture_target_plan(CaptureTargetFacts facts) {
        return {.release = facts.present && !facts.matching_extent,
            .allocate = !facts.present || !facts.matching_extent};
    }

    struct CaptureLayout {
        bool layered = false;
        bool separate_windows = false;
        bool separate_top_backdrop = false;
    };

    constexpr CaptureLayout capture_plane_layout(CaptureFeatures features) {
        return {.layered = features.layered,
            .separate_windows = features.layered && !features.individual,
            .separate_top_backdrop = features.layered && features.individual};
    }

    enum class CaptureBoundary { Wallpaper, BeforeWindows, AfterWindows };
    enum class CapturePlane { Wallpaper, Bottom, Windows, TopBackdrop };
    struct CaptureBoundaryPlan {
        std::array<CapturePlane, 2> planes{};
        std::size_t count = 0;
    };
    struct CaptureRect {
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
    };
    struct CapturePixelBounds {
        double left = 0;
        double top = 0;
        double right = 0;
        double bottom = 0;
    };
    struct CaptureWindowFacts {
        bool present = false;
        bool mapped = false;
        bool hidden = false;
        bool visible = false;
        bool popup_pass = false;
        bool visible_popup = false;
        bool overlay = false;
        CapturePixelBounds bounds;
        double output_width = 0;
        double output_height = 0;
    };
    struct CaptureWindowPlan {
        std::optional<CaptureRect> crop;
    };
    struct CaptureLayerFacts {
        bool background = false;
        bool popup = false;
    };
    struct CaptureCompletionFacts {
        bool native_captured = false;
        bool wallpaper_captured = false;
    };

    // This state follows the native scene's ordered callbacks. Failure invalidates
    // the retained capture, but never suppresses the remainder of native drawing.
    class CaptureSession {
      public:
        explicit CaptureSession(CaptureFeatures features = {}) : m_features(features) {
        }

        [[nodiscard]] bool include_layer(CaptureLayerFacts facts) const {
            return !m_features.native_hud || (facts.background && !facts.popup);
        }
        [[nodiscard]] bool needs_window_details(const CaptureWindowFacts &facts) const {
            return m_features.layered && m_features.individual && facts.present && facts.mapped &&
                   !facts.hidden && facts.visible && !m_failed;
        }
        [[nodiscard]] bool accepts_fadeout(bool selected_window_fadeout) const {
            return !m_features.individual || !selected_window_fadeout;
        }
        CaptureBoundaryPlan boundary(CaptureBoundary boundary) {
            CaptureBoundaryPlan result;
            const auto ADD = [&](CapturePlane plane) { result.planes[result.count++] = plane; };
            if (boundary == CaptureBoundary::Wallpaper) {
                ADD(CapturePlane::Wallpaper);
                m_wallpaper_seen = true;
            } else if (m_features.layered && boundary == CaptureBoundary::BeforeWindows) {
                if (!m_pre_windows_seen) {
                    ADD(CapturePlane::Bottom);
                    m_pre_windows_seen = true;
                } else if (m_features.individual) {
                    m_failed = true;
                }
            } else if (m_features.layered && boundary == CaptureBoundary::AfterWindows) {
                if (!m_features.individual) {
                    ADD(CapturePlane::Windows);
                }
                if (m_features.individual) {
                    ADD(CapturePlane::TopBackdrop);
                }
                m_post_windows_seen = true;
            }
            return result;
        }
        CaptureWindowPlan window(const CaptureWindowFacts &facts) {
            CaptureWindowPlan result;
            if (!needs_window_details(facts) ||
                (facts.popup_pass && !facts.visible_popup && !facts.overlay)) {
                return result;
            }
            if (!m_pre_windows_seen || m_post_windows_seen) {
                m_failed = true;
                return result;
            }
            const double LEFT = std::clamp(std::floor(facts.bounds.left), 0.0, facts.output_width);
            const double TOP = std::clamp(std::floor(facts.bounds.top), 0.0, facts.output_height);
            const double RIGHT = std::clamp(std::ceil(facts.bounds.right), 0.0, facts.output_width);
            const double BOTTOM =
                std::clamp(std::ceil(facts.bounds.bottom), 0.0, facts.output_height);
            if (RIGHT <= LEFT || BOTTOM <= TOP) {
                return result;
            }
            result.crop =
                CaptureRect{.x = LEFT, .y = TOP, .width = RIGHT - LEFT, .height = BOTTOM - TOP};
            return result;
        }
        [[nodiscard]] bool complete(CaptureCompletionFacts facts) const {
            return facts.native_captured && !m_failed &&
                   (!m_features.wallpaper || (m_wallpaper_seen && facts.wallpaper_captured)) &&
                   (!m_features.layered || (m_pre_windows_seen && m_post_windows_seen));
        }

      private:
        bool m_failed = false;
        CaptureFeatures m_features;
        bool m_wallpaper_seen = false;
        bool m_pre_windows_seen = false;
        bool m_post_windows_seen = false;
    };

}  // namespace hyprimate::workspace
