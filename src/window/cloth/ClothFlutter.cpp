#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mdspan>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "perf/LiveProfiler.hpp"

// Jolt's base header must precede its dependent headers.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Core/Array.h>
#include <Jolt/Core/Core.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyVertex.h>
#include <Jolt/RegisterTypes.h>

#include "shared/Core.hpp"
#include "window/cloth/ClothFlutter.hpp"
#include "window/shared/SheetFrame.hpp"

namespace hyprimate {
    namespace {
        constexpr int GRID = SHEET_CELLS + 1;
        constexpr double STEP_SECONDS = 1.0 / 120.0;
        constexpr double OPEN_LENGTH = 1.4;
        constexpr double SETTLE_FROM = 0.84;
        constexpr double GRAVITY = 9.81;
        constexpr double RADIUS = 0.002;
        constexpr double FALLBACK_LENGTH = 0.18;
        constexpr int MAX_OBSTACLES = 32;
        constexpr float CLOTH_INV_MASS = 4.0F, STRETCH_COMPLIANCE = 4e-6F, BEND_COMPLIANCE = 1.2F;
        constexpr std::uint64_t BASE_SEED = 0xC1074F1A77E2ULL;
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): Workers share this
        // test pause flag.
        std::atomic<bool> s_pause{false};
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): Worker pause
        // threshold is shared with the test seam.
        std::atomic<int> s_pause_after{0};

        // Live movement displays this far behind the latest target. The worker answers a target
        // only after the compositor posts it, so the snapshot pair around the displayed time must
        // already exist one 60 Hz frame plus one step later. Displaying any later alternates
        // interpolation with extrapolation from whichever snapshots happen to be ready.
        constexpr double MOVEMENT_LATENCY = 4 * STEP_SECONDS;
        // Air resists fabric across its face, damping the opening folds as the gathered header
        // spreads. Closes move their sheets in plane, so their gusts stay undamped.
        constexpr double OPEN_AIR_DRAG =
            4.0;  // per simulated second, on the velocity along the local normal

        // NOLINTNEXTLINE(modernize-avoid-variadic-functions): Jolt Trace requires a C variadic
        // callback.
        void silent_trace([[maybe_unused]] const char *format, ...) {
        }
        Vec3 add(Vec3 left, Vec3 right) noexcept {
            return {.x = left.x + right.x, .y = left.y + right.y, .z = left.z + right.z};
        }
        Vec3 mul(Vec3 vector, double factor) noexcept {
            return {.x = vector.x * factor, .y = vector.y * factor, .z = vector.z * factor};
        }
        Vec3 sub(Vec3 left, Vec3 right) noexcept {
            return add(left, mul(right, -1));
        }
        Vec3 lerp(Vec3 start, Vec3 end, double fraction) noexcept {
            return add(mul(start, 1 - fraction), mul(end, fraction));
        }
        std::size_t index(int column, int row) noexcept {
            return (static_cast<std::size_t>(row) * GRID) + column;
        }
        double smooth_derivative(double start, double end, double value) noexcept {
            const double PROGRESS = std::clamp((value - start) / (end - start), 0.0, 1.0);
            return 6 * PROGRESS * (1 - PROGRESS) / (end - start);
        }
        SheetGrid rest_grid() noexcept {
            SheetGrid out{};
            auto *vertex = out.begin();
            for (int j = 0; j < GRID; ++j) {
                for (int i = 0; i < GRID; ++i) {
                    *vertex = {
                        .x = (static_cast<double>(i) / SHEET_CELLS) - 0.5,
                        .y = -static_cast<double>(j) / SHEET_CELLS,
                        .z = 0,
                    };
                    ++vertex;
                }
            }
            return out;
        }
        // A curtain has enough material across its width to form vertical folds. Integrate
        // unit-length tangents so gathering spends width in depth without pre-stretching the mesh.
        // Jolt, not a display warp, unfolds this drape.
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
        float cloth_inverse_mass(int row) noexcept {
            // A sewn hem carries extra inertia without turning the whole panel into a heavy rubber
            // membrane.
            return CLOTH_INV_MASS /
                   static_cast<float>(
                       1 + (2 * smoothstep(.92, 1, static_cast<double>(row) / SHEET_CELLS)));
        }
        bool valid_box(const Box &box) noexcept {
            // Logical desktop geometry is far smaller than these bounds. Reject values that cannot
            // safely pass through float Jolt collision geometry and pixel-space projection, even
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
    }  // namespace

    struct ClothFlutter::Run {
        struct Float3 {
            float x, y, z;
        };
        struct VertexSample {
            Float3 position, velocity;
        };
        using Frame = std::array<VertexSample, SHEET_GRID_VERTICES>;
        static_assert(std::is_standard_layout_v<Float3> && std::is_standard_layout_v<VertexSample>);
        static_assert(sizeof(Float3) == 3 * sizeof(float) && alignof(Float3) == alignof(float));
        static_assert(offsetof(Float3, x) == 0 && offsetof(Float3, y) == sizeof(float) &&
                      offsetof(Float3, z) == 2 * sizeof(float));
        static_assert(
            sizeof(VertexSample) == 6 * sizeof(float) && alignof(VertexSample) == alignof(float));
        static_assert(offsetof(VertexSample, position) == 0 &&
                      offsetof(VertexSample, velocity) == 3 * sizeof(float));
        static_assert(sizeof(Frame) == SHEET_GRID_VERTICES * 6 * sizeof(float) &&
                      alignof(Frame) == alignof(float));
        struct Target {
            double time = 0;
            Box box{};
            ClothMovementParams movement{};
        };
        struct Snapshot {
            double time = 0;
            Vec3 rigid_velocity{};
            Frame values{};
        };
        // Buffer a full stall interval of targets at up to 1000 Hz, and the resulting 120 Hz
        // snapshot burst.
        static constexpr unsigned INPUT_CAPACITY = 256, OUTPUT_CAPACITY = 40;
        bool live = false;
        ClothMovementParams movement{};
        Vec3 publish_velocity{};  // rigid motion omitted by the strength-scaled translating
                                  // simulation frame
        Vec3 snapshot_rigid_velocity{};
        std::array<Target, INPUT_CAPACITY> targets{};
        std::array<Snapshot, OUTPUT_CAPACITY> snapshots{};
        std::atomic<unsigned> input_head{0}, input_tail{0}, output_head{0}, output_tail{0};
        std::atomic<bool> overflow{false};
        Transition kind = Transition::Open;
        Edge edge = Edge::Bottom;
        Box window{}, screen{};
        double length = OPEN_LENGTH;
        double max_initial_speed = 0;
        Vec3 travel{};
        int frames = 0;
        SheetGrid start{}, velocity{};
        // Every six-float position/velocity slot is written once, then published by recorded's
        // release store.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): Recording
        // length is determined at runtime.
        std::unique_ptr<Frame[]> samples;
        std::atomic<int> recorded{0}, outside_frame{0};
        std::atomic<bool> finished{false}, released{false};
        std::stop_source stop;
        std::thread worker;  // only the registry reaper joins it

        [[nodiscard]] static bool pinned(int row) noexcept {
            return row == 0;
        }
        void sample(
            SheetGrid &positions, double time_seconds, SheetGrid &velocities) const noexcept {
            const int COUNT = recorded.load(std::memory_order_acquire);
            if (COUNT < 2) {
                positions = start;
                velocities = velocity;
                return;
            }
            const double FRAME_POS =
                std::clamp(time_seconds / STEP_SECONDS, 0.0, static_cast<double>(COUNT - 1));
            const int FRAME_BEFORE = std::min(static_cast<int>(FRAME_POS), COUNT - 2);
            const double FRACTION = FRAME_POS - FRAME_BEFORE;
            const double FRACTION_SQUARED = FRACTION * FRACTION;
            const double FRACTION_CUBED = FRACTION_SQUARED * FRACTION;
            const double H00 = (2 * FRACTION_CUBED) - (3 * FRACTION_SQUARED) + 1;
            const double H10 = FRACTION_CUBED - (2 * FRACTION_SQUARED) + FRACTION;
            const double H01 = (-2 * FRACTION_CUBED) + (3 * FRACTION_SQUARED);
            const double H11 = FRACTION_CUBED - FRACTION_SQUARED;
            const double D00 = ((6 * FRACTION_SQUARED) - (6 * FRACTION)) / STEP_SECONDS;
            const double D10 = (3 * FRACTION_SQUARED) - (4 * FRACTION) + 1;
            const double D01 = ((-6 * FRACTION_SQUARED) + (6 * FRACTION)) / STEP_SECONDS;
            const double D11 = (3 * FRACTION_SQUARED) - (2 * FRACTION);
            const std::span<const Frame> PUBLISHED(samples.get(), static_cast<std::size_t>(COUNT));
            const auto FRAME_BEFORE_ITER = std::next(PUBLISHED.begin(), FRAME_BEFORE);
            const auto *before = FRAME_BEFORE_ITER->begin();
            const auto *after = std::next(FRAME_BEFORE_ITER)->begin();
            auto *velocity = velocities.begin();
            std::size_t vertex_index = 0;
            for (Vec3 &position : positions) {
                const auto &sample_before = *before++;
                const auto &sample_after = *after++;
                Vec3 &current_velocity = *velocity;
                ++velocity;
                const Vec3 POS_BEFORE{
                    .x = sample_before.position.x,
                    .y = sample_before.position.y,
                    .z = sample_before.position.z,
                };
                const Vec3 VEL_BEFORE{
                    .x = sample_before.velocity.x,
                    .y = sample_before.velocity.y,
                    .z = sample_before.velocity.z,
                };
                const Vec3 POS_AFTER{
                    .x = sample_after.position.x,
                    .y = sample_after.position.y,
                    .z = sample_after.position.z,
                };
                const Vec3 VEL_AFTER{
                    .x = sample_after.velocity.x,
                    .y = sample_after.velocity.y,
                    .z = sample_after.velocity.z,
                };
                if (vertex_index++ < GRID) {
                    // Interpolation between solved pocket positions cannot overshoot an end stop or
                    // reorder samples.
                    position = lerp(POS_BEFORE, POS_AFTER, FRACTION);
                    current_velocity = lerp(VEL_BEFORE, VEL_AFTER, FRACTION);
                    continue;
                }
                position = add(add(mul(POS_BEFORE, H00), mul(VEL_BEFORE, H10 * STEP_SECONDS)),
                    add(mul(POS_AFTER, H01), mul(VEL_AFTER, H11 * STEP_SECONDS)));
                current_velocity = add(add(mul(POS_BEFORE, D00), mul(VEL_BEFORE, D10)),
                    add(mul(POS_AFTER, D01), mul(VEL_AFTER, D11)));
                const double EXTRA =
                    std::clamp(time_seconds - ((COUNT - 1) * STEP_SECONDS), 0.0, STEP_SECONDS);
                position = add(position, mul(current_velocity, EXTRA));
            }
        }
    };

    struct ClothFlutter::MovementPlayback {
        Run::Snapshot a{}, b{};
        Box target{}, previous{};
        ClothMovementParams params{};
        Vec3 rigid_velocity{};
        double time = 0, last_motion = 0;
        bool producer = true, settling = false;
    };

    struct ClothFlutter::Fallback {
        SheetGrid positions{}, velocities{};  // real-time velocity
        double alpha = 1;
    };

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

    namespace {
        using Run = ClothFlutter::Run;
        struct Lease {
            Lease() = default;
            Lease(const Lease &) = delete;
            Lease &operator=(const Lease &) = delete;
            Lease(Lease &&) = delete;
            Lease &operator=(Lease &&) = delete;

            std::shared_ptr<Run> run;
            ~Lease() {
                if (run) {
                    run->stop.request_stop();
                    run->released.store(true, std::memory_order_release);
                }
            }
        };
        class Registry {
          public:
            // NOLINTNEXTLINE(performance-unnecessary-value-param): A reference adds a dependent
            // load per reaper poll in GCC -O2.
            Registry() : reaper([this](std::stop_token stop) { reap(stop); }) {
            }
            Registry(const Registry &) = delete;
            Registry &operator=(const Registry &) = delete;
            Registry(Registry &&) = delete;
            Registry &operator=(Registry &&) = delete;
            ~Registry() {
                reaper.request_stop();
                changed.notify_all();
                reaper.join();
                for (const auto &run : runs) {
                    run->stop.request_stop();
                }
                for (const auto &run : runs) {
                    if (run->worker.joinable()) {
                        run->worker.join();
                    }
                }
                runs.clear();
            }
            template <typename Work>
            void launch(const std::shared_ptr<Run> &run, Work &&work) {
                const std::scoped_lock LOCK(mutex);
                runs.push_back(run);
                try {
                    // Finished can be published before the thread constructor returns.
                    // Serialize member publication with the reaper's inspection.
                    run->worker = std::thread([run, work = std::forward<Work>(work)] {
                        try {
                            work(*run);
                        } catch (...) {
                            // A failed solver publishes the same terminal state as cancellation.
                        }
                        run->finished.store(true, std::memory_order_release);
                    });
                } catch (...) {
                    runs.pop_back();
                    throw;
                }
                changed.notify_one();
            }

          private:
            void reap(const std::stop_token &stop) {
                std::unique_lock lock(mutex);
                while (!stop.stop_requested()) {
                    changed.wait_for(lock, std::chrono::milliseconds(2));
                    for (auto iterator = runs.begin(); iterator != runs.end();) {
                        if (!(*iterator)->finished.load(std::memory_order_acquire) ||
                            !(*iterator)->released.load(std::memory_order_acquire)) {
                            ++iterator;
                            continue;
                        }
                        auto done = std::move(*iterator);
                        iterator = runs.erase(iterator);
                        lock.unlock();
                        if (done->worker.joinable()) {
                            done->worker.join();
                        }
                        {
                            const perf::CpuScope PROFILE{{perf::Domain::Window, "cloth",
                                done->live ? "move" :
                                    (done->kind == Transition::Close ? "close" : "open")},
                                perf::Phase::Worker};
                            done.reset();  // Free the recording off the playback thread.
                        }
                        lock.lock();
                        iterator = runs.begin();  // add() may have reallocated the vector while the
                                                  // lock was released
                    }
                }
            }
            std::mutex mutex;
            std::condition_variable changed;
            std::vector<std::shared_ptr<Run>> runs;
            std::jthread reaper;
        };
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): Physics lifetime owns
        // the shared worker reaper.
        std::unique_ptr<Registry> s_registry;
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): Serializes Jolt
        // ownership and worker admission.
        std::mutex s_lifecycle_mutex;
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): Protected by
        // s_lifecycle_mutex.
        unsigned s_physics_owners = 0;

        bool outside(const Run &run, const JPH::Array<JPH::SoftBodyVertex> &vertices) noexcept {
            const double WIDTH = run.window.w;
            const double HEIGHT = run.window.h;
            const double LENGTH_SCALE = std::sqrt(WIDTH * HEIGHT);
            const double FOCAL_LENGTH = 3 * LENGTH_SCALE;
            // NOLINTNEXTLINE(readability-use-anyofallof): GCC native algorithm alternatives add
            // setup and branch work.
            for (const auto &vertex : vertices) {
                const double SCALE =
                    FOCAL_LENGTH / std::max(0.25 * FOCAL_LENGTH,
                                       FOCAL_LENGTH - (vertex.mPosition.GetZ() * LENGTH_SCALE));
                const double POS_X =
                    run.window.x + (WIDTH / 2) + (vertex.mPosition.GetX() * WIDTH * SCALE);
                const double POS_Y = run.window.y + (HEIGHT / 2) +
                                     (((-vertex.mPosition.GetY() * HEIGHT) - (HEIGHT / 2)) * SCALE);
                if ((run.edge == Edge::Bottom && POS_Y <= run.screen.y + run.screen.h) ||
                    (run.edge == Edge::Top && POS_Y >= run.screen.y) ||
                    (run.edge == Edge::Left && POS_X >= run.screen.x) ||
                    (run.edge == Edge::Right && POS_X <= run.screen.x + run.screen.w)) {
                    return false;
                }
            }
            return true;
        }
        bool record(Run &run, int frame, const JPH::Array<JPH::SoftBodyVertex> &vertices) noexcept {
            const double TRAVEL = std::hypot(run.travel.x, run.travel.y);
            const double POSITION_LIMIT = 16 + TRAVEL + (GRAVITY * run.length * run.length) +
                                          (run.max_initial_speed * run.length);
            const double VELOCITY_LIMIT = 50 + (8 * TRAVEL / run.length) +
                                          (3 * GRAVITY * run.length) +
                                          (1.5 * run.max_initial_speed);
            for (const auto &soft_vertex : vertices) {
                if (!std::isfinite(soft_vertex.mPosition.GetX()) ||
                    !std::isfinite(soft_vertex.mPosition.GetY()) ||
                    !std::isfinite(soft_vertex.mPosition.GetZ()) ||
                    !std::isfinite(soft_vertex.mVelocity.GetX()) ||
                    !std::isfinite(soft_vertex.mVelocity.GetY()) ||
                    !std::isfinite(soft_vertex.mVelocity.GetZ()) ||
                    std::max({
                        std::abs(soft_vertex.mPosition.GetX()),
                        std::abs(soft_vertex.mPosition.GetY()),
                        std::abs(soft_vertex.mPosition.GetZ()),
                    }) > POSITION_LIMIT ||
                    std::max({
                        std::abs(soft_vertex.mVelocity.GetX()),
                        std::abs(soft_vertex.mVelocity.GetY()),
                        std::abs(soft_vertex.mVelocity.GetZ()),
                    }) > VELOCITY_LIMIT) {
                    return false;
                }
            }
            auto &samples = run.samples[static_cast<std::size_t>(frame)];
            auto *sample_it = samples.begin();
            for (const auto &vertex : vertices) {
                auto &sample = *sample_it;
                ++sample_it;
                sample.position.x = vertex.mPosition.GetX();
                sample.position.y = vertex.mPosition.GetY();
                sample.position.z = vertex.mPosition.GetZ();
                sample.velocity.x = vertex.mVelocity.GetX();
                sample.velocity.y = vertex.mVelocity.GetY();
                sample.velocity.z = vertex.mVelocity.GetZ();
            }
            if (run.kind == Transition::Close &&
                run.outside_frame.load(std::memory_order_relaxed) == run.frames &&
                outside(run, vertices)) {
                run.outside_frame.store(frame, std::memory_order_release);
            }
            run.recorded.store(frame + 1, std::memory_order_release);
            return true;
        }
        struct SimInput {
            std::vector<Box> obstacles;
            double flutter = 0;
            double phase_a = 0, phase_b = 0, freq_a = 0;
        };
        // Invisible samples carry an effective header-column mass. The soft body follows their
        // solved positions, but does not feed forces back: this is a reduced-coupling model of a
        // continuous sleeve.
        class Pocket {
            class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
                [[nodiscard]] bool ShouldCollide([[maybe_unused]] JPH::ObjectLayer object_layer,
                    [[maybe_unused]] JPH::BroadPhaseLayer broad_phase_layer) const override {
                    return false;
                }
            };

          public:
            explicit Pocket(
                const Run &run, const JPH::Array<JPH::SoftBodyVertex> *current = nullptr)
                : pairs(1), broad_phase(1, 1) {
                using namespace JPH;
                broad_phase.MapObjectToBroadPhaseLayer(0, BroadPhaseLayer(0));
                system.Init(GRID, 0, 1, 1, broad_phase, object_vs_broad_phase, pairs);
                // Signed neighbour limits prevent tunnelling or sample swapping under abrupt
                // reversal.
                PhysicsSettings physics = system.GetPhysicsSettings();
                physics.mBaumgarte = 1.0F;
                physics.mNumVelocitySteps = 64;
                physics.mNumPositionSteps = 64;
                system.SetPhysicsSettings(physics);
                system.SetGravity(JPH::Vec3::sZero());
                auto &bodies = system.GetBodyInterfaceNoLock();
                // Shapes provide effective header masses, not rendered hardware or collision-based
                // exclusion.
                const RefConst<Shape> SHAPE = new BoxShape(JPH::Vec3(HALF_WIDTH, .01F, .01F), 0.0F);
                const auto *start_point = run.start.begin();
                const auto *speed = run.velocity.begin();
                auto *body_id = ids.begin();
                auto *constraint = constraints.begin();
                auto *neighbour = neighbours.begin();
                const auto *previous_id = ids.begin();
                for (int i = 0; i < GRID; ++i) {
                    const float POS_X =
                        std::clamp(current != nullptr ? (*current)[i].mPosition.GetX()
                                                      : static_cast<float>(start_point[i].x),
                            -LIMIT,
                            LIMIT);
                    BodyCreationSettings creation(
                        SHAPE, RVec3(POS_X, 0, 0), Quat::sIdentity(), EMotionType::Dynamic, 0);
                    creation.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
                    creation.mMassPropertiesOverride.mMass = MASS;
                    creation.mGravityFactor = 0;
                    creation.mLinearDamping = 0;
                    creation.mFriction = 0;
                    creation.mAllowSleeping = false;
                    creation.mMaxLinearVelocity = 8;
                    *body_id = bodies.CreateAndAddBody(creation, EActivation::Activate);
                    if (body_id->IsInvalid()) {
                        return;
                    }
                    const float VELOCITY =
                        current != nullptr
                            ? (*current)[i].mVelocity.GetX()
                            : static_cast<float>(speed[i].x * run.movement.springiness);
                    bodies.SetLinearVelocity(*body_id, JPH::Vec3(VELOCITY, 0, 0));
                    SliderConstraintSettings slider;
                    slider.mPoint1 = slider.mPoint2 = RVec3(POS_X, 0, 0);
                    slider.mLimitsMin = -LIMIT - POS_X;
                    slider.mLimitsMax = LIMIT - POS_X;
                    *constraint = bodies.CreateConstraint(&slider, BodyID(), *body_id);
                    if (*constraint == nullptr) {
                        return;
                    }
                    system.AddConstraint(*constraint++);
                    if (i > 0) {
                        SliderConstraintSettings spacing;
                        spacing.mPoint1 =
                            bodies.GetPosition(*previous_id) + RVec3(2 * HALF_WIDTH, 0, 0);
                        spacing.mPoint2 = RVec3(POS_X, 0, 0);
                        spacing.mLimitsMin = 0;
                        spacing.mLimitsMax = 1;
                        *neighbour = bodies.CreateConstraint(&spacing, *previous_id++, *body_id);
                        if (*neighbour == nullptr) {
                            return;
                        }
                        system.AddConstraint(*neighbour++);
                    }
                    ++body_id;
                }
                ready = true;
            }
            Pocket(const Pocket &) = delete;
            Pocket &operator=(const Pocket &) = delete;
            Pocket(Pocket &&) = delete;
            Pocket &operator=(Pocket &&) = delete;

            ~Pocket() {
                auto &bodies = system.GetBodyInterfaceNoLock();
                for (const auto &constraint : neighbours) {
                    if (constraint != nullptr) {
                        system.RemoveConstraint(constraint);
                    }
                }
                for (const auto &constraint : constraints) {
                    if (constraint != nullptr) {
                        system.RemoveConstraint(constraint);
                    }
                }
                for (const auto &body_id : ids) {
                    if (!body_id.IsInvalid()) {
                        bodies.RemoveBody(body_id);
                        bodies.DestroyBody(body_id);
                    }
                }
            }

            bool update(double acceleration,
                JPH::TempAllocator &temp,
                double rod_velocity,
                JPH::JobSystem &jobs,
                double quiet,
                double springiness) {
                const auto SPRINGINESS = static_cast<float>(springiness);
                auto &bodies = system.GetBodyInterfaceNoLock();
                // Reinforce the trailing end by up to 4x; blend through zero speed when stopping or
                // reversing.
                const float DIRECTION =
                    static_cast<float>(std::clamp(rod_velocity / .05, -1.0, 1.0));
                const float TRAILING = .5F * std::abs(DIRECTION);
                int body_index = 0;
                for (const auto &body_id : ids) {
                    const float POS_X = bodies.GetPosition(body_id).GetX();
                    const auto REST =
                        static_cast<float>((static_cast<double>(body_index++) / SHEET_CELLS) - .5);
                    const float VELOCITY = bodies.GetLinearVelocity(body_id).GetX();
                    // A distributed lengthwise spring restores the sleeve during travel, not only
                    // after it stops. Jolt integrates the spring force; spring damping controls the
                    // rebound without sliding friction.
                    const float EFFECTIVE_SPRING = SPRING * (4 - (3 * SPRINGINESS)) *
                                                   (1 + (3 * (TRAILING - (DIRECTION * REST))));
                    const float EFFECTIVE_DAMPING = DAMPING * std::sqrt(EFFECTIVE_SPRING / SPRING);
                    const auto FORCE =
                        static_cast<float>((-MASS * acceleration * SPRINGINESS) +
                                           ((EFFECTIVE_SPRING + (quiet * 8)) * (REST - POS_X)) -
                                           (EFFECTIVE_DAMPING * VELOCITY));
                    bodies.AddForce(body_id, JPH::Vec3(FORCE, 0, 0));
                }
                return system.Update(static_cast<float>(STEP_SECONDS), 1, &temp, &jobs) ==
                       JPH::EPhysicsUpdateError::None;
            }

            float position(int column) {
                return system.GetBodyInterfaceNoLock().GetPosition(ids[column]).GetX();
            }
            bool valid() const {
                return ready;
            }

          private:
            static constexpr float MASS = 1.0F, SPRING = 48.0F, DAMPING = 4.0F;
            static constexpr float HALF_WIDTH = .002F, LIMIT = .5F - HALF_WIDTH;
            JPH::ObjectLayerPairFilterTable pairs;
            JPH::BroadPhaseLayerInterfaceTable broad_phase;
            BroadFilter object_vs_broad_phase;
            JPH::PhysicsSystem system;
            std::array<JPH::BodyID, GRID> ids{};
            std::array<JPH::Ref<JPH::TwoBodyConstraint>, GRID> constraints{};
            std::array<JPH::Ref<JPH::TwoBodyConstraint>, GRID - 1> neighbours{};
            bool ready = false;
        };

        bool publish_movement(
            Run &run, double time, const JPH::Array<JPH::SoftBodyVertex> &vertices) {
            const unsigned HEAD = run.output_head.load(std::memory_order_relaxed);
            if (HEAD - run.output_tail.load(std::memory_order_acquire) == Run::OUTPUT_CAPACITY) {
                run.overflow.store(true, std::memory_order_release);
                return false;
            }
            auto &snapshot = run.snapshots.at(HEAD % Run::OUTPUT_CAPACITY);
            snapshot.time = time;
            snapshot.rigid_velocity = run.snapshot_rigid_velocity;
            std::size_t vertex_index = 0;
            auto *sample_it = snapshot.values.begin();
            for (const auto &vertex : vertices) {
                const auto POSITION = vertex.mPosition;
                const auto VELOCITY = vertex.mVelocity;
                // Header vertices follow the rod directly; only the free panel uses the partial
                // inertial rebase.
                const auto &frame_velocity =
                    vertex_index < GRID ? run.snapshot_rigid_velocity : run.publish_velocity;
                if (!std::isfinite(POSITION.GetX()) || !std::isfinite(POSITION.GetY()) ||
                    !std::isfinite(POSITION.GetZ()) || !std::isfinite(VELOCITY.GetX()) ||
                    !std::isfinite(VELOCITY.GetY()) || !std::isfinite(VELOCITY.GetZ())) {
                    return false;
                }
                auto &sample = *sample_it;
                ++sample_it;
                sample.position.x = POSITION.GetX();
                sample.position.y = POSITION.GetY();
                sample.position.z = POSITION.GetZ();
                sample.velocity.x = static_cast<float>(VELOCITY.GetX() + frame_velocity.x);
                sample.velocity.y = static_cast<float>(VELOCITY.GetY() + frame_velocity.y);
                sample.velocity.z = static_cast<float>(VELOCITY.GetZ() + frame_velocity.z);
                ++vertex_index;
            }
            run.output_head.store(HEAD + 1, std::memory_order_release);
            run.recorded.fetch_add(1, std::memory_order_release);
            return true;
        }

        // NOLINTNEXTLINE(readability-function-cognitive-complexity): Splitting risks per-vertex
        // calls and publication ordering.
        void simulate_movement(Run &run, double phase) {
            std::optional<perf::CpuScope> setup_profile{std::in_place,
                perf::Key{perf::Domain::Window, "cloth", "move"}, perf::Phase::Worker};
            using namespace JPH;
            constexpr ObjectLayer CLOTH = 0;
            const ObjectLayerPairFilterTable PAIRS(1);
            BroadPhaseLayerInterfaceTable broad_phase(1, 1);
            broad_phase.MapObjectToBroadPhaseLayer(CLOTH, BroadPhaseLayer(0));
            const ObjectVsBroadPhaseLayerFilterTable OBJECT_VS_BROAD_PHASE(
                broad_phase, 1, PAIRS, 1);
            TempAllocatorImpl temp(std::size_t{8} * 1024 * 1024);
            JobSystemSingleThreaded jobs(cMaxPhysicsJobs);
            PhysicsSystem system;
            system.Init(1, 0, 32, 32, broad_phase, OBJECT_VS_BROAD_PHASE, PAIRS);
            system.SetGravity(JPH::Vec3(0, static_cast<float>(-GRAVITY * run.movement.gravity), 0));
            const auto STOPPED = [&] { return run.stop.stop_requested(); };
            if (STOPPED()) {
                return;
            }
            const SheetGrid REST = rest_grid();
            std::array<double, GRID> pleats{};
            int column = 0;
            for (auto &pleat : pleats) {
                pleat = std::sin((8 * std::numbers::pi * column++ / SHEET_CELLS) + phase);
            }
            const Ref<SoftBodySharedSettings> SETTINGS = new SoftBodySharedSettings;
            std::size_t vertex_index = 0;
            for (const auto REST_POSITION : REST) {
                SETTINGS->mVertices.emplace_back(Float3(static_cast<float>(REST_POSITION.x),
                                                     static_cast<float>(REST_POSITION.y),
                                                     0),
                    Float3(0, 0, 0),
                    vertex_index < GRID
                        ? 0.0F
                        : cloth_inverse_mass(static_cast<int>(vertex_index / GRID)));
                ++vertex_index;
            }
            for (int j = 0; j < SHEET_CELLS; ++j) {
                for (int i = 0; i < SHEET_CELLS; ++i) {
                    const auto TOP_LEFT = static_cast<uint32>(index(i, j));
                    const auto TOP_RIGHT = static_cast<uint32>(index(i + 1, j));
                    const auto BOTTOM_LEFT = static_cast<uint32>(index(i, j + 1));
                    const auto BOTTOM_RIGHT = static_cast<uint32>(index(i + 1, j + 1));
                    SETTINGS->AddFace(
                        SoftBodySharedSettings::Face(TOP_LEFT, BOTTOM_LEFT, TOP_RIGHT));
                    SETTINGS->AddFace(
                        SoftBodySharedSettings::Face(TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT));
                }
            }
            const SoftBodySharedSettings::VertexAttributes ATTRIBUTES(STRETCH_COMPLIANCE,
                STRETCH_COMPLIANCE,
                BEND_COMPLIANCE,
                SoftBodySharedSettings::ELRAType::GeodesicDistance);
            SETTINGS->CreateConstraints(
                &ATTRIBUTES, 1, SoftBodySharedSettings::EBendType::Dihedral);
            SETTINGS->Optimize();
            if (STOPPED()) {
                return;
            }
            SoftBodyCreationSettings creation(SETTINGS, RVec3::sZero(), Quat::sIdentity(), CLOTH);
            creation.mUpdatePosition = false;
            creation.mAllowSleeping = false;
            creation.mNumIterations = 8;
            creation.mLinearDamping = 2;
            creation.mMaxLinearVelocity = 20;
            auto &bodies = system.GetBodyInterfaceNoLock();
            const BodyID BODY_ID = bodies.CreateAndAddSoftBody(creation, EActivation::Activate);
            if (BODY_ID.IsInvalid()) {
                return;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): CreateAndAddSoftBody
            // guarantees soft-body motion; Jolt has no typed accessor.
            auto *motion = static_cast<SoftBodyMotionProperties *>(
                system.GetBodyLockInterfaceNoLock().TryGetBody(BODY_ID)->GetMotionProperties());
            auto &vertices = motion->GetVertices();
            auto *vertex = vertices.begin();
            const auto *velocity = run.velocity.begin();
            for (const auto POSITION : run.start) {
                const auto VELOCITY = *velocity++;
                vertex->mPosition = JPH::Vec3(static_cast<float>(POSITION.x),
                    static_cast<float>(POSITION.y),
                    static_cast<float>(POSITION.z));
                vertex++->mVelocity = JPH::Vec3(static_cast<float>(VELOCITY.x),
                    static_cast<float>(VELOCITY.y),
                    static_cast<float>(VELOCITY.z));
            }
            std::optional<Pocket> pocket;
            if (run.movement.springiness > 0) {
                pocket.emplace(run);
            }
            if (pocket && !pocket->valid()) {
                bodies.RemoveBody(BODY_ID);
                bodies.DestroyBody(BODY_ID);
                return;
            }
            bool good = publish_movement(run, -STEP_SECONDS, vertices) &&
                        publish_movement(run, 0, vertices);
            Run::Target previous{.time = 0, .box = run.window};
            Box box = run.window;
            double time = 0;
            double last_motion = 0;
            double previous_rod_velocity = 0;
            setup_profile.reset();
            while (good && !STOPPED()) {
                if (s_pause.load(std::memory_order_acquire) &&
                    run.recorded.load(std::memory_order_relaxed) >=
                        s_pause_after.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const unsigned TAIL = run.input_tail.load(std::memory_order_relaxed);
                if (TAIL == run.input_head.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const Run::Target TARGET = run.targets.at(TAIL % Run::INPUT_CAPACITY);
                const perf::CpuScope PROFILE{
                    {perf::Domain::Window, "cloth", "move"}, perf::Phase::Worker};
                run.input_tail.store(TAIL + 1, std::memory_order_release);
                const ClothMovementParams &movement = TARGET.movement;
                system.SetGravity(JPH::Vec3(0, static_cast<float>(-GRAVITY * movement.gravity), 0));
                if (!pocket && movement.springiness > 0) {
                    pocket.emplace(run, &vertices);
                    if (!pocket->valid()) {
                        break;
                    }
                }
                while (time + STEP_SECONDS <= TARGET.time + 1e-10 && good && !STOPPED()) {
                    const double NEXT_TIME = time + STEP_SECONDS;
                    const double BLEND = std::clamp(
                        (NEXT_TIME - previous.time) / (TARGET.time - previous.time), 0.0, 1.0);
                    const Box NEXT{
                        .x = previous.box.x + (BLEND * (TARGET.box.x - previous.box.x)),
                        .y = previous.box.y + (BLEND * (TARGET.box.y - previous.box.y)),
                        .w = previous.box.w + (BLEND * (TARGET.box.w - previous.box.w)),
                        .h = previous.box.h + (BLEND * (TARGET.box.h - previous.box.h)),
                    };
                    const double DELTA_X = NEXT.x - box.x;
                    const double DELTA_Y = NEXT.y - box.y;
                    run.snapshot_rigid_velocity = {
                        .x = DELTA_X / STEP_SECONDS / NEXT.w,
                        .y = -DELTA_Y / STEP_SECONDS / NEXT.h,
                        .z = 0,
                    };
                    const double GAIN = std::min(movement.strength, 1.5);
                    run.publish_velocity = {
                        .x = (1 - GAIN) * DELTA_X / STEP_SECONDS / NEXT.w,
                        .y = -(1 - GAIN) * DELTA_Y / STEP_SECONDS / NEXT.h,
                        .z = 0,
                    };
                    const double SPEED =
                        std::hypot(DELTA_X, DELTA_Y) / STEP_SECONDS / std::min(NEXT.w, NEXT.h);
                    if (SPEED > 1e-5 ||
                        std::abs(NEXT.w - box.w) + std::abs(NEXT.h - box.h) > 1e-5) {
                        last_motion = NEXT_TIME;
                    }
                    const double QUIET = NEXT_TIME - last_motion;
                    const double SETTLE = smoothstep(0.05, 0.5, QUIET);
                    const double INHERITED = std::exp(-6 * NEXT_TIME);
                    const double ROD_VELOCITY = DELTA_X / STEP_SECONDS / NEXT.w;
                    const double ACCELERATION =
                        GAIN * (ROD_VELOCITY - previous_rod_velocity) / STEP_SECONDS;
                    previous_rod_velocity = ROD_VELOCITY;
                    if (pocket &&
                        !pocket->update(
                            ACCELERATION, temp, ROD_VELOCITY, jobs, SETTLE, movement.springiness)) {
                        good = false;
                        break;
                    }
                    const double LENGTH_SCALE = std::sqrt(NEXT.w * NEXT.h);
                    motion->SetLinearDamping(static_cast<float>(2 + (9 * SETTLE)));
                    const double GUST = std::sin(phase + (NEXT_TIME * 7));
                    auto *movement_vertex = vertices.begin();
                    const auto *rest_position = REST.begin();
                    const auto *start_position = run.start.begin();
                    for (int j = 0; j < GRID; ++j) {
                        const auto *pleat = pleats.begin();
                        for (int i = 0; i < GRID; ++i) {
                            auto &vertex = *movement_vertex;
                            ++movement_vertex;
                            auto position = vertex.mPosition;
                            if (j != 0) {
                                position = JPH::Vec3(
                                    static_cast<float>(
                                        (box.x + (box.w / 2) + (position.GetX() * box.w) - NEXT.x -
                                            (NEXT.w / 2) + ((1 - GAIN) * DELTA_X)) /
                                        NEXT.w),
                                    static_cast<float>(
                                        ((GAIN * DELTA_Y) + (position.GetY() * box.h)) / NEXT.h),
                                    static_cast<float>(
                                        position.GetZ() * std::sqrt(box.w * box.h) / LENGTH_SCALE));
                            }
                            auto velocity = vertex.mVelocity;
                            velocity =
                                JPH::Vec3(static_cast<float>(velocity.GetX() * box.w / NEXT.w),
                                    static_cast<float>(velocity.GetY() * box.h / NEXT.h),
                                    static_cast<float>(
                                        velocity.GetZ() * std::sqrt(box.w * box.h) / LENGTH_SCALE));
                            const auto REST_POS = *rest_position++;
                            const Vec3 OLD_OFFSET = sub(*start_position++, REST_POS);
                            const JPH::Vec3 DESIRED(
                                static_cast<float>(REST_POS.x + (OLD_OFFSET.x * INHERITED)),
                                static_cast<float>(REST_POS.y + (OLD_OFFSET.y * INHERITED)),
                                static_cast<float>(OLD_OFFSET.z * INHERITED));
                            if (j == 0) {
                                const float POS_X = pocket ? pocket->position(i) : DESIRED.GetX();
                                velocity = (JPH::Vec3(POS_X, 0, 0) - position) /
                                           static_cast<float>(STEP_SECONDS);
                            } else {
                                const double DOWN = static_cast<double>(j) / SHEET_CELLS;
                                const double RESPONSE = vertex.mInvMass / CLOTH_INV_MASS;
                                const double SPRING =
                                    4 + (30 * (1 - DOWN) * (1 - DOWN)) + (120 * SETTLE);
                                velocity += (DESIRED - position) *
                                            static_cast<float>(SPRING * RESPONSE * STEP_SECONDS);
                                const double DRIVE = std::min(SPEED, 3.0) * movement.strength;
                                // Alternating pressure excites long folds instead of inflating the
                                // entire sheet forward.
                                const double PRESSURE = *pleat *
                                                        (16 + (8 * movement.flutter * GUST)) *
                                                        (.3 + (.7 * DOWN));
                                velocity += JPH::Vec3(0,
                                    0,
                                    static_cast<float>(RESPONSE * DRIVE * PRESSURE * STEP_SECONDS));
                            }
                            vertex.mPosition = position;
                            vertex.mVelocity = velocity;
                            ++pleat;
                        }
                    }
                    if (system.Update(static_cast<float>(STEP_SECONDS), 1, &temp, &jobs) !=
                        EPhysicsUpdateError::None) {
                        good = false;
                        break;
                    }
                    // Keep Jolt's solved pose and velocity together. Clamping vertices back toward
                    // the flat panel breaks its constraints and clips inertial upward folds.
                    box = NEXT;
                    time = NEXT_TIME;
                    good = publish_movement(run, time, vertices);
                }
                previous = TARGET;
            }
            bodies.RemoveBody(BODY_ID);
            bodies.DestroyBody(BODY_ID);
        }

        // NOLINTNEXTLINE(readability-function-cognitive-complexity): Splitting risks per-vertex
        // calls and neighbor update ordering.
        void simulate(Run &run, const SimInput &input) {
            const perf::Key KEY{perf::Domain::Window, "cloth",
                run.kind == Transition::Close ? "close" : "open"};
            std::optional<perf::CpuScope> setup_profile{
                std::in_place, KEY, perf::Phase::Worker};
            using namespace JPH;
            const auto STOPPED = [&] { return run.stop.stop_requested(); };
            const auto WAIT_PUBLICATION = [&] {
                while (s_pause.load(std::memory_order_acquire) &&
                       run.recorded.load(std::memory_order_acquire) >=
                           s_pause_after.load(std::memory_order_relaxed) &&
                       !STOPPED()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return !STOPPED();
            };
            if (!WAIT_PUBLICATION()) {
                return;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): Recording
            // length is determined at runtime.
            run.samples = std::make_unique<Run::Frame[]>(static_cast<std::size_t>(run.frames));
            if (setup_profile->session() != 0) {
                perf::resource_sample(KEY,
                    sizeof(Run) + static_cast<std::uint64_t>(run.frames) * sizeof(Run::Frame),
                    0, setup_profile->session());
            }
            constexpr ObjectLayer STATIC = 0;
            constexpr ObjectLayer CLOTH = 1;
            constexpr ObjectLayer LAYERS = 2;
            ObjectLayerPairFilterTable pairs(LAYERS);
            pairs.EnableCollision(STATIC, CLOTH);
            BroadPhaseLayerInterfaceTable broad_phase(LAYERS, 2);
            broad_phase.MapObjectToBroadPhaseLayer(STATIC, BroadPhaseLayer(0));
            broad_phase.MapObjectToBroadPhaseLayer(CLOTH, BroadPhaseLayer(1));
            const ObjectVsBroadPhaseLayerFilterTable OBJECT_VS_BROAD_PHASE(
                broad_phase, 2, pairs, LAYERS);
            TempAllocatorImpl temp(std::size_t{8} * 1024 * 1024);
            JobSystemSingleThreaded jobs(cMaxPhysicsJobs);
            PhysicsSystem system;
            if (STOPPED()) {
                return;
            }
            system.Init(
                MAX_OBSTACLES + 4, 0, 1024, 1024, broad_phase, OBJECT_VS_BROAD_PHASE, pairs);
            system.SetGravity(JPH::Vec3(0, static_cast<float>(-GRAVITY), 0));
            BodyInterface &bodies = system.GetBodyInterfaceNoLock();
            const auto ADD_BOX = [&](JPH::Vec3 half, JPH::Vec3 centre) {
                bodies.CreateAndAddBody(BodyCreationSettings(new BoxShape(half, 0.0F),
                                            RVec3(centre),
                                            Quat::sIdentity(),
                                            EMotionType::Static,
                                            STATIC),
                    EActivation::DontActivate);
            };
            ADD_BOX(JPH::Vec3(100, 100, 1), JPH::Vec3(0, 0, static_cast<float>(-1 - (2 * RADIUS))));
            for (const Box &obstacle : input.obstacles) {
                ADD_BOX(JPH::Vec3(static_cast<float>(obstacle.w / 2),
                            static_cast<float>(obstacle.h / 2),
                            0.515F),
                    JPH::Vec3(static_cast<float>(obstacle.x + (obstacle.w / 2)),
                        static_cast<float>(obstacle.y + (obstacle.h / 2)),
                        -0.485F));
            }
            if (STOPPED()) {
                return;
            }
            const Ref<SoftBodySharedSettings> SETTINGS = new SoftBodySharedSettings;
            const SheetGrid REST = rest_grid();
            std::array<double, GRID> gust_columns{};
            if (input.flutter > 0) {
                int column = 0;
                for (auto &gust : gust_columns) {
                    gust =
                        std::sin((8 * std::numbers::pi * column++ / SHEET_CELLS) + input.phase_a);
                }
            }
            // Keep the wall behind the exact display plane: a flat, fully free sheet must not start
            // embedded by its radius.
            const auto *rest_position = REST.begin();
            for (int j = 0; j < GRID; ++j) {
                for (int i = 0; i < GRID; ++i) {
                    const Vec3 &rest_point = *rest_position++;
                    SETTINGS->mVertices.emplace_back(Float3(static_cast<float>(rest_point.x),
                                                         static_cast<float>(rest_point.y),
                                                         static_cast<float>(rest_point.z)),
                        Float3(0, 0, 0),
                        Run::pinned(j) ? 0.0F : cloth_inverse_mass(j));
                }
            }
            for (int j = 0; j < SHEET_CELLS; ++j) {
                for (int i = 0; i < SHEET_CELLS; ++i) {
                    const auto TOP_LEFT = static_cast<uint32>(index(i, j));
                    const auto TOP_RIGHT = static_cast<uint32>(index(i + 1, j));
                    const auto BOTTOM_LEFT = static_cast<uint32>(index(i, j + 1));
                    const auto BOTTOM_RIGHT = static_cast<uint32>(index(i + 1, j + 1));
                    SETTINGS->AddFace(
                        SoftBodySharedSettings::Face(TOP_LEFT, BOTTOM_LEFT, TOP_RIGHT));
                    SETTINGS->AddFace(
                        SoftBodySharedSettings::Face(TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT));
                }
            }
            const SoftBodySharedSettings::VertexAttributes ATTRIBUTES(STRETCH_COMPLIANCE,
                STRETCH_COMPLIANCE,
                BEND_COMPLIANCE,
                SoftBodySharedSettings::ELRAType::GeodesicDistance);
            SETTINGS->CreateConstraints(
                &ATTRIBUTES, 1, SoftBodySharedSettings::EBendType::Dihedral);
            SETTINGS->Optimize();
            if (STOPPED()) {
                return;
            }
            SoftBodyCreationSettings creation(SETTINGS, RVec3::sZero(), Quat::sIdentity(), CLOTH);
            creation.mUpdatePosition = false;
            creation.mAllowSleeping = false;
            creation.mNumIterations = 8;
            creation.mVertexRadius = static_cast<float>(RADIUS);
            creation.mLinearDamping = 1.5F;
            creation.mFriction = 0.3F;
            creation.mMaxLinearVelocity = static_cast<float>(std::max(500.0,
                (1.5 * run.max_initial_speed) +
                    (8 * std::hypot(run.travel.x, run.travel.y) / run.length)));
            creation.mFacesDoubleSided = true;
            const BodyID BODY_ID = bodies.CreateAndAddSoftBody(creation, EActivation::Activate);
            if (BODY_ID.IsInvalid()) {
                return;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): CreateAndAddSoftBody
            // guarantees soft-body motion; Jolt has no typed accessor.
            auto *motion = static_cast<SoftBodyMotionProperties *>(
                system.GetBodyLockInterfaceNoLock().TryGetBody(BODY_ID)->GetMotionProperties());
            auto &vertices = motion->GetVertices();
            auto *vertex = vertices.begin();
            const auto *velocity = run.velocity.begin();
            for (const auto POSITION : run.start) {
                const auto VELOCITY = *velocity++;
                vertex->mPosition = JPH::Vec3(static_cast<float>(POSITION.x),
                    static_cast<float>(POSITION.y),
                    static_cast<float>(POSITION.z));
                vertex++->mVelocity = JPH::Vec3(static_cast<float>(VELOCITY.x),
                    static_cast<float>(VELOCITY.y),
                    static_cast<float>(VELOCITY.z));
            }
            setup_profile.reset();
            if (run.kind == Transition::Open) {
                // A level, gathered sleeve compresses the first material rows. Relax that sewn
                // pocket with Jolt on the worker before publishing frame zero, instead of
                // displaying its initial constraint correction.
                motion->SetLinearDamping(8);
                for (int step = 0; step < 24; ++step) {
                    const perf::CpuScope PROFILE{
                        {perf::Domain::Window, "cloth", "open"}, perf::Phase::Worker};
                    if (!WAIT_PUBLICATION() ||
                        system.Update(static_cast<float>(STEP_SECONDS), 1, &temp, &jobs) !=
                            EPhysicsUpdateError::None) {
                        bodies.RemoveBody(BODY_ID);
                        bodies.DestroyBody(BODY_ID);
                        return;
                    }
                }
                for (auto &vertex : vertices) {
                    vertex.mVelocity = JPH::Vec3::sZero();
                }
                motion->SetLinearDamping(1.5F);
            }
            setup_profile.emplace(KEY, perf::Phase::Worker);
            std::optional<Pocket> pocket;
            if (run.kind == Transition::Close && run.movement.springiness > 0) {
                pocket.emplace(run);
                if (!pocket->valid()) {
                    bodies.RemoveBody(BODY_ID);
                    bodies.DestroyBody(BODY_ID);
                    return;
                }
            }
            if (!record(run, 0, vertices)) {
                bodies.RemoveBody(BODY_ID);
                bodies.DestroyBody(BODY_ID);
                return;
            }
            const auto OPEN_AIR_DRAG_FACTOR =
                static_cast<float>(1 - std::exp(-OPEN_AIR_DRAG * STEP_SECONDS));
            setup_profile.reset();
            for (int step = 1; step < run.frames && WAIT_PUBLICATION(); ++step) {
                const perf::CpuScope PROFILE{{perf::Domain::Window, "cloth",
                    run.kind == Transition::Close ? "close" : "open"}, perf::Phase::Worker};
                const double TIME = (step - 1) * STEP_SECONDS;
                if (pocket) {
                    const double PROGRESS =
                        std::clamp((TIME + STEP_SECONDS) / run.length, 0.0, 1.0);
                    const double ROD_VELOCITY = run.travel.x * 30 * PROGRESS * PROGRESS *
                                                (1 - PROGRESS) * (1 - PROGRESS) / run.length;
                    if (!pocket->update(0, temp, ROD_VELOCITY, jobs, 0, run.movement.springiness)) {
                        break;
                    }
                }
                if (run.kind == Transition::Open) {
                    motion->SetLinearDamping(
                        static_cast<float>(1.5 + (8 * smoothstep(0.65, 0.95, TIME / run.length))));
                }
                const double HEADER_SPREAD =
                    run.kind == Transition::Open
                        ? smoothstep(0, .72, (TIME + STEP_SECONDS) / run.length)
                        : 0;
                // Jolt initializes this fixed-size vertex array from the 57-by-57 rest grid.
                using Vertex = std::remove_reference_t<decltype(*vertices.begin())>;
                const std::mdspan<Vertex, std::extents<std::size_t, GRID, GRID>> VERTEX_GRID{
                    &*vertices.begin()};
                auto *step_vertex = vertices.begin();
                const auto *step_start = run.start.begin();
                const auto *step_rest = REST.begin();
                for (int j = 0; j < GRID; ++j) {
                    const double ROW_GUST =
                        input.flutter > 0
                            ? std::sin((input.freq_a * TIME) +
                                       (.8 * static_cast<double>(j) / SHEET_CELLS) + input.phase_b)
                            : 0;
                    const auto *gust = gust_columns.begin();
                    for (int i = 0; i < GRID; ++i) {
                        auto &vertex = *step_vertex;
                        ++step_vertex;
                        const Vec3 &start = *step_start++;
                        const Vec3 &rest = *step_rest++;
                        if (Run::pinned(j)) {
                            Vec3 target = start;
                            if (run.kind == Transition::Open) {
                                target = lerp(target, rest, HEADER_SPREAD);
                            }
                            if (run.kind == Transition::Close) {
                                const double CLOSE_PROGRESS =
                                    std::clamp((TIME + STEP_SECONDS) / run.length, 0.0, 1.0);
                                const double DISTANCE =
                                    CLOSE_PROGRESS * CLOSE_PROGRESS * CLOSE_PROGRESS *
                                    (10 + (CLOSE_PROGRESS * (-15 + (6 * CLOSE_PROGRESS))));
                                const double INHERITED_TIME = (TIME + STEP_SECONDS) *
                                                              (1 - CLOSE_PROGRESS) *
                                                              (1 - CLOSE_PROGRESS);
                                target.x = (pocket ? pocket->position(i) : target.x) +
                                           (run.travel.x * DISTANCE);
                                target.y += (run.travel.y * DISTANCE) +
                                            (run.velocity.front().y * INHERITED_TIME);
                                target.z = 0;
                            }
                            const JPH::Vec3 TARGET_POS(static_cast<float>(target.x),
                                static_cast<float>(target.y),
                                static_cast<float>(target.z));
                            vertex.mVelocity =
                                (TARGET_POS - vertex.mPosition) / static_cast<float>(STEP_SECONDS);
                        } else if (run.kind == Transition::Open || input.flutter > 0) {
                            const auto VERTEX_AT = [&](int column, int row) {
                                return VERTEX_GRID[row, column].mPosition;
                            };
                            const auto TANGENT_X = VERTEX_AT(std::min(i + 1, SHEET_CELLS), j) -
                                                   VERTEX_AT(std::max(i - 1, 0), j);
                            const auto TANGENT_Y = VERTEX_AT(i, std::min(j + 1, SHEET_CELLS)) -
                                                   VERTEX_AT(i, std::max(j - 1, 0));
                            const auto NORMAL =
                                TANGENT_X.Cross(TANGENT_Y).NormalizedOr(JPH::Vec3::sAxisZ());
                            if (run.kind == Transition::Open) {
                                vertex.mVelocity -=
                                    NORMAL * (vertex.mVelocity.Dot(NORMAL) * OPEN_AIR_DRAG_FACTOR);
                            }
                            if (input.flutter > 0) {
                                const double WAVE = *gust * ROW_GUST;
                                vertex.mVelocity +=
                                    NORMAL * static_cast<float>(5 * input.flutter * WAVE *
                                                                STEP_SECONDS * NORMAL.GetZ() *
                                                                (vertex.mInvMass / CLOTH_INV_MASS));
                            }
                        }
                        ++gust;
                    }
                }
                if (system.Update(static_cast<float>(STEP_SECONDS), 1, &temp, &jobs) !=
                    EPhysicsUpdateError::None) {
                    break;
                }
                if (!record(run, step, vertices)) {
                    break;
                }
            }
            bodies.RemoveBody(BODY_ID);
            bodies.DestroyBody(BODY_ID);
        }
    }  // namespace

    ClothPhysics::ClothPhysics() {
        const std::scoped_lock LOCK(s_lifecycle_mutex);
        if (s_physics_owners != 0) {
            ++s_physics_owners;
            return;
        }
        auto registry = std::make_unique<Registry>();
        JPH::RegisterDefaultAllocator();
        JPH::Trace = silent_trace;
        auto factory = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = factory.get();
        try {
            JPH::RegisterTypes();
        } catch (...) {
            JPH::UnregisterTypes();
            JPH::Factory::sInstance = nullptr;
            throw;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): Jolt's singleton survives until the last
        // owner shuts down.
        (void)factory.release();
        s_registry = std::move(registry);
        s_physics_owners = 1;
    }
    ClothPhysics::~ClothPhysics() {
        const std::scoped_lock LOCK(s_lifecycle_mutex);
        if (--s_physics_owners != 0) {
            return;
        }
        s_registry.reset();
        s_pause.store(false, std::memory_order_release);
        JPH::UnregisterTypes();
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): Jolt's singleton is deleted after
        // workers join and types unregister.
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
    void ClothPhysics::pause_workers_for_testing(bool pause, int after_snapshots_count) noexcept {
        s_pause_after.store(std::max(0, after_snapshots_count), std::memory_order_relaxed);
        s_pause.store(pause, std::memory_order_release);
    }

    void ClothFlutter::begin_movement(
        const ClothMovementParams &params, std::uint64_t seed, const Box &capture) noexcept try {
        const std::scoped_lock LOCK(s_lifecycle_mutex);
        auto movement = params;
        if (!s_registry || !valid_box(capture) || !sanitize(movement) || movement.strength <= 0) {
            reset();
            return;
        }
        const auto RUN = std::make_shared<Run>();
        RUN->live = true;
        RUN->window = capture;
        RUN->movement = movement;
        const double ALPHA = opacity();
        sample_vertices(RUN->start, RUN->velocity);
        auto playback = std::make_unique<MovementPlayback>();
        playback->target = playback->previous = capture;
        playback->params = movement;
        playback->a.time = -STEP_SECONDS;
        playback->b.time = 0;
        const auto *velocity = RUN->velocity.begin();
        auto *first_sample = playback->a.values.begin();
        auto *second_sample = playback->b.values.begin();
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
        const auto LEASE = std::make_shared<Lease>();
        LEASE->run = RUN;
        s_registry->launch(RUN, [PHASE](Run &run) { simulate_movement(run, PHASE); });
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
        if (!valid_box(capture) || !std::isfinite(elapsed_seconds) || elapsed_seconds < 0 ||
            elapsed_seconds > STALL_SECONDS) {
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
        const std::scoped_lock LOCK(s_lifecycle_mutex);
        ClothParams cloth_params = params;
        if (!s_registry || (kind != Transition::Open && kind != Transition::Close) ||
            !sanitize(cloth_params) || !valid_box(window) || !valid_close_geometry(geometry)) {
            reset();
            return;
        }
        const auto RUN = std::make_shared<Run>();
        RUN->kind = kind;
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
        const auto LEASE = std::make_shared<Lease>();
        LEASE->run = RUN;
        s_registry->launch(RUN, [input = std::move(input)](Run &run) { simulate(run, input); });
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
            unsigned tail = m_run->output_tail.load(std::memory_order_relaxed);
            const unsigned HEAD = m_run->output_head.load(std::memory_order_acquire);
            const bool PROGRESSED = tail != HEAD && playback.b.time < WANTED;
            for (; tail != HEAD && playback.b.time < WANTED; ++tail) {
                playback.a = playback.b;
                playback.b = m_run->snapshots.at(tail % Run::OUTPUT_CAPACITY);
            }
            m_run->output_tail.store(tail, std::memory_order_release);
            playback.time =
                std::min(WANTED, playback.b.time);  // a starved worker holds the newest pose
            if (m_run->recorded.load(std::memory_order_acquire) < 2) {
                m_startup_wait += elapsed_seconds;
                if (m_startup_wait >= STALL_SECONDS ||
                    m_run->finished.load(std::memory_order_acquire)) {
                    start_fallback();
                }
            } else {
                m_startup_wait = PROGRESSED || WANTED <= playback.b.time + 1e-8
                                     ? 0
                                     : m_startup_wait + elapsed_seconds;
                playback.settling = m_t - playback.last_motion >= 0.5;
                if (m_run->overflow.load(std::memory_order_acquire) ||
                    m_startup_wait >= STALL_SECONDS ||
                    m_run->finished.load(std::memory_order_acquire) || playback.settling) {
                    start_fallback();
                }
            }
            return active();
        }
        const bool FINISHED = m_run->finished.load(std::memory_order_acquire);
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
                const bool FINISHED_NOW = m_run->finished.load(std::memory_order_acquire);
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
            const bool FINISHED_NOW = m_run->finished.load(std::memory_order_acquire);
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
    void ClothFlutter::sample_vertices(SheetGrid &positions, SheetGrid &velocities) const noexcept {
        if (!active()) {
            positions = rest_grid();
            velocities = {};
            return;
        }
        if (m_fallback) {
            const double PROGRESS = std::clamp(m_fallback_time / FALLBACK_LENGTH, 0.0, 1.0);
            const double PROGRESS_SQUARED = PROGRESS * PROGRESS;
            const double PROGRESS_CUBED = PROGRESS_SQUARED * PROGRESS;
            const SheetGrid REST = rest_grid();
            const auto *start = m_fallback->positions.begin();
            const auto *speed = m_fallback->velocities.begin();
            const auto *rest = REST.begin();
            auto *velocity = velocities.begin();
            std::size_t vertex_index = 0;
            for (Vec3 &position : positions) {
                const Vec3 START_POS = *start++;
                const Vec3 SPEED = *speed++;
                const Vec3 &rest_position = *rest++;
                Vec3 &current_velocity = *velocity;
                ++velocity;
                if (m_phase != Phase::Closing) {
                    if (vertex_index < GRID) {
                        // A common convex return keeps the sleeve ordered and inside the rod even
                        // without a worker.
                        position =
                            lerp(START_POS, rest_position, PROGRESS_SQUARED * (3 - (2 * PROGRESS)));
                        current_velocity = mul(sub(rest_position, START_POS),
                            6 * PROGRESS * (1 - PROGRESS) / FALLBACK_LENGTH);
                    } else {
                        position = add(
                            add(mul(START_POS, (2 * PROGRESS_CUBED) - (3 * PROGRESS_SQUARED) + 1),
                                mul(SPEED,
                                    (PROGRESS_CUBED - (2 * PROGRESS_SQUARED) + PROGRESS) *
                                        FALLBACK_LENGTH)),
                            mul(rest_position, (-2 * PROGRESS_CUBED) + (3 * PROGRESS_SQUARED)));
                        current_velocity = add(
                            add(mul(START_POS,
                                    ((6 * PROGRESS_SQUARED) - (6 * PROGRESS)) / FALLBACK_LENGTH),
                                mul(SPEED, (3 * PROGRESS_SQUARED) - (4 * PROGRESS) + 1)),
                            mul(rest_position,
                                ((-6 * PROGRESS_SQUARED) + (6 * PROGRESS)) / FALLBACK_LENGTH));
                    }
                    if (m_phase == Phase::Movement && m_movement) {
                        current_velocity = add(current_velocity, m_movement->rigid_velocity);
                    }
                } else {
                    position = add(START_POS,
                        mul(SPEED, FALLBACK_LENGTH * (PROGRESS - (0.5 * PROGRESS_SQUARED))));
                    current_velocity = mul(SPEED, 1 - PROGRESS);
                }
                ++vertex_index;
            }
            return;
        }
        if (m_phase == Phase::Movement && m_movement) {
            const auto &previous = m_movement->a;
            const auto &next = m_movement->b;
            const double INTERVAL = std::max(STEP_SECONDS, next.time - previous.time);
            const double BLEND =
                std::clamp((m_movement->time - previous.time) / INTERVAL, 0.0, 1.0);
            const Vec3 SNAPSHOT_RIGID = lerp(previous.rigid_velocity, next.rigid_velocity, BLEND);
            const auto *previous_sample_it = previous.values.begin();
            const auto *next_sample_it = next.values.begin();
            auto *velocity = velocities.begin();
            for (Vec3 &position : positions) {
                const auto &previous_sample = *previous_sample_it++;
                const auto &next_sample = *next_sample_it++;
                const Vec3 PREVIOUS_POS{
                    .x = previous_sample.position.x,
                    .y = previous_sample.position.y,
                    .z = previous_sample.position.z,
                };
                const Vec3 NEXT_POS{
                    .x = next_sample.position.x,
                    .y = next_sample.position.y,
                    .z = next_sample.position.z,
                };
                const Vec3 PREVIOUS_VEL{
                    .x = previous_sample.velocity.x,
                    .y = previous_sample.velocity.y,
                    .z = previous_sample.velocity.z,
                };
                const Vec3 NEXT_VEL{
                    .x = next_sample.velocity.x,
                    .y = next_sample.velocity.y,
                    .z = next_sample.velocity.z,
                };
                // Snapshot velocities carry the rigid motion of their own steps; the displayed
                // sheet moves with the latest capture, which can already have started or stopped.
                *velocity = add(sub(lerp(PREVIOUS_VEL, NEXT_VEL, BLEND), SNAPSHOT_RIGID),
                    m_movement->rigid_velocity);
                ++velocity;
                position = lerp(PREVIOUS_POS, NEXT_POS, BLEND);
            }
            return;
        }
        m_run->sample(positions, m_t, velocities);
        if (m_phase == Phase::Opening) {
            const double SETTLE_BLEND = smoothstep(SETTLE_FROM, 1, m_t / m_run->length);
            const double SETTLE_RATE =
                smooth_derivative(SETTLE_FROM, 1, m_t / m_run->length) / m_run->length;
            const auto REST = rest_grid();
            auto *velocity = velocities.begin();
            const auto *rest = REST.begin();
            for (Vec3 &position : positions) {
                *velocity =
                    add(mul(*velocity, 1 - SETTLE_BLEND), mul(sub(*rest, position), SETTLE_RATE));
                position = lerp(position, *rest, SETTLE_BLEND);
                ++velocity;
                ++rest;
            }
        }
        for (auto &speed : velocities) {
            speed = m_transition_held ? Vec3{} : mul(speed, m_rate);
        }
    }
    bool ClothFlutter::wait_for_recording(double timeout_seconds) const noexcept {
        if (!m_run || !std::isfinite(timeout_seconds) || timeout_seconds < 0) {
            return false;
        }
        const auto DEADLINE =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_seconds);
        while (!m_run->finished.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < DEADLINE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_run->recorded.load(std::memory_order_acquire) == m_run->frames;
    }
    bool ClothFlutter::wait_for_snapshots(int count, double timeout_seconds) const noexcept {
        if (!m_run || count < 0 || (!m_run->live && count > m_run->frames) ||
            !std::isfinite(timeout_seconds) || timeout_seconds < 0) {
            return false;
        }
        const auto DEADLINE =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_seconds);
        while (m_run->recorded.load(std::memory_order_acquire) < count &&
               !m_run->finished.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < DEADLINE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_run->recorded.load(std::memory_order_acquire) >= count;
    }
    void ClothFlutter::frame(Vec2 size, SheetFrame &out) const noexcept {
        if (!active()) {
            flat_sheet(out);
            return;
        }
        SheetGrid world;
        SheetGrid velocity;  // sample_vertices writes both
        sample_vertices(world, velocity);
        const double WIDTH = std::isfinite(size.x) && size.x > 0 ? size.x : 1;
        const double HEIGHT = std::isfinite(size.y) && size.y > 0 ? size.y : 1;
        const double LENGTH_SCALE = std::sqrt(WIDTH * HEIGHT);
        for (auto &position : world) {
            position = {
                .x = position.x * WIDTH,
                .y = (-position.y * HEIGHT) - (HEIGHT / 2),
                .z = position.z * LENGTH_SCALE,
            };
        }
        project_sheet(world, {.x = WIDTH, .y = HEIGHT}, m_params.shading, out, m_params.shine);
    }
}  // namespace hyprimate
