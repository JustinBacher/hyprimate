#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "shared/Core.hpp"
#include "window/cloth/ClothFlutter.hpp"
#include "window/cloth/ClothPlayback.hpp"
#include "window/cloth/ClothPrivate.hpp"
#include "window/cloth/ClothSimulation.hpp"
#include "window/cloth/ClothWorkers.hpp"
#include "window/shared/SheetFrame.hpp"

namespace hyprimate {
    namespace {
        using cloth_detail::FALLBACK_LENGTH;
        using cloth_detail::GRID;
        using cloth_detail::MAX_OBSTACLES;
        using cloth_detail::STEP_SECONDS;
        using cloth_detail::SimInput;
        using cloth_detail::TerminalReason;
        using cloth_detail::WorkerAdmission;
        using cloth_detail::mul;
        using cloth_detail::rest_grid;
        using cloth_detail::sub;

        constexpr std::uint64_t BASE_SEED = 0xC1074F1A77E2ULL;

        // Live movement displays this far behind the latest target. The worker answers a target
        // only after the compositor posts it, so the snapshot pair around the displayed time must
        // already exist one 60 Hz frame plus one step later. Displaying any later alternates
        // interpolation with extrapolation from whichever snapshots happen to be ready.
        constexpr double MOVEMENT_LATENCY = 4 * STEP_SECONDS;

        const char *failure_message(TerminalReason reason) noexcept {
            switch (reason) {
                case TerminalReason::SolverFailure:
                    return "solver failure";
                case TerminalReason::RecordingFailure:
                    return "recording rejected a sample";
                case TerminalReason::RecordingAllocationFailure:
                    return "recording allocation failed";
                case TerminalReason::Running:
                case TerminalReason::Completed:
                case TerminalReason::Cancelled:
                case TerminalReason::QueueOverflow:
                    return nullptr;
            }
            return nullptr;
        }

        void report_worker_failure(ClothFlutter::Run &run) noexcept {
            const char *const MESSAGE = failure_message(run.terminal.load(std::memory_order_acquire));
            if (!MESSAGE || run.report_closed.exchange(true, std::memory_order_relaxed)) {
                return;
            }
            std::fprintf(stderr,
                "hyprimate: cloth worker operation=%s seed=%" PRIu64 " recorded=%d: %s\n",
                run.live ? "move" : (run.kind == Transition::Close ? "close" : "open"),
                run.seed,
                run.recorded.load(std::memory_order_relaxed),
                MESSAGE);
        }

        // A curtain has enough material across its width to form vertical folds. Integrate
        // unit-length tangents so gathering spends width in depth without pre-stretching the mesh.
        // The cloth solve, not a display warp, unfolds this drape.
        SheetGrid curtain_grid() noexcept {
            std::array<Vec2, GRID> header{};
            auto *previous = header.begin();
            auto *current = std::next(previous);
            for (int i = 1; i < GRID; ++i, ++previous, ++current) {
                const double ANGLE = 1.1 * std::sin(10 * std::numbers::pi * (i - .5) / SHEET_CELLS);
                *current = {
                    .x = previous->x + (std::cos(ANGLE) / SHEET_CELLS),
                    .y = previous->y + (std::sin(ANGLE) / SHEET_CELLS),
                };
            }
            SheetGrid out{};
            auto *vertex = out.begin();
            for (int j = 0; j < GRID; ++j) {
                for (const auto &fold : header) {
                    const double DOWN = static_cast<double>(j) / SHEET_CELLS;
                    *vertex = {
                        .x = fold.x - (header.back().x / 2),
                        .y = -DOWN,
                        .z = fold.y * smoothstep(0, .18, DOWN),
                    };
                    ++vertex;
                }
            }
            return out;
        }
        bool valid_box(const Box &box) noexcept {
            // Logical desktop geometry is far smaller than these bounds. Reject values that cannot
            // safely pass through float collision geometry and pixel-space projection, even
            // when the original doubles are finite.
            return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) &&
                   std::isfinite(box.h) && std::abs(box.x) <= 1e9 && std::abs(box.y) <= 1e9 &&
                   box.w >= 1 && box.h >= 1 && box.w <= 1e7 && box.h <= 1e7;
        }
        bool valid_close_geometry(const std::optional<ClothCloseGeometry> &geometry) noexcept {
            return !geometry ||
                   (valid_box(geometry->screen) &&
                       (geometry->edge == Edge::Top || geometry->edge == Edge::Bottom ||
                           geometry->edge == Edge::Left || geometry->edge == Edge::Right));
        }
        void collect_obstacles(std::vector<Box> &out,
            std::span<const Box> obstacles,
            const Box &window,
            Transition kind,
            Edge edge) {
            std::vector<std::pair<double, Box>> nearest;
            for (const Box &obstacle : obstacles) {
                if (!valid_box(obstacle) ||
                    (obstacle.x < window.x + window.w && obstacle.x + obstacle.w > window.x &&
                        obstacle.y < window.y + window.h && obstacle.y + obstacle.h > window.y)) {
                    continue;
                }
                // Leave the closing sheet's exit corridor clear, while retaining adjacent
                // obstacles.
                if (kind == Transition::Close) {
                    const bool OVERLAP_X =
                        obstacle.x < window.x + window.w && obstacle.x + obstacle.w > window.x;
                    const bool OVERLAP_Y =
                        obstacle.y < window.y + window.h && obstacle.y + obstacle.h > window.y;
                    if ((edge == Edge::Bottom && OVERLAP_X && obstacle.y >= window.y + window.h) ||
                        (edge == Edge::Top && OVERLAP_X && obstacle.y + obstacle.h <= window.y) ||
                        (edge == Edge::Left && OVERLAP_Y && obstacle.x + obstacle.w <= window.x) ||
                        (edge == Edge::Right && OVERLAP_Y && obstacle.x >= window.x + window.w)) {
                        continue;
                    }
                }
                const double GAP_X = std::max(
                    {window.x - obstacle.x - obstacle.w, obstacle.x - window.x - window.w, 0.0});
                const double GAP_Y = std::max(
                    {window.y - obstacle.y - obstacle.h, obstacle.y - window.y - window.h, 0.0});
                nearest.push_back({std::hypot(GAP_X, GAP_Y),
                    {.x = (obstacle.x - window.x - (window.w / 2)) / window.w,
                        .y = -(obstacle.y + obstacle.h - window.y) / window.h,
                        .w = obstacle.w / window.w,
                        .h = obstacle.h / window.h}});
            }
            std::ranges::stable_sort(nearest,
                [](const auto &left, const auto &right) { return left.first < right.first; });
            for (const auto &obstacle : std::span(nearest).first(
                     std::min(nearest.size(), static_cast<std::size_t>(MAX_OBSTACLES)))) {
                out.push_back(obstacle.second);
            }
        }
        bool sanitize(ClothParams &params) noexcept {
            if (!std::isfinite(params.duration) || params.duration <= 0 ||
                !std::isfinite(params.flutter) || !std::isfinite(params.shading) ||
                !std::isfinite(params.variation) || !std::isfinite(params.shine)) {
                return false;
            }
            params.flutter = std::clamp(params.flutter, 0.0, 1.0);
            params.shading = std::clamp(params.shading, 0.0, 1.0);
            params.shine = std::clamp(params.shine, 0.0, 1.0);
            params.variation = std::clamp(params.variation, 0.0, 1.0);
            return true;
        }
        bool sanitize(ClothMovementParams &params) noexcept {
            if (!std::isfinite(params.strength) || !std::isfinite(params.flutter) ||
                !std::isfinite(params.shading) || !std::isfinite(params.variation) ||
                !std::isfinite(params.shine) || !std::isfinite(params.springiness) ||
                !std::isfinite(params.gravity)) {
                return false;
            }
            params.strength = std::clamp(params.strength, 0.0, 1.5);
            params.flutter = std::clamp(params.flutter, 0.0, 1.0);
            params.shading = std::clamp(params.shading, 0.0, 1.0);
            params.variation = std::clamp(params.variation, 0.0, 1.0);
            params.shine = std::clamp(params.shine, 0.0, 1.0);
            params.springiness = std::clamp(params.springiness, 0.0, 1.0);
            params.gravity = std::clamp(params.gravity, 0.0, 3.0);
            return true;
        }
        bool valid_transition(Transition kind,
            ClothParams &params,
            const Box &window,
            const std::optional<ClothCloseGeometry> &geometry) noexcept {
            return (kind == Transition::Open || kind == Transition::Close) && sanitize(params) &&
                   valid_box(window) && valid_close_geometry(geometry);
        }
        bool valid_movement(ClothMovementParams &params, const Box &capture) noexcept {
            return valid_box(capture) && sanitize(params) && params.strength > 0;
        }
    }  // namespace

    ClothFlutter::ClothFlutter() = default;
    ClothFlutter::~ClothFlutter() = default;
    ClothFlutter::ClothFlutter(ClothFlutter &&) noexcept = default;
    ClothFlutter &ClothFlutter::operator=(ClothFlutter &&) noexcept = default;
    ClothFlutter::ClothFlutter(const ClothFlutter &other)
        : m_phase(other.m_phase),
          m_params(other.m_params),
          m_t(other.m_t),
          m_rate(other.m_rate),
          m_startup_wait(other.m_startup_wait),
          m_fallback_time(other.m_fallback_time),
          m_initial_opacity(other.m_initial_opacity),
          m_startup_recorded(other.m_startup_recorded),
          m_transition_held(other.m_transition_held),
          m_run(other.m_movement ? nullptr : other.m_run),
          m_fallback(other.m_fallback) {
        if (other.m_movement) {
            m_movement = std::make_unique<MovementPlayback>(*other.m_movement);
            m_movement->producer =
                false;  // retained closes copy the displayed pose, never the live input stream
        }
    }
    ClothFlutter &ClothFlutter::operator=(const ClothFlutter &other) {
        if (this != &other) {
            ClothFlutter copy(other);
            *this = std::move(copy);
        }
        return *this;
    }

    bool ClothFlutter::can_begin(Transition kind,
        const ClothParams &params,
        const Box &window,
        std::optional<ClothCloseGeometry> geometry) noexcept {
        auto checked = params;
        return valid_transition(kind, checked, window, geometry);
    }

    bool ClothFlutter::can_begin_movement(
        const ClothMovementParams &params, const Box &capture) noexcept {
        auto checked = params;
        return valid_movement(checked, capture);
    }
    bool ClothFlutter::can_update_movement(const Box &capture, double elapsed_seconds) noexcept {
        return valid_box(capture) && std::isfinite(elapsed_seconds) && elapsed_seconds >= 0 &&
               elapsed_seconds <= STALL_SECONDS;
    }

    void ClothFlutter::begin_movement(
        const ClothMovementParams &params, std::uint64_t seed, const Box &capture) noexcept try {
        const WorkerAdmission admission;
        auto movement = params;
        if (!admission.available() || !valid_movement(movement, capture)) {
            reset();
            return;
        }
        const auto RUN = std::make_shared<Run>();
        RUN->live = true;
        RUN->seed = seed;
        RUN->window = capture;
        RUN->movement = movement;
        const double ALPHA = opacity();
        sample_vertices(RUN->start, RUN->velocity);
        auto playback = std::make_unique<MovementPlayback>();
        playback->target = playback->previous = capture;
        playback->params = movement;
        playback->a().time = -STEP_SECONDS;
        playback->b().time = 0;
        const auto *velocity = RUN->velocity.begin();
        auto *first_sample = playback->a().values.begin();
        auto *second_sample = playback->b().values.begin();
        for (const auto POSITION : RUN->start) {
            const auto VELOCITY = *velocity++;
            const Run::VertexSample VALUE{
                .position =
                    {
                        .x = static_cast<float>(POSITION.x),
                        .y = static_cast<float>(POSITION.y),
                        .z = static_cast<float>(POSITION.z),
                    },
                .velocity =
                    {
                        .x = static_cast<float>(VELOCITY.x),
                        .y = static_cast<float>(VELOCITY.y),
                        .z = static_cast<float>(VELOCITY.z),
                    },
            };
            *first_sample = VALUE;
            ++first_sample;
            *second_sample = VALUE;
            ++second_sample;
        }
        std::uint64_t fixed = BASE_SEED;
        std::uint64_t random = seed;
        const double PHASE = 2 * std::numbers::pi *
                             ((unit_draw(fixed) * (1 - RUN->movement.variation)) +
                                 (unit_draw(random) * RUN->movement.variation));
        const auto LEASE = admission.launch_movement(RUN, PHASE);
        m_run = std::shared_ptr<Run>(LEASE, RUN.get());
        m_movement = std::move(playback);
        m_params = {
            .duration = 1,
            .flutter = RUN->movement.flutter,
            .shading = RUN->movement.shading,
            .variation = RUN->movement.variation,
            .shine = RUN->movement.shine,
        };
        m_initial_opacity = ALPHA;
        m_t = 0;
        m_rate = 1;
        m_startup_wait = m_fallback_time = 0;
        m_fallback.reset();
        m_phase = Phase::Movement;
    } catch (...) {
        reset();
    }

    void ClothFlutter::set_movement_params(const ClothMovementParams &params) noexcept {
        auto movement = params;
        if (!moving() || !m_movement || !m_movement->producer || !sanitize(movement)) {
            return;
        }
        m_movement->params = movement;
        m_params.shading = movement.shading;
        m_params.shine = movement.shine;
    }

    void ClothFlutter::update_movement(const Box &capture, double elapsed_seconds) noexcept {
        if (!moving() || !m_movement || !m_movement->producer) {
            return;
        }
        if (!can_update_movement(capture, elapsed_seconds)) {
            reset();
            return;
        }
        const Box OLD = m_movement->target;
        if (capture.x != OLD.x || capture.y != OLD.y || capture.w != OLD.w || capture.h != OLD.h) {
            if (m_fallback && m_movement->settling) {
                // Motion during the exact-flat transition starts from its displayed pose.
                const auto PARAMS = m_movement->params;
                begin_movement(PARAMS, BASE_SEED, OLD);
            }
            if (!m_movement) {
                return;
            }
            m_movement->last_motion = m_t;
            m_movement->target = capture;
        }
    }

    bool ClothFlutter::moving() const noexcept {
        return m_phase == Phase::Movement;
    }

    void ClothFlutter::begin(Transition kind,
        const ClothParams &params,
        std::uint64_t seed,
        const Box &window,
        std::span<const Box> obstacles,
        std::optional<ClothCloseGeometry> geometry) noexcept try {
        const WorkerAdmission admission;
        ClothParams cloth_params = params;
        if (!admission.available() || !valid_transition(kind, cloth_params, window, geometry)) {
            reset();
            return;
        }
        const auto RUN = std::make_shared<Run>();
        RUN->kind = kind;
        RUN->seed = seed;
        RUN->window = window;
        RUN->screen = geometry ? geometry->screen
                               : Box{.x = window.x, .y = window.y, .w = window.w, .h = window.h};
        RUN->edge = geometry ? geometry->edge : Edge::Bottom;
        const double OLD_OPACITY = opacity();
        if (kind == Transition::Close && active() && m_phase != Phase::Closing) {
            if (m_movement) {
                RUN->movement.springiness = m_movement->params.springiness;
            }
            sample_vertices(RUN->start, RUN->velocity);
        } else {
            RUN->start = rest_grid();
        }
        if (kind == Transition::Open) {
            RUN->start = curtain_grid();
        } else {
            const double WIDTH = window.w;
            const double HEIGHT = window.h;
            RUN->length = 1.4;
            if (RUN->edge == Edge::Bottom) {
                RUN->travel.y =
                    -(std::max(0.0, (RUN->screen.y + RUN->screen.h - window.y) / HEIGHT) + 2.5);
            }
            if (RUN->edge == Edge::Top) {
                RUN->travel.y = std::max(0.0, (window.y - RUN->screen.y) / HEIGHT) + 2.5;
            }
            if (RUN->edge == Edge::Left) {
                RUN->travel.x = -(std::max(0.0, (window.x - RUN->screen.x) / WIDTH) + 2.5);
            }
            if (RUN->edge == Edge::Right) {
                RUN->travel.x =
                    std::max(0.0, (RUN->screen.x + RUN->screen.w - window.x - WIDTH) / WIDTH) + 2.5;
            }
        }
        RUN->frames = static_cast<int>(std::ceil(RUN->length / STEP_SECONDS)) + 1;
        RUN->length = (RUN->frames - 1) * STEP_SECONDS;
        const double RATE = RUN->length / cloth_params.duration;
        for (auto &initial_velocity : RUN->velocity) {
            initial_velocity = mul(initial_velocity,
                1 / RATE);  // displayed real-time velocity -> new simulation clock
            RUN->max_initial_speed = std::max(RUN->max_initial_speed,
                std::sqrt((initial_velocity.x * initial_velocity.x) +
                          (initial_velocity.y * initial_velocity.y) +
                          (initial_velocity.z * initial_velocity.z)));
        }
        std::uint64_t base = BASE_SEED;
        std::uint64_t random = seed;
        const auto DRAW = [&] {
            const double BASE_DRAW = unit_draw(base);
            const double RANDOM_DRAW = unit_draw(random);
            return BASE_DRAW + (cloth_params.variation * (RANDOM_DRAW - BASE_DRAW));
        };
        SimInput input;
        input.flutter = cloth_params.flutter;
        input.phase_a = 2 * std::numbers::pi * DRAW();
        input.phase_b = 2 * std::numbers::pi * DRAW();
        input.freq_a = 9 + (4 * DRAW());
        collect_obstacles(input.obstacles, obstacles, window, kind, RUN->edge);
        RUN->outside_frame.store(RUN->frames, std::memory_order_relaxed);
        const auto LEASE = admission.launch_transition(RUN, std::move(input));
        m_run = std::shared_ptr<Run>(LEASE, RUN.get());
        m_params = cloth_params;
        m_movement.reset();
        m_rate = RATE;
        m_t = 0;
        m_startup_wait = 0;
        m_startup_recorded = 0;
        m_transition_held = false;
        m_fallback_time = 0;
        m_fallback.reset();
        m_initial_opacity = kind == Transition::Close ? OLD_OPACITY : 1;
        m_phase = kind == Transition::Open ? Phase::Opening : Phase::Closing;
    } catch (...) {
        reset();
    }

    void ClothFlutter::start_fallback() noexcept {
        // Only an active playback can adopt a failure; supersession and cancellation stay quiet.
        // This runs outside worker admission and the registry mutex.
        if (m_run) {
            report_worker_failure(*m_run);
        }
        try {
            auto fallback = std::make_shared<Fallback>();
            sample_vertices(fallback->positions, fallback->velocities);
            if (m_phase == Phase::Movement && m_movement) {
                for (auto &velocity : fallback->velocities) {
                    velocity = sub(velocity, m_movement->rigid_velocity);
                }
            }
            fallback->alpha = opacity();
            m_fallback = std::move(fallback);
            m_fallback_time = 0;
            // The fallback owns its pose. Only the last playback lease stops the worker.
            m_run.reset();
        } catch (...) {
            if (m_phase != Phase::Closing) {
                reset();
            } else {
                m_phase = Phase::Closed;
                m_run.reset();
            }
        }
    }
    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Splitting risks fresh atomic-read
    // ordering and frame-path calls.
    bool ClothFlutter::advance(double elapsed_seconds) noexcept {
        if (!active()) {
            return false;
        }
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0 || elapsed_seconds > 0.25) {
            if (m_phase != Phase::Closing) {
                reset();
            } else {
                m_phase = Phase::Closed;
                m_run.reset();
            }
            return false;
        }
        if (moving() && m_movement && elapsed_seconds > 0) {
            const Box OLD = m_movement->previous;
            const Box NOW = m_movement->target;
            m_movement->rigid_velocity = {
                .x = (NOW.x - OLD.x) / elapsed_seconds / NOW.w,
                .y = -(NOW.y - OLD.y) / elapsed_seconds / NOW.h,
                .z = 0,
            };
            m_movement->previous = NOW;
        }
        if (m_fallback) {
            m_fallback_time += elapsed_seconds;
            if (m_fallback_time >= FALLBACK_LENGTH) {
                if (m_phase != Phase::Closing) {
                    reset();
                } else {
                    m_phase = Phase::Closed;
                    m_run.reset();
                    m_fallback.reset();
                }
            }
            return active();
        }
        if (moving() && m_movement) {
            auto &playback = *m_movement;
            if (!playback.producer) {
                start_fallback();
                return active();
            }
            if (elapsed_seconds > 0) {
                m_t += elapsed_seconds;
                const unsigned HEAD = m_run->input_head.load(std::memory_order_relaxed);
                if (HEAD - m_run->input_tail.load(std::memory_order_acquire) ==
                    Run::INPUT_CAPACITY) {
                    m_run->overflow.store(true, std::memory_order_release);
                } else {
                    m_run->targets.at(HEAD % Run::INPUT_CAPACITY) = {
                        .time = m_t,
                        .box = playback.target,
                        .movement = playback.params,
                    };
                    m_run->input_head.store(HEAD + 1, std::memory_order_release);
                }
            }
            // Consume snapshots only until the pair brackets the displayed time; later ones stay
            // queued for later frames.
            const double WANTED = std::max(0.0, m_t - MOVEMENT_LATENCY);
            const bool PROGRESSED = cloth_detail::consume_movement(*m_run, playback, WANTED);
            if (m_run->recorded.load(std::memory_order_acquire) < 2) {
                m_startup_wait += elapsed_seconds;
                if (m_startup_wait >= STALL_SECONDS || m_run->finished()) {
                    start_fallback();
                }
            } else {
                m_startup_wait = PROGRESSED || WANTED <= playback.b().time + 1e-8
                                     ? 0
                                     : m_startup_wait + elapsed_seconds;
                playback.settling = m_t - playback.last_motion >= 0.5;
                if (m_run->overflow.load(std::memory_order_acquire) ||
                    m_startup_wait >= STALL_SECONDS || m_run->finished() || playback.settling) {
                    start_fallback();
                }
            }
            return active();
        }
        const bool FINISHED = m_run->finished();
        const int RECORDED = m_run->recorded.load(std::memory_order_acquire);
        const bool COMPLETE = RECORDED >= m_run->frames;
        if (FINISHED && !COMPLETE) {
            start_fallback();
            return active();
        }
        if (RECORDED < 2) {
            if (elapsed_seconds > 0) {
                m_transition_held = true;
            }
            if (RECORDED > m_startup_recorded) {
                m_startup_recorded = RECORDED;
                m_startup_wait = 0;
            }
            m_startup_wait += elapsed_seconds;
            if (m_startup_wait >= STALL_SECONDS) {
                const bool FINISHED_NOW = m_run->finished();
                const int RECORDED_NOW = m_run->recorded.load(std::memory_order_acquire);
                if ((!FINISHED_NOW || RECORDED_NOW >= m_run->frames) &&
                    RECORDED_NOW > m_startup_recorded) {
                    m_startup_recorded = RECORDED_NOW;
                    m_startup_wait = 0;
                } else {
                    start_fallback();
                }
            }
            return active();
        }
        const double OLD_TIME = m_t;
        const double WANTED = std::min(OLD_TIME + (elapsed_seconds * m_rate), m_run->length);
        const double FRONTIER =
            std::min(m_run->length, ((RECORDED - 1) * STEP_SECONDS) + STEP_SECONDS);
        m_t = std::min(WANTED, FRONTIER);
        const bool WAITING =
            !COMPLETE && (WANTED > FRONTIER + 1e-10 || WANTED >= m_run->length - 1e-10);
        if (elapsed_seconds > 0) {
            m_transition_held = m_t <= OLD_TIME + 1e-10;
        }
        if (COMPLETE) {
            m_startup_wait = 0;
        } else if (FRONTIER > OLD_TIME + 1e-10) {
            const double SERVED = std::clamp((FRONTIER - OLD_TIME) / m_rate, 0.0, elapsed_seconds);
            m_startup_wait = WAITING ? elapsed_seconds - SERVED : 0;
        } else if (WAITING) {
            m_startup_wait += elapsed_seconds;
        }
        if (WAITING && m_startup_wait >= STALL_SECONDS) {
            const bool FINISHED_NOW = m_run->finished();
            const int RECORDED_NOW = m_run->recorded.load(std::memory_order_acquire);
            if ((!FINISHED_NOW || RECORDED_NOW >= m_run->frames) && RECORDED_NOW > RECORDED) {
                m_startup_wait = 0;
            } else {
                start_fallback();
            }
            return active();
        }
        if (COMPLETE && m_t >= m_run->length - 1e-10) {
            if (m_phase == Phase::Opening) {
                reset();
            } else {
                m_phase = Phase::Closed;
                m_run.reset();
            }
        }
        return active();
    }
    void ClothFlutter::reset() noexcept {
        m_phase = Phase::Idle;
        m_t = 0;
        m_startup_recorded = 0;
        m_transition_held = false;
        m_run.reset();
        m_fallback.reset();
        m_movement.reset();
    }
    bool ClothFlutter::active() const noexcept {
        if (moving()) {
            return m_movement != nullptr;  // A retained movement pose needs no worker.
        }
        return (m_phase == Phase::Opening || m_phase == Phase::Closing) && (m_run || m_fallback);
    }
    bool ClothFlutter::closed() const noexcept {
        return m_phase == Phase::Closed;
    }
    bool ClothFlutter::shaded() const noexcept {
        return m_params.shading > 0;
    }
    double ClothFlutter::opacity() const noexcept {
        if (m_phase == Phase::Idle) {
            return 1;
        }
        if (m_phase == Phase::Closed) {
            return 0;
        }
        if (m_fallback) {
            return m_phase != Phase::Closing
                       ? m_fallback->alpha + ((1 - m_fallback->alpha) *
                                                 smoothstep(0, FALLBACK_LENGTH, m_fallback_time))
                       : m_fallback->alpha * (1 - smoothstep(0, FALLBACK_LENGTH, m_fallback_time));
        }
        if (m_phase == Phase::Opening) {
            return smoothstep(0, 0.08, m_t / m_run->length);
        }
        if (m_phase == Phase::Movement) {
            return m_initial_opacity + ((1 - m_initial_opacity) * smoothstep(0, 0.08, m_t));
        }
        const double OUTSIDE_TIME =
            m_run->outside_frame.load(std::memory_order_acquire) * STEP_SECONDS;
        return m_initial_opacity *
               (1 - smoothstep(
                        OUTSIDE_TIME, std::max(OUTSIDE_TIME + STEP_SECONDS, m_run->length), m_t));
    }
}  // namespace hyprimate
