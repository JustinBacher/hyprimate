#include "ClothFlutter.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <numbers>
#include <thread>
#include <vector>

namespace liquid_glass {
namespace {
constexpr int           GRID = PAPER_CELLS + 1;
constexpr double        DT = 1.0 / 120.0;
constexpr double        OPEN_LENGTH = 1.4;
constexpr double        SETTLE_FROM = 0.84;
constexpr double        GRAVITY = 9.81;
constexpr double        RADIUS = 0.002;
constexpr double        FALL_DAMPING = 0.15;
constexpr double        STARTUP_TIMEOUT = 0.15;
constexpr double        FALLBACK_LENGTH = 0.18;
constexpr int           MAX_OBSTACLES = 32;
constexpr std::uint64_t BASE_SEED = 0xC1074F1A77E2ULL;
std::atomic<bool>       s_live{false}, s_pause{false};
std::atomic<int>        s_pauseAfter{0};

void        silentTrace(const char *, ...) {}
Vec3        add(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3        mul(Vec3 a, double b) noexcept { return {a.x * b, a.y * b, a.z * b}; }
Vec3        sub(Vec3 a, Vec3 b) noexcept { return add(a, mul(b, -1)); }
Vec3        lerp(Vec3 a, Vec3 b, double t) noexcept { return add(mul(a, 1 - t), mul(b, t)); }
std::size_t index(int i, int j) noexcept { return std::size_t(j) * GRID + i; }
double      smooth(double a, double b, double x) noexcept {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
double smoothDerivative(double a, double b, double x) noexcept {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return 6 * t * (1 - t) / (b - a);
}
SheetGrid restGrid() noexcept {
    SheetGrid out{};
    for (int j = 0; j < GRID; ++j)
        for (int i = 0; i < GRID; ++i)
            out[index(i, j)] = {double(i) / PAPER_CELLS - 0.5, -double(j) / PAPER_CELLS, 0};
    return out;
}
bool validBox(const Box &b) noexcept {
    // Logical desktop geometry is far smaller than these bounds. Reject values that cannot safely pass through
    // float Jolt collision geometry and pixel-space projection, even when the original doubles are finite.
    return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.w) && std::isfinite(b.h) &&
           std::abs(b.x) <= 1e9 && std::abs(b.y) <= 1e9 && b.w >= 1 && b.h >= 1 && b.w <= 1e7 && b.h <= 1e7;
}
bool sanitize(ClothParams &p) noexcept {
    if (!std::isfinite(p.duration) || p.duration <= 0 || !std::isfinite(p.flutter) || !std::isfinite(p.shading) ||
        !std::isfinite(p.variation))
        return false;
    p.duration = std::clamp(p.duration, 0.1, 4.0);
    p.flutter = std::clamp(p.flutter, 0.0, 1.0);
    p.shading = std::clamp(p.shading, 0.0, 1.0);
    p.variation = std::clamp(p.variation, 0.0, 1.0);
    return true;
}
} // namespace

struct ClothFlutter::Run {
    Transition kind = Transition::Open;
    Edge       edge = Edge::Bottom;
    Box        window{}, screen{};
    double     length = OPEN_LENGTH;
    double     maxInitialSpeed = 0;
    Vec3       travel{};
    int        frames = 0;
    SheetGrid  start{}, velocity{};
    // Every six-float position/velocity slot is written once, then published by recorded's release store.
    std::vector<float> samples;
    std::atomic<int>   recorded{0}, outsideFrame{0};
    std::atomic<bool>  finished{false}, released{false};
    std::stop_source   stop;
    std::thread        worker; // only the registry reaper joins it

    double playable() const noexcept { return std::max(0, recorded.load(std::memory_order_acquire) - 1) * DT; }
    bool   pinned(int i, int j) const noexcept {
        return kind == Transition::Open ? j == 0
               : edge == Edge::Top      ? j == 0
               : edge == Edge::Left     ? i == 0
               : edge == Edge::Right    ? i == PAPER_CELLS
                                        : false;
    }
    void sample(double t, SheetGrid &p, SheetGrid &v) const noexcept {
        const int count = recorded.load(std::memory_order_acquire);
        if (count < 2) {
            p = start;
            v = velocity;
            return;
        }
        const double at = std::clamp(t / DT, 0.0, double(count - 1));
        const int    a = std::min(int(at), count - 2), b = a + 1;
        const double u = at - a, u2 = u * u, u3 = u2 * u;
        const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u;
        const double h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
        const double d00 = (6 * u2 - 6 * u) / DT, d10 = 3 * u2 - 4 * u + 1;
        const double d01 = (-6 * u2 + 6 * u) / DT, d11 = 3 * u2 - 2 * u;
        for (std::size_t k = 0; k < p.size(); ++k) {
            const float *s0 = samples.data() + (std::size_t(a) * p.size() + k) * 6;
            const float *s1 = samples.data() + (std::size_t(b) * p.size() + k) * 6;
            const Vec3   p0{s0[0], s0[1], s0[2]}, v0{s0[3], s0[4], s0[5]};
            const Vec3   p1{s1[0], s1[1], s1[2]}, v1{s1[3], s1[4], s1[5]};
            p[k] = add(add(mul(p0, h00), mul(v0, h10 * DT)), add(mul(p1, h01), mul(v1, h11 * DT)));
            v[k] = add(add(mul(p0, d00), mul(v0, d10)), add(mul(p1, d01), mul(v1, d11)));
            const double extra = std::clamp(t - (count - 1) * DT, 0.0, DT);
            p[k] = add(p[k], mul(v[k], extra));
        }
    }
};

struct ClothFlutter::Fallback {
    SheetGrid positions{}, velocities{}; // real-time velocity
    double    alpha = 1;
};

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
    Registry() : reaper([this](std::stop_token stop) { reap(stop); }) {}
    ~Registry() {
        reaper.request_stop();
        changed.notify_all();
        reaper.join();
        for (auto &run : runs)
            run->stop.request_stop();
        for (auto &run : runs)
            if (run->worker.joinable())
                run->worker.join();
        runs.clear();
    }
    void add(const std::shared_ptr<Run> &run) {
        std::lock_guard lock(mutex);
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
                if (done->worker.joinable())
                    done->worker.join();
                done.reset(); // potentially large recording is freed here, never on playback's last-reference path
                lock.lock();
                it = runs.begin(); // add() may have reallocated the vector while the lock was released
            }
        }
    }
    std::mutex                        mutex;
    std::condition_variable           changed;
    std::vector<std::shared_ptr<Run>> runs;
    std::jthread                      reaper;
};
std::unique_ptr<Registry> s_registry;

bool outside(const Run &run, const JPH::Array<JPH::SoftBodyVertex> &vertices) noexcept {
    const double W = run.window.w, H = run.window.h, L = std::sqrt(W * H), f = 3 * L;
    for (const auto &v : vertices) {
        const double scale = f / std::max(0.25 * f, f - v.mPosition.GetZ() * L);
        const double x = run.window.x + W / 2 + v.mPosition.GetX() * W * scale;
        const double y = run.window.y + H / 2 + (-v.mPosition.GetY() * H - H / 2) * scale;
        if ((run.edge == Edge::Bottom && y <= run.screen.y + run.screen.h) ||
            (run.edge == Edge::Top && y >= run.screen.y) || (run.edge == Edge::Left && x >= run.screen.x) ||
            (run.edge == Edge::Right && x <= run.screen.x + run.screen.w))
            return false;
    }
    return true;
}
bool record(Run &run, int frame, const JPH::Array<JPH::SoftBodyVertex> &vertices) noexcept {
    const double travel = std::hypot(run.travel.x, run.travel.y);
    const double positionLimit = 16 + travel + GRAVITY * run.length * run.length + run.maxInitialSpeed * run.length;
    const double velocityLimit = 50 + 8 * travel / run.length + 3 * GRAVITY * run.length + 1.5 * run.maxInitialSpeed;
    for (const auto &v : vertices) {
        if (!std::isfinite(v.mPosition.GetX()) || !std::isfinite(v.mPosition.GetY()) ||
            !std::isfinite(v.mPosition.GetZ()) || !std::isfinite(v.mVelocity.GetX()) ||
            !std::isfinite(v.mVelocity.GetY()) || !std::isfinite(v.mVelocity.GetZ()) ||
            std::max({std::abs(v.mPosition.GetX()), std::abs(v.mPosition.GetY()), std::abs(v.mPosition.GetZ())}) >
                positionLimit ||
            std::max({std::abs(v.mVelocity.GetX()), std::abs(v.mVelocity.GetY()), std::abs(v.mVelocity.GetZ())}) >
                velocityLimit)
            return false;
    }
    float *p = run.samples.data() + std::size_t(frame) * PAPER_GRID_VERTICES * 6;
    for (const auto &v : vertices) {
        *p++ = v.mPosition.GetX();
        *p++ = v.mPosition.GetY();
        *p++ = v.mPosition.GetZ();
        *p++ = v.mVelocity.GetX();
        *p++ = v.mVelocity.GetY();
        *p++ = v.mVelocity.GetZ();
    }
    if (run.kind == Transition::Close && run.outsideFrame.load(std::memory_order_relaxed) == run.frames &&
        outside(run, vertices))
        run.outsideFrame.store(frame, std::memory_order_release);
    run.recorded.store(frame + 1, std::memory_order_release);
    return true;
}
struct SimInput {
    std::vector<Box> obstacles;
    double           flutter = 0;
    double           phaseA = 0, phaseB = 0, freqA = 0, freqB = 0;
};

void simulate(Run &run, const SimInput &in) {
    using namespace JPH;
    const auto stopped = [&] { return run.stop.stop_requested(); };
    const auto waitPublication = [&] {
        while (s_pause.load(std::memory_order_acquire) &&
               run.recorded.load(std::memory_order_acquire) >= s_pauseAfter.load(std::memory_order_relaxed) &&
               !stopped())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return !stopped();
    };
    if (!waitPublication())
        return;
    run.samples.resize(std::size_t(run.frames) * PAPER_GRID_VERTICES * 6);
    enum : ObjectLayer { STATIC = 0, CLOTH = 1, LAYERS = 2 };
    ObjectLayerPairFilterTable pairs(LAYERS);
    pairs.EnableCollision(STATIC, CLOTH);
    BroadPhaseLayerInterfaceTable broadPhase(LAYERS, 2);
    broadPhase.MapObjectToBroadPhaseLayer(STATIC, BroadPhaseLayer(0));
    broadPhase.MapObjectToBroadPhaseLayer(CLOTH, BroadPhaseLayer(1));
    ObjectVsBroadPhaseLayerFilterTable objectVsBroadPhase(broadPhase, 2, pairs, LAYERS);
    TempAllocatorImpl                  temp(8 << 20);
    JobSystemSingleThreaded            jobs(cMaxPhysicsJobs);
    PhysicsSystem                      system;
    if (stopped())
        return;
    system.Init(MAX_OBSTACLES + 4, 0, 1024, 1024, broadPhase, objectVsBroadPhase, pairs);
    system.SetGravity(JPH::Vec3(0, float(-GRAVITY), 0));
    BodyInterface &bodies = system.GetBodyInterfaceNoLock();
    const auto     addBox = [&](JPH::Vec3 half, JPH::Vec3 centre) {
        bodies.CreateAndAddBody(BodyCreationSettings(new BoxShape(half, 0.0F), RVec3(centre), Quat::sIdentity(),
                                                     EMotionType::Static, STATIC),
                                EActivation::DontActivate);
    };
    addBox(JPH::Vec3(100, 100, 1), JPH::Vec3(0, 0, float(-1 - 2 * RADIUS)));
    for (const Box &o : in.obstacles)
        addBox(JPH::Vec3(float(o.w / 2), float(o.h / 2), 0.515F),
               JPH::Vec3(float(o.x + o.w / 2), float(o.y + o.h / 2), -0.485F));
    if (stopped())
        return;
    Ref<SoftBodySharedSettings> settings = new SoftBodySharedSettings;
    const SheetGrid             rest = restGrid();
    // Keep the wall behind the exact display plane: a flat, fully free sheet must not start embedded by its radius.
    // Four times the inverse mass preserves areal density when a coarse cell becomes four simulated cells.
    // Compliance scales with inverse mass to preserve XPBD alpha/inverse-mass ratios. At 57x57 a nonzero
    // stretch/shear compliance is essential: perfectly rigid local constraints amplify floating-point noise
    // in an unanchored sheet. Long-range attachments keep opening and pulling inextensible at sheet scale.
    constexpr float INV_MASS = 4.0F, BEND_COMPLIANCE = 4e-2F;
    for (int j = 0; j < GRID; ++j)
        for (int i = 0; i < GRID; ++i) {
            const Vec3 &p = rest[index(i, j)];
            settings->mVertices.emplace_back(Float3(float(p.x), float(p.y), float(p.z)), Float3(0, 0, 0),
                                             run.pinned(i, j) ? 0.0F : INV_MASS);
        }
    for (int j = 0; j < PAPER_CELLS; ++j)
        for (int i = 0; i < PAPER_CELLS; ++i) {
            const auto a = uint32(index(i, j)), b = uint32(index(i + 1, j)), c = uint32(index(i, j + 1)),
                       d = uint32(index(i + 1, j + 1));
            settings->AddFace(SoftBodySharedSettings::Face(a, c, b));
            settings->AddFace(SoftBodySharedSettings::Face(b, c, d));
        }
    const bool                                     falling = run.kind == Transition::Close && run.edge == Edge::Bottom;
    const SoftBodySharedSettings::VertexAttributes attributes(
        1e-4F, 1e-4F, BEND_COMPLIANCE,
        falling ? SoftBodySharedSettings::ELRAType::None : SoftBodySharedSettings::ELRAType::GeodesicDistance);
    settings->CreateConstraints(&attributes, 1, SoftBodySharedSettings::EBendType::Dihedral);
    settings->Optimize();
    if (stopped())
        return;
    SoftBodyCreationSettings creation(settings, RVec3::sZero(), Quat::sIdentity(), CLOTH);
    creation.mUpdatePosition = false;
    creation.mAllowSleeping = false;
    creation.mNumIterations = 8;
    creation.mVertexRadius = float(RADIUS);
    creation.mLinearDamping = falling ? float(FALL_DAMPING) : 1.5F;
    creation.mFriction = 0.3F;
    creation.mMaxLinearVelocity =
        float(std::max(500.0, 1.5 * run.maxInitialSpeed + 8 * std::hypot(run.travel.x, run.travel.y) / run.length));
    creation.mFacesDoubleSided = true;
    const BodyID id = bodies.CreateAndAddSoftBody(creation, EActivation::Activate);
    if (id.IsInvalid())
        return;
    auto *motion = static_cast<SoftBodyMotionProperties *>(
        system.GetBodyLockInterfaceNoLock().TryGetBody(id)->GetMotionProperties());
    auto &vertices = motion->GetVertices();
    for (std::size_t k = 0; k < rest.size(); ++k) {
        const auto p = run.start[k], v = run.velocity[k];
        vertices[k].mPosition = JPH::Vec3(float(p.x), float(p.y), float(p.z));
        vertices[k].mVelocity = JPH::Vec3(float(v.x), float(v.y), float(v.z));
    }
    if (!record(run, 0, vertices)) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
        return;
    }
    for (int step = 1; step < run.frames && waitPublication(); ++step) {
        const double time = (step - 1) * DT;
        if (run.kind == Transition::Open)
            motion->SetLinearDamping(float(1.5 + 8 * smooth(0.65, 0.95, time / run.length)));
        for (int j = 0; j < GRID; ++j)
            for (int i = 0; i < GRID; ++i) {
                const auto k = index(i, j);
                auto      &v = vertices[k];
                if (run.pinned(i, j)) {
                    Vec3 target = run.start[k];
                    if (run.kind == Transition::Close) {
                        const double t = std::clamp((time + DT) / run.length, 0.0, 1.0);
                        const double distance = t * t * t * (10 + t * (-15 + 6 * t));
                        target = add(target, add(mul(run.travel, distance),
                                                 mul(run.velocity[k], (time + DT) * (1 - t) * (1 - t))));
                    }
                    const JPH::Vec3 p(float(target.x), float(target.y), float(target.z));
                    v.mVelocity = (p - v.mPosition) / float(DT);
                } else if (in.flutter > 0) {
                    const auto   at = [&](int x, int y) { return vertices[index(x, y)].mPosition; };
                    const auto   dx = at(std::min(i + 1, PAPER_CELLS), j) - at(std::max(i - 1, 0), j);
                    const auto   dy = at(i, std::min(j + 1, PAPER_CELLS)) - at(i, std::max(j - 1, 0));
                    const auto   n = dx.Cross(dy).NormalizedOr(JPH::Vec3::sAxisZ());
                    const double wave =
                        std::sin(in.freqA * time + 2 * std::numbers::pi * v.mPosition.GetX() + in.phaseA) *
                        std::sin(in.freqB * time + 3 * std::numbers::pi * v.mPosition.GetY() + in.phaseB);
                    v.mVelocity += n * float(3 * in.flutter * (0.55 + 0.45 * wave) * DT * n.GetZ());
                }
            }
        if (system.Update(float(DT), 1, &temp, &jobs) != EPhysicsUpdateError::None)
            break;
        if (!record(run, step, vertices))
            break;
    }
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
}
} // namespace

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

void ClothFlutter::begin(Transition kind, const ClothParams &params, std::uint64_t seed, const Box &window,
                         std::span<const Box> obstacles, std::optional<ClothCloseGeometry> geometry) {
    ClothParams p = params;
    if ((kind != Transition::Open && kind != Transition::Close) || !sanitize(p) ||
        !s_live.load(std::memory_order_acquire) || !validBox(window) ||
        (geometry &&
         (!validBox(geometry->screen) || (geometry->edge != Edge::Top && geometry->edge != Edge::Bottom &&
                                          geometry->edge != Edge::Left && geometry->edge != Edge::Right)))) {
        reset();
        return;
    }
    auto run = std::make_shared<Run>();
    run->kind = kind;
    run->window = window;
    run->screen = geometry ? geometry->screen : Box{window.x, window.y, window.w, window.h};
    run->edge = geometry ? geometry->edge : Edge::Bottom;
    const double oldOpacity = opacity();
    if (kind == Transition::Close && m_phase == Phase::Opening && m_run)
        sampleVertices(run->start, run->velocity);
    else
        run->start = restGrid();
    if (kind == Transition::Open) {
        constexpr double angle = 80 * std::numbers::pi / 180;
        for (Vec3 &v : run->start) {
            v.z = -v.y * std::sin(angle);
            v.y *= std::cos(angle);
        }
    } else {
        const double W = window.w, H = window.h;
        if (run->edge == Edge::Bottom) {
            double top = 0, upward = 0;
            for (std::size_t k = 0; k < run->start.size(); ++k) {
                top = std::max(top, run->start[k].y);
                upward = std::max(upward, run->velocity[k].y);
            }
            const double distance = std::max(0.0, (run->screen.y + run->screen.h - window.y) / H) + top + 0.5;
            // Conservative gravity-only flight time with damping; no downward kinematic drive or attachments.
            double       t = 1.1;
            const double simUp = upward * p.duration / 1.1;
            while (t < 12 &&
                   GRAVITY / FALL_DAMPING * (t - (1 - std::exp(-FALL_DAMPING * t)) / FALL_DAMPING) - simUp * t <
                       distance)
                t += DT;
            run->length = t;
        } else {
            run->length = 1.4;
            if (run->edge == Edge::Top)
                run->travel.y = std::max(0.0, (window.y - run->screen.y) / H) + 2.5;
            if (run->edge == Edge::Left)
                run->travel.x = -(std::max(0.0, (window.x - run->screen.x) / W) + 2.5);
            if (run->edge == Edge::Right)
                run->travel.x = std::max(0.0, (run->screen.x + run->screen.w - window.x - W) / W) + 2.5;
        }
    }
    run->frames = int(std::ceil(run->length / DT)) + 1;
    run->length = (run->frames - 1) * DT;
    const double rate = run->length / p.duration;
    for (auto &v : run->velocity) {
        v = mul(v, 1 / rate); // displayed real-time velocity -> new simulation clock
        run->maxInitialSpeed = std::max(run->maxInitialSpeed, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
    }
    std::uint64_t base = BASE_SEED, random = seed;
    const auto    draw = [&] {
        const double b = double(splitmix64(base) >> 11) * 0x1.0p-53;
        const double r = double(splitmix64(random) >> 11) * 0x1.0p-53;
        return b + p.variation * (r - b);
    };
    SimInput input;
    input.flutter = p.flutter;
    input.phaseA = 2 * std::numbers::pi * draw();
    input.phaseB = 2 * std::numbers::pi * draw();
    input.freqA = 9 + 4 * draw();
    input.freqB = 6 + 3 * draw();
    std::vector<std::pair<double, Box>> nearest;
    for (const Box &o : obstacles) {
        if (!validBox(o) ||
            (o.x < window.x + window.w && o.x + o.w > window.x && o.y < window.y + window.h && o.y + o.h > window.y))
            continue;
        // A closing sheet must clear the screen. Slabs directly across its exit corridor would trap a
        // zero-flutter sheet in the desktop plane; omit those while retaining obstacles beside that corridor.
        if (kind == Transition::Close) {
            const bool overlapX = o.x < window.x + window.w && o.x + o.w > window.x;
            const bool overlapY = o.y < window.y + window.h && o.y + o.h > window.y;
            if ((run->edge == Edge::Bottom && overlapX && o.y >= window.y + window.h) ||
                (run->edge == Edge::Top && overlapX && o.y + o.h <= window.y) ||
                (run->edge == Edge::Left && overlapY && o.x + o.w <= window.x) ||
                (run->edge == Edge::Right && overlapY && o.x >= window.x + window.w))
                continue;
        }
        const double dx = std::max({window.x - o.x - o.w, o.x - window.x - window.w, 0.0});
        const double dy = std::max({window.y - o.y - o.h, o.y - window.y - window.h, 0.0});
        nearest.push_back({std::hypot(dx, dy),
                           {(o.x - window.x - window.w / 2) / window.w, -(o.y + o.h - window.y) / window.h,
                            o.w / window.w, o.h / window.h}});
    }
    std::stable_sort(nearest.begin(), nearest.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (std::size_t k = 0; k < std::min(nearest.size(), std::size_t(MAX_OBSTACLES)); ++k)
        input.obstacles.push_back(nearest[k].second);
    run->outsideFrame.store(run->frames, std::memory_order_relaxed);
    try {
        run->worker = std::thread([run, input = std::move(input)] {
            try {
                simulate(*run, input);
            } catch (...) {
            }
            run->finished.store(true, std::memory_order_release);
        });
        s_registry->add(run);
        auto lease = std::make_shared<Lease>();
        lease->run = run;
        m_run = std::shared_ptr<Run>(lease, run.get());
    } catch (...) {
        run->stop.request_stop();
        if (run->worker.joinable())
            run->worker.join();
        reset();
        return;
    }
    m_params = p;
    m_rate = rate;
    m_t = 0;
    m_startupWait = 0;
    m_fallbackTime = 0;
    m_fallback.reset();
    m_initialOpacity = kind == Transition::Close ? oldOpacity : 1;
    m_phase = kind == Transition::Open ? Phase::Opening : Phase::Closing;
}

void ClothFlutter::startFallback() noexcept {
    try {
        auto fallback = std::make_shared<Fallback>();
        sampleVertices(fallback->positions, fallback->velocities);
        fallback->alpha = opacity();
        m_fallback = std::move(fallback);
        m_fallbackTime = 0;
        m_run->stop.request_stop();
    } catch (...) {
        if (m_phase == Phase::Opening)
            reset();
        else
            m_phase = Phase::Closed;
    }
}
bool ClothFlutter::advance(double dt) noexcept {
    if (!active())
        return false;
    if (!std::isfinite(dt) || dt < 0 || dt > 0.25) {
        if (m_phase == Phase::Opening)
            reset();
        else {
            m_phase = Phase::Closed;
            m_run.reset();
        }
        return false;
    }
    if (m_fallback) {
        m_fallbackTime += dt;
        if (m_fallbackTime >= FALLBACK_LENGTH) {
            if (m_phase == Phase::Opening)
                reset();
            else {
                m_phase = Phase::Closed;
                m_run.reset();
                m_fallback.reset();
            }
        }
        return active();
    }
    if (m_run->recorded.load(std::memory_order_acquire) < 2) {
        m_startupWait += dt;
        if (m_startupWait >= STARTUP_TIMEOUT || m_run->finished.load(std::memory_order_acquire))
            startFallback();
        return active();
    }
    const double wanted = std::min(m_t + dt * m_rate, m_run->length);
    const double available = m_run->playable();
    m_t = std::min(wanted, available + DT);
    if (wanted > available + DT + 1e-10)
        startFallback();
    else if (m_t >= m_run->length - 1e-10) {
        if (m_phase == Phase::Opening)
            reset();
        else {
            m_phase = Phase::Closed;
            m_run.reset();
        }
    }
    return active();
}
void ClothFlutter::reset() noexcept {
    m_phase = Phase::Idle;
    m_t = 0;
    m_run.reset();
    m_fallback.reset();
}
bool   ClothFlutter::active() const noexcept { return m_phase == Phase::Opening || m_phase == Phase::Closing; }
bool   ClothFlutter::closed() const noexcept { return m_phase == Phase::Closed; }
bool   ClothFlutter::shaded() const noexcept { return m_params.shading > 0; }
double ClothFlutter::opacity() const noexcept {
    if (m_phase == Phase::Idle)
        return 1;
    if (m_phase == Phase::Closed)
        return 0;
    if (m_fallback)
        return m_phase == Phase::Opening
                   ? m_fallback->alpha + (1 - m_fallback->alpha) * smooth(0, FALLBACK_LENGTH, m_fallbackTime)
                   : m_fallback->alpha * (1 - smooth(0, FALLBACK_LENGTH, m_fallbackTime));
    if (m_phase == Phase::Opening)
        return smooth(0, 0.08, m_t / m_run->length);
    const double outsideTime = m_run->outsideFrame.load(std::memory_order_acquire) * DT;
    return m_initialOpacity * (1 - smooth(outsideTime, std::max(outsideTime + DT, m_run->length), m_t));
}
void ClothFlutter::sampleVertices(SheetGrid &p, SheetGrid &v) const noexcept {
    if (!active() || !m_run) {
        p = restGrid();
        v = {};
        return;
    }
    if (m_fallback) {
        const double    t = std::clamp(m_fallbackTime / FALLBACK_LENGTH, 0.0, 1.0), t2 = t * t, t3 = t2 * t;
        const SheetGrid rest = restGrid();
        for (std::size_t k = 0; k < p.size(); ++k) {
            const Vec3 a = m_fallback->positions[k], speed = m_fallback->velocities[k];
            if (m_phase == Phase::Opening) {
                p[k] = add(add(mul(a, 2 * t3 - 3 * t2 + 1), mul(speed, (t3 - 2 * t2 + t) * FALLBACK_LENGTH)),
                           mul(rest[k], -2 * t3 + 3 * t2));
                v[k] = add(add(mul(a, (6 * t2 - 6 * t) / FALLBACK_LENGTH), mul(speed, 3 * t2 - 4 * t + 1)),
                           mul(rest[k], (-6 * t2 + 6 * t) / FALLBACK_LENGTH));
            } else {
                p[k] = add(a, mul(speed, FALLBACK_LENGTH * (t - 0.5 * t2)));
                v[k] = mul(speed, 1 - t);
            }
        }
        return;
    }
    m_run->sample(m_t, p, v);
    if (m_phase == Phase::Opening) {
        const double s = smooth(SETTLE_FROM, 1, m_t / m_run->length);
        const double d = smoothDerivative(SETTLE_FROM, 1, m_t / m_run->length) / m_run->length;
        const auto   rest = restGrid();
        for (std::size_t k = 0; k < p.size(); ++k) {
            v[k] = add(mul(v[k], 1 - s), mul(sub(rest[k], p[k]), d));
            p[k] = lerp(p[k], rest[k], s);
        }
    }
    for (auto &speed : v)
        speed = mul(speed, m_rate);
}
bool ClothFlutter::waitForRecording(double timeout) const noexcept {
    if (!m_run || !std::isfinite(timeout) || timeout < 0)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (!m_run->finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return m_run->recorded.load(std::memory_order_acquire) == m_run->frames;
}
bool ClothFlutter::waitForSnapshots(int count, double timeout) const noexcept {
    if (!m_run || count < 0 || count > m_run->frames || !std::isfinite(timeout) || timeout < 0)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (m_run->recorded.load(std::memory_order_acquire) < count &&
           !m_run->finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return m_run->recorded.load(std::memory_order_acquire) >= count;
}
void ClothFlutter::facets(Vec2 size, PaperFacets &out) const noexcept {
    if (!active() || !m_run) {
        flatSheetFacets(out);
        return;
    }
    SheetGrid world{}, velocity{};
    sampleVertices(world, velocity);
    const double W = std::isfinite(size.x) && size.x > 0 ? size.x : 1,
                 H = std::isfinite(size.y) && size.y > 0 ? size.y : 1;
    const double L = std::sqrt(W * H);
    for (auto &p : world)
        p = {p.x * W, -p.y * H - H / 2, p.z * L};
    sheetFacets(world, {W, H}, m_params.shading, out, SheetLighting::ClothSmooth);
}
} // namespace liquid_glass
