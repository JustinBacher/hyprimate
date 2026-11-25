#include <utility>

#include "IntactTransportPrivate.hpp"

namespace hyprimate::construction::studies {
    using namespace intact_transport_detail;
    namespace {

        constexpr double COAST_SECONDS = .085;

        constexpr Vec2 origin(Box b) noexcept {
            return {b.x, b.y};
        }

        Vec2 common_translation(Box previous, Box current) noexcept {
            const auto median = [](double a, double b) {
                return std::clamp(0.0, std::min(a, b), std::max(a, b));
            };
            return {median(current.x - previous.x, right(current) - right(previous)),
                median(current.y - previous.y, bottom(current) - bottom(previous))};
        }

        Box nearest_output(Box body, std::span<const Box> outputs) noexcept {
            Box best;
            std::pair<double, double> score{-1, 0};
            const Vec2 center{body.x + .5 * body.w, body.y + .5 * body.h};
            for (const Box out : outputs) {
                if (!usable(out)) continue;
                const double w =
                    std::max(0.0, std::min(right(body), right(out)) - std::max(body.x, out.x));
                const double h =
                    std::max(0.0, std::min(bottom(body), bottom(out)) - std::max(body.y, out.y));
                const Vec2 d{std::max({out.x - center.x, 0.0, center.x - right(out)}),
                    std::max({out.y - center.y, 0.0, center.y - bottom(out)})};
                const std::pair candidate{w * h, -dot(d, d)};
                if (candidate > score) {
                    score = candidate;
                    best = out;
                }
            }
            return best;
        }

    }  // namespace

    Vec2 IntactTransportFace::point(Vec2 local) const noexcept {
        if (source.w <= 0 || source.h <= 0) return origin;
        return origin + u * (local.x / source.w) + v * (local.y / source.h);
    }

    IntactTransport::IntactTransport(IntactTransportKind kind, std::uint64_t seed,
        double worker_size) noexcept
        : m_kind(kind), m_seed(seed), m_worker_size(worker_size) {
    }

    void IntactTransport::reset() noexcept {
        m_active = m_working = m_was_held = m_drag_translated = false;
        m_shadow = {};
        m_bearer_count = m_crew_count = 0;
        m_crew_roster = 0;
        m_crane = {};
        m_crane_pose.reset();
        m_velocity = {};
        m_pressure = m_angle = m_angular_velocity = m_coast = m_pickup_remaining = 0;
    }

    void IntactTransport::pose_face(double lift, double radians) noexcept {
        const Box body = m_face.source;
        if (lift == 0) {
            m_face = {body, origin(body), {body.w, 0}, {0, body.h}, 0};
            return;
        }
        const double c = std::cos(radians), s = std::sin(radians);
        const Vec2 u{c * body.w, s * body.w}, v{-s * body.h, c * body.h};
        m_face = {
            body, origin(body) + (Vec2{body.w, body.h} - u - v) * .5 - Vec2{0, lift}, u, v, lift};
    }

    bool IntactTransport::step(const animation::Step &step, std::span<const Box> outputs,
        std::optional<Box> owner) noexcept {
        const Box previous = m_face.source;
        const Box body = m_face.source = usable(step.body) ? step.body : Box{};
        const bool scope_changed = m_observed && step.workspace != m_workspace;
        const Box from = m_observed                                       ? previous
                         : step.motion && !step.motion->workspace_shifted ? step.motion->from_body
                                                                          : Box{};
        m_observed = true;
        m_workspace = step.workspace;
        m_shadow = {};

        const Vec2 shift = usable(from) && usable(body) ? common_translation(from, body) : Vec2{};
        const bool moved = shift.x != 0 || shift.y != 0;
        const bool resized = (step.motion && step.motion->resized) ||
                             (usable(from) && (body.w != from.w || body.h != from.h));
        if (!step.movement_enabled || step.dying || !step.workspace_visible || !usable(body) ||
            scope_changed || step.stalled || !std::isfinite(step.elapsed) || step.elapsed < 0 ||
            step.elapsed > STALL_SECONDS ||
            length(shift) > 1.5 * std::max({from.w, from.h, body.w, body.h})) {
            reset();
            pose_face(0, 0);
            return true;
        }

        const double dt = step.elapsed;
        const bool release = m_was_held && !step.held;
        m_was_held = step.held;
        m_coast = release ? 0 : moved ? COAST_SECONDS : std::max(0.0, m_coast - dt);
        // A held pause keeps contacts alive. Do not interpret acquisition from resize or wallpaper
        // motion as a pickup.
        const bool grab =
            step.held &&
            (m_active || moved || (!resized && !(step.motion && step.motion->workspace_shifted)));
        // A short translation gets one bounded pickup even if the grab ends before the crew
        // arrives. The deadline is never renewed by the release or by repeated motion samples.
        const bool fully_raised =
            m_kind == IntactTransportKind::CrewCarry ? m_pressure == 1 : m_crane.lift == 1;
        const bool pickup_started =
            moved && !fully_raised && (step.held || release ? !m_drag_translated : !m_working);
        if (pickup_started)
            m_pickup_remaining = m_kind == IntactTransportKind::CrewCarry ? .80 : 1.25;
        else if (fully_raised)
            m_pickup_remaining = 0;
        else
            m_pickup_remaining = std::max(0.0, m_pickup_remaining - dt);
        m_drag_translated = step.held && (m_drag_translated || moved);
        const bool work = grab || m_coast > 0 || m_pickup_remaining > 0;
        const bool fresh = !m_active;
        if (fresh && !work) {
            pose_face(0, 0);
            return true;
        }
        if (outputs.empty()) outputs = std::span<const Box>{&step.screen, 1};
        const Box apron = owner ? *owner : nearest_output(body, outputs);
        if (!usable(apron)) {
            reset();
            pose_face(0, 0);
            return !owner;
        }

        const bool regrab = work && !m_working;
        if (regrab) {
            m_work_age = m_release_age = 0;
        } else if (work) {
            m_work_age += dt;
        } else {
            if (m_working) {
                const double long_hold = ease((m_work_age - .66) / 2);
                m_settle = std::lerp(.108, .184, long_hold);
                m_exit = std::lerp(.378, .612, long_hold);
                m_release_pressure = m_pressure;
            }
            m_release_age += dt;
        }
        m_working = work;
        m_active = true;

        const double sample = step.motion ? step.motion->sample_seconds : 0;
        const double interval = sample > 0 && sample <= STALL_SECONDS ? sample : dt;
        const double max_speed = m_kind == IntactTransportKind::Crane ? 2400 : 1200;
        const double distance = length(shift);
        const Vec2 velocity = interval > 0 && distance > 0
                                  ? shift * (std::min(max_speed, distance / interval) / distance)
                                  : Vec2{};
        if (resized && usable(previous)) {
            const double ratio = std::min(body.w, body.h) / std::min(previous.w, previous.h);
            for (std::size_t i = 0; i < m_crew_count; ++i) {
                auto &a = m_crew[i];
                a.height *= ratio;
                a.stance_offset = a.stance_offset * ratio;
                a.staging = a.staging * ratio;
                a.stride *= ratio;
                a.trigger *= ratio;
            }
        }
        if (m_kind == IntactTransportKind::CrewCarry) {
            if (fresh || (work && (moved || resized) && m_crew_count < MAX_BEARERS))
                place_crew(apron);
            advance_crew(dt, velocity, work, regrab, resized);
        } else if (!advance_crane(dt, apron, outputs, owner.has_value(), velocity, work, fresh,
                       pickup_started, resized)) {
            reset();
            pose_face(0, 0);
            return false;
        }
        if (!m_active) reset();
        return true;
    }

}  // namespace hyprimate::construction::studies
