#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <tuple>
#include <vector>

#include <cstdio>

#include "Check.hpp"

#include "window/WindowAnimationFacts.hpp"
#include "window/construction/ConstructionArt.hpp"
#include "window/construction/ConstructionSite.hpp"
#include "window/construction/ConstructionCrew.hpp"

using namespace hyprimate;
namespace {
using namespace construction;

constexpr double OPEN_SECONDS=.62;

Vec3 point(const ConstructionQuad &q, double u, double v) {
    return {q.origin[0] + u*q.u[0] + v*q.v[0], q.origin[1] + u*q.u[1] + v*q.v[1],
        q.origin[2] + u*q.u[2] + v*q.v[2]};
}

bool owns_uv(const ConstructionQuad &q, Part part) {
    return q.surface == ConstructionSurface::Atlas &&
        q.uv[0] == float(part.col*64+4)/ATLAS_WIDTH &&
        q.uv[1] == float(part.row*64+4)/ATLAS_HEIGHT;
}

Vec3 socket(const ConstructionQuad &q, Vec2 texel) {
    return point(q, (texel.x-4)/56, (texel.y-4)/56);
}

const ConstructionQuad *paint(const ConstructionBatch &batch, Part part) {
    for (std::size_t i = 0; i < batch.count; ++i)
        if (owns_uv(batch.quads[i],part)) return &batch.quads[i];
    return nullptr;
}

void crew_built_opening() {
    ConstructionSite site;
    constexpr Box BODY{96,96,800,550};
    constexpr Box AREA{0,0,1280,720};
    site.build(BODY, BODY, AREA, 42, OPEN_SECONDS);
    bool parallel_work=false, different_pitch=false, above_sill=false, below_sill=false, machine=false;
    for (int i = 0; i < 80 && site.active(); ++i) {
        const auto &batch = site.batch();
        machine |= paint(batch, CAB) || paint(batch, HOOK) || paint(batch, CHASSIS) || paint(batch, SLING_EYE);
        int workers = 0, moving_faces = 0;
        double min_pitch=INFINITY, max_pitch=-INFINITY;
        for (std::size_t k = 0; k < batch.count; ++k) {
            const auto &q = batch.quads[k];
            workers += owns_uv(q,HEAD);
            if (q.surface!=ConstructionSurface::Client || q.uv[2]<=32 || q.uv[3]<=32) continue;
            const Vec3 rest{q.uv[0]+.5*q.uv[2],double(q.uv[1])+q.uv[3],0};
            const Vec3 travel=point(q,.5,1)-rest;
            above_sill |= travel.y < -2;
            below_sill |= travel.y > 2;
            if (length(travel)<2 && std::abs(q.u[2])+std::abs(q.v[2])<=.1) continue;
            ++moving_faces;
            // Left/right reflections of a flat lateral carry cannot produce
            // clearly different pitch among the concurrently traveling faces.
            const double pitch=double(q.v[2])/q.uv[3];
            min_pitch=std::min(min_pitch,pitch);
            max_pitch=std::max(max_pitch,pitch);
        }
        parallel_work |= workers >= 3 && moving_faces >= 2;
        different_pitch |= moving_faces>=2 && max_pitch-min_pitch>.04;
        site.step({.elapsed=1.0/120,.body=BODY,.capture=BODY,.goal_body=BODY,
            .rest_capture=BODY,.screen=AREA,.work_area=AREA,.motion={},.open_enabled=true});
    }
    CHECK(!machine);
    CHECK(parallel_work);
    CHECK(above_sill && below_sill);
    CHECK(different_pitch);
}

Vec3 marker_source_point(Box body) {
    // Strictly inside the seeded lower-right bay for either take.
    return {body.w*.76,body.h*.73,0};
}

const ConstructionQuad *marker_face(const ConstructionBatch &batch, Box body) {
    const Vec3 marker=marker_source_point(body);
    const ConstructionQuad *best = nullptr;
    for (std::size_t i=0; i<batch.count; ++i) {
        const auto &q = batch.quads[i];
        if (q.surface == ConstructionSurface::Client &&
            marker.x > q.uv[0] && marker.x < double(q.uv[0])+q.uv[2] &&
            marker.y > q.uv[1] && marker.y < double(q.uv[1])+q.uv[3] &&
            (!best || q.uv[2]*q.uv[3] < best->uv[2]*best->uv[3])) best = &q;
    }
    return best;
}

bool identity_mapping(const ConstructionQuad &q) {
    // The identity publisher casts the same values into position and source.
    // Exact comparison distinguishes the first seated frame from a nearly flat pose.
    return q.origin == std::array<float,3>{q.uv[0],q.uv[1],0} &&
        q.u == std::array<float,3>{q.uv[2],0,0} && q.v == std::array<float,3>{0,q.uv[3],0};
}

Vec3 marker_location(const ConstructionQuad &q, Box body) {
    const Vec3 source=marker_source_point(body);
    return point(q,(source.x-q.uv[0])/q.uv[2],(source.y-q.uv[1])/q.uv[3]);
}

bool full_identity(const ConstructionQuad &q, Box body) {
    return identity_mapping(q) && q.uv == std::array<float,4>{0,0,float(body.w),float(body.h)};
}

struct MarkerTrace {
    int missing_before=0, first=-1, seat=-1, strike=-1;
    Vec3 first_position{}, previous_position{};
    double max_step=0;
    bool tilted=false, first_tilted=false, lost_marker=false;
};

void trace_marker(MarkerTrace &trace, const ConstructionBatch &batch, Box body, int ms) {
    const auto *face=marker_face(batch,body);
    if (!face) {
        if (trace.first<0) ++trace.missing_before;
        else trace.lost_marker=true;
        return;
    }
    const Vec3 position=marker_location(*face,body);
    if (trace.first<0) {
        trace.first=ms;
        trace.first_position=position;
        trace.first_tilted=!identity_mapping(*face);
    } else {
        trace.max_step=std::max(trace.max_step,length(position-trace.previous_position));
    }
    if (!identity_mapping(*face)) trace.tilted=true;
    else if (trace.tilted && trace.seat<0) trace.seat=ms;
    trace.previous_position=position;
}

struct WorkerPaint {
    const ConstructionQuad *far_glove=nullptr, *near_boot=nullptr, *far_boot=nullptr, *hammer=nullptr;
};

double stance_gap(const WorkerPaint &worker, Vec3 near, Vec3 far) {
    if (!worker.near_boot || !worker.far_boot) return INFINITY;
    return std::max(length(socket(*worker.near_boot,NEAR_BOOT.tip)-near),
        length(socket(*worker.far_boot,FAR_BOOT.tip)-far));
}

struct ReceivingPaint { WorkerPaint guide, fitter; };

ReceivingPaint receiving_workers(const ConstructionBatch &batch, double left, double right, double sill, double scale) {
    // Group the painted limbs of each rig; identify the receivers by their
    // planted sockets, not where they appear in the crew's paint order.
    ReceivingPaint result;
    WorkerPaint worker;
    double guide_gap=INFINITY, fitter_gap=INFINITY;
    const auto consider=[&] {
        if (worker.hammer) {
            const double gap=stance_gap(worker,{right-32*scale,sill,9+2.9*scale},{right-20*scale,sill,9-2.7*scale});
            if (gap<fitter_gap) { fitter_gap=gap; result.fitter=worker; }
        } else {
            const double gap=stance_gap(worker,{left-22*scale,sill,8+2.9*scale},{left-10*scale,sill,8-2.7*scale});
            if (gap<guide_gap) { guide_gap=gap; result.guide=worker; }
        }
    };
    for (std::size_t i=0; i<batch.count; ++i) {
        const auto &q=batch.quads[i];
        if (owns_uv(q,FAR_THIGH)) {
            consider();
            worker={};
        } else if (owns_uv(q,FAR_BOOT)) worker.far_boot=&q;
        else if (owns_uv(q,NEAR_BOOT)) worker.near_boot=&q;
        else if (owns_uv(q,GLOVE) && !worker.far_glove) worker.far_glove=&q;
        else if (owns_uv(q,HAMMER)) worker.hammer=&q;
    }
    consider();
    if (guide_gap>.5) result.guide={};
    if (fitter_gap>.5) result.fitter={};
    return result;
}

const ConstructionQuad *receiving_bolt(const ConstructionBatch &batch, Box body) {
    const double scale=.075*std::min(body.w,body.h)/CREW_HEIGHT;
    const Vec3 target{body.w-6*scale,body.h-31*scale,5};
    for (std::size_t i=0; i<batch.count; ++i) {
        const auto &q=batch.quads[i];
        if (owns_uv(q,FASTENER) && length(socket(q,FASTENER.pivot)-target)<=.5) return &q;
    }
    return nullptr;
}

bool on_sill(const ConstructionBatch &batch, Vec3 sole) {
    for (std::size_t i=0; i<batch.count; ++i) {
        const auto &q=batch.quads[i];
        // The ledge top is a solid face in the horizontal X–Z plane.
        if (q.surface == ConstructionSurface::Solid && q.u[0]>0 && q.v[2]>0 &&
            q.u[1]==0 && q.u[2]==0 && q.v[0]==0 && q.v[1]==0 &&
            std::abs(sole.y-q.origin[1])<=.5 &&
            sole.x>=q.origin[0] && sole.x<=double(q.origin[0])+q.u[0] &&
            sole.z>=q.origin[2] && sole.z<=double(q.origin[2])+q.v[2]) return true;
    }
    return false;
}

struct ContactSample { double grip=INFINITY, stance=INFINITY, hammer=INFINITY; };

ContactSample sample_contacts(const ConstructionBatch &batch, const ConstructionQuad &face,
    const ConstructionQuad *bolt, double scale) {
    ContactSample sample;
    const double x=face.uv[0], y=double(face.uv[1])+face.uv[3];
    const auto [guide,fitter]=receiving_workers(batch,x,double(face.uv[0])+face.uv[2],y,scale);
    // The fixture has no capture trim; socket height follows the rig scale in
    // the moving panel's own plane.
    if (guide.far_glove)
        sample.grip=length(socket(*guide.far_glove,GLOVE.tip)-point(face,0,1-32*scale/face.uv[3]));
    if (guide.near_boot && guide.far_boot) {
        const Vec3 near=socket(*guide.near_boot,NEAR_BOOT.tip), far=socket(*guide.far_boot,FAR_BOOT.tip);
        if (on_sill(batch,near) && on_sill(batch,far))
            sample.stance=std::max(length(near-Vec3{x-22*scale,y,8+2.9*scale}),
                length(far-Vec3{x-10*scale,y,8-2.7*scale}));
    }
    if (fitter.hammer && bolt)
        sample.hammer=length(socket(*fitter.hammer,HAMMER.tip)-socket(*bolt,FASTENER.pivot));
    return sample;
}

bool same_publication(const ConstructionBatch &a, const ConstructionBatch &b) {
    return a.count == b.count && a.overflow == b.overflow &&
        std::equal(a.quads.begin(),a.quads.begin()+a.count,b.quads.begin(),[](const auto &x, const auto &y) {
            return x.origin == y.origin && x.u == y.u && x.v == y.v && x.uv == y.uv &&
                x.tint == y.tint && x.surface == y.surface;
        });
}

bool complete_identity_coverage(const ConstructionBatch &batch, Box body) {
    // Partition the source domain at every client-quad boundary. Each open cell must
    // have exactly one identity-mapped owner; this catches both seams and overdraw.
    if (batch.overflow) return false;
    std::array<double,2*ConstructionBatch{}.quads.size()+2> xs{}, ys{};
    std::size_t nx=2, ny=2, clients=0;
    xs[0]=0; xs[1]=body.w; ys[0]=0; ys[1]=body.h;
    for (std::size_t i=0; i<batch.count; ++i) {
        const auto &q = batch.quads[i];
        if (q.surface != ConstructionSurface::Client) continue;
        ++clients;
        const double x0=q.uv[0], x1=x0+q.uv[2], y0=q.uv[1], y1=y0+q.uv[3];
        if (!identity_mapping(q) || x0 < -.002 || y0 < -.002 ||
            x1 > body.w+.002 || y1 > body.h+.002 || nx+2>xs.size() || ny+2>ys.size()) return false;
        xs[nx++]=std::clamp(x0,0.,body.w); xs[nx++]=std::clamp(x1,0.,body.w);
        ys[ny++]=std::clamp(y0,0.,body.h); ys[ny++]=std::clamp(y1,0.,body.h);
    }
    if (!clients) return false;
    std::sort(xs.begin(),xs.begin()+nx); std::sort(ys.begin(),ys.begin()+ny);
    for (std::size_t x=1; x<nx; ++x) {
        if (xs[x]-xs[x-1] < .001) continue;
        for (std::size_t y=1; y<ny; ++y) {
            if (ys[y]-ys[y-1] < .001) continue;
            const double mx=(xs[x]+xs[x-1])*.5, my=(ys[y]+ys[y-1])*.5;
            int owners=0;
            for (std::size_t i=0; i<batch.count; ++i) {
                const auto &q=batch.quads[i];
                owners += q.surface == ConstructionSurface::Client &&
                    mx > q.uv[0] && mx < double(q.uv[0])+q.uv[2] &&
                    my > q.uv[1] && my < double(q.uv[1])+q.uv[3];
            }
            if (owners != 1) return false;
        }
    }
    return true;
}
using FaceRect = std::array<float,4>;

struct OpeningTrace {
    std::vector<FaceRect> first_work;
    std::vector<FaceRect> seated;
    std::vector<std::size_t> client_counts;
    bool supported_seat = false, active_identity = false;
};

bool body_face(const ConstructionQuad &q, Box body) {
    return q.surface == ConstructionSurface::Client && q.uv[0] >= -.002F && q.uv[1] >= -.002F &&
        q.uv[0] + q.uv[2] <= body.w + .002 && q.uv[1] + q.uv[3] <= body.h + .002;
}

bool has_published_sill(const ConstructionBatch &batch, const ConstructionQuad &face) {
    const double left = face.uv[0], right = left + face.uv[2];
    const double bottom = face.uv[1] + face.uv[3];
    for (std::size_t i = 0; i < batch.count; ++i) {
        const auto &q = batch.quads[i];
        if (q.surface == ConstructionSurface::Solid && q.u[0] > 0 && q.v[2] > 0 &&
            q.u[1] == 0 && q.u[2] == 0 && q.v[0] == 0 && q.v[1] == 0 &&
            std::abs(q.origin[1] - bottom) <= .5 && std::abs(q.origin[2] + 7) <= .5 &&
            std::min(right, double(q.origin[0]) + q.u[0]) > std::max(left, double(q.origin[0])))
            return true;
    }
    return false;
}

OpeningTrace opening_trace(std::uint64_t seed) {
    constexpr Box BODY{96,96,800,550}, AREA{0,0,1280,960};
    constexpr int DURATION_MS = 620;
    struct FaceState { FaceRect source; bool moving = false, seated = false; };
    std::array<FaceState, ConstructionBatch{}.quads.size()> faces{};
    std::size_t face_count = 0;
    OpeningTrace trace;
    ConstructionSite site, replay;
    site.build(BODY, BODY, AREA, seed, OPEN_SECONDS);
    replay.build(BODY, BODY, AREA, seed, OPEN_SECONDS);
    CHECK(site.active() && replay.active());

    for (int ms = 0; ms <= DURATION_MS; ++ms) {
        const auto &batch = site.batch();
        CHECK(!batch.overflow);
        CHECK(same_publication(batch, replay.batch()));
        std::size_t client_count = 0;
        for (std::size_t i = 0; i < batch.count; ++i) {
            const auto &q = batch.quads[i];
            if (!body_face(q, BODY)) continue;
            ++client_count;
            const FaceRect source = q.uv;
            std::size_t index = 0;
            while (index < face_count && faces[index].source != source) ++index;
            if (index == face_count && !identity_mapping(q)) {
                CHECK(face_count < faces.size());
                if (face_count < faces.size()) {
                    faces[face_count].source = source;
                    index = face_count++;
                    trace.first_work.push_back(source);
                }
            }
            if (index < face_count && !identity_mapping(q)) faces[index].moving = true;
            if (index < face_count && faces[index].moving && !faces[index].seated && identity_mapping(q)) {
                faces[index].seated = true;
                trace.seated.push_back(source);
                if (std::abs(double(source[1]) + source[3] - BODY.h) <= .002 &&
                    has_published_sill(batch, q)) trace.supported_seat = true;
            }
        }
        trace.client_counts.push_back(client_count);
        if (site.active() && complete_identity_coverage(batch, BODY))
            trace.active_identity = true;
        if (ms != DURATION_MS) {
            const animation::Step tick{.elapsed=.001,.body=BODY,.capture=BODY,.goal_body=BODY,
                .rest_capture=BODY,.screen=AREA,.work_area=AREA,.motion={},.open_enabled=true};
            site.step(tick);
            replay.step(tick);
        }
    }
    CHECK(trace.active_identity);
    const animation::Step retire{.elapsed=.001,.body=BODY,.capture=BODY,.goal_body=BODY,
        .rest_capture=BODY,.screen=AREA,.work_area=AREA,.motion={},.open_enabled=true};
    site.step(retire);
    replay.step(retire);
    CHECK(!site.active() && site.batch().count == 0 && !site.batch().overflow);
    CHECK(same_publication(site.batch(), replay.batch()));

    return trace;
}

FaceRect mirrored(FaceRect r, double width) {
    r[0] = static_cast<float>(width - r[0] - r[2]);
    return r;
}

double layout_distance(const std::vector<FaceRect> &a, const std::vector<FaceRect> &b,
    bool reflect_b) {
    if (a.size() != b.size() || a.empty()) return 0;
    auto lhs = a, rhs = b;
    for (auto &r : lhs) {
        r[0] /= 800; r[1] /= 550; r[2] /= 800; r[3] /= 550;
    }
    for (auto &r : rhs) {
        if (reflect_b) r = mirrored(r, 800);
        r[0] /= 800; r[1] /= 550; r[2] /= 800; r[3] /= 550;
    }
    const auto order = [](const FaceRect &x, const FaceRect &y) {
        return std::tie(x[1],x[0],x[3],x[2]) < std::tie(y[1],y[0],y[3],y[2]);
    };
    std::sort(lhs.begin(), lhs.end(), order);
    std::sort(rhs.begin(), rhs.end(), order);
    double distance = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i)
        for (std::size_t k = 0; k < 4; ++k) distance += std::abs(lhs[i][k] - rhs[i][k]);
    return distance / (4 * lhs.size());
}

void opening_diversity_and_support() {
    constexpr std::array<std::uint64_t,8> seeds{42,43,44,45,46,47,48,49};
    std::array<OpeningTrace,seeds.size()> traces;
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        traces[i] = opening_trace(seeds[i]);
        CHECK(traces[i].supported_seat);
        CHECK(!traces[i].first_work.empty() && !traces[i].seated.empty());
    }

    bool multiple_counts_at_same_time = false;
    for (std::size_t ms = 0; ms < traces[0].client_counts.size(); ++ms) {
        const auto first = traces[0].client_counts[ms];
        for (std::size_t i = 1; i < traces.size(); ++i)
            multiple_counts_at_same_time |= traces[i].client_counts[ms] != first;
    }
    CHECK(multiple_counts_at_same_time);

    const auto seat_order = [](const std::vector<FaceRect> &seated) {
        std::vector<std::size_t> rows(seated.size());
        for (std::size_t i = 0; i < rows.size(); ++i) rows[i] = i;
        std::sort(rows.begin(), rows.end(), [&](std::size_t a, std::size_t b) {
            const double ay = seated[a][1] + seated[a][3] * .5;
            const double by = seated[b][1] + seated[b][3] * .5;
            if (ay != by) return ay < by;
            return seated[a][0] + seated[a][2] * .5 < seated[b][0] + seated[b][2] * .5;
        });
        std::vector<std::size_t> ranks(seated.size());
        for (std::size_t rank = 0; rank < rows.size(); ++rank) ranks[rows[rank]] = rank;
        std::vector<std::size_t> order;
        order.reserve(seated.size());
        for (std::size_t i = 0; i < seated.size(); ++i) order.push_back(ranks[i]);
        return order;
    };

    bool non_mirrored_layout_difference = false, seat_order_difference = false;

    for (std::size_t i = 0; i < traces.size(); ++i) {
        for (std::size_t j = i + 1; j < traces.size(); ++j) {
            const double direct = layout_distance(traces[i].first_work, traces[j].first_work, false);
            const double reflected = layout_distance(traces[i].first_work, traces[j].first_work, true);

            non_mirrored_layout_difference |= direct > .012 && reflected > .012;
            if (traces[i].seated.size() == traces[j].seated.size())
                seat_order_difference |= seat_order(traces[i].seated) != seat_order(traces[j].seated);
        }
    }
    CHECK(non_mirrored_layout_difference || seat_order_difference);
}


MarkerTrace contacts(std::uint64_t seed) {
    constexpr Box BODY{96,96,800,550}, AREA{0,0,1280,960};
    constexpr int FRAMES=static_cast<int>(OPEN_SECONDS*1000)-1;
    const animation::Step tick{.elapsed=.001,.body=BODY,.capture=BODY,.goal_body=BODY,
        .rest_capture=BODY,.screen=AREA,.work_area=AREA,.motion={},.open_enabled=true};
    ConstructionSite site, repeat;
    site.build(BODY,BODY,AREA,seed,OPEN_SECONDS);
    repeat.build(BODY,BODY,AREA,seed,OPEN_SECONDS);
    MarkerTrace trace;
    std::array<ContactSample,FRAMES+1> samples{};
    int first_bolt=-1;
    bool deterministic=true, split_covered=false, checked_coverage=false;
    for (int ms=0; ms<=FRAMES; ++ms) {
        if (ms) { site.step(tick); repeat.step(tick); }
        const auto &batch=site.batch();
        deterministic &= same_publication(batch,repeat.batch());
        trace_marker(trace,batch,BODY,ms);
        const auto *bolt=receiving_bolt(batch,BODY);
        if (bolt && first_bolt<0) first_bolt=ms;
        if (const auto *face=marker_face(batch,BODY)) {
            if (!full_identity(*face,BODY)) {
                samples[ms]=sample_contacts(batch,*face,bolt,.075*std::min(BODY.w,BODY.h)/CREW_HEIGHT);
                split_covered=complete_identity_coverage(batch,BODY);
            } else if (!checked_coverage) {
                // Verify the last assembled split frame as well as the active
                // coalesced identity frame; the latter cannot hide a missing bay.
                CHECK(site.active() && split_covered && complete_identity_coverage(batch,BODY));
                checked_coverage=true;
            }
        }
    }
    CHECK(deterministic && checked_coverage);
    CHECK(trace.missing_before>0 && trace.missing_before==trace.first && !trace.lost_marker);
    CHECK(trace.first_tilted && trace.seat>trace.first && trace.max_step>.1);
    CHECK(length(trace.first_position-marker_source_point(BODY))>1);
    CHECK(trace.seat>=0 && first_bolt==trace.seat);
    if (trace.first<0 || trace.seat<=trace.first) return trace;

    // Recover the receiving clock from first publication and exact seating.
    // The face publishes at job phase .067, before the guide is latched.
    constexpr double FIRST_FACE_PHASE=.067;
    const double span_ms=(trace.seat-trace.first)/(PANEL_SEAT-FIRST_FACE_PHASE);
    const int guide_start=trace.seat-static_cast<int>(std::lround((PANEL_SEAT-GUIDE_LATCH)*span_ms));
    constexpr int GUIDE_WAIT_SAMPLE=static_cast<int>(OPEN_SECONDS*400); // Both guides have planted.
    CHECK(guide_start>GUIDE_WAIT_SAMPLE && guide_start<trace.seat);
    double grip_error=0, stance_error=0, contact_error=INFINITY;
    for (int ms=GUIDE_WAIT_SAMPLE; ms<=trace.seat; ++ms) {
        if (ms>=guide_start) grip_error=std::max(grip_error,samples[ms].grip);
        stance_error=std::max(stance_error,samples[ms].stance);
    }
    for (int ms=trace.seat+1; ms<=std::min(trace.seat+100,FRAMES); ++ms) {
        if (samples[ms].hammer < contact_error) {
            contact_error=samples[ms].hammer;
            trace.strike=ms;
        }
    }
    const double rebound=trace.strike>trace.seat && trace.strike+25<=FRAMES ?
        samples[trace.strike+25].hammer : INFINITY;
    CHECK(grip_error<=.5 && stance_error<=.5);
    CHECK(contact_error<=.5 && std::isfinite(rebound) && rebound>3);
    std::printf("seed=%llu face=%dms seat=%dms strike=%dms guide_samples=%d "
        "world_px: stance_max=%.5f guide_grip_max=%.5f fastener_contact=%.5f rebound=%.5f\n",
        static_cast<unsigned long long>(seed),trace.first,trace.seat,trace.strike,trace.seat-guide_start+1,
        stance_error,grip_error,contact_error,rebound);
    return trace;
}

void installation(Box body, std::uint64_t seed, double hz) {
    const Box capture{body.x - 3.25, body.y - 6.5, body.w + 8.5, body.h + 13};
    const Box area{10, 20, 1400, 900};
    ConstructionSite site;
    site.build(body, capture, area, seed, OPEN_SECONDS);
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
            .goal_body=body,.rest_capture=capture,.screen=area,.work_area=area,.motion={},.open_enabled=true});
    }
    CHECK(lifted && live_at_rest && !site.active());
    std::printf("%.0fx%.0f seed=%llu simulated_Hz=%.0f peak_quads=%zu stream_bytes=%zu storage_bytes=%zu\n",
        body.w,body.h,static_cast<unsigned long long>(seed),hz,peak,peak*sizeof(ConstructionQuad),sizeof(site));
}

void animated_capture_keeps_the_opening_pose_and_clock() {
    constexpr Box BODY{170,160,800,550}, AREA{0,0,1400,900};
    constexpr Box INITIAL{168,158,804,554};
    ConstructionSite reference, animated;
    reference.build(BODY, INITIAL, AREA, 42, OPEN_SECONDS);
    animated.build(BODY, INITIAL, AREA, 42, OPEN_SECONDS);
    Box capture = INITIAL;
    const auto world = [&](const ConstructionQuad &quad, int corner, Box domain) {
        const auto p = point(quad, corner & 1, corner >> 1);
        const Vec2 center{BODY.x - domain.x + BODY.w * .5, BODY.y - domain.y + BODY.h * .5};
        const double w = 1 - p.z / (4 * std::max(BODY.w, BODY.h));
        return Vec2{domain.x + center.x + (p.x - center.x) / w,
            domain.y + center.y + (p.y - center.y) / w};
    };
    bool seated = false;
    for (int frame = 0; reference.active() && frame < 120; ++frame) {
        const double trim = std::min(frame, 24) * .19;
        capture = {168,158 - trim,804,554 + 2 * trim};
        const auto tick = [&](ConstructionSite &site, Box domain) {
            site.step({.elapsed=1.0/120,.body=BODY,.capture=domain,.goal_body=BODY,
                .rest_capture=domain,.screen=AREA,.work_area=AREA,.motion={},.open_enabled=true});
        };
        tick(reference, INITIAL);
        tick(animated, capture);
        CHECK(animated.active() == reference.active());
        CHECK(animated.capture().x == capture.x && animated.capture().y == capture.y &&
            animated.capture().w == capture.w && animated.capture().h == capture.h);
        const auto &before = reference.batch(), &after = animated.batch();
        CHECK(before.count == after.count && !after.overflow);
        for (std::size_t i = 0; i < std::min(before.count, after.count); ++i) {
            CHECK(before.quads[i].surface == after.quads[i].surface);
            if (before.quads[i].surface == ConstructionSurface::Client) continue;
            for (int corner = 0; corner < 4; ++corner) {
                const auto a = world(before.quads[i], corner, INITIAL);
                const auto b = world(after.quads[i], corner, capture);
                CHECK(std::abs(a.x-b.x) < .005 && std::abs(a.y-b.y) < .005);
            }
        }
        if (animated.active() && after.count == 1) {
            seated = true;
            CHECK((after.quads[0].uv == std::array<float,4>{
                0,0,static_cast<float>(capture.w),static_cast<float>(capture.h)}));
        }
    }
    CHECK(seated && !reference.active() && !animated.active());
}
void scaled_rigs_keep_world_contacts() {
    constexpr Vec3 floor{180,240,9};
    for (const auto job : {CrewJob::Carry,CrewJob::Guide,CrewJob::Brace,CrewJob::Fasten,CrewJob::Weld}) {
        for (double work : {.44,.55,FASTENER_STRIKE,.80}) {
            const CrewCues cues{.floor=floor,.far_contact=floor+Vec3{13,-32,-4},
                .near_contact=floor+Vec3{19,-31,-4},.job=job,.facing=1,.approach=0,
                .arrival=1,.work=work,.departure=0,.aim_contacts=true,.steady_during_finish=true};
            ConstructionBatch reference;
            draw_crew(reference,cues);
            CHECK(reference.count>0 && !reference.overflow);
            for (double scale : {.125,.5,1.0,2.0,4.0}) {
                auto scaled=cues;
                scaled.scale=scale;
                scaled.far_contact=floor+(cues.far_contact-floor)*scale;
                scaled.near_contact=floor+(cues.near_contact-floor)*scale;
                ConstructionBatch batch;
                draw_crew(batch,scaled);
                CHECK(batch.count==reference.count && !batch.overflow);
                for (std::size_t i=0;i<std::min(batch.count,reference.count);++i) {
                    CHECK(batch.quads[i].surface==reference.quads[i].surface);
                    CHECK(batch.quads[i].uv==reference.quads[i].uv);
                    for (int corner=0;corner<4;++corner) {
                        const Vec3 expected=floor+(point(reference.quads[i],corner&1,corner>>1)-floor)*scale;
                        CHECK(length(point(batch.quads[i],corner&1,corner>>1)-expected)<.002);
                    }
                }
                if (const auto *boot=paint(batch,NEAR_BOOT))
                    CHECK(std::abs(socket(*boot,NEAR_BOOT.tip).y-floor.y)<.002);
                if (const auto *boot=paint(batch,FAR_BOOT))
                    CHECK(std::abs(socket(*boot,FAR_BOOT.tip).y-floor.y)<.002);
            }
            auto hidden=cues;
            hidden.scale=0;
            ConstructionBatch batch;
            draw_crew(batch,hidden);
            CHECK(batch.count==0 && !batch.overflow);
        }
    }
}

void opening_worker_size_preserves_source_faces() {
    for (const Box body : {Box{100,100,480,320},Box{100,100,800,550},Box{100,100,1600,1100}}) {
        const Box area{0,0,body.w+400,body.h+400};
        for (double multiplier : {0.0,.5,1.0,2.0}) {
            ConstructionSite reference,scaled;
            reference.build(body,body,area,42,OPEN_SECONDS);
            scaled.build(body,body,area,42,OPEN_SECONDS,multiplier);
            CHECK(reference.active() && scaled.active());
            bool saw_workers=false,saw_client=false,saw_identity=false;
            std::vector<std::array<float, 4>> reference_sources, scaled_sources;
            for (int frame=0;frame<80 && reference.active();++frame) {
                const auto &a=reference.batch(),&b=scaled.batch();
                CHECK(scaled.active() && !b.overflow);
                for (std::size_t i = 0; i < a.count; ++i)
                    if (a.quads[i].surface == ConstructionSurface::Client)
                        reference_sources.push_back(a.quads[i].uv);
                for (std::size_t i=0;i<b.count;++i) {
                    const auto &q=b.quads[i];
                    for (int corner=0;corner<4;++corner) {
                        const Vec3 p=point(q,corner&1,corner>>1);
                        CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
                    }
                    if (owns_uv(q,HEAD)) {
                        saw_workers=true;
                        const double scale=.075*std::min(body.w,body.h)/CREW_HEIGHT*multiplier;
                        const double head_height=56*(CREW_HEIGHT-23.1-13.2-.9)/(HEAD.pivot.y-HEAD.tip.y)*scale;
                        CHECK(std::abs(length(Vec3{q.v[0],q.v[1],q.v[2]})-head_height)<.002);
                    }
                    if (q.surface!=ConstructionSurface::Client) continue;
                    saw_client=true;
                    saw_identity|=full_identity(q,body);
                    // Source origins and sizes are independently rounded to float.
                    const double tolerance =
                        2 * std::numeric_limits<float>::epsilon() * std::max(body.w, body.h);
                    const bool in_bounds = q.uv[0] >= 0 && q.uv[1] >= 0 && q.uv[2] > 0 &&
                                           q.uv[3] > 0 &&
                                           double(q.uv[0]) + q.uv[2] <= body.w + tolerance &&
                                           double(q.uv[1]) + q.uv[3] <= body.h + tolerance;
                    if (!in_bounds)
                        std::fprintf(stderr,
                            "client source xywh=(%.9g,%.9g,%.9g,%.9g) capture=(%.9g,%.9g) "
                            "tolerance=%.9g\n",
                            q.uv[0],
                            q.uv[1],
                            q.uv[2],
                            q.uv[3],
                            body.w,
                            body.h,
                            tolerance);
                    CHECK(in_bounds);
                    scaled_sources.push_back(q.uv);
                }
                const animation::Step tick{.elapsed=1.0/120,.body=body,.capture=body,.goal_body=body,
                    .rest_capture=body,.screen=area,.work_area=area,.motion={},.open_enabled=true};
                reference.step(tick);
                scaled.step(tick);
            }
            const auto unique_sources = [](auto &sources) {
                std::sort(sources.begin(), sources.end());
                sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
            };
            unique_sources(reference_sources);
            unique_sources(scaled_sources);
            // Station availability may retime or repose a panel; its captured
            // source rectangles must still be the same over the whole opening.
            CHECK(reference_sources == scaled_sources);
            CHECK(saw_workers==(multiplier>0));
            CHECK(saw_client && saw_identity && !scaled.active());
        }
    }
}

void straddling_guide_must_fit_the_output() {
    constexpr Box body{0, 0, 1920, 1080};
    constexpr Box open_area{-2000, -1000, 6000, 4000};
    constexpr double scale = .075 * 1080 / CREW_HEIGHT * 2;
    ConstructionSite reference;
    reference.build(body, body, open_area, 42, OPEN_SECONDS, 2);
    CHECK(reference.active());
    if (!reference.active()) return;
    reference.step({.elapsed = OPEN_SECONDS * .4,
        .body = body,
        .capture = body,
        .goal_body = body,
        .rest_capture = body,
        .screen = open_area,
        .work_area = open_area,
        .motion = {},
        .open_enabled = true});
    const auto *face = marker_face(reference.batch(), body);
    CHECK(face);
    if (!face) return;
    const auto workers = receiving_workers(
        reference.batch(), face->uv[0], double(face->uv[0]) + face->uv[2], body.h, scale);
    CHECK(workers.guide.near_boot && workers.guide.far_boot);
    if (!workers.guide.near_boot || !workers.guide.far_boot) return;

    // Put the output edge beyond both actual planted guide soles. The window
    // still substantially overlaps the output and its fitter remains visible.
    const double edge = std::max(socket(*workers.guide.near_boot, NEAR_BOOT.tip).x,
                            socket(*workers.guide.far_boot, FAR_BOOT.tip).x) +
                        1;
    const Box straddling{edge, -1000, body.w + 400 - edge, 4000};
    ConstructionSite clipped;
    clipped.build(body, body, straddling, 42, OPEN_SECONDS, 2);
    CHECK(!clipped.active());
}

void scaled_station_decks_do_not_overlap() {
    constexpr Box body{0, 0, 1920, 1080};
    constexpr Box area{-2000, -1000, 6000, 4000};
    constexpr double scale = .075 * 1080 / CREW_HEIGHT * 2;
    bool compared = false, overlap = false;
    for (std::uint64_t seed = 0; seed < 128 && !overlap; ++seed) {
        ConstructionSite site;
        site.build(body, body, area, seed, OPEN_SECONDS, 2);
        for (int frame = 0; frame < 120 && site.active() && !overlap; ++frame) {
            std::vector<const ConstructionQuad *> decks;
            const auto &batch = site.batch();
            for (std::size_t i = 0; i < batch.count; ++i) {
                const auto &q = batch.quads[i];
                // Ordinary station deck tops are horizontal solid quads at z=-5.
                // Read their painted extents, not the scheduler's reservations.
                if (q.surface == ConstructionSurface::Solid && q.tint[3] > 0 && q.origin[2] == -5 &&
                    q.u[0] > 0 && q.u[1] == 0 && q.u[2] == 0 && q.v[0] == 0 && q.v[1] == 0 &&
                    q.v[2] >= 24)
                    decks.push_back(&q);
            }
            for (std::size_t i = 0; i < decks.size(); ++i) {
                for (std::size_t j = i + 1; j < decks.size(); ++j) {
                    const auto &a = *decks[i], &b = *decks[j];
                    if (std::abs(a.origin[1] - b.origin[1]) >= 32 * scale) continue;
                    compared = true;
                    overlap |= a.origin[0] < double(b.origin[0]) + b.u[0] &&
                               b.origin[0] < double(a.origin[0]) + a.u[0];
                }
            }
            site.step({.elapsed = OPEN_SECONDS / 120,
                .body = body,
                .capture = body,
                .goal_body = body,
                .rest_capture = body,
                .screen = area,
                .work_area = area,
                .motion = {},
                .open_enabled = true});
        }
    }
    CHECK(compared);
    CHECK(!overlap);
}

}  // namespace

int main() {
    crew_built_opening();
    opening_diversity_and_support();
    animated_capture_keeps_the_opening_pose_and_clock();
    scaled_rigs_keep_world_contacts();
    opening_worker_size_preserves_source_faces();
    straddling_guide_must_fit_the_output();
    scaled_station_decks_do_not_overlap();
    const auto early=contacts(42), delayed=contacts(43);
    const int delay=delayed.seat-early.seat;
    CHECK(delay>=30);
    CHECK(std::abs((delayed.first-early.first)-delay)<=2);
    CHECK(std::abs((delayed.strike-early.strike)-delay)<=2);
    for (auto seed : {42ULL,43ULL}) {
        installation({170,160,800,550}, seed, 60);
        installation({161.6,80.8,480,320}, seed, 120);
        installation({170,370,800,550}, seed, 60); // Flush with the bottom of the work area (y=920).
    }
    return g_failures == 0 ? 0 : 1;
}
