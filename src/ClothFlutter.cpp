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
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

namespace liquid_glass {
namespace {

// The cloth is simulated on a coarser grid than it is drawn: every sim cell is drawn as 2 x 2 sheet cells, the
// in-between vertices filled in by cubic subdivision.
constexpr int         SIM_CELLS = PAPER_CELLS / 2;
constexpr int         SIM_GRID = SIM_CELLS + 1;
constexpr std::size_t SIM_VERTICES = std::size_t(SIM_GRID) * SIM_GRID;
static_assert(PAPER_CELLS == 2 * SIM_CELLS);
constexpr int SHEET_GRID = PAPER_CELLS + 1;

constexpr double MIN_DURATION = 0.1;
constexpr double MAX_DURATION = 4.0;
constexpr double MAX_STEP = 0.25; // seconds; larger or invalid steps abort the animation
// Longest step actually played; a compositor stall delays the cloth instead of jumping it (as PaperCrumple).
constexpr double        MAX_PLAYED_STEP = 1.0 / 30.0;
constexpr std::uint64_t BASE_SEED = 0xC1074F1A77E2ULL;

// Simulated length of each animation; `duration` plays it faster or slower.
constexpr double OPEN_LENGTH = 1.4;
constexpr double CLOSE_LENGTH = 1.1;
constexpr int    STEP_HZ = 120; // solver steps per simulated second
constexpr int    FRAME_HZ = 60; // recorded frames per simulated second
constexpr int    STEPS_PER_FRAME = STEP_HZ / FRAME_HZ;
static_assert(STEP_HZ % FRAME_HZ == 0);

// Opening: fades in over the first share, then from SETTLE_FROM pulls every vertex onto its rest place, so the open
// ends exactly on the flat window whatever the cloth was still doing.
constexpr double OPEN_FADE_END = 0.08;
constexpr double SETTLE_FROM = 0.7;
// Closing: the roll reaches the top at ROLL_END and the cloth fades from CLOSE_FADE_FROM.
constexpr double ROLL_END = 0.85;
constexpr double CLOSE_FADE_FROM = 0.78;
// The roll takes up cloth as progress^SLACK_POWER while it climbs with progress, so it lags and the cloth between the
// top edge and the roll buckles out.
constexpr double SLACK_POWER = 1.6;
// Kinematic (wound) vertices close on their place on the roll with this time constant instead of jumping to it.
constexpr double CATCH_TIME = 0.03;

// Sheet units: the cloth has unit area, so every window, whatever its size, falls and flutters alike.
// Roll: innermost radius and the gap between layers, relative to the sheet height.
constexpr double ROLL_INNER = 0.045;
constexpr double ROLL_GAP = 0.015;
constexpr double GRAVITY = 9.81;
constexpr double VERTEX_RADIUS = 0.004;
constexpr double OBSTACLE_DEPTH = 0.03; // how far other windows stand out from the desktop
constexpr int    MAX_OBSTACLES = 32;
// Gust acceleration at flutter 1, pushing the cloth off the desktop wherever it faces the viewer.
constexpr double GUST = 3.0;

constexpr float BEND_COMPLIANCE = 1e-2F;
constexpr float LINEAR_DAMPING = 1.5F;
constexpr float FRICTION = 0.3F;
constexpr int   ITERATIONS = 8;

std::atomic<bool> s_physicsLive{false};

// Jolt's warnings would go to stdout inside the compositor.
void silentTrace(const char *, ...) {}

double clampUnit(double v) noexcept { return std::clamp(v, 0.0, 1.0); }

double smoothstep(double a, double b, double x) noexcept {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double unit(std::uint64_t &state) noexcept { return static_cast<double>(splitmix64(state) >> 11) * 0x1.0p-53; }

bool sanitize(ClothParams &p) noexcept {
    if (!std::isfinite(p.duration) || !std::isfinite(p.flutter) || !std::isfinite(p.shading) ||
        !std::isfinite(p.variation))
        return false;
    if (!(p.duration > 0.0))
        return false;
    p.duration = std::clamp(p.duration, MIN_DURATION, MAX_DURATION);
    p.flutter = clampUnit(p.flutter);
    p.shading = clampUnit(p.shading);
    p.variation = clampUnit(p.variation);
    return true;
}

std::size_t simIndex(int i, int j) noexcept { return std::size_t(j) * SIM_GRID + std::size_t(i); }

// Sim vertices in sheet units: x right, y up (0 at the top edge, -h at the bottom), z towards the viewer.
using SimGrid = std::array<Vec3, SIM_VERTICES>;

Vec3 lerp(Vec3 a, Vec3 b, double t) noexcept { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

// A roll wound from the cloth's bottom edge, touching the desktop at (contactY, 0) with the unwound cloth running up
// the desktop from there. `wound` is the length on the roll; `along` how far a point lies inside it from the contact.
// Returns the point's (y, z).
Vec2 rollPoint(double contactY, double wound, double along, double h) noexcept {
    const double inner = ROLL_INNER * h;
    const double b = ROLL_GAP * h / (2.0 * std::numbers::pi); // radius lost per radian
    const double turns = (-inner + std::sqrt(inner * inner + 2.0 * b * wound)) / b;
    const double outer = inner + b * turns;
    along = std::clamp(along, 0.0, wound);
    // Arc length from the outer end: outer * psi - b * psi^2 / 2.
    const double psi = (outer - std::sqrt(std::max(0.0, outer * outer - 2.0 * b * along))) / b;
    const double r = outer - b * psi;
    return {contactY - r * std::sin(psi), outer - r * std::cos(psi)};
}

struct Gusts {
    double phaseA = 0, phaseB = 0, freqA = 0, freqB = 0;
};

} // namespace

struct ClothFlutter::Run {
    Transition         kind = Transition::Open;
    double             length = 0;  // simulated seconds
    double             w = 1, h = 1; // sheet size, unit area
    int                frames = 0;
    std::vector<float> positions;   // frames * SIM_VERTICES * 3, written by the worker before it publishes
    std::atomic<int>   recorded{0}; // frames [0, recorded) are final
    std::atomic<bool>  finished{false};
    std::jthread       worker; // last: joined before the recording is freed

    double playable() const noexcept {
        const int n = recorded.load(std::memory_order_acquire);
        return n == frames ? length : double(std::max(n - 1, 0)) / FRAME_HZ;
    }

    Vec3 vertex(int frame, std::size_t v) const noexcept {
        const float *p = positions.data() + (std::size_t(frame) * SIM_VERTICES + v) * 3;
        return {p[0], p[1], p[2]};
    }

    // Recorded shape at simulated time t, Catmull-Rom between frames so vertex velocities stay continuous.
    void sample(double t, SimGrid &out) const noexcept {
        const int    last = std::max(recorded.load(std::memory_order_acquire) - 1, 0);
        const double at = std::clamp(t * FRAME_HZ, 0.0, double(last));
        const int    f0 = std::min(int(at), std::max(last - 1, 0));
        const int    f1 = std::min(f0 + 1, last);
        const int    fBack = std::max(f0 - 1, 0);
        const int    fNext = std::min(f0 + 2, last);
        const double u = std::min(at - f0, 1.0), u2 = u * u, u3 = u2 * u;
        const double w0 = 0.5 * (-u + 2.0 * u2 - u3);
        const double w1 = 0.5 * (2.0 - 5.0 * u2 + 3.0 * u3);
        const double w2 = 0.5 * (u + 4.0 * u2 - 3.0 * u3);
        const double w3 = 0.5 * (-u2 + u3);
        for (std::size_t v = 0; v < SIM_VERTICES; ++v) {
            const Vec3 a = vertex(fBack, v), b = vertex(f0, v), c = vertex(f1, v), d = vertex(fNext, v);
            out[v] = {w0 * a.x + w1 * b.x + w2 * c.x + w3 * d.x, w0 * a.y + w1 * b.y + w2 * c.y + w3 * d.y,
                      w0 * a.z + w1 * b.z + w2 * c.z + w3 * d.z};
        }
    }

    // The flat cloth, just off the desktop.
    SimGrid rest() const noexcept {
        SimGrid out{};
        for (int j = 0; j < SIM_GRID; ++j)
            for (int i = 0; i < SIM_GRID; ++i)
                out[simIndex(i, j)] = {(double(i) / SIM_CELLS - 0.5) * w, -double(j) / SIM_CELLS * h, 0.0};
        return out;
    }
};

namespace {

using Run = ClothFlutter::Run;

void record(Run &run, int frame, const JPH::Array<JPH::SoftBodyVertex> &vertices) noexcept {
    float *p = run.positions.data() + std::size_t(frame) * SIM_VERTICES * 3;
    for (const auto &v : vertices) {
        *p++ = v.mPosition.GetX();
        *p++ = v.mPosition.GetY();
        *p++ = v.mPosition.GetZ();
    }
    run.recorded.store(frame + 1, std::memory_order_release);
}

struct SimInput {
    SimGrid          start{};
    SimGrid          velocity{};
    std::vector<Box> obstacles; // sheet units, y up: x, y = left, bottom
    double           flutter = 0;
    double           tilt = 0; // [-1, 1]: which side of the roll leads
    Gusts            gusts{};
};

void simulate(Run &run, const SimInput &in, std::stop_token stop) noexcept {
    using namespace JPH;
    enum : ObjectLayer { STATIC = 0, CLOTH = 1, LAYERS = 2 };
    ObjectLayerPairFilterTable pairs(LAYERS);
    pairs.EnableCollision(STATIC, CLOTH);
    BroadPhaseLayerInterfaceTable broadPhase(LAYERS, 2);
    broadPhase.MapObjectToBroadPhaseLayer(STATIC, BroadPhaseLayer(0));
    broadPhase.MapObjectToBroadPhaseLayer(CLOTH, BroadPhaseLayer(1));
    ObjectVsBroadPhaseLayerFilterTable objectVsBroadPhase(broadPhase, 2, pairs, LAYERS);
    TempAllocatorImpl                  temp(4 << 20);
    JobSystemSingleThreaded            jobs(cMaxPhysicsJobs);
    PhysicsSystem                      system;
    if (stop.stop_requested())
        return;
    system.Init(MAX_OBSTACLES + 4, 0, 1024, 1024, broadPhase, objectVsBroadPhase, pairs);
    system.SetGravity(JPH::Vec3(0, float(-GRAVITY), 0));
    BodyInterface &bodies = system.GetBodyInterfaceNoLock();

    // The desktop: a wall filling z < 0.
    const auto addBox = [&](JPH::Vec3 halfExtent, JPH::Vec3 centre) {
        bodies.CreateAndAddBody(BodyCreationSettings(new BoxShape(halfExtent, 0.0F), RVec3(centre), Quat::sIdentity(),
                                                     EMotionType::Static, STATIC),
                                EActivation::DontActivate);
    };
    addBox(JPH::Vec3(100, 100, 1), JPH::Vec3(0, 0, -1));
    for (const Box &o : in.obstacles) {
        const float depth = float(OBSTACLE_DEPTH) + 1.0F;
        addBox(JPH::Vec3(float(o.w / 2), float(o.h / 2), depth / 2),
               JPH::Vec3(float(o.x + o.w / 2), float(o.y + o.h / 2), float(OBSTACLE_DEPTH) - depth / 2));
    }

    if (stop.stop_requested())
        return;
    Ref<SoftBodySharedSettings> settings = new SoftBodySharedSettings;
    const SimGrid               rest = run.rest();
    for (int j = 0; j < SIM_GRID; ++j)
        for (int i = 0; i < SIM_GRID; ++i) {
            const Vec3 &p = rest[simIndex(i, j)];
            settings->mVertices.emplace_back(Float3(float(p.x), float(p.y), float(p.z)), Float3(0, 0, 0),
                                             j == 0 ? 0.0F : 1.0F);
        }
    for (int j = 0; j < SIM_CELLS; ++j)
        for (int i = 0; i < SIM_CELLS; ++i) {
            const auto a = uint32(simIndex(i, j)), b = uint32(simIndex(i + 1, j)), c = uint32(simIndex(i, j + 1)),
                       d = uint32(simIndex(i + 1, j + 1));
            settings->AddFace(SoftBodySharedSettings::Face(a, c, d));
            settings->AddFace(SoftBodySharedSettings::Face(a, d, b));
        }
    // Inextensible: stiff edges, and long-range attachments to the pinned top edge keep it from sagging.
    const SoftBodySharedSettings::VertexAttributes attributes(0.0F, 0.0F, BEND_COMPLIANCE,
                                                              SoftBodySharedSettings::ELRAType::GeodesicDistance);
    settings->CreateConstraints(&attributes, 1, SoftBodySharedSettings::EBendType::Dihedral);
    settings->Optimize();
    if (stop.stop_requested())
        return;

    SoftBodyCreationSettings creation(settings, RVec3::sZero(), Quat::sIdentity(), CLOTH);
    creation.mUpdatePosition = false; // vertex positions stay in world space
    creation.mAllowSleeping = false;
    creation.mNumIterations = ITERATIONS;
    creation.mVertexRadius = float(VERTEX_RADIUS);
    creation.mLinearDamping = LINEAR_DAMPING;
    creation.mFriction = FRICTION;
    creation.mFacesDoubleSided = true;
    if (stop.stop_requested())
        return;
    const BodyID id = bodies.CreateAndAddSoftBody(creation, EActivation::Activate);
    if (id.IsInvalid())
        return;

    {
        BodyLockWrite lock(system.GetBodyLockInterfaceNoLock(), id);
        auto         &vertices = static_cast<SoftBodyMotionProperties *>(lock.GetBody().GetMotionProperties())->GetVertices();
        for (std::size_t v = 0; v < SIM_VERTICES; ++v) {
            const Vec3 &p = in.start[v];
            const Vec3 &u = in.velocity[v];
            vertices[v].mPosition = JPH::Vec3(float(p.x), float(p.y), float(p.z));
            vertices[v].mVelocity = JPH::Vec3(float(u.x), float(u.y), float(u.z));
        }
    }

    const double dt = 1.0 / STEP_HZ;
    const double w = run.w, h = run.h;
    const double pi = std::numbers::pi;
    const int    steps = (run.frames - 1) * STEPS_PER_FRAME;
    for (int step = 1; step <= steps && !stop.stop_requested(); ++step) {
        const double time = (step - 1) * dt;
        {
            BodyLockWrite lock(system.GetBodyLockInterfaceNoLock(), id);
            auto &vertices = static_cast<SoftBodyMotionProperties *>(lock.GetBody().GetMotionProperties())->GetVertices();

            // A freely simulated, tightly wound sheet self-intersects before gravity can unroll it.
            // Guide the wound part in both directions; the unwound cloth stays dynamic.
            {
                for (int i = 0; i < SIM_GRID; ++i) {
                    const double across = double(i) / SIM_CELLS - 0.5;
                    const double t = time / run.length - 0.04 * in.tilt * across;
                    const bool opening = run.kind == Transition::Open;
                    const double progress = smoothstep(0.0, opening ? SETTLE_FROM : ROLL_END, t);
                    const double climbed = opening ? 1.0 - progress : progress;
                    const double taken = opening ? climbed : std::pow(climbed, SLACK_POWER);
                    const double unwound = h * (1.0 - taken);
                    const double contactY = -h * (1.0 - climbed);
                    for (int j = 1; j < SIM_GRID; ++j) {
                        const double s = h * j / SIM_CELLS;
                        auto        &v = vertices[simIndex(i, j)];
                        if (s <= unwound) {
                            v.mInvMass = 1.0F;
                            continue;
                        }
                        const Vec2      yz = rollPoint(contactY, h - unwound, s - unwound, h);
                        const JPH::Vec3 target(v.mPosition.GetX(), float(yz.x), float(yz.y + VERTEX_RADIUS));
                        v.mInvMass = 0.0F;
                        v.mVelocity = (target - v.mPosition) / float(std::max(dt, CATCH_TIME));
                    }
                }
            }

            // Gusts push every free vertex off the desktop by how squarely it faces the viewer.
            if (in.flutter > 0.0) {
                const auto at = [&](int i, int j) { return vertices[simIndex(i, j)].mPosition; };
                for (int j = 1; j < SIM_GRID; ++j)
                    for (int i = 0; i < SIM_GRID; ++i) {
                        auto &v = vertices[simIndex(i, j)];
                        if (v.mInvMass == 0.0F)
                            continue;
                        const JPH::Vec3 dx = at(std::min(i + 1, SIM_CELLS), j) - at(std::max(i - 1, 0), j);
                        const JPH::Vec3 dy = at(i, std::min(j + 1, SIM_CELLS)) - at(i, std::max(j - 1, 0));
                        const JPH::Vec3 n = dx.Cross(dy).NormalizedOr(JPH::Vec3::sAxisZ());
                        const double    x = v.mPosition.GetX() / w, y = v.mPosition.GetY() / h;
                        const double    wave = std::sin(in.gusts.freqA * time + 2.0 * pi * x + in.gusts.phaseA) *
                                            std::sin(in.gusts.freqB * time + 3.0 * pi * y + in.gusts.phaseB);
                        const double push = GUST * in.flutter * (0.55 + 0.45 * wave) * dt;
                        v.mVelocity += n * float(push * n.GetZ());
                    }
            }
        }
        if (system.Update(float(dt), 1, &temp, &jobs) != EPhysicsUpdateError::None)
            break;
        if (step % STEPS_PER_FRAME == 0) {
            BodyLockRead lock(system.GetBodyLockInterfaceNoLock(), id);
            record(run, step / STEPS_PER_FRAME,
                   static_cast<const SoftBodyMotionProperties *>(lock.GetBody().GetMotionProperties())->GetVertices());
        }
    }
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
}

// Fills the in-between sheet vertices of a sim grid by four-point cubic subdivision; the ends extrapolate linearly.
void subdivide(const SimGrid &sim, SheetGrid &out) noexcept {
    const auto refine = [](auto get, auto put, int n) {
        for (int k = 0; k < n; ++k) {
            put(2 * k, get(k));
            if (k + 1 == n)
                break;
            const Vec3 a = k > 0 ? get(k - 1) : lerp(get(k + 1), get(k), 2.0);
            const Vec3 b = get(k), c = get(k + 1);
            const Vec3 d = k + 2 < n ? get(k + 2) : lerp(get(k), get(k + 1), 2.0);
            put(2 * k + 1, Vec3{(-a.x + 9.0 * b.x + 9.0 * c.x - d.x) / 16.0, (-a.y + 9.0 * b.y + 9.0 * c.y - d.y) / 16.0,
                                (-a.z + 9.0 * b.z + 9.0 * c.z - d.z) / 16.0});
        }
    };
    // Rows of the sim grid refined across, then every sheet column refined down.
    std::array<Vec3, std::size_t(SIM_GRID) * SHEET_GRID> rows{};
    for (int j = 0; j < SIM_GRID; ++j)
        refine([&](int i) { return sim[simIndex(i, j)]; },
               [&](int i, Vec3 p) { rows[std::size_t(j) * SHEET_GRID + std::size_t(i)] = p; }, SIM_GRID);
    for (int i = 0; i < SHEET_GRID; ++i)
        refine([&](int j) { return rows[std::size_t(j) * SHEET_GRID + std::size_t(i)]; },
               [&](int j, Vec3 p) { out[std::size_t(j) * SHEET_GRID + std::size_t(i)] = p; }, SIM_GRID);
}

std::shared_ptr<Run> makeRun(Transition kind, double w, double h) {
    auto run = std::make_shared<Run>();
    run->kind = kind;
    run->length = kind == Transition::Open ? OPEN_LENGTH : CLOSE_LENGTH;
    run->w = w;
    run->h = h;
    run->frames = int(std::ceil(run->length * FRAME_HZ)) + 1;
    run->positions.resize(std::size_t(run->frames) * SIM_VERTICES * 3);
    return run;
}

} // namespace

ClothPhysics::ClothPhysics() {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = silentTrace;
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    s_physicsLive.store(true, std::memory_order_release);
}

ClothPhysics::~ClothPhysics() {
    s_physicsLive.store(false, std::memory_order_release);
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

void ClothFlutter::begin(Transition kind, const ClothParams &params, std::uint64_t seed, const Box &window,
                         std::span<const Box> obstacles) {
    ClothParams p = params;
    const double W = window.w, H = window.h;
    if (!sanitize(p) || !s_physicsLive.load(std::memory_order_acquire) || !std::isfinite(W) || !std::isfinite(H) ||
        !(W >= 1.0) || !(H >= 1.0) || !std::isfinite(window.x) || !std::isfinite(window.y)) {
        reset();
        return;
    }

    std::uint64_t base = BASE_SEED, random = seed;
    const auto    draw = [&]() {
        const double b = unit(base), r = unit(random);
        return b + p.variation * (r - b);
    };

    // Square simulation cells avoid unstable, extremely skinny bending constraints on unusual window shapes.
    auto         run = makeRun(kind, 1.0, 1.0);
    SimInput     in;
    in.flutter = p.flutter;
    in.tilt = 2.0 * draw() - 1.0;
    in.gusts = {.phaseA = 2.0 * std::numbers::pi * draw(),
                .phaseB = 2.0 * std::numbers::pi * draw(),
                .freqA = 9.0 + 4.0 * draw(),
                .freqB = 6.0 + 3.0 * draw()};

    // Overlapping windows are at other stacking depths; starting inside a solid box destabilizes the cloth.
    const double centreX = window.x + W / 2;
    std::array<std::pair<double, Box>, MAX_OBSTACLES> nearest{};
    std::size_t count = 0;
    for (const Box &o : obstacles) {
        if (!std::isfinite(o.x) || !std::isfinite(o.y) || !(o.w > 0.0) || !(o.h > 0.0) || !std::isfinite(o.w) ||
            !std::isfinite(o.h))
            continue;
        const bool overlaps = o.x < window.x + W && o.x + o.w > window.x && o.y < window.y + H &&
                              o.y + o.h > window.y;
        if (overlaps)
            continue;
        const double dx = std::max({window.x - (o.x + o.w), o.x - (window.x + W), 0.0});
        const double dy = std::max({window.y - (o.y + o.h), o.y - (window.y + H), 0.0});
        const double distance = std::hypot(dx, dy);
        std::size_t pos = 0;
        while (pos < count && nearest[pos].first <= distance)
            ++pos;
        if (pos == nearest.size())
            continue;
        const std::size_t end = std::min(count, nearest.size() - 1);
        for (std::size_t i = end; i > pos; --i)
            nearest[i] = nearest[i - 1];
        nearest[pos] = {distance, {(o.x - centreX) / W, -(o.y + o.h - window.y) / H, o.w / W, o.h / H}};
        count = std::min(count + 1, nearest.size());
    }
    in.obstacles.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        in.obstacles.push_back(nearest[i].second);

    if (kind == Transition::Open) {
        // Every row below the pinned top edge starts wound on the roll, the bottom edge innermost.
        const double h = run->h;
        for (int j = 0; j < SIM_GRID; ++j) {
            const Vec2 yz = rollPoint(0.0, h, h * j / SIM_CELLS, h);
            for (int i = 0; i < SIM_GRID; ++i) {
                const double across = double(i) / SIM_CELLS - 0.5;
                in.start[simIndex(i, j)] = {across * run->w, yz.x, yz.y + VERTEX_RADIUS};
                // The leading side of the roll drops a little sooner.
                if (j > 0)
                    in.velocity[simIndex(i, j)] = {0.0, -0.08 * in.tilt * across, 0.0};
            }
        }
    } else if (m_phase == Phase::Opening && m_run) {
        // Continue from the opening's shape and motion.
        m_run->sample(m_t, in.start);
        SimGrid ahead{};
        const double dt = 1.0 / FRAME_HZ;
        m_run->sample(m_t + dt, ahead);
        const double settle = smoothstep(SETTLE_FROM, 1.0, m_t / m_run->length);
        const SimGrid rest = m_run->rest();
        for (std::size_t v = 0; v < SIM_VERTICES; ++v) {
            in.start[v] = lerp(in.start[v], rest[v], settle);
            ahead[v] = lerp(ahead[v], rest[v], settle);
            in.velocity[v] = {(ahead[v].x - in.start[v].x) / dt, (ahead[v].y - in.start[v].y) / dt,
                              (ahead[v].z - in.start[v].z) / dt};
        }
    } else {
        in.start = run->rest();
    }
    // Frame 0 is the start itself, recorded here so the cloth can draw before the worker has stepped at all.
    for (std::size_t v = 0; v < SIM_VERTICES; ++v) {
        Vec3 &p = in.start[v];
        p.z = std::max(p.z, VERTEX_RADIUS);
        float *out = run->positions.data() + v * 3;
        out[0] = float(p.x);
        out[1] = float(p.y);
        out[2] = float(p.z);
    }
    run->recorded.store(1, std::memory_order_relaxed);

    try {
        Run *target = run.get();
        run->worker = std::jthread([target, input = std::move(in)](std::stop_token stop) {
            simulate(*target, input, stop);
            target->finished.store(true, std::memory_order_release);
        });
    } catch (...) {
        reset();
        return;
    }

    m_params = p;
    m_run = std::move(run);
    m_rate = m_run->length / p.duration;
    m_t = 0.0;
    m_phase = kind == Transition::Open ? Phase::Opening : Phase::Closing;
}

bool ClothFlutter::advance(double elapsedSeconds) noexcept {
    if (!active())
        return false;
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 || elapsedSeconds > MAX_STEP) {
        if (m_phase == Phase::Opening)
            reset();
        else
            m_phase = Phase::Closed;
        return active();
    }
    // Never past the recorded frames: a slow worker holds the cloth rather than skipping it.
    m_t = std::min(m_t + std::min(elapsedSeconds, MAX_PLAYED_STEP) * m_rate, m_run->playable());
    if (m_t >= m_run->length || (m_run->finished.load(std::memory_order_acquire) && m_t >= m_run->playable())) {
        if (m_phase == Phase::Opening)
            reset();
        else
            m_phase = Phase::Closed;
    }
    return active();
}

void ClothFlutter::reset() noexcept {
    m_phase = Phase::Idle;
    m_t = 0.0;
    m_run.reset();
}

bool ClothFlutter::active() const noexcept { return m_phase == Phase::Opening || m_phase == Phase::Closing; }

bool ClothFlutter::closed() const noexcept { return m_phase == Phase::Closed; }

double ClothFlutter::opacity() const noexcept {
    switch (m_phase) {
        case Phase::Idle: return 1.0;
        case Phase::Closed: return 0.0;
        case Phase::Opening: return smoothstep(0.0, OPEN_FADE_END, m_t / m_run->length);
        case Phase::Closing: return 1.0 - smoothstep(CLOSE_FADE_FROM, 1.0, m_t / m_run->length);
    }
    return 1.0;
}

bool ClothFlutter::shaded() const noexcept { return m_params.shading > 0.0; }

void ClothFlutter::facets(Vec2 size, PaperFacets &out) const noexcept {
    if (!active() || !m_run) {
        flatSheetFacets(out);
        return;
    }
    SimGrid sim{};
    m_run->sample(m_t, sim);
    double settle = 0.0;
    if (m_phase == Phase::Opening) {
        settle = smoothstep(SETTLE_FROM, 1.0, m_t / m_run->length);
        const SimGrid rest = m_run->rest();
        for (std::size_t v = 0; v < SIM_VERTICES; ++v)
            sim[v] = lerp(sim[v], rest[v], settle);
    }
    // Match the worker's stored precision so interrupting an opening cannot reorder equal-depth facets.
    for (Vec3 &p : sim)
        p = {float(p.x), float(p.y), float(std::max(p.z, VERTEX_RADIUS * (1.0 - settle)))};

    const double W = std::isfinite(size.x) && size.x > 0.0 ? size.x : 1.0;
    const double H = std::isfinite(size.y) && size.y > 0.0 ? size.y : 1.0;
    const double L = std::sqrt(W * H);
    SheetGrid    world{};
    subdivide(sim, world);
    // Sheet units, y up from the top edge -> logical pixels about the rest centre, y down.
    for (Vec3 &p : world)
        p = {p.x * W, -p.y * H - H / 2.0, p.z * L};
    sheetFacets(world, {W, H}, m_params.shading, out);
}

} // namespace liquid_glass
