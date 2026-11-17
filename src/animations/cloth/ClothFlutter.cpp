#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

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

#include "animations/cloth/ClothFlutter.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace hyprimate {
    namespace {
        constexpr int GRID = PAPER_CELLS + 1;
        constexpr double DT = 1.0 / 120.0;
        constexpr double OPEN_LENGTH = 1.4;
        constexpr double SETTLE_FROM = 0.84;
        constexpr double GRAVITY = 9.81;
        constexpr double RADIUS = 0.002;
        constexpr double FALLBACK_LENGTH = 0.18;
        constexpr int MAX_OBSTACLES = 32;
        constexpr float CLOTH_INV_MASS = 4.0F, STRETCH_COMPLIANCE = 4e-6F, BEND_COMPLIANCE = 1.2F;
        constexpr std::uint64_t BASE_SEED = 0xC1074F1A77E2ULL;
        std::atomic<bool> s_live{false}, s_pause{false};
        std::atomic<int> s_pauseAfter{0};

        // Live movement displays this far behind the latest target. The worker answers a target
        // only after the compositor posts it, so the snapshot pair around the displayed time must
        // already exist one 60 Hz frame plus one step later. Displaying any later alternates
        // interpolation with extrapolation from whichever snapshots happen to be ready.
        constexpr double MOVEMENT_LATENCY = 4 * DT;
        // Air resists fabric across its face, damping the opening folds as the gathered header
        // spreads. Closes move their sheets in plane, so their gusts stay undamped.
        constexpr double OPEN_AIR_DRAG =
            4.0;  // per simulated second, on the velocity along the local normal

        void silentTrace(const char *, ...) {
        }
        Vec3 add(Vec3 a, Vec3 b) noexcept {
            return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
        }
        Vec3 mul(Vec3 a, double b) noexcept {
            return {.x = a.x * b, .y = a.y * b, .z = a.z * b};
        }
        Vec3 sub(Vec3 a, Vec3 b) noexcept {
            return add(a, mul(b, -1));
        }
        Vec3 lerp(Vec3 a, Vec3 b, double t) noexcept {
            return add(mul(a, 1 - t), mul(b, t));
        }
        std::size_t index(int i, int j) noexcept {
            return (std::size_t(j) * GRID) + i;
        }
        double smoothDerivative(double a, double b, double x) noexcept {
            const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
            return 6 * t * (1 - t) / (b - a);
        }
        SheetGrid restGrid() noexcept {
            SheetGrid out{};
            for (int j = 0; j < GRID; ++j) {
                for (int i = 0; i < GRID; ++i) {
                    out[index(i, j)] = {
                        (double(i) / PAPER_CELLS) - 0.5, -static_cast<double>(j) / PAPER_CELLS, 0};
                }
            }
            return out;
        }
        // A curtain has enough material across its width to form vertical folds. Integrate
        // unit-length tangents so gathering spends width in depth without pre-stretching the mesh.
        // Jolt, not a display warp, unfolds this drape.
        SheetGrid curtainGrid() noexcept {
            std::array<Vec2, GRID> header{};
            for (int i = 1; i < GRID; ++i) {
                const double ANGLE = 1.1 * std::sin(10 * std::numbers::pi * (i - .5) / PAPER_CELLS);
                header[i] = {.x = header[i - 1].x + (std::cos(ANGLE) / PAPER_CELLS),
                    .y = header[i - 1].y + (std::sin(ANGLE) / PAPER_CELLS)};
            }
            SheetGrid out{};
            for (int j = 0; j < GRID; ++j) {
                for (int i = 0; i < GRID; ++i) {
                    const double DOWN = static_cast<double>(j) / PAPER_CELLS;
                    out[index(i, j)] = {.x = header[i].x - (header.back().x / 2),
                        .y = -DOWN,
                        .z = header[i].y * smoothstep(0, .18, DOWN)};
                }
            }
            return out;
        }
        float clothInverseMass(int row) noexcept {
            // A sewn hem carries extra inertia without turning the whole panel into a heavy rubber
            // membrane.
            return CLOTH_INV_MASS /
                   static_cast<float>(
                       1 + (2 * smoothstep(.92, 1, static_cast<double>(row) / PAPER_CELLS)));
        }
        bool validBox(const Box &b) noexcept {
            // Logical desktop geometry is far smaller than these bounds. Reject values that cannot
            // safely pass through float Jolt collision geometry and pixel-space projection, even
            // when the original doubles are finite.
            return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.w) &&
                   std::isfinite(b.h) && std::abs(b.x) <= 1e9 && std::abs(b.y) <= 1e9 && b.w >= 1 &&
                   b.h >= 1 && b.w <= 1e7 && b.h <= 1e7;
        }
        bool sanitize(ClothParams &p) noexcept {
            if (!std::isfinite(p.duration) || p.duration <= 0 || !std::isfinite(p.flutter) ||
                !std::isfinite(p.shading) || !std::isfinite(p.variation) ||
                !std::isfinite(p.shine)) {
                return false;
            }
            p.duration = std::clamp(p.duration, 0.1, 4.0);
            p.flutter = std::clamp(p.flutter, 0.0, 1.0);
            p.shading = std::clamp(p.shading, 0.0, 1.0);
            p.shine = std::clamp(p.shine, 0.0, 1.0);
            p.variation = std::clamp(p.variation, 0.0, 1.0);
            return true;
        }
    }  // namespace

    struct ClothFlutter::Run {
        struct Target {
            double time = 0;
            Box box{};
        };
        struct Snapshot {
            double time = 0;
            Vec3 rigidVelocity{};
            std::array<float, PAPER_GRID_VERTICES * 6> values{};
        };
        // Buffer a full stall interval of targets at up to 1000 Hz, and the resulting 120 Hz
        // snapshot burst.
        static constexpr unsigned INPUT_CAPACITY = 256, OUTPUT_CAPACITY = 40;
        bool live = false;
        ClothMovementParams movement{};
        Vec3 publishVelocity{};  // rigid motion omitted by the strength-scaled translating
                                 // simulation frame
        Vec3 snapshotRigidVelocity{};
        std::array<Target, INPUT_CAPACITY> targets{};
        std::array<Snapshot, OUTPUT_CAPACITY> snapshots{};
        std::atomic<unsigned> inputHead{0}, inputTail{0}, outputHead{0}, outputTail{0};
        std::atomic<bool> overflow{false};
        Transition kind = Transition::Open;
        Edge edge = Edge::Bottom;
        Box window{}, screen{};
        double length = OPEN_LENGTH;
        double maxInitialSpeed = 0;
        Vec3 travel{};
        int frames = 0;
        SheetGrid start{}, velocity{};
        // Every six-float position/velocity slot is written once, then published by recorded's
        // release store.
        std::vector<float> samples;
        std::atomic<int> recorded{0}, outsideFrame{0};
        std::atomic<bool> finished{false}, released{false};
        std::stop_source stop;
        std::thread worker;  // only the registry reaper joins it

        bool pinned(int j) const noexcept {
            return j == 0;
        }
        void sample(double t, SheetGrid &p, SheetGrid &v) const noexcept {
            const int COUNT = recorded.load(std::memory_order_acquire);
            if (COUNT < 2) {
                p = start;
                v = velocity;
                return;
            }
            const double at = std::clamp(t / DT, 0.0, static_cast<double>(COUNT - 1));
            const int a = std::min(int(at), COUNT - 2);
            const int b = a + 1;
            const double u = at - a;
            const double u2 = u * u;
            const double u3 = u2 * u;
            const double H00 = 2 * u3 - 3 * u2 + 1;
            const double H10 = u3 - 2 * u2 + u;
            const double H01 = -2 * u3 + 3 * u2;
            const double H11 = u3 - u2;
            const double D00 = (6 * u2 - 6 * u) / DT;
            const double D10 = 3 * u2 - 4 * u + 1;
            const double D01 = (-6 * u2 + 6 * u) / DT;
            const double D11 = 3 * u2 - 2 * u;
            for (std::size_t k = 0; k < p.size(); ++k) {
                const float *s0 = samples.data() + (((std::size_t(a) * p.size()) + k) * 6);
                const float *s1 = samples.data() + (((std::size_t(b) * p.size()) + k) * 6);
                const Vec3 p0{s0[0], s0[1], s0[2]};
                const Vec3 v0{s0[3], s0[4], s0[5]};
                const Vec3 p1{s1[0], s1[1], s1[2]};
                const Vec3 v1{s1[3], s1[4], s1[5]};
                if (k < GRID) {
                    // Interpolation between solved pocket positions cannot overshoot an end stop or
                    // reorder samples.
                    p[k] = lerp(p0, p1, u);
                    v[k] = lerp(v0, v1, u);
                    continue;
                }
                p[k] =
                    add(add(mul(p0, H00), mul(v0, H10 * DT)), add(mul(p1, H01), mul(v1, H11 * DT)));
                v[k] = add(add(mul(p0, D00), mul(v0, D10)), add(mul(p1, D01), mul(v1, D11)));
                const double EXTRA = std::clamp(t - ((COUNT - 1) * DT), 0.0, DT);
                p[k] = add(p[k], mul(v[k], EXTRA));
            }
        }
    };

    struct ClothFlutter::MovementPlayback {
        Run::Snapshot a{}, b{};
        Box target{}, previous{};
        Vec3 rigidVelocity{};
        double time = 0, lastMotion = 0;
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
          m_startupWait(other.m_startupWait),
          m_fallbackTime(other.m_fallbackTime),
          m_initialOpacity(other.m_initialOpacity),
          m_startupRecorded(other.m_startupRecorded),
          m_transitionHeld(other.m_transitionHeld),
          m_run(other.m_run),
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
            std::shared_ptr<Run> run;
            ~Lease() {
                run->stop.request_stop();
                run->released.store(true, std::memory_order_release);
            }
        };
        class Registry {
          public:
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
            void add(const std::shared_ptr<Run> &run) {
                const std::lock_guard LOCK(mutex);
                runs.push_back(run);
                changed.notify_one();
            }

          private:
            void reap(std::stop_token stop) {
                std::unique_lock lock(mutex);
                while (!stop.stop_requested()) {
                    changed.wait_for(lock, std::chrono::milliseconds(2));
                    for (auto it = runs.begin(); it != runs.end();) {
                        if (!(*it)->finished.load(std::memory_order_acquire) ||
                            !(*it)->released.load(std::memory_order_acquire)) {
                            ++it;
                            continue;
                        }
                        auto done = std::move(*it);
                        it = runs.erase(it);
                        lock.unlock();
                        if (done->worker.joinable()) {
                            done->worker.join();
                        }
                        done.reset();  // potentially large recording is freed here, never on
                                       // playback's last-reference path
                        lock.lock();
                        it = runs.begin();  // add() may have reallocated the vector while the lock
                                            // was released
                    }
                }
            }
            std::mutex mutex;
            std::condition_variable changed;
            std::vector<std::shared_ptr<Run>> runs;
            std::jthread reaper;
        };
        std::unique_ptr<Registry> s_registry;

        bool outside(const Run &run, const JPH::Array<JPH::SoftBodyVertex> &vertices) noexcept {
            const double W = run.window.w;
            const double H = run.window.h;
            const double L = std::sqrt(W * H);
            const double f = 3 * L;
            for (const auto &v : vertices) {
                const double SCALE = f / std::max(0.25 * f, f - (v.mPosition.GetZ() * L));
                const double x = run.window.x + (W / 2) + (v.mPosition.GetX() * W * SCALE);
                const double y =
                    run.window.y + (H / 2) + (((-v.mPosition.GetY() * H) - (H / 2)) * SCALE);
                if ((run.edge == Edge::Bottom && y <= run.screen.y + run.screen.h) ||
                    (run.edge == Edge::Top && y >= run.screen.y) ||
                    (run.edge == Edge::Left && x >= run.screen.x) ||
                    (run.edge == Edge::Right && x <= run.screen.x + run.screen.w)) {
                    return false;
                }
            }
            return true;
        }
        bool record(Run &run, int frame, const JPH::Array<JPH::SoftBodyVertex> &vertices) noexcept {
            const double TRAVEL = std::hypot(run.travel.x, run.travel.y);
            const double POSITION_LIMIT = 16 + TRAVEL + (GRAVITY * run.length * run.length) +
                                          (run.maxInitialSpeed * run.length);
            const double VELOCITY_LIMIT = 50 + (8 * TRAVEL / run.length) +
                                          (3 * GRAVITY * run.length) + (1.5 * run.maxInitialSpeed);
            for (const auto &v : vertices) {
                if (!std::isfinite(v.mPosition.GetX()) || !std::isfinite(v.mPosition.GetY()) ||
                    !std::isfinite(v.mPosition.GetZ()) || !std::isfinite(v.mVelocity.GetX()) ||
                    !std::isfinite(v.mVelocity.GetY()) || !std::isfinite(v.mVelocity.GetZ()) ||
                    std::max({std::abs(v.mPosition.GetX()),
                        std::abs(v.mPosition.GetY()),
                        std::abs(v.mPosition.GetZ())}) > POSITION_LIMIT ||
                    std::max({std::abs(v.mVelocity.GetX()),
                        std::abs(v.mVelocity.GetY()),
                        std::abs(v.mVelocity.GetZ())}) > VELOCITY_LIMIT) {
                    return false;
                }
            }
            float *p = run.samples.data() + (std::size_t(frame) * PAPER_GRID_VERTICES * 6);
            for (const auto &v : vertices) {
                *p++ = v.mPosition.GetX();
                *p++ = v.mPosition.GetY();
                *p++ = v.mPosition.GetZ();
                *p++ = v.mVelocity.GetX();
                *p++ = v.mVelocity.GetY();
                *p++ = v.mVelocity.GetZ();
            }
            if (run.kind == Transition::Close &&
                run.outsideFrame.load(std::memory_order_relaxed) == run.frames &&
                outside(run, vertices)) {
                run.outsideFrame.store(frame, std::memory_order_release);
            }
            run.recorded.store(frame + 1, std::memory_order_release);
            return true;
        }
        struct SimInput {
            std::vector<Box> obstacles;
            double flutter = 0;
            double phaseA = 0, phaseB = 0, freqA = 0;
        };
        // Invisible samples carry an effective header-column mass. The soft body follows their
        // solved positions, but does not feed forces back: this is a reduced-coupling model of a
        // continuous sleeve.
        class Pocket {
            class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
                bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override {
                    return false;
                }
            };

          public:
            explicit Pocket(const SheetGrid &start, const SheetGrid &velocity, double amount)
                : SPRINGINESS(static_cast<float>(amount)), pairs(1), broadPhase(1, 1) {
                using namespace JPH;
                broadPhase.MapObjectToBroadPhaseLayer(0, BroadPhaseLayer(0));
                system.Init(GRID, 0, 1, 1, broadPhase, objectVsBroadPhase, pairs);
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
                for (int i = 0; i < GRID; ++i) {
                    const float x = std::clamp(static_cast<float>(start[i].x), -LIMIT, LIMIT);
                    BodyCreationSettings creation(
                        SHAPE, RVec3(x, 0, 0), Quat::sIdentity(), EMotionType::Dynamic, 0);
                    creation.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
                    creation.mMassPropertiesOverride.mMass = MASS;
                    creation.mGravityFactor = 0;
                    creation.mLinearDamping = 0;
                    creation.mFriction = 0;
                    creation.mAllowSleeping = false;
                    creation.mMaxLinearVelocity = 8;
                    ids[i] = bodies.CreateAndAddBody(creation, EActivation::Activate);
                    if (ids[i].IsInvalid()) {
                        return;
                    }
                    bodies.SetLinearVelocity(
                        ids[i], JPH::Vec3(static_cast<float>(velocity[i].x) * SPRINGINESS, 0, 0));
                    SliderConstraintSettings slider;
                    slider.mPoint1 = slider.mPoint2 = RVec3(x, 0, 0);
                    slider.mLimitsMin = -LIMIT - x;
                    slider.mLimitsMax = LIMIT - x;
                    constraints[i] = bodies.CreateConstraint(&slider, BodyID(), ids[i]);
                    if (!constraints[i]) {
                        return;
                    }
                    system.AddConstraint(constraints[i]);
                    if (i > 0) {
                        SliderConstraintSettings spacing;
                        spacing.mPoint1 =
                            bodies.GetPosition(ids[i - 1]) + RVec3(2 * HALF_WIDTH, 0, 0);
                        spacing.mPoint2 = RVec3(x, 0, 0);
                        spacing.mLimitsMin = 0;
                        spacing.mLimitsMax = 1;
                        neighbours[i - 1] = bodies.CreateConstraint(&spacing, ids[i - 1], ids[i]);
                        if (!neighbours[i - 1]) {
                            return;
                        }
                        system.AddConstraint(neighbours[i - 1]);
                    }
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
                    if (constraint) {
                        system.RemoveConstraint(constraint);
                    }
                }
                for (const auto &constraint : constraints) {
                    if (constraint) {
                        system.RemoveConstraint(constraint);
                    }
                }
                for (const auto &id : ids) {
                    if (!id.IsInvalid()) {
                        bodies.RemoveBody(id);
                        bodies.DestroyBody(id);
                    }
                }
            }

            bool update(double acceleration,
                double rodVelocity,
                double quiet,
                JPH::TempAllocator &temp,
                JPH::JobSystem &jobs) {
                auto &bodies = system.GetBodyInterfaceNoLock();
                // Reinforce the trailing end by up to 4x; blend through zero speed when stopping or
                // reversing.
                const float DIRECTION =
                    static_cast<float>(std::clamp(rodVelocity / .05, -1.0, 1.0));
                const float TRAILING = .5F * std::abs(DIRECTION);
                for (int i = 0; i < GRID; ++i) {
                    const float x = float(bodies.GetPosition(ids[i]).GetX());
                    const auto REST = static_cast<float>((double(i) / PAPER_CELLS) - .5);
                    const float VELOCITY = bodies.GetLinearVelocity(ids[i]).GetX();
                    // A distributed lengthwise spring restores the sleeve during travel, not only
                    // after it stops. Jolt integrates the spring force; spring damping controls the
                    // rebound without sliding friction.
                    const float EFFECTIVE_SPRING = SPRING * (4 - (3 * SPRINGINESS)) *
                                                   (1 + (3 * (TRAILING - (DIRECTION * REST))));
                    const float EFFECTIVE_DAMPING = DAMPING * std::sqrt(EFFECTIVE_SPRING / SPRING);
                    const auto FORCE =
                        static_cast<float>((-MASS * acceleration * SPRINGINESS) +
                                           ((EFFECTIVE_SPRING + (quiet * 8)) * (REST - x)) -
                                           (EFFECTIVE_DAMPING * VELOCITY));
                    bodies.AddForce(ids[i], JPH::Vec3(FORCE, 0, 0));
                }
                return system.Update(static_cast<float>(DT), 1, &temp, &jobs) ==
                       JPH::EPhysicsUpdateError::None;
            }

            float position(int i) {
                return float(system.GetBodyInterfaceNoLock().GetPosition(ids[i]).GetX());
            }
            bool valid() const {
                return ready;
            }

          private:
            static constexpr float MASS = 1.0F, SPRING = 48.0F, DAMPING = 4.0F;
            static constexpr float HALF_WIDTH = .002F, LIMIT = .5F - HALF_WIDTH;
            const float SPRINGINESS;
            JPH::ObjectLayerPairFilterTable pairs;
            JPH::BroadPhaseLayerInterfaceTable broadPhase;
            BroadFilter objectVsBroadPhase;
            JPH::PhysicsSystem system;
            std::array<JPH::BodyID, GRID> ids{};
            std::array<JPH::Ref<JPH::TwoBodyConstraint>, GRID> constraints{};
            std::array<JPH::Ref<JPH::TwoBodyConstraint>, GRID - 1> neighbours{};
            bool ready = false;
        };

        bool publishMovement(
            Run &run, double time, const JPH::Array<JPH::SoftBodyVertex> &vertices) {
            const unsigned HEAD = run.outputHead.load(std::memory_order_relaxed);
            if (HEAD - run.outputTail.load(std::memory_order_acquire) == Run::OUTPUT_CAPACITY) {
                run.overflow.store(true, std::memory_order_release);
                return false;
            }
            auto &snapshot = run.snapshots[HEAD % Run::OUTPUT_CAPACITY];
            snapshot.time = time;
            snapshot.rigidVelocity = run.snapshotRigidVelocity;
            float *out = snapshot.values.data();
            for (std::size_t k = 0; k < vertices.size(); ++k) {
                const auto &vertex = vertices[k];
                const auto p = vertex.mPosition;
                const auto v = vertex.mVelocity;
                // Header vertices follow the rod directly; only the free panel uses the partial
                // inertial rebase.
                const auto &frameVelocity =
                    k < GRID ? run.snapshotRigidVelocity : run.publishVelocity;
                if (!std::isfinite(p.GetX()) || !std::isfinite(p.GetY()) ||
                    !std::isfinite(p.GetZ()) || !std::isfinite(v.GetX()) ||
                    !std::isfinite(v.GetY()) || !std::isfinite(v.GetZ())) {
                    return false;
                }
                *out++ = p.GetX();
                *out++ = p.GetY();
                *out++ = p.GetZ();
                *out++ = static_cast<float>(v.GetX() + frameVelocity.x);
                *out++ = static_cast<float>(v.GetY() + frameVelocity.y);
                *out++ = static_cast<float>(v.GetZ() + frameVelocity.z);
            }
            run.outputHead.store(HEAD + 1, std::memory_order_release);
            run.recorded.fetch_add(1, std::memory_order_release);
            return true;
        }

        void simulateMovement(Run &run, double phase) {
            using namespace JPH;
            enum : ObjectLayer { CLOTH = 0 };
            const ObjectLayerPairFilterTable PAIRS(1);
            BroadPhaseLayerInterfaceTable broadPhase(1, 1);
            broadPhase.MapObjectToBroadPhaseLayer(CLOTH, BroadPhaseLayer(0));
            const ObjectVsBroadPhaseLayerFilterTable OBJECT_VS_BROAD_PHASE(broadPhase, 1, PAIRS, 1);
            TempAllocatorImpl temp(8 << 20);
            JobSystemSingleThreaded jobs(cMaxPhysicsJobs);
            PhysicsSystem system;
            system.Init(1, 0, 32, 32, broadPhase, OBJECT_VS_BROAD_PHASE, PAIRS);
            system.SetGravity(JPH::Vec3(0, static_cast<float>(-GRAVITY * run.movement.gravity), 0));
            const auto STOPPED = [&] { return run.stop.stop_requested(); };
            if (STOPPED()) {
                return;
            }
            const SheetGrid REST = restGrid();
            std::array<double, GRID> pleats{};
            for (int i = 0; i < GRID; ++i) {
                pleats[i] = std::sin((8 * std::numbers::pi * i / PAPER_CELLS) + phase);
            }
            const Ref<SoftBodySharedSettings> SETTINGS = new SoftBodySharedSettings;
            for (std::size_t k = 0; k < REST.size(); ++k) {
                const auto p = REST[k];
                SETTINGS->mVertices.emplace_back(
                    Float3(static_cast<float>(p.x), static_cast<float>(p.y), 0),
                    Float3(0, 0, 0),
                    k < GRID ? 0.0F : clothInverseMass(static_cast<int>(k / GRID)));
            }
            for (int j = 0; j < PAPER_CELLS; ++j) {
                for (int i = 0; i < PAPER_CELLS; ++i) {
                    const auto a = uint32(index(i, j));
                    const auto b = uint32(index(i + 1, j));
                    const auto c = uint32(index(i, j + 1));
                    const auto d = uint32(index(i + 1, j + 1));
                    SETTINGS->AddFace(SoftBodySharedSettings::Face(a, c, b));
                    SETTINGS->AddFace(SoftBodySharedSettings::Face(b, c, d));
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
            const BodyID id = bodies.CreateAndAddSoftBody(creation, EActivation::Activate);
            if (id.IsInvalid()) {
                return;
            }
            auto *motion = static_cast<SoftBodyMotionProperties *>(
                system.GetBodyLockInterfaceNoLock().TryGetBody(id)->GetMotionProperties());
            auto &vertices = motion->GetVertices();
            for (std::size_t k = 0; k < REST.size(); ++k) {
                const auto p = run.start[k];
                const auto v = run.velocity[k];
                vertices[k].mPosition = JPH::Vec3(
                    static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
                vertices[k].mVelocity = JPH::Vec3(
                    static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
            }
            std::optional<Pocket> pocket;
            if (run.movement.springiness > 0) {
                pocket.emplace(run.start, run.velocity, run.movement.springiness);
            }
            if (pocket && !pocket->valid()) {
                bodies.RemoveBody(id);
                bodies.DestroyBody(id);
                return;
            }
            bool good = publishMovement(run, -DT, vertices) && publishMovement(run, 0, vertices);
            Run::Target previous{.time = 0, .box = run.window};
            Box box = run.window;
            double time = 0;
            double lastMotion = 0;
            double previousRodVelocity = 0;
            while (good && !STOPPED()) {
                if (s_pause.load(std::memory_order_acquire) &&
                    run.recorded.load(std::memory_order_relaxed) >=
                        s_pauseAfter.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const unsigned TAIL = run.inputTail.load(std::memory_order_relaxed);
                if (TAIL == run.inputHead.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const Run::Target TARGET = run.targets[TAIL % Run::INPUT_CAPACITY];
                run.inputTail.store(TAIL + 1, std::memory_order_release);
                while (time + DT <= TARGET.time + 1e-10 && good && !STOPPED()) {
                    const double NEXT_TIME = time + DT;
                    const double u = std::clamp(
                        (NEXT_TIME - previous.time) / (TARGET.time - previous.time), 0.0, 1.0);
                    const Box NEXT{.x = previous.box.x + (u * (TARGET.box.x - previous.box.x)),
                        .y = previous.box.y + (u * (TARGET.box.y - previous.box.y)),
                        .w = previous.box.w + (u * (TARGET.box.w - previous.box.w)),
                        .h = previous.box.h + (u * (TARGET.box.h - previous.box.h))};
                    const double dx = NEXT.x - box.x;
                    const double dy = NEXT.y - box.y;
                    run.snapshotRigidVelocity = {
                        .x = dx / DT / NEXT.w, .y = -dy / DT / NEXT.h, .z = 0};
                    const double GAIN = std::min(run.movement.strength, 1.5);
                    run.publishVelocity = {.x = (1 - GAIN) * dx / DT / NEXT.w,
                        .y = -(1 - GAIN) * dy / DT / NEXT.h,
                        .z = 0};
                    const double SPEED = std::hypot(dx, dy) / DT / std::min(NEXT.w, NEXT.h);
                    if (SPEED > 1e-5 ||
                        std::abs(NEXT.w - box.w) + std::abs(NEXT.h - box.h) > 1e-5) {
                        lastMotion = NEXT_TIME;
                    }
                    const double QUIET = NEXT_TIME - lastMotion;
                    const double SETTLE = smoothstep(0.05, 0.5, QUIET);
                    const double INHERITED = std::exp(-6 * NEXT_TIME);
                    const double ROD_VELOCITY = dx / DT / NEXT.w;
                    const double ACCELERATION = GAIN * (ROD_VELOCITY - previousRodVelocity) / DT;
                    previousRodVelocity = ROD_VELOCITY;
                    if (pocket && !pocket->update(ACCELERATION, ROD_VELOCITY, SETTLE, temp, jobs)) {
                        good = false;
                        break;
                    }
                    const double L = std::sqrt(NEXT.w * NEXT.h);
                    motion->SetLinearDamping(static_cast<float>(2 + (9 * SETTLE)));
                    const double GUST = std::sin(phase + (NEXT_TIME * 7));
                    for (int j = 0; j < GRID; ++j) {
                        for (int i = 0; i < GRID; ++i) {
                            const auto k = index(i, j);
                            auto &vertex = vertices[k];
                            auto p = vertex.mPosition;
                            if (j != 0) {
                                p = JPH::Vec3(static_cast<float>(
                                                  (box.x + (box.w / 2) + (p.GetX() * box.w) -
                                                      NEXT.x - (NEXT.w / 2) + ((1 - GAIN) * dx)) /
                                                  NEXT.w),
                                    static_cast<float>(((GAIN * dy) + (p.GetY() * box.h)) / NEXT.h),
                                    static_cast<float>(p.GetZ() * std::sqrt(box.w * box.h) / L));
                            }
                            auto v = vertex.mVelocity;
                            v = JPH::Vec3(static_cast<float>(v.GetX() * box.w / NEXT.w),
                                static_cast<float>(v.GetY() * box.h / NEXT.h),
                                static_cast<float>(v.GetZ() * std::sqrt(box.w * box.h) / L));
                            const auto r = REST[k];
                            const Vec3 OLD_OFFSET = sub(run.start[k], r);
                            const JPH::Vec3 DESIRED(
                                static_cast<float>(r.x + (OLD_OFFSET.x * INHERITED)),
                                static_cast<float>(r.y + (OLD_OFFSET.y * INHERITED)),
                                static_cast<float>(OLD_OFFSET.z * INHERITED));
                            if (j == 0) {
                                const float x = pocket ? pocket->position(i) : DESIRED.GetX();
                                v = (JPH::Vec3(x, 0, 0) - p) / static_cast<float>(DT);
                            } else {
                                const double DOWN = static_cast<double>(j) / PAPER_CELLS;
                                const double RESPONSE = vertex.mInvMass / CLOTH_INV_MASS;
                                const double SPRING =
                                    4 + (30 * (1 - DOWN) * (1 - DOWN)) + (120 * SETTLE);
                                v += (DESIRED - p) * static_cast<float>(SPRING * RESPONSE * DT);
                                const double DRIVE = std::min(SPEED, 3.0) * run.movement.strength;
                                // Alternating pressure excites long folds instead of inflating the
                                // entire sheet forward.
                                const double PRESSURE = pleats[i] *
                                                        (16 + (8 * run.movement.flutter * GUST)) *
                                                        (.3 + (.7 * DOWN));
                                v += JPH::Vec3(
                                    0, 0, static_cast<float>(RESPONSE * DRIVE * PRESSURE * DT));
                            }
                            vertex.mPosition = p;
                            vertex.mVelocity = v;
                        }
                    }
                    if (system.Update(static_cast<float>(DT), 1, &temp, &jobs) !=
                        EPhysicsUpdateError::None) {
                        good = false;
                        break;
                    }
                    // Keep Jolt's solved pose and velocity together. Clamping vertices back toward
                    // the flat panel breaks its constraints and clips inertial upward folds.
                    box = NEXT;
                    time = NEXT_TIME;
                    good = publishMovement(run, time, vertices);
                }
                previous = TARGET;
            }
            bodies.RemoveBody(id);
            bodies.DestroyBody(id);
        }

        void simulate(Run &run, const SimInput &in) {
            using namespace JPH;
            const auto STOPPED = [&] { return run.stop.stop_requested(); };
            const auto WAIT_PUBLICATION = [&] {
                while (s_pause.load(std::memory_order_acquire) &&
                       run.recorded.load(std::memory_order_acquire) >=
                           s_pauseAfter.load(std::memory_order_relaxed) &&
                       !STOPPED()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return !STOPPED();
            };
            if (!WAIT_PUBLICATION()) {
                return;
            }
            run.samples.resize(static_cast<std::size_t>(run.frames) * PAPER_GRID_VERTICES * 6);
            enum : ObjectLayer { STATIC = 0, CLOTH = 1, LAYERS = 2 };
            ObjectLayerPairFilterTable pairs(LAYERS);
            pairs.EnableCollision(STATIC, CLOTH);
            BroadPhaseLayerInterfaceTable broadPhase(LAYERS, 2);
            broadPhase.MapObjectToBroadPhaseLayer(STATIC, BroadPhaseLayer(0));
            broadPhase.MapObjectToBroadPhaseLayer(CLOTH, BroadPhaseLayer(1));
            const ObjectVsBroadPhaseLayerFilterTable OBJECT_VS_BROAD_PHASE(
                broadPhase, 2, pairs, LAYERS);
            TempAllocatorImpl temp(8 << 20);
            JobSystemSingleThreaded jobs(cMaxPhysicsJobs);
            PhysicsSystem system;
            if (STOPPED()) {
                return;
            }
            system.Init(MAX_OBSTACLES + 4, 0, 1024, 1024, broadPhase, OBJECT_VS_BROAD_PHASE, pairs);
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
            for (const Box &o : in.obstacles) {
                ADD_BOX(JPH::Vec3(static_cast<float>(o.w / 2), static_cast<float>(o.h / 2), 0.515F),
                    JPH::Vec3(static_cast<float>(o.x + (o.w / 2)),
                        static_cast<float>(o.y + (o.h / 2)),
                        -0.485F));
            }
            if (STOPPED()) {
                return;
            }
            const Ref<SoftBodySharedSettings> SETTINGS = new SoftBodySharedSettings;
            const SheetGrid REST = restGrid();
            std::array<double, GRID> gustColumns{};
            if (in.flutter > 0) {
                for (int i = 0; i < GRID; ++i) {
                    gustColumns[i] = std::sin((8 * std::numbers::pi * i / PAPER_CELLS) + in.phaseA);
                }
            }
            // Keep the wall behind the exact display plane: a flat, fully free sheet must not start
            // embedded by its radius.
            for (int j = 0; j < GRID; ++j) {
                for (int i = 0; i < GRID; ++i) {
                    const Vec3 &p = REST[index(i, j)];
                    SETTINGS->mVertices.emplace_back(Float3(static_cast<float>(p.x),
                                                         static_cast<float>(p.y),
                                                         static_cast<float>(p.z)),
                        Float3(0, 0, 0),
                        run.pinned(j) ? 0.0F : clothInverseMass(j));
                }
            }
            for (int j = 0; j < PAPER_CELLS; ++j) {
                for (int i = 0; i < PAPER_CELLS; ++i) {
                    const auto a = uint32(index(i, j));
                    const auto b = uint32(index(i + 1, j));
                    const auto c = uint32(index(i, j + 1));
                    const auto d = uint32(index(i + 1, j + 1));
                    SETTINGS->AddFace(SoftBodySharedSettings::Face(a, c, b));
                    SETTINGS->AddFace(SoftBodySharedSettings::Face(b, c, d));
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
                (1.5 * run.maxInitialSpeed) +
                    (8 * std::hypot(run.travel.x, run.travel.y) / run.length)));
            creation.mFacesDoubleSided = true;
            const BodyID id = bodies.CreateAndAddSoftBody(creation, EActivation::Activate);
            if (id.IsInvalid()) {
                return;
            }
            auto *motion = static_cast<SoftBodyMotionProperties *>(
                system.GetBodyLockInterfaceNoLock().TryGetBody(id)->GetMotionProperties());
            auto &vertices = motion->GetVertices();
            for (std::size_t k = 0; k < REST.size(); ++k) {
                const auto p = run.start[k];
                const auto v = run.velocity[k];
                vertices[k].mPosition = JPH::Vec3(
                    static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
                vertices[k].mVelocity = JPH::Vec3(
                    static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
            }
            if (run.kind == Transition::Open) {
                // A level, gathered sleeve compresses the first material rows. Relax that sewn
                // pocket with Jolt on the worker before publishing frame zero, instead of
                // displaying its initial constraint correction.
                motion->SetLinearDamping(8);
                for (int step = 0; step < 24; ++step) {
                    if (!WAIT_PUBLICATION() ||
                        system.Update(static_cast<float>(DT), 1, &temp, &jobs) !=
                            EPhysicsUpdateError::None) {
                        bodies.RemoveBody(id);
                        bodies.DestroyBody(id);
                        return;
                    }
                }
                for (auto &vertex : vertices) {
                    vertex.mVelocity = JPH::Vec3::sZero();
                }
                motion->SetLinearDamping(1.5F);
            }
            std::optional<Pocket> pocket;
            if (run.kind == Transition::Close && run.movement.springiness > 0) {
                pocket.emplace(run.start, run.velocity, run.movement.springiness);
                if (!pocket->valid()) {
                    bodies.RemoveBody(id);
                    bodies.DestroyBody(id);
                    return;
                }
            }
            if (!record(run, 0, vertices)) {
                bodies.RemoveBody(id);
                bodies.DestroyBody(id);
                return;
            }
            const auto OPEN_AIR_DRAG_FACTOR = static_cast<float>(1 - std::exp(-OPEN_AIR_DRAG * DT));
            for (int step = 1; step < run.frames && WAIT_PUBLICATION(); ++step) {
                const double TIME = (step - 1) * DT;
                if (pocket) {
                    const double t = std::clamp((TIME + DT) / run.length, 0.0, 1.0);
                    const double ROD_VELOCITY =
                        run.travel.x * 30 * t * t * (1 - t) * (1 - t) / run.length;
                    if (!pocket->update(0, ROD_VELOCITY, 0, temp, jobs)) {
                        break;
                    }
                }
                if (run.kind == Transition::Open) {
                    motion->SetLinearDamping(
                        static_cast<float>(1.5 + (8 * smoothstep(0.65, 0.95, TIME / run.length))));
                }
                const double HEADER_SPREAD =
                    run.kind == Transition::Open ? smoothstep(0, .72, (TIME + DT) / run.length) : 0;
                for (int j = 0; j < GRID; ++j) {
                    const double ROW_GUST =
                        in.flutter > 0
                            ? std::sin((in.freqA * TIME) +
                                       (.8 * static_cast<double>(j) / PAPER_CELLS) + in.phaseB)
                            : 0;
                    for (int i = 0; i < GRID; ++i) {
                        const auto k = index(i, j);
                        auto &v = vertices[k];
                        if (run.pinned(j)) {
                            Vec3 target = run.start[k];
                            if (run.kind == Transition::Open) {
                                target = lerp(target, REST[k], HEADER_SPREAD);
                            }
                            if (run.kind == Transition::Close) {
                                const double t = std::clamp((TIME + DT) / run.length, 0.0, 1.0);
                                const double DISTANCE = t * t * t * (10 + (t * (-15 + (6 * t))));
                                const double INHERITED_TIME = (TIME + DT) * (1 - t) * (1 - t);
                                target.x = (pocket ? pocket->position(i) : target.x) +
                                           (run.travel.x * DISTANCE);
                                target.y += (run.travel.y * DISTANCE) +
                                            (run.velocity[0].y * INHERITED_TIME);
                                target.z = 0;
                            }
                            const JPH::Vec3 p(static_cast<float>(target.x),
                                static_cast<float>(target.y),
                                static_cast<float>(target.z));
                            v.mVelocity = (p - v.mPosition) / static_cast<float>(DT);
                        } else if (run.kind == Transition::Open || in.flutter > 0) {
                            const auto at = [&](int x, int y) {
                                return vertices[index(x, y)].mPosition;
                            };
                            const auto dx =
                                at(std::min(i + 1, PAPER_CELLS), j) - at(std::max(i - 1, 0), j);
                            const auto dy =
                                at(i, std::min(j + 1, PAPER_CELLS)) - at(i, std::max(j - 1, 0));
                            const auto n = dx.Cross(dy).NormalizedOr(JPH::Vec3::sAxisZ());
                            if (run.kind == Transition::Open) {
                                v.mVelocity -= n * (v.mVelocity.Dot(n) * OPEN_AIR_DRAG_FACTOR);
                            }
                            if (in.flutter > 0) {
                                const double WAVE = gustColumns[i] * ROW_GUST;
                                v.mVelocity +=
                                    n * static_cast<float>(5 * in.flutter * WAVE * DT * n.GetZ() *
                                                           (v.mInvMass / CLOTH_INV_MASS));
                            }
                        }
                    }
                }
                if (system.Update(static_cast<float>(DT), 1, &temp, &jobs) !=
                    EPhysicsUpdateError::None) {
                    break;
                }
                if (!record(run, step, vertices)) {
                    break;
                }
            }
            bodies.RemoveBody(id);
            bodies.DestroyBody(id);
        }
    }  // namespace

    ClothPhysics::ClothPhysics() {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = silentTrace;
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        s_registry = std::make_unique<Registry>();
        s_live.store(true, std::memory_order_release);
    }
    ClothPhysics::~ClothPhysics() {
        s_live.store(false, std::memory_order_release);
        s_registry.reset();
        s_pause.store(false, std::memory_order_release);
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
    void ClothPhysics::pauseWorkersForTesting(bool pause, int afterSnapshots) noexcept {
        s_pauseAfter.store(std::max(0, afterSnapshots), std::memory_order_relaxed);
        s_pause.store(pause, std::memory_order_release);
    }

    void ClothFlutter::beginMovement(
        const ClothMovementParams &params, std::uint64_t seed, const Box &capture) {
        if (!validBox(capture) || !s_live.load(std::memory_order_acquire) ||
            !std::isfinite(params.strength) || params.strength <= 0 ||
            !std::isfinite(params.flutter) || !std::isfinite(params.shading) ||
            !std::isfinite(params.variation) || !std::isfinite(params.shine) ||
            !std::isfinite(params.springiness) || !std::isfinite(params.gravity)) {
            reset();
            return;
        }
        const auto RUN = std::make_shared<Run>();
        RUN->live = true;
        RUN->window = capture;
        RUN->movement = {.strength = std::clamp(params.strength, 0.0, 1.5),
            .flutter = std::clamp(params.flutter, 0.0, 1.0),
            .shading = std::clamp(params.shading, 0.0, 1.0),
            .variation = std::clamp(params.variation, 0.0, 1.0),
            .shine = std::clamp(params.shine, 0.0, 1.0),
            .springiness = std::clamp(params.springiness, 0.0, 1.0),
            .gravity = std::clamp(params.gravity, 0.0, 3.0)};
        const double ALPHA = opacity();
        sampleVertices(RUN->start, RUN->velocity);
        auto playback = std::make_unique<MovementPlayback>();
        playback->target = playback->previous = capture;
        playback->a.time = -DT;
        playback->b.time = 0;
        for (std::size_t k = 0; k < RUN->start.size(); ++k) {
            const auto p = RUN->start[k];
            const auto v = RUN->velocity[k];
            const std::array<float, 6> VALUE{static_cast<float>(p.x),
                static_cast<float>(p.y),
                static_cast<float>(p.z),
                static_cast<float>(v.x),
                static_cast<float>(v.y),
                static_cast<float>(v.z)};
            std::copy(VALUE.begin(), VALUE.end(), playback->a.values.begin() + (k * 6));
            std::copy(VALUE.begin(), VALUE.end(), playback->b.values.begin() + (k * 6));
        }
        std::uint64_t fixed = BASE_SEED;
        std::uint64_t random = seed;
        const double PHASE = 2 * std::numbers::pi *
                             ((unit_draw(fixed) * (1 - RUN->movement.variation)) +
                                 (unit_draw(random) * RUN->movement.variation));
        try {
            RUN->worker = std::thread([RUN, PHASE] {
                try {
                    simulateMovement(*RUN, PHASE);
                } catch (...) {
                }
                RUN->finished.store(true, std::memory_order_release);
            });
            s_registry->add(RUN);
            const auto LEASE = std::make_shared<Lease>();
            LEASE->run = RUN;
            m_run = std::shared_ptr<Run>(LEASE, RUN.get());
        } catch (...) {
            RUN->stop.request_stop();
            if (RUN->worker.joinable()) {
                RUN->worker.join();
            }
            reset();
            return;
        }
        m_movement = std::move(playback);
        m_params = {.duration = 1,
            .flutter = RUN->movement.flutter,
            .shading = RUN->movement.shading,
            .variation = RUN->movement.variation,
            .shine = RUN->movement.shine};
        m_initialOpacity = ALPHA;
        m_t = 0;
        m_rate = 1;
        m_startupWait = m_fallbackTime = 0;
        m_fallback.reset();
        m_phase = Phase::Movement;
    }

    void ClothFlutter::updateMovement(const Box &capture, double elapsed) noexcept {
        if (!moving() || !m_movement || !m_movement->producer) {
            return;
        }
        if (!validBox(capture) || !std::isfinite(elapsed) || elapsed < 0 ||
            elapsed > STALL_SECONDS) {
            reset();
            return;
        }
        const Box OLD = m_movement->target;
        if (capture.x != OLD.x || capture.y != OLD.y || capture.w != OLD.w || capture.h != OLD.h) {
            if (m_fallback && m_movement->settling) {
                // Motion during the exact-flat transition starts from its displayed pose.
                const auto PARAMS = m_run->movement;
                beginMovement(PARAMS, BASE_SEED, OLD);
            }
            if (!m_movement) {
                return;
            }
            m_movement->lastMotion = m_t;
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
        std::optional<ClothCloseGeometry> geometry) {
        ClothParams p = params;
        if ((kind != Transition::Open && kind != Transition::Close) || !sanitize(p) ||
            !s_live.load(std::memory_order_acquire) || !validBox(window) ||
            (geometry && (!validBox(geometry->screen) ||
                             (geometry->edge != Edge::Top && geometry->edge != Edge::Bottom &&
                                 geometry->edge != Edge::Left && geometry->edge != Edge::Right)))) {
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
        if (kind == Transition::Close &&
            (m_phase == Phase::Opening || m_phase == Phase::Movement) && m_run) {
            RUN->movement.springiness = m_run->movement.springiness;
            sampleVertices(RUN->start, RUN->velocity);
        } else {
            RUN->start = restGrid();
        }
        if (kind == Transition::Open) {
            RUN->start = curtainGrid();
        } else {
            const double W = window.w;
            const double H = window.h;
            RUN->length = 1.4;
            if (RUN->edge == Edge::Bottom) {
                RUN->travel.y =
                    -(std::max(0.0, (RUN->screen.y + RUN->screen.h - window.y) / H) + 2.5);
            }
            if (RUN->edge == Edge::Top) {
                RUN->travel.y = std::max(0.0, (window.y - RUN->screen.y) / H) + 2.5;
            }
            if (RUN->edge == Edge::Left) {
                RUN->travel.x = -(std::max(0.0, (window.x - RUN->screen.x) / W) + 2.5);
            }
            if (RUN->edge == Edge::Right) {
                RUN->travel.x =
                    std::max(0.0, (RUN->screen.x + RUN->screen.w - window.x - W) / W) + 2.5;
            }
        }
        RUN->frames = static_cast<int>(std::ceil(RUN->length / DT)) + 1;
        RUN->length = (RUN->frames - 1) * DT;
        const double RATE = RUN->length / p.duration;
        for (auto &v : RUN->velocity) {
            v = mul(v, 1 / RATE);  // displayed real-time velocity -> new simulation clock
            RUN->maxInitialSpeed =
                std::max(RUN->maxInitialSpeed, std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z)));
        }
        std::uint64_t base = BASE_SEED;
        std::uint64_t random = seed;
        const auto DRAW = [&] {
            const double b = unit_draw(base);
            const double r = unit_draw(random);
            return b + (p.variation * (r - b));
        };
        SimInput input;
        input.flutter = p.flutter;
        input.phaseA = 2 * std::numbers::pi * DRAW();
        input.phaseB = 2 * std::numbers::pi * DRAW();
        input.freqA = 9 + (4 * DRAW());
        std::vector<std::pair<double, Box>> nearest;
        for (const Box &o : obstacles) {
            if (!validBox(o) || (o.x < window.x + window.w && o.x + o.w > window.x &&
                                    o.y < window.y + window.h && o.y + o.h > window.y)) {
                continue;
            }
            // A closing sheet must clear the screen. Slabs directly across its exit corridor would
            // trap a zero-flutter sheet in the desktop plane; omit those while retaining obstacles
            // beside that corridor.
            if (kind == Transition::Close) {
                const bool OVERLAP_X = o.x < window.x + window.w && o.x + o.w > window.x;
                const bool OVERLAP_Y = o.y < window.y + window.h && o.y + o.h > window.y;
                if ((RUN->edge == Edge::Bottom && OVERLAP_X && o.y >= window.y + window.h) ||
                    (RUN->edge == Edge::Top && OVERLAP_X && o.y + o.h <= window.y) ||
                    (RUN->edge == Edge::Left && OVERLAP_Y && o.x + o.w <= window.x) ||
                    (RUN->edge == Edge::Right && OVERLAP_Y && o.x >= window.x + window.w)) {
                    continue;
                }
            }
            const double dx = std::max({window.x - o.x - o.w, o.x - window.x - window.w, 0.0});
            const double dy = std::max({window.y - o.y - o.h, o.y - window.y - window.h, 0.0});
            nearest.push_back({std::hypot(dx, dy),
                {.x = (o.x - window.x - (window.w / 2)) / window.w,
                    .y = -(o.y + o.h - window.y) / window.h,
                    .w = o.w / window.w,
                    .h = o.h / window.h}});
        }
        std::stable_sort(nearest.begin(), nearest.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        for (std::size_t k = 0;
            k < std::min(nearest.size(), static_cast<std::size_t>(MAX_OBSTACLES));
            ++k) {
            input.obstacles.push_back(nearest[k].second);
        }
        RUN->outsideFrame.store(RUN->frames, std::memory_order_relaxed);
        try {
            RUN->worker = std::thread([RUN, input = std::move(input)] {
                try {
                    simulate(*RUN, input);
                } catch (...) {
                }
                RUN->finished.store(true, std::memory_order_release);
            });
            s_registry->add(RUN);
            const auto LEASE = std::make_shared<Lease>();
            LEASE->run = RUN;
            m_run = std::shared_ptr<Run>(LEASE, RUN.get());
        } catch (...) {
            RUN->stop.request_stop();
            if (RUN->worker.joinable()) {
                RUN->worker.join();
            }
            reset();
            return;
        }
        m_params = p;
        m_movement.reset();
        m_rate = RATE;
        m_t = 0;
        m_startupWait = 0;
        m_startupRecorded = 0;
        m_transitionHeld = false;
        m_fallbackTime = 0;
        m_fallback.reset();
        m_initialOpacity = kind == Transition::Close ? OLD_OPACITY : 1;
        m_phase = kind == Transition::Open ? Phase::Opening : Phase::Closing;
    }

    void ClothFlutter::startFallback() noexcept {
        try {
            auto fallback = std::make_shared<Fallback>();
            sampleVertices(fallback->positions, fallback->velocities);
            if (m_phase == Phase::Movement && m_movement) {
                for (auto &v : fallback->velocities) {
                    v = sub(v, m_movement->rigidVelocity);
                }
            }
            fallback->alpha = opacity();
            m_fallback = std::move(fallback);
            m_fallbackTime = 0;
            m_run->stop.request_stop();
        } catch (...) {
            if (m_phase != Phase::Closing) {
                reset();
            } else {
                m_phase = Phase::Closed;
            }
        }
    }
    bool ClothFlutter::advance(double dt) noexcept {
        if (!active()) {
            return false;
        }
        if (!std::isfinite(dt) || dt < 0 || dt > 0.25) {
            if (m_phase != Phase::Closing) {
                reset();
            } else {
                m_phase = Phase::Closed;
                m_run.reset();
            }
            return false;
        }
        if (moving() && m_movement && dt > 0) {
            const Box OLD = m_movement->previous;
            const Box NOW = m_movement->target;
            m_movement->rigidVelocity = {
                .x = (NOW.x - OLD.x) / dt / NOW.w, .y = -(NOW.y - OLD.y) / dt / NOW.h, .z = 0};
            m_movement->previous = NOW;
        }
        if (m_fallback) {
            m_fallbackTime += dt;
            if (m_fallbackTime >= FALLBACK_LENGTH) {
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
                startFallback();
                return active();
            }
            if (dt > 0) {
                m_t += dt;
                const unsigned HEAD = m_run->inputHead.load(std::memory_order_relaxed);
                if (HEAD - m_run->inputTail.load(std::memory_order_acquire) ==
                    Run::INPUT_CAPACITY) {
                    m_run->overflow.store(true, std::memory_order_release);
                } else {
                    m_run->targets[HEAD % Run::INPUT_CAPACITY] = {
                        .time = m_t, .box = playback.target};
                    m_run->inputHead.store(HEAD + 1, std::memory_order_release);
                }
            }
            // Consume snapshots only until the pair brackets the displayed time; later ones stay
            // queued for later frames.
            const double WANTED = std::max(0.0, m_t - MOVEMENT_LATENCY);
            unsigned tail = m_run->outputTail.load(std::memory_order_relaxed);
            const unsigned HEAD = m_run->outputHead.load(std::memory_order_acquire);
            const bool PROGRESSED = tail != HEAD && playback.b.time < WANTED;
            for (; tail != HEAD && playback.b.time < WANTED; ++tail) {
                playback.a = playback.b;
                playback.b = m_run->snapshots[tail % Run::OUTPUT_CAPACITY];
            }
            m_run->outputTail.store(tail, std::memory_order_release);
            playback.time =
                std::min(WANTED, playback.b.time);  // a starved worker holds the newest pose
            if (m_run->recorded.load(std::memory_order_acquire) < 2) {
                m_startupWait += dt;
                if (m_startupWait >= STALL_SECONDS ||
                    m_run->finished.load(std::memory_order_acquire)) {
                    startFallback();
                }
            } else {
                m_startupWait =
                    PROGRESSED || WANTED <= playback.b.time + 1e-8 ? 0 : m_startupWait + dt;
                playback.settling = m_t - playback.lastMotion >= 0.5;
                if (m_run->overflow.load(std::memory_order_acquire) ||
                    m_startupWait >= STALL_SECONDS ||
                    m_run->finished.load(std::memory_order_acquire) || playback.settling) {
                    startFallback();
                }
            }
            return active();
        }
        const bool FINISHED = m_run->finished.load(std::memory_order_acquire);
        const int RECORDED = m_run->recorded.load(std::memory_order_acquire);
        const bool COMPLETE = RECORDED >= m_run->frames;
        if (FINISHED && !COMPLETE) {
            startFallback();
            return active();
        }
        if (RECORDED < 2) {
            if (dt > 0) {
                m_transitionHeld = true;
            }
            if (RECORDED > m_startupRecorded) {
                m_startupRecorded = RECORDED;
                m_startupWait = 0;
            }
            m_startupWait += dt;
            if (m_startupWait >= STALL_SECONDS) {
                const bool FINISHED_NOW = m_run->finished.load(std::memory_order_acquire);
                const int RECORDED_NOW = m_run->recorded.load(std::memory_order_acquire);
                if (FINISHED_NOW && RECORDED_NOW < m_run->frames) {
                    startFallback();
                } else if (RECORDED_NOW > m_startupRecorded) {
                    m_startupRecorded = RECORDED_NOW;
                    m_startupWait = 0;
                } else {
                    startFallback();
                }
            }
            return active();
        }
        const double OLD_TIME = m_t;
        const double WANTED = std::min(OLD_TIME + (dt * m_rate), m_run->length);
        const double FRONTIER = std::min(m_run->length, ((RECORDED - 1) * DT) + DT);
        m_t = std::min(WANTED, FRONTIER);
        const bool WAITING =
            !COMPLETE && (WANTED > FRONTIER + 1e-10 || WANTED >= m_run->length - 1e-10);
        if (dt > 0) {
            m_transitionHeld = m_t <= OLD_TIME + 1e-10;
        }
        if (COMPLETE) {
            m_startupWait = 0;
        } else if (FRONTIER > OLD_TIME + 1e-10) {
            const double SERVED = std::clamp((FRONTIER - OLD_TIME) / m_rate, 0.0, dt);
            m_startupWait = WAITING ? dt - SERVED : 0;
        } else if (WAITING) {
            m_startupWait += dt;
        }
        if (WAITING && m_startupWait >= STALL_SECONDS) {
            const bool FINISHED_NOW = m_run->finished.load(std::memory_order_acquire);
            const int RECORDED_NOW = m_run->recorded.load(std::memory_order_acquire);
            if (FINISHED_NOW && RECORDED_NOW < m_run->frames) {
                startFallback();
            } else if (RECORDED_NOW > RECORDED) {
                m_startupWait = 0;
            } else {
                startFallback();
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
        m_startupRecorded = 0;
        m_transitionHeld = false;
        m_run.reset();
        m_fallback.reset();
        m_movement.reset();
    }
    bool ClothFlutter::active() const noexcept {
        return m_phase == Phase::Opening || m_phase == Phase::Movement || m_phase == Phase::Closing;
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
                                                 smoothstep(0, FALLBACK_LENGTH, m_fallbackTime))
                       : m_fallback->alpha * (1 - smoothstep(0, FALLBACK_LENGTH, m_fallbackTime));
        }
        if (m_phase == Phase::Opening) {
            return smoothstep(0, 0.08, m_t / m_run->length);
        }
        if (m_phase == Phase::Movement) {
            return m_initialOpacity + ((1 - m_initialOpacity) * smoothstep(0, 0.08, m_t));
        }
        const double OUTSIDE_TIME = m_run->outsideFrame.load(std::memory_order_acquire) * DT;
        return m_initialOpacity *
               (1 - smoothstep(OUTSIDE_TIME, std::max(OUTSIDE_TIME + DT, m_run->length), m_t));
    }
    void ClothFlutter::sampleVertices(SheetGrid &p, SheetGrid &v) const noexcept {
        if (!active() || !m_run) {
            p = restGrid();
            v = {};
            return;
        }
        if (m_fallback) {
            const double t = std::clamp(m_fallbackTime / FALLBACK_LENGTH, 0.0, 1.0);
            const double t2 = t * t;
            const double t3 = t2 * t;
            const SheetGrid REST = restGrid();
            for (std::size_t k = 0; k < p.size(); ++k) {
                const Vec3 a = m_fallback->positions[k];
                const Vec3 SPEED = m_fallback->velocities[k];
                if (m_phase != Phase::Closing) {
                    if (k < GRID) {
                        // A common convex return keeps the sleeve ordered and inside the rod even
                        // without a worker.
                        p[k] = lerp(a, REST[k], t2 * (3 - (2 * t)));
                        v[k] = mul(sub(REST[k], a), 6 * t * (1 - t) / FALLBACK_LENGTH);
                    } else {
                        p[k] = add(add(mul(a, (2 * t3) - (3 * t2) + 1),
                                       mul(SPEED, (t3 - (2 * t2) + t) * FALLBACK_LENGTH)),
                            mul(REST[k], (-2 * t3) + (3 * t2)));
                        v[k] = add(add(mul(a, ((6 * t2) - (6 * t)) / FALLBACK_LENGTH),
                                       mul(SPEED, (3 * t2) - (4 * t) + 1)),
                            mul(REST[k], ((-6 * t2) + (6 * t)) / FALLBACK_LENGTH));
                    }
                    if (m_phase == Phase::Movement && m_movement) {
                        v[k] = add(v[k], m_movement->rigidVelocity);
                    }
                } else {
                    p[k] = add(a, mul(SPEED, FALLBACK_LENGTH * (t - (0.5 * t2))));
                    v[k] = mul(SPEED, 1 - t);
                }
            }
            return;
        }
        if (m_phase == Phase::Movement && m_movement) {
            const auto &a = m_movement->a;
            const auto &b = m_movement->b;
            const double INTERVAL = std::max(DT, b.time - a.time);
            const double at = std::clamp((m_movement->time - a.time) / INTERVAL, 0.0, 1.0);
            const Vec3 SNAPSHOT_RIGID = lerp(a.rigidVelocity, b.rigidVelocity, at);
            for (std::size_t k = 0; k < p.size(); ++k) {
                const float *x = a.values.data() + k * 6;
                const float *y = b.values.data() + k * 6;
                const Vec3 p0{x[0], x[1], x[2]};
                const Vec3 p1{y[0], y[1], y[2]};
                const Vec3 v0{x[3], x[4], x[5]};
                const Vec3 v1{y[3], y[4], y[5]};
                // Snapshot velocities carry the rigid motion of their own steps; the displayed
                // sheet moves with the latest capture, which can already have started or stopped.
                v[k] = add(sub(lerp(v0, v1, at), SNAPSHOT_RIGID), m_movement->rigidVelocity);
                p[k] = lerp(p0, p1, at);
            }
            return;
        }
        m_run->sample(m_t, p, v);
        if (m_phase == Phase::Opening) {
            const double s = smoothstep(SETTLE_FROM, 1, m_t / m_run->length);
            const double d = smoothDerivative(SETTLE_FROM, 1, m_t / m_run->length) / m_run->length;
            const auto REST = restGrid();
            for (std::size_t k = 0; k < p.size(); ++k) {
                v[k] = add(mul(v[k], 1 - s), mul(sub(REST[k], p[k]), d));
                p[k] = lerp(p[k], REST[k], s);
            }
        }
        for (auto &speed : v) {
            speed = m_transitionHeld ? Vec3{} : mul(speed, m_rate);
        }
    }
    bool ClothFlutter::waitForRecording(double timeout) const noexcept {
        if (!m_run || !std::isfinite(timeout) || timeout < 0) {
            return false;
        }
        const auto DEADLINE =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
        while (!m_run->finished.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < DEADLINE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_run->recorded.load(std::memory_order_acquire) == m_run->frames;
    }
    bool ClothFlutter::waitForSnapshots(int count, double timeout) const noexcept {
        if (!m_run || count < 0 || (!m_run->live && count > m_run->frames) ||
            !std::isfinite(timeout) || timeout < 0) {
            return false;
        }
        const auto DEADLINE =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
        while (m_run->recorded.load(std::memory_order_acquire) < count &&
               !m_run->finished.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < DEADLINE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_run->recorded.load(std::memory_order_acquire) >= count;
    }
    void ClothFlutter::frame(Vec2 size, SheetFrame &out) const noexcept {
        if (!active() || !m_run) {
            flatSheet(out);
            return;
        }
        SheetGrid world;
        SheetGrid velocity;  // sampleVertices writes both
        sampleVertices(world, velocity);
        const double W = std::isfinite(size.x) && size.x > 0 ? size.x : 1;
        const double H = std::isfinite(size.y) && size.y > 0 ? size.y : 1;
        const double L = std::sqrt(W * H);
        for (auto &p : world) {
            p = {p.x * W, (-p.y * H) - (H / 2), p.z * L};
        }
        projectSheet(world,
            {.x = W, .y = H},
            m_params.shading,
            SheetLighting::ClothSmooth,
            out,
            m_params.shine);
    }
}  // namespace hyprimate
