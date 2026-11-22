#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "Check.hpp"
#include "WindowAnimationFacts.hpp"
#include "animations/construction/ConstructionArt.hpp"
#include "animations/construction/ConstructionSite.hpp"

using namespace hyprimate;
namespace {
using namespace construction;

Vec3 point(const ConstructionQuad &q, double u, double v) {
    return {q.origin[0] + u*q.u[0] + v*q.v[0], q.origin[1] + u*q.u[1] + v*q.v[1],
        q.origin[2] + u*q.u[2] + v*q.v[2]};
}

const ConstructionQuad *paint(const ConstructionBatch &batch, Part part) {
    for (std::size_t i = 0; i < batch.count; ++i) {
        const auto &q = batch.quads[i];
        if (q.surface == ConstructionSurface::Atlas &&
            q.uv[0] == float(part.col*64+4)/ATLAS_WIDTH && q.uv[1] == float(part.row*64+4)/ATLAS_HEIGHT)
            return &q;
    }
    return nullptr;
}

Vec3 socket(const ConstructionQuad &q, Vec2 texel) {
    return point(q, (texel.x-4)/56, (texel.y-4)/56);
}

void contacts(std::uint64_t seed) {
    ConstructionSite site;
    constexpr Box BODY{96,96,800,550};
    site.build(BODY,BODY,{0,0,1280,960},seed,1);
    double guide_error = 0, strike_error = 100, rebound_distance = 0, boot_error = 0;
    Vec3 planted_near{}, planted_far{};
    int guide_samples = 0;
    const int seat_ms = seed & 1U ? 740 : 660;
    for (int ms = 0; ms <= seat_ms + 70; ++ms) {
        site.step({.elapsed = ms == 0 ? 0.0 : .001, .body=BODY, .capture=BODY,
            .goal_body=BODY,.rest_capture=BODY,.screen={0,0,1280,960},.work_area={0,0,1280,960},
            .motion={},.open_enabled=true});
        const auto &batch = site.batch();
        const auto *far = paint(batch,FAR_BOOT), *near = paint(batch,NEAR_BOOT);
        const auto *glove = paint(batch,GLOVE), *hammer = paint(batch,HAMMER), *bolt = paint(batch,FASTENER);
        if (!far || !near || !glove || !hammer || !bolt) {
            CHECK(false && "missing painted contact part");
            return;
        }
        const auto far_sole = socket(*far,FAR_BOOT.tip), near_sole = socket(*near,NEAR_BOOT.tip);
        if (ms == 0) planted_far = far_sole;
        if (ms == 95) planted_near = near_sole;
        if (ms < 90) boot_error = std::max(boot_error,length(far_sole-planted_far));
        if (ms >= 95 && ms < 178) boot_error = std::max(boot_error,length(near_sole-planted_near));
        if (ms >= 532 && ms < seat_ms) {
            // Compare the actual painted fingers to the left edge of the posed, sampled front.
            for (std::size_t i = 0; i < batch.count; ++i) {
                const auto &q = batch.quads[i];
                if (q.surface != ConstructionSurface::Client || (q.u[2] == 0 && q.v[2] == 0)) continue;
                const Vec3 top = point(q,0,0), edge = point(q,0,1)-top;
                const Vec3 delta = socket(*glove,GLOVE.tip) - top;
                guide_error = std::max(guide_error,length(delta-edge*std::clamp(dot(delta,edge)/dot(edge,edge),0.,1.)));
                ++guide_samples;
            }
        }
        if (ms == seat_ms + 55) strike_error = length(socket(*hammer,HAMMER.tip)-socket(*bolt,FASTENER.pivot));
        if (ms == seat_ms + 69) rebound_distance = length(socket(*hammer,HAMMER.tip)-socket(*bolt,FASTENER.pivot));
    }
    CHECK(guide_samples > 0 && guide_error <= .5);
    CHECK(strike_error <= .5 && rebound_distance > 3 && boot_error <= .5);
    std::printf("seed=%llu world_px: stance_max=%.5f guide_max=%.5f strike_at_cue=%.5f\n",
        static_cast<unsigned long long>(seed),boot_error,guide_error,strike_error);
}

void installation(Box body, std::uint64_t seed, double hz) {
    const Box capture{body.x - 3.25, body.y - 6.5, body.w + 8.5, body.h + 13};
    const Box area{10, 20, 1400, 900};
    ConstructionSite site;
    site.build(body, capture, area, seed, 1.1);
    CHECK(site.active());
    const double focal = 4*std::max({body.w,body.h,64.0});
    const Vec2 center{body.x - capture.x + body.w / 2, body.y - capture.y + body.h / 2};
    bool live_at_rest = false, lifted = false;
    std::size_t peak = 0;
    for (int frame = 0; site.active() && frame < hz*2; ++frame) {
        const auto &batch = site.batch();
        const auto bounds = site.bounds();
        CHECK(!batch.overflow);
        CHECK(bounds.x <= 0 && bounds.y <= 0 && bounds.x+bounds.w >= capture.w && bounds.y+bounds.h >= capture.h);
        peak = std::max(peak,batch.count);
        for (std::size_t i = 0; i < batch.count; ++i) {
            const auto &q = batch.quads[i];
            if (q.surface == ConstructionSurface::Client) {
                lifted |= std::abs(q.u[2])+std::abs(q.v[2]) > .1;
                if (batch.count == 1) {
                    // The final *published*, still active pose uses the whole continuous sampling domain.
                    CHECK((q.origin == std::array<float,3>{0,0,0}));
                    CHECK((q.u == std::array<float,3>{static_cast<float>(capture.w),0,0}));
                    CHECK((q.v == std::array<float,3>{0,static_cast<float>(capture.h),0}));
                    CHECK((q.uv == std::array<float,4>{0,0,static_cast<float>(capture.w),static_cast<float>(capture.h)}));
                    live_at_rest = true;
                }
            }
            for (int corner = 0; corner != 4; ++corner) {
                const int u = corner&1, v = corner>>1;
                const double w = 1.0 - (q.origin[2]+u*q.u[2]+v*q.v[2])/focal;
                CHECK(w >= .875 && w <= 1.125);
                const double x = center.x + (q.origin[0]+u*q.u[0]+v*q.v[0]-center.x)/w;
                const double y = center.y + (q.origin[1]+u*q.u[1]+v*q.v[1]-center.y)/w;
                CHECK(x >= bounds.x-.001 && x <= bounds.x+bounds.w+.001);
                CHECK(y >= bounds.y-.001 && y <= bounds.y+bounds.h+.001);
            }
        }
        site.step({.elapsed = frame ? 1/hz : 0, .body=body,.capture=capture,
            .goal_body=body,.rest_capture=capture,.work_area=area,.motion={},.open_enabled=true});
    }
    CHECK(lifted && live_at_rest && !site.active());
    std::printf("%.0fx%.0f seed=%llu simulated_Hz=%.0f peak_quads=%zu stream_bytes=%zu storage_bytes=%zu\n",
        body.w,body.h,static_cast<unsigned long long>(seed),hz,peak,peak*sizeof(ConstructionQuad),sizeof(site));
}
}  // namespace

int main() {
    for (auto seed : {42ULL,43ULL}) {
        contacts(seed);
        installation({170,160,800,550}, seed, 60);
        installation({161.6,80.8,480,320}, seed, 120);
    }
    return g_failures == 0 ? 0 : 1;
}
