#include "animations/construction/ConstructionMotionSite.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "animations/construction/ConstructionMotionArt.hpp"

namespace hyprimate::construction {
namespace {
bool size_changed(Box a, Box b) noexcept {
    return a.w != b.w || a.h != b.h;
}

bool translated(Box a, Box b) noexcept {
    return a.x != b.x || a.y != b.y;
}

bool overlaps(Box a, Box b) noexcept {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool same_box(Box a, Box b) noexcept {
    return !translated(a, b) && !size_changed(a, b);
}

bool touches_other_output(Box box, Box owner, std::span<const Box> outputs) noexcept {
    return std::ranges::any_of(outputs, [&](Box output) {
        return is_usable(output) && !same_box(output, owner) && overlaps(box, output);
    });
}

bool valid_interval(double seconds) noexcept {
    return std::isfinite(seconds) && seconds >= 0;
}

bool valid_time(double seconds) noexcept {
    return valid_interval(seconds) && seconds <= STALL_SECONDS;
}

Box initial_body(const animation::Motion &motion, Box body) noexcept {
    Box from = motion.from_body;
    if (motion.workspace_shifted) {
        // The first sample exposes only offset-adjusted previous geometry. Its size is
        // authoritative; its previous local origin cannot be recovered from these facts.
        from.x = body.x;
        from.y = body.y;
    } else {
        // A stationary workspace offset can still be nonzero. Remove it from both samples.
        from.x = body.x + (from.x - motion.body.x);
        from.y = body.y + (from.y - motion.body.y);
    }
    return from;
}
}  // namespace

bool is_capture_identity(const ConstructionBatch &batch, Box capture) noexcept {
    if (batch.count != 1) return false;
    const auto &quad = batch.quads[0];
    const auto width = static_cast<float>(capture.w);
    const auto height = static_cast<float>(capture.h);
    return quad.surface == ConstructionSurface::Client &&
        quad.origin == std::array{0.F, 0.F, 0.F} &&
        quad.u == std::array{width, 0.F, 0.F} && quad.v == std::array{0.F, height, 0.F} &&
        quad.uv == std::array{0.F, 0.F, width, height} &&
        quad.tint == std::array<unsigned char, 4>{255, 255, 255, 255};
}

bool MotionSite::admits(const animation::Motion &motion) noexcept {
    return valid_interval(motion.sample_seconds) && is_usable(motion.from_body) && is_usable(motion.body) &&
        ((motion.resized && size_changed(motion.from_body, motion.body)) ||
            (!motion.workspace_shifted && motion.translated && translated(motion.from_body, motion.body)));
}

MotionSite::Kind MotionSite::kind() const noexcept {
    if (std::holds_alternative<studies::IntactTransport>(m_model))
        return m_variant == TransportVariant::Crane ? Kind::Crane : Kind::Carry;
    if (std::holds_alternative<studies::EdgeStretch>(m_model)) return Kind::Resize;
    if (std::holds_alternative<studies::PieceRelocation>(m_model)) return Kind::Pieces;
    return Kind::None;
}

animation::Frame MotionSite::advance(const animation::Step &step) noexcept {
    if (m_done || m_variant == TransportVariant::Native) return {};
    const auto finish = [this](bool interrupted = false) noexcept {
        m_done = true;
        m_batch.count = 0;
        m_batch.overflow = false;
        return animation::Frame{.interrupted = interrupted};
    };
    const auto previous = kind();
    const bool fresh = previous == Kind::None;
    if (!step.movement_enabled || step.dying || !step.workspace_visible || step.stalled ||
        !valid_time(step.elapsed) || !is_usable(step.body) || !is_usable(step.capture) ||
        !is_usable(step.screen) || (!fresh && (step.monitor != m_monitor || step.workspace != m_workspace))) {
        return finish(true);
    }

    if (fresh && (!step.motion || !admits(*step.motion))) return finish();
    const Box from = fresh ? initial_body(*step.motion, step.body) : m_body;
    const bool resize = size_changed(from, step.body);
    const bool move = translated(from, step.body);
    const double max_span = std::max({from.w, from.h, step.body.w, step.body.h});
    // A missing/rejected tracker sample must not turn a teleport, stale frame, or scope rebase
    // into a new job. A decoration-only sample remains eligible to update the capture below.
    if (!is_usable(from) ||
        ((resize || move) && (!step.motion || (!fresh && !valid_time(step.motion->sample_seconds)) ||
            size_changed(step.motion->body, step.body) ||
            std::hypot(step.body.x - from.x, step.body.y - from.y) > 1.5 * max_span)) ||
        (step.motion && step.motion->workspace_shifted && !move && !resize)) {
        return finish(true);
    }

    auto next = previous;
    const bool moving_grab = step.held && !step.resize_held;
    if (resize && !moving_grab) {
        next = Kind::Resize;
    } else if (move || (resize && moving_grab)) {
        // During a move grab, incidental configures do not enlist an edge crew. Intact
        // transport can follow the new size; a cut load has incompatible source geometry.
        if (step.resize_held && previous != Kind::Resize) return finish();
        if (!step.resize_held) {
            switch (m_variant) {
                case TransportVariant::Crane: next = Kind::Crane; break;
                case TransportVariant::CrewCarry: next = Kind::Carry; break;
                case TransportVariant::PieceByPiece:
                    if (resize) return finish(true);
                    next = Kind::Pieces;
                    break;
                case TransportVariant::Native: return finish();
            }
        }
    }
    if (next == Kind::None) return finish();

    const bool alternate = next == Kind::Crane || next == Kind::Pieces;
    const auto owner = step.visible_output_owner.value_or(step.screen);
    const auto outputs = step.visible_outputs;
    if (alternate) {
        // The host can cull by the native body before looking at transformed bounds.
        if (!is_usable(owner) || !overlaps(step.body, owner) ||
            touches_other_output(step.capture, owner, outputs) ||
            (step.visible_output_owner && std::ranges::none_of(outputs, [&](Box output) {
                return same_box(output, owner);
            })) ||
            (previous == next && !same_box(owner, m_output_owner))) {
            return finish(true);
        }
        if (next == Kind::Pieces &&
            (!overlaps(from, owner) || touches_other_output(from, owner, outputs))) {
            return finish(true);
        }
        m_output_owner = owner;
    }

    const auto area = is_usable(step.work_area) ? step.work_area : step.screen;
    // A quiet output may go arbitrarily long without a sample. The first measured
    // geometry is still current; don't infer velocity from the dormant interval.
    const double measured = step.motion && valid_time(step.motion->sample_seconds) ?
        step.motion->sample_seconds : 0;
    bool active;
    if (next == Kind::Resize) {
        if (previous != Kind::Resize) {
            // Seed the previous local rectangle without advancing the roster/clock. EdgeStretch
            // then separates common translation from near/far edge work from these exact facts.
            m_model.emplace<studies::EdgeStretch>(m_seed).step({.body = from, .work_area = area});
        }
        auto &model = std::get<studies::EdgeStretch>(m_model);
        model.step({.body = step.body, .work_area = area, .elapsed = step.elapsed,
            .sample_seconds = measured, .held = step.resize_held});
        active = model.active();
    } else if (next == Kind::Pieces) {
        auto *model = std::get_if<studies::PieceRelocation>(&m_model);
        if (!model) {
            model = &m_model.emplace<studies::PieceRelocation>();
            model->reset(from, m_seed);
        }
        model->step(step.body, step.elapsed, moving_grab);
        active = model->active();
    } else {
        auto *model = std::get_if<studies::IntactTransport>(&m_model);
        const auto crane_owner = next == Kind::Crane ? std::optional{owner} : std::nullopt;
        const auto crane_outputs = crane_owner ? outputs : std::span<const Box>{};
        if (!model) {
            const auto transport = next == Kind::Crane ? studies::IntactTransportKind::Crane :
                studies::IntactTransportKind::CrewCarry;
            model = &m_model.emplace<studies::IntactTransport>(transport, m_seed);
            auto acquisition = step;
            acquisition.motion->from_body = from;
            acquisition.motion->body = step.body;
            acquisition.motion->workspace_shifted = false;
            acquisition.motion->resized = resize;
            acquisition.motion->sample_seconds = measured;
            if (!model->step(acquisition, crane_outputs, crane_owner)) return finish(true);
        } else if (!model->step(step, crane_outputs, crane_owner)) {
            return finish(true);
        }
        active = model->active();
    }
    if (!active) return finish();

    m_body = step.body;
    m_capture = step.capture;
    m_monitor = step.monitor;
    m_workspace = step.workspace;
    Box bounds;
    if (next == Kind::Pieces) {
        bounds = emit_piece_batch(m_batch, m_body, m_capture, std::get<studies::PieceRelocation>(m_model));
    } else if (next == Kind::Crane) {
        bounds = emit_crane_batch(m_batch, m_body, m_capture, std::get<studies::IntactTransport>(m_model));
    } else {
        bounds = emit_motion_batch(m_batch, m_body, m_capture,
            std::get_if<studies::IntactTransport>(&m_model), std::get_if<studies::EdgeStretch>(&m_model));
    }
    if (!is_usable(bounds) || !m_batch.count || m_batch.overflow) return finish(true);
    if (alternate && touches_other_output(
            {bounds.x + m_capture.x, bounds.y + m_capture.y, bounds.w, bounds.h}, owner, outputs))
        return finish(true);
    return {.running = true, .bounds = bounds, .native_presentation = is_capture_identity(m_batch, m_capture)};
}

}  // namespace hyprimate::construction
