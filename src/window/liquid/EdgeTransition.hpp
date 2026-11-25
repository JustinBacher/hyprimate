#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "window/shared/EdgeFrame.hpp"

namespace hyprimate {

    // Edge-transition tuning. Finite values are clamped to [0, 1].
    struct TransitionParams {
        double elasticity = 0.5;  // [0,1] spring stiffness; higher = snappier
        double momentum = 0.5;  // [0,1] inertia / overshoot; 0 = none
        double duration_ms = -1.0;  // -1 keeps the natural timing; 0 skips the transition
    };

    // Pop-out lifecycle modelled on a droplet leaving a liquid surface. Open: a small round bead
    // pops out from behind a screen edge, the edge liquid fillets, necks, snaps and recoils behind
    // it, then the bead grows (along the edge first) into the window at its rest box. Close: the
    // window shrinks back into a bead near the edge, the edge rises to meet it and the bead dives
    // behind the edge. Fixed 1/240 s substeps; no compositor dependency.
    class EdgeTransition {
      public:
        // Invalid frame, parameters or non-positive strength leave it idle. A Close begun during an
        // Open continues from the current bead on the Open's edge and ignores `frame`; visible open
        // liquid snaps into a recoil. For a Close: `close_splash` plays a single splash bump at the
        // dive point once the bead is behind the edge; false ends the close at the drain. Ignored
        // for an Open.
        void begin(Transition kind,
            const EdgeFrame &frame,
            const TransitionParams &params,
            double strength,
            bool close_splash = true) noexcept;
        // Nonfinite, negative or > STALL_SECONDS steps abort: Open returns to idle, Close to
        // closed.
        bool advance(double elapsed_seconds) noexcept;
        void reset() noexcept;
        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool closed() const noexcept;
        [[nodiscard]] const EdgeFrame &frame() const noexcept {
            return m_frame;
        }

        // Maps a point given relative to the rest capture centre (logical px; `size` is the capture
        // size) to its displayed position relative to that centre. Exact identity unless active.
        // Radially homogeneous outline map (rest rectangle -> rounded/scaled outline) plus a
        // translation along the edge normal; it never folds the image.
        [[nodiscard]] Vec2 map(Vec2 from_centre, Vec2 size) const noexcept;

        // The map for one capture size in the current state, with its constants computed once:
        // rest_map(size).map(p) equals map(p, size). Build it once per grid or frame and call map()
        // per sample.
        class RestMap {
          public:
            [[nodiscard]] Vec2 map(Vec2 from_centre) const noexcept;

          private:
            friend class EdgeTransition;
            bool identity = true;  // inactive or invalid size: the map is the identity
            bool horizontal = false;  // the edge runs horizontally, so the tangent is x
            bool rectangle = true;  // roundness below 2e-4: the outline is the rectangle itself
            Vec2 along{.x = 1.0, .y = 0.0};
            Vec2 inward{.x = 0.0, .y = 0.0};
            double half_t = 1.0, half_n = 1.0;
            double exponent = 2.0,
                   inv_exponent = 0.5;  // outline exponent 2 / roundness and its reciprocal
            double t_base = 0.0, t_scale = 0.0, n_base = 0.0, n_scale = 0.0;
        };
        [[nodiscard]] RestMap rest_map(Vec2 size) const noexcept;
        // Window-centre translation since the previous call (begin() does not count). Feeds the
        // movement field.
        Vec2 take_travel() noexcept;
        // The pop-out as a box, for a window whose real box is driven to follow it. `centre` is the
        // bead's centre relative to the rest capture centre; `half` is its half-extent per screen
        // axis for a rest capture of `rest_size`. Both are the rest values when idle.
        [[nodiscard]] Vec2 bead_centre() const noexcept;
        [[nodiscard]] Vec2 bead_half(Vec2 rest_size) const noexcept;

        // Edge liquid height in px into the screen at tangential coordinate t. 0 unless active;
        // never negative.
        [[nodiscard]] double meniscus(double tangent) const noexcept;
        // meniscus() is 0 outside [meniscus_low, meniscus_high]; empty (low == high) when there is
        // none.
        [[nodiscard]] double meniscus_low() const noexcept;
        [[nodiscard]] double meniscus_high() const noexcept;
        [[nodiscard]] double meniscus_peak() const noexcept;

      private:
        enum class Phase : std::uint8_t { Idle, Opening, Closing, Closed };
        struct Spring {
            double value = 0.0;
            double velocity = 0.0;
        };
        // One bell of edge liquid, centred on the body's tangential centre.
        struct Lobe {
            double height = 0.0;  // px into the screen; 0 means unused
            double plateau = 0.0;  // half-width of the flat top
            double skirt = 1.0;  // falloff width beyond the plateau
        };
        // Liquid is the maximum over the visible lobes: the open liquid or a recoil, plus the
        // splash during a close (a close interrupting an opening can carry a decaying recoil into
        // the splash).
        struct Profile {
            static constexpr int K_MAX_LOBES = 2;
            std::array<Lobe, K_MAX_LOBES> lobes{};
            // Adds a visible lobe to the first free slot. Callers stay within K_MAX_LOBES.
            void add(const Lobe &lobe) noexcept {
                if (!(lobe.height > 0.0)) {
                    return;
                }
                for (Lobe &slot : lobes) {
                    if (!(slot.height > 0.0)) {
                        slot = lobe;
                        return;
                    }
                }
            }
            [[nodiscard]] double peak() const noexcept {
                double peak_height = 0.0;
                for (const Lobe &lobe : lobes) {
                    peak_height = lobe.height > peak_height ? lobe.height : peak_height;
                }
                return peak_height;
            }
        };

        void step() noexcept;
        void step_and_refresh() noexcept;
        void abort() noexcept;
        void start_recoil(const Lobe
                &snap) noexcept;  // the recoil starts at snap.height with snap's plateau and skirt
        void advance_recoil() noexcept;
        [[nodiscard]] bool quiet() const noexcept;
        [[nodiscard]] bool recoil_settled() const noexcept;
        [[nodiscard]] double e_normal() const noexcept;  // bead half-extent into the screen (n)
        [[nodiscard]] double near_side() const noexcept;  // n of the bead's side nearest the edge
        [[nodiscard]] double far_side()
            const noexcept;  // n of the bead's side farthest from the edge
        [[nodiscard]] Lobe live_lobe()
            const noexcept;  // opening only: attached neck or crossing fillets
        [[nodiscard]] Lobe recoil_lobe() const noexcept;
        [[nodiscard]] Lobe splash_lobe() const noexcept;
        void refresh_profile() noexcept;

        Phase m_phase = Phase::Idle;
        EdgeFrame m_frame{};
        double m_half_n = 0.0;  // capture half-extent along the normal
        double m_radius = 0.0;  // bead radius r
        double m_tuck = 0.0;  // Fs
        double m_pinch = 0.0;  // Lp
        double m_neck_skirt = 0.0;  // sigma_n
        double m_cross_skirt = 0.0;  // sigma_c
        double m_hover = 0.0;  // nh: hover centre
        Spring m_hop{};  // centre n during the hop
        double m_hop_omega = 0.0, m_hop_zeta = 1.0;
        Spring m_spread_t{};  // tangential spread (0 bead .. 1 rest)
        Spring m_spread_n{};  // normal spread
        double m_grow_omega = 0.0, m_grow_zeta = 1.0;
        bool m_growing = false;
        double m_nc = 0.0;  // displayed centre n (absolute, from the edge)
        double m_nc0 = 0.0, m_s_t0 = 1.0, m_s_n0 = 1.0;  // close timeline start
        double m_duration = 0.0, m_time = 0.0;
        double m_close_momentum =
            0.5;  // changes the dive curve; 0.5 preserves the original trajectory
        bool m_drained = false;
        bool m_close_splash = true;
        bool m_splashing = false;
        double m_splash_time = 0.0, m_splash_duration = 0.0;  // splash clock and length (s)
        bool m_recoiling = false;
        Spring m_recoil{};
        double m_recoil_omega = 0.0, m_recoil_zeta = 1.0, m_recoil_time = 0.0;
        Lobe m_snap{};  // the recoil's plateau and skirt at the snap
        Profile m_profile{};  // cached profile for the current state
        double m_taken = 0.0;  // centre n at the last take_travel()
        double m_accumulator = 0.0;
        double m_elapsed_ms = 0.0, m_requested_ms = 0.0;
        int m_total_steps = 0, m_elapsed_steps = 0;
    };

}  // namespace hyprimate
