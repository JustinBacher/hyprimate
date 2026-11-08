#pragma once

#include "LiquidMesh.hpp"

#include <array>
#include <optional>
#include <span>

namespace liquid_glass {

enum class Transition { Open, Close };
enum class Edge { Left, Right, Top, Bottom };

// Logical pixels, y down.
struct Box {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

// Rest geometry in the edge frame. n = distance from the screen edge into the screen; t = tangential coordinate.
// Every value is for points given relative to the capture (decorated + shadow) centre at rest.
struct EdgeFrame {
    Edge   edge         = Edge::Bottom;
    double edgeDistance = 0.0; // n of the capture centre (> 0)
    double captureFar   = 0.0; // n of the capture side farthest from the edge
    double bodyNear     = 0.0; // n of the decorated window side nearest the edge (>= 0)
    double bodyFar      = 0.0; // n of the decorated window side farthest from the edge
    double bodyT0       = 0.0; // tangential span of the decorated window
    double bodyT1       = 0.0;
};

// Chooses the edge a window emerges from or drains into. An edge is valid when the decorated body lies inside the
// screen along its normal. preferFallback (first window on its workspace, or close_side=origin) picks `fallback` when
// valid. Otherwise the neighbours decide: each body side sums the length it shares with neighbours whose facing gap is
// in [-2, 64] px. The sides within 1 px of the largest contact point at the opposite screen edges, and the valid
// candidate nearest the body centre wins. Without usable contact, the valid edge nearest the body centre wins. Ties
// within 1 px go to `fallback`, then Bottom, Right, Left, Top. nullopt for unusable boxes or when no edge is valid.
std::optional<EdgeFrame> resolveEdgeFrame(const Box& body, const Box& capture, const Box& screen, std::span<const Box> neighbours, Edge fallback,
                                          bool preferFallback) noexcept;

// Edge frame <-> point relative to the capture centre.
Vec2   edgePoint(const EdgeFrame& frame, double t, double n) noexcept;
double edgeNormal(const EdgeFrame& frame, Vec2 p) noexcept;
double edgeTangent(const EdgeFrame& frame, Vec2 p) noexcept;
// Unit vector pointing from the edge into the screen.
Vec2 inwardNormal(Edge edge) noexcept;

// Pop-out lifecycle modelled on a droplet leaving a liquid surface. Open: a small round bead pops out from behind a
// screen edge, the edge liquid fillets, necks, snaps and recoils behind it, then the bead grows (along the edge
// first) into the window at its rest box. Close: the window shrinks back into a bead near the edge, the edge rises
// to meet it and the bead dives behind the edge. Fixed 1/240 s substeps; no compositor dependency.
class EdgeTransition {
  public:
    // Invalid frame, parameters or non-positive strength leave it idle. A Close begun during an Open continues
    // from the current bead on the Open's edge and ignores `frame`; visible open liquid snaps into a recoil. For a
    // Close: `closeSplash` plays a single splash bump at the dive point once the bead is
    // behind the edge; false ends the close at the drain. Ignored for an Open.
    void begin(Transition kind, const EdgeFrame& frame, const MaterialParams& params, double strength,
               bool closeSplash = true) noexcept;
    // Nonfinite, negative or > 0.25 s steps abort: Open returns to idle, Close to closed.
    bool advance(double elapsedSeconds) noexcept;
    void reset() noexcept;
    bool active() const noexcept;
    bool closed() const noexcept;
    const EdgeFrame& frame() const noexcept { return m_frame; }

    // Maps a point given relative to the rest capture centre (logical px; `size` is the capture size) to its
    // displayed position relative to that centre. Exact identity unless active. Radially homogeneous outline map
    // (rest rectangle -> rounded/scaled outline) plus a translation along the edge normal; it never folds the image.
    Vec2 map(Vec2 fromCentre, Vec2 size) const noexcept;
    // Renderer guard for the movement field: 1 when inactive or `mesh` settled, else the largest of 1, 1/2, 1/4, ...
    // (0 when none qualifies) at which every triangle of the renderer's 32x32 grid keeps >= 20% of its gain-0 area.
    // `restSize` is the renderer's sample size and `compensation` its open scale c; the grid is evaluated exactly as the
    // renderer composes it: map(((u-0.5)*S + gain*d) * c, S*c).
    double displacementGain(const LiquidMesh& mesh, Vec2 restSize, Vec2 compensation) const noexcept;
    // Window-centre translation since the previous call (begin() does not count). Feeds the movement field.
    Vec2 takeTravel() noexcept;

    // Edge liquid height in px into the screen at tangential coordinate t. 0 unless active; never negative.
    double meniscus(double t) const noexcept;
    // meniscus() is 0 outside [meniscusLow, meniscusHigh]; empty (low == high) when there is none.
    double meniscusLow() const noexcept;
    double meniscusHigh() const noexcept;
    double meniscusPeak() const noexcept;

  private:
    enum class Phase { Idle, Opening, Closing, Closed };
    struct Spring {
        double value    = 0.0;
        double velocity = 0.0;
    };
    // One bell of edge liquid, centred on the body's tangential centre.
    struct Lobe {
        double height  = 0.0; // px into the screen; 0 means unused
        double plateau = 0.0; // half-width of the flat top
        double skirt   = 1.0; // falloff width beyond the plateau
    };
    // Liquid is the maximum over the visible lobes: the open liquid or a recoil, plus the splash during a close (a
    // close interrupting an opening can carry a decaying recoil into the splash).
    struct Profile {
        static constexpr int kMaxLobes = 2;
        std::array<Lobe, kMaxLobes>   lobes{};
        // Adds a visible lobe to the first free slot. Callers stay within kMaxLobes.
        void add(const Lobe& lobe) noexcept {
            if (!(lobe.height > 0.0))
                return;
            for (Lobe& slot : lobes) {
                if (!(slot.height > 0.0)) {
                    slot = lobe;
                    return;
                }
            }
        }
        double peak() const noexcept {
            double h = 0.0;
            for (const Lobe& l : lobes)
                h = l.height > h ? l.height : h;
            return h;
        }
    };

    void    step() noexcept;
    void    abort() noexcept;
    void    startRecoil(const Lobe& snap) noexcept; // the recoil starts at snap.height with snap's plateau and skirt
    void    advanceRecoil() noexcept;
    bool    quiet() const noexcept;
    bool    recoilSettled() const noexcept;
    double  eNormal() const noexcept;   // bead half-extent into the screen (n)
    double  nearSide() const noexcept;  // n of the bead's side nearest the edge
    double  farSide() const noexcept;   // n of the bead's side farthest from the edge
    Lobe    liveLobe() const noexcept;  // opening only: attached neck or crossing fillets
    Lobe    recoilLobe() const noexcept;
    Lobe    splashLobe() const noexcept;
    void    refreshProfile() noexcept;

    Phase     m_phase   = Phase::Idle;
    EdgeFrame m_frame{};
    double    m_halfN       = 0.0; // capture half-extent along the normal
    double    m_radius      = 0.0; // bead radius r
    double    m_tuck        = 0.0; // Fs
    double    m_pinch       = 0.0; // Lp
    double    m_neckSkirt   = 0.0; // sigma_n
    double    m_crossSkirt  = 0.0; // sigma_c
    double    m_hover       = 0.0; // nh: hover centre
    Spring    m_hop{};             // centre n during the hop
    double    m_hopOmega = 0.0, m_hopZeta = 1.0;
    Spring    m_spreadT{};         // tangential spread (0 bead .. 1 rest)
    Spring    m_spreadN{};         // normal spread
    double    m_growOmega = 0.0, m_growZeta = 1.0;
    bool      m_growing   = false;
    double    m_nc        = 0.0;   // displayed centre n (absolute, from the edge)
    double    m_nc0       = 0.0, m_sT0 = 1.0, m_sN0 = 1.0; // close timeline start
    double    m_duration  = 0.0, m_time = 0.0;
    double    m_closeMomentum = 0.5; // changes the dive curve; 0.5 preserves the original trajectory
    bool      m_drained   = false;
    bool      m_closeSplash = true;
    bool      m_splashing   = false;
    double    m_splashTime = 0.0, m_splashDuration = 0.0; // splash clock and length (s)
    bool      m_recoiling = false;
    Spring    m_recoil{};
    double    m_recoilOmega = 0.0, m_recoilZeta = 1.0, m_recoilTime = 0.0;
    Lobe      m_snap{};    // the recoil's plateau and skirt at the snap
    Profile   m_profile{}; // cached profile for the current state
    double    m_taken       = 0.0; // centre n at the last takeTravel()
    double    m_accumulator = 0.0;
};

} // namespace liquid_glass
