#include "ConstructionSite.hpp"

#include "ConstructionCrew.hpp"

#include <numbers>

#include "window/WindowAnimationFacts.hpp"

namespace hyprimate {
namespace {
using namespace construction;

constexpr double RETIRE = .93;
constexpr double CLEAR_LEDGE = .916;
constexpr double PLACEMENT_DONE = 1.09;
// Retime the receiving job here; its guide, seat, strike and release all use the same local clock.
constexpr double RECEIVING_START = .15, DELAYED_ARRIVAL = .07, RECEIVING_SPAN = .64;
constexpr double RECEIVING_FACING = -1;
// Guide and fitter are waiting on the carrier, not running on its delayed arrival clock.
constexpr double GUIDE_ENTERS = .19, GUIDE_PLANTS = .338;
constexpr double FITTER_ENTERS = .278, FITTER_PLANTS = .445;
constexpr double WITHDRAW = FASTENER_STRIKE + .11, RECEIVING_CLEAR = WITHDRAW + .185;
constexpr double BEARING_GAP = .034;
constexpr double EDGE_EPS = 1e-7;
static_assert(RECEIVING_START + DELAYED_ARRIVAL + RECEIVING_SPAN * RECEIVING_CLEAR < CLEAR_LEDGE);
// Move the actual receiving sockets here (logical px from their panel edges).
// The strike target and the painted washer share FASTENER_INSET / FASTENER_ABOVE_LEDGE.
constexpr double GUIDE_ABOVE_LEDGE = 32;
constexpr double FASTENER_INSET = 6, FASTENER_ABOVE_LEDGE = 31;

constexpr Tint NAVY{29, 46, 58, 255}, STEEL{65, 100, 111, 255}, MINT{152, 188, 175, 255};
constexpr Tint DARK_GOLD{148, 86, 47, 255}, GOLD{225, 160, 79, 255}, PALE_GOLD{247, 206, 121, 255};
constexpr Tint WHITE{255, 255, 255, 255};

double right(Box box) noexcept { return box.x + box.w; }
double bottom(Box box) noexcept { return box.y + box.h; }

double progress(double from, double to, double time) noexcept {
    return std::clamp((time - from) / (to - from), 0.0, 1.0);
}

bool usable(Box box) noexcept {
    return is_usable(box) && std::isfinite(right(box)) && std::isfinite(bottom(box));
}

Box from_edges(double left, double top, double r, double b) noexcept {
    return {left, top, r - left, b - top};
}

double overlap_width(Box a, Box b) noexcept {
    return std::max(0.0, std::min(right(a), right(b)) - std::max(a.x, b.x));
}

Box capture_source(Box bay, Box body, Box cap) noexcept {
    // Adjacent bays share edges and one sampling domain. Only the perimeter
    // pieces take nearby trim; an output-wide capture is never a moving wall.
    return from_edges(bay.x == body.x ? 0 : bay.x, bay.y == body.y ? 0 : bay.y,
        std::abs(right(bay) - right(body)) < EDGE_EPS ? cap.w : right(bay),
        std::abs(bottom(bay) - bottom(body)) < EDGE_EPS ? cap.h : bottom(bay));
}

double approach_from(Box bay, Box area, double preferred) noexcept {
    // When the receiving edge is crowded, stage from inboard instead of walking
    // off the output. Actual straddling client geometry is never clipped here.
    const double left_room = bay.x - area.x, right_room = right(area) - right(bay);
    return ((preferred < 0 ? left_room : right_room) >= std::abs(preferred) ||
        (preferred < 0 ? left_room >= right_room : right_room >= left_room)) ? preferred : -preferred;
}

double walk_phase(double phase) noexcept {
    return progress(0, CARRY_WALK_END, phase);
}

double carrier_retreat(double phase) noexcept {
    return progress(PANEL_SEAT + .047, 1, phase);
}

double pivot_x(Box bay, double facing) noexcept { return facing > 0 ? right(bay) : bay.x; }

struct Panel {
    // Held at one lower corner. The opposite bottom edge stays on or above the ledge.
    Vec3 heel, across, down, normal;
    double pivot;

    Vec3 at(double x, double y, double depth = 0) const noexcept {
        return heel + across * (x - pivot) + down * y + normal * depth;
    }
};

Panel orient_panel(Vec3 heel, double pivot, double roll, double yaw, double pitch) noexcept {
    const double cr = std::cos(roll), sr = std::sin(roll), cy = std::cos(yaw), sy = std::sin(yaw);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const auto turn = [cr, sr](Vec3 p) -> Vec3 { return {p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z}; };
    return {heel, turn({cy, 0, -sy}), turn({sy * sp, cp, cy * sp}), turn({sy * cp, -sp, cy * cp}), pivot};
}

Panel pose_receiving_panel(Box bay, double facing, double offset, double phase, double walk) noexcept {
    const double pivot = facing > 0 ? bay.w : 0;
    const Vec3 heel{pivot_x(bay, facing), bottom(bay), 0};
    if (phase >= PANEL_SEAT) return {heel, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, pivot};

    const double shift = 1 - smoothstep(CARRY_WALK_END, PANEL_SEAT, phase);
    const double raise = smoothstep(.10, .285, phase);
    const double load = .70 + .30 * raise;
    const double rock = std::sin(std::numbers::pi * walk);
    const double roll = facing * shift * std::min({.28, 46 / bay.h, 59 / bay.w}) * (load + .09 * rock);
    const double yaw = -facing * shift * std::min(.19, 49 / bay.w) * (.72 + .28 * rock);
    const double pitch = shift * std::min(.037, 9 / bay.h) * load;
    return orient_panel(heel + Vec3{offset, -(3.4 * raise + 2.5 * rock) * shift, (7.5 + 3 * rock) * shift},
        pivot, roll, yaw, pitch);
}

double placement_walk(double progress_to_seat, double pace) noexcept {
    return progress(0, std::clamp(.82 * pace, .65, .90), progress_to_seat);
}

Panel pose_placement(Box bay, const PlacementRecipe &recipe, double to_seat, double walk) noexcept {
    const double hinge = recipe.roll > 0 ? bay.w : 0;
    const Vec3 rest{bay.x + hinge, bottom(bay), 0};
    if (to_seat >= 1) return {rest, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, hinge};

    // Carry to the mouth of the opening, rotate into its rails, then make one
    // small correction as the heel goes home. The terms all decay into the seat.
    const double tempo = std::clamp(recipe.pace, .78, 1.18);
    const double transfer = 1 - smoothstep(.13 * tempo, .89, to_seat);
    const double turn = 1 - smoothstep(.36 * tempo, 1, to_seat);
    const double rock = smoothstep(.55, .76, to_seat) * (1 - smoothstep(.76, 1, to_seat));
    const bool rises_in_front = recipe.approach_y > 10;
    // During a rise, the back flange must clear the front edge of the shoe
    // across its entire width, not just at the heel.
    const double pitch = rises_in_front ? 0 : std::clamp(recipe.approach_y / bay.h, -.30, .30) * turn;
    const double front = rises_in_front ? 25 * (1 - smoothstep(.79, .90, to_seat)) : 0;
    const double depth = std::max(front, (7 + .13 * std::abs(recipe.approach_y)) * turn);
    const Vec3 heel = rest + Vec3{
        walk_offset(recipe.approach_x, walk) + recipe.nudge * rock,
        recipe.approach_y * transfer - .55 * std::abs(recipe.nudge) * rock,
        depth + 2 * rock};
    return orient_panel(heel, hinge, recipe.roll * turn * (1 - .21 * rock),
        recipe.yaw * turn * (1 - .37 * rock), pitch);
}

struct FrameJob {
    Panel panel;
    double phase, arrival, offset, departure, work;
};

void client(ConstructionBatch &batch, Box source, Vec3 origin, Vec3 u, Vec3 v) noexcept {
    if (source.w <= 0 || source.h <= 0) return;
    quad(batch, origin, u * source.w, v * source.h, WHITE, ConstructionSurface::Client,
        {static_cast<float>(source.x), static_cast<float>(source.y), static_cast<float>(source.w), static_cast<float>(source.h)});
}

void identity(ConstructionBatch &batch, Box source) noexcept {
    client(batch, source, {source.x, source.y, 0}, {1, 0, 0}, {0, 1, 0});
}

void outsets(ConstructionBatch &batch, Box cap, Box body) noexcept {
    identity(batch, {0, 0, cap.w, body.y});
    identity(batch, {0, body.y, body.x, body.h});
    identity(batch, {right(body), body.y, cap.w - right(body), body.h});
    identity(batch, {0, bottom(body), cap.w, cap.h - bottom(body)});
}

void line(ConstructionBatch &batch, Vec3 a, Vec3 b, double width, Tint color) noexcept {
    const Vec3 along = b - a;
    const Vec3 side = unit({along.y, -along.x, 0}, {1, 0, 0}) * width;
    quad(batch, a - side * .5, along, side, color);
}

void moving_face(ConstructionBatch &batch, Box bay, Box source, const Panel &p, double to_seat) noexcept {
    const double w = bay.w, h = bay.h;
    const double trim = 1 - smoothstep(.585 / PANEL_SEAT, 1, to_seat);
    constexpr double BACK = -5.5, LIP = 2.4;
    // The flange sits outside the glazing, with distinct geometric returns.
    // No opaque backing underneath client alpha, even when the panel is pitched.
    quad(batch, p.at(-LIP, -h - LIP, BACK), p.across * LIP, p.down * (h + 2 * LIP), tint(STEEL, trim));
    quad(batch, p.at(w, -h - LIP, BACK), p.across * LIP, p.down * (h + 2 * LIP), tint(NAVY, trim));
    quad(batch, p.at(0, -h - LIP, BACK), p.across * w, p.down * LIP, tint(MINT, trim));
    quad(batch, p.at(0, 0, BACK), p.across * w, p.down * LIP, tint(STEEL, trim));
    quad(batch, p.at(0, -h, BACK), p.normal * -BACK, p.down * h, tint(STEEL, trim));
    quad(batch, p.at(w, -h, BACK), p.normal * -BACK, p.down * h, tint(NAVY, trim));
    quad(batch, p.at(0, -h, BACK), p.across * w, p.normal * -BACK, tint(PALE_GOLD, trim));
    quad(batch, p.at(0, 0, BACK), p.across * w, p.normal * -BACK, tint(DARK_GOLD, trim));
    client(batch, source, p.at(source.x - bay.x, source.y - bottom(bay)), p.across, p.down);
}

void receiving_ledge(ConstructionBatch &batch, Box bay, Box area, double y,
    double opacity, double offset, double departure) noexcept {
    if (departure >= 1) return;
    opacity *= 1 - smoothstep(.1, 1, departure);
    const double x = std::max(area.x, bay.x + std::min(-57.0, offset - 13));
    const double r = std::min(right(area), right(bay) + std::max(0.0, offset) + 5);
    const double height = std::min(5.0, bottom(area) - y);
    if (r <= x || height <= 0 || y < area.y) return;
    const Vec3 top{x, y, -7}, width{r - x, 0, 0}, depth{0, 0, 22};
    quad(batch, top, width, depth, tint({137, 107, 69, 255}, opacity));
    quad(batch, top + depth, width, {0, height, 0}, tint(NAVY, opacity));
    line(batch, top + depth, top + depth + width, 1.5, tint(GOLD, opacity));

    const double height_below = std::min(13.0, bottom(area) - y - height);
    if (height_below <= 0) return;
    // A landed panel rests on the ledge; upper ledges brace into the bay below it.
    for (const double u : {.16, .84}) {
        const Vec3 bracket = top + depth + width * u + Vec3{0, height, 0};
        line(batch, bracket, bracket + Vec3{0, height_below, -8}, 3.5, tint(STEEL, opacity));
        line(batch, bracket + Vec3{-8, 0, 0}, bracket + Vec3{0, height_below, -8}, 2.1, tint(DARK_GOLD, opacity));
    }
}

void station_deck(ConstructionBatch &batch, const auto &station, double opacity) noexcept {
    constexpr Vec3 DEPTH{0, 0, 24};
    const Vec3 top{station.deck_left, station.floor.y, -5}, width{station.deck_right - station.deck_left, 0, 0};
    quad(batch, top, width, DEPTH, tint({137, 107, 69, 255}, opacity));
    quad(batch, top + DEPTH, width, {0, 4, 0}, tint(NAVY, opacity));
    line(batch, top + DEPTH, top + DEPTH + width, 1.4, tint(GOLD, opacity));
    const Vec3 mount{station.bearing, station.bearing_y, -5};
    line(batch, {station.floor.x, station.floor.y + 4, 17}, mount, 3.1, tint(STEEL, opacity));
    line(batch, mount + Vec3{-4, -5, 0}, mount, 3, tint(NAVY, opacity));
    if (station.ladder_top.y < station.ladder_base.y) {
        // The top ties to the shoe, not to the moving face. The crew rig paints
        // the occupied rails/rungs.
        const Vec3 post = station.ladder_top - Vec3{0, 0, 8};
        line(batch, post, station.ladder_top, 3, tint(STEEL, opacity));
        line(batch, post, mount, 3.2, tint(STEEL, opacity));
    }
}

Vec3 face_socket(const Panel &panel, Box bay, Vec3 target) noexcept {
    // A high corner can be out of reach while the broad face is still available.
    const Vec3 delta = target - panel.heel;
    const double x = std::clamp(panel.pivot + dot(delta, panel.across), 0.0, bay.w);
    const double y = std::clamp(dot(delta, panel.down), -bay.h + 5, -3.0);
    return panel.at(x, y);
}

double helping_until(const PlacementRecipe &recipe) noexcept {
    if (recipe.weld) return WELD_COMPLETE;
    return recipe.fasten ? FASTENER_STRIKE : PANEL_SEAT;
}

double placement_work(const PlacementRecipe &recipe, double to_seat, double strike,
    double seat, double clear, double time) noexcept {
    if (time < seat) return PANEL_SEAT * to_seat;
    if (!recipe.weld && !recipe.fasten)
        return std::lerp(PANEL_SEAT, PLACEMENT_DONE, progress(seat, clear, time));
    if (time <= strike) return std::lerp(PANEL_SEAT, FASTENER_STRIKE, progress(seat, strike, time));
    const double finish = recipe.weld ? std::min(clear - .01, strike + .026) : strike;
    if (time < finish) return std::lerp(FASTENER_STRIKE, WELD_COMPLETE, progress(strike, finish, time));
    return std::lerp(helping_until(recipe), PLACEMENT_DONE, progress(finish, clear, time));
}

void placement_crew(ConstructionBatch &batch, Box bay, const PlacementRecipe &recipe, const FrameJob &frame,
    const auto &station, bool tool_worker) noexcept {
    const bool on_ladder = station.ladder_top.y < station.ladder_base.y;
    const double arrival = station.guide ? progress(0, .57, frame.phase) : frame.arrival;
    const double f = station.facing;
    const Vec3 stand = on_ladder ? mix(station.ladder_base, station.ladder_top, 7.0 / 12) :
        station.floor + Vec3{walk_offset(station.walk, arrival), 0, 0};
    const Vec3 far = face_socket(frame.panel, bay, stand + Vec3{f * 13.5, -34, -4});
    const Vec3 near = face_socket(frame.panel, bay, stand + Vec3{f * 16.0, -26, -4});
    const bool travelling = std::abs(recipe.approach_x) > 16 && std::abs(recipe.approach_y) < 16;
    CrewJob role = station.guide ? CrewJob::Guide : travelling ? CrewJob::Carry : CrewJob::Brace;
    Vec3 contact = near;
    const bool finishing = tool_worker && frame.work >= PANEL_SEAT;
    if (finishing) {
        role = recipe.weld ? CrewJob::Weld : CrewJob::Fasten;
        const Vec3 foot = on_ladder ? stand : station.floor;
        contact = {std::clamp(foot.x + f * 19.0, bay.x + 5, right(bay) - 5),
            std::clamp(foot.y - 31, bay.y + 6, bottom(bay) - 6), 5};
        const double opacity = 1 - smoothstep(.1, .9, frame.departure);
        // Physical seam plate and shared tool target; keep the dedicated
        // receiver's washer and response distinct.
        quad(batch, contact + Vec3{-3, -3, -5}, {0, 0, 4}, {0, 6, 0}, tint(STEEL, opacity));
        quad(batch, contact + Vec3{-3, -3, -1}, {6, 0, 0}, {0, 6, 0}, tint(NAVY, opacity));
        line(batch, contact + Vec3{-1.7, 0, 0}, contact + Vec3{1.7, 0, 0}, 2, tint(STEEL, opacity));
    }
    draw_crew(batch, {.floor = station.floor, .far_contact = far, .near_contact = contact, .job = role,
        .facing = f, .approach = station.walk, .arrival = arrival, .work = frame.work,
        .departure = frame.departure, .hold_until = helping_until(recipe), .cadence = 1 / recipe.pace,
        .ladder = {.base = station.ladder_base, .top = station.ladder_top},
        .aim_contacts = true, .steady_during_finish = finishing});
}

void contact_response(ConstructionBatch &batch, Vec3 bolt, double phase, double opacity) noexcept {
    const double hit = (phase - FASTENER_STRIKE) / .048;
    stamp(batch, FASTENER, bolt, (phase < FASTENER_STRIKE ? .155 : .119), opacity);
    if (hit < 0 || hit >= 1) return;
    // One impact as the washer seats; not a dust loop.
    constexpr std::array<Vec2, 3> RAYS{{{1, -.56}, {.47, -.91}, {.87, .61}}};
    for (const auto ray : RAYS) {
        const Vec3 direction{ray.x, ray.y, 0};
        const Vec3 from = bolt + direction * (2.8 + 7 * hit);
        line(batch, from, from + direction * (4.2 * (1 - hit)), 1.35, tint(PALE_GOLD, opacity * (1 - hit)));
    }
}

void receiving_crew(ConstructionBatch &batch, Box bay, const Panel &panel, const auto &guide_station,
    const auto &fitter_station, double time, double phase) noexcept {
    const double guide_done = progress(.735, .918, phase);
    const Vec3 guide = panel.at(0, guide_station.floor.y - bottom(bay) - GUIDE_ABOVE_LEDGE);
    // The guide arrives on the same cue in both takes; the delayed carrier makes
    // them keep the ready stance longer rather than playing the same pose later.
    draw_crew(batch, {.floor = guide_station.floor, .far_contact = guide, .job = CrewJob::Guide,
        .facing = guide_station.facing, .approach = guide_station.walk,
        .arrival = progress(GUIDE_ENTERS, GUIDE_PLANTS, time),
        .work = phase, .departure = guide_done});

    const Vec3 bolt{right(bay) - FASTENER_INSET, fitter_station.floor.y - FASTENER_ABOVE_LEDGE, 5};
    const double fastener_done = progress(WITHDRAW, RECEIVING_CLEAR, phase);
    if (phase >= PANEL_SEAT && fastener_done < 1)
        contact_response(batch, bolt, phase, 1 - smoothstep(0, .82, fastener_done));
    draw_crew(batch, {.floor = fitter_station.floor, .near_contact = bolt, .job = CrewJob::Fasten,
        .facing = fitter_station.facing, .approach = fitter_station.walk,
        .arrival = progress(FITTER_ENTERS, FITTER_PLANTS, time),
        .work = phase, .departure = fastener_done});
}

Box projected_bounds(const ConstructionBatch &batch, Box body, Box capture) noexcept {
    const double focal = 4 * std::max({body.w, body.h, 64.0});
    const Vec2 center{body.x + body.w * .5, body.y + body.h * .5};
    double min_x = 0, min_y = 0, max_x = capture.w, max_y = capture.h;
    for (std::size_t i = 0; i < batch.count; ++i) {
        const auto &q = batch.quads[i];
        for (unsigned corner = 0; corner != 4; ++corner) {
            const double u = (corner & 1U) != 0, v = (corner & 2U) != 0;
            const double w = 1.0 - (q.origin[2] + u * q.u[2] + v * q.v[2]) / focal;
            const double x = center.x + (q.origin[0] + u * q.u[0] + v * q.v[0] - center.x) / w;
            const double y = center.y + (q.origin[1] + u * q.u[1] + v * q.v[1] - center.y) / w;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    return {min_x - 2, min_y - 2, max_x - min_x + 4, max_y - min_y + 4};
}
}  // namespace

// Only the receiver is authored as a fixed beat. The rest is a rectangular
// partition over shared cuts, with courses that can bear across several seats.
void ConstructionSite::build(Box body, Box capture, Box work_area, std::uint64_t seed, double duration) {
    m_body = body;
    m_capture = capture;
    m_duration = duration;
    m_elapsed = 0;
    m_active = false;
    m_job_count = 0;
    m_batch.count = 0;
    m_batch.overflow = false;
    m_bounds = {};
    constexpr double MIN_WIDTH = 8 * CREW_HEIGHT, MIN_HEIGHT = 5.5 * CREW_HEIGHT;
    if (!usable(body) || !usable(capture) || !usable(work_area) || !std::isfinite(duration) || duration <= 0 ||
        body.w < MIN_WIDTH || body.h < MIN_HEIGHT) return;

    const Box local{body.x - capture.x, body.y - capture.y, body.w, body.h};
    const Box area{work_area.x - capture.x, work_area.y - capture.y, work_area.w, work_area.h};
    // Do not invent client pixels if the capture does not contain its body. A site
    // can straddle outputs but needs enough visible area to stage actual workers.
    if (!usable(local) || !usable(area) || local.x < 0 || local.y < 0 ||
        right(local) > capture.w || bottom(local) > capture.h ||
        std::min(right(local), right(area)) - std::max(local.x, area.x) < 2 * CREW_HEIGHT ||
        std::min(bottom(local), bottom(area)) - std::max(local.y, area.y) < CREW_HEIGHT) return;

    m_local_body = local;
    m_work_area = area;
    m_oversized_outsets = std::max({local.x, local.y, capture.w - right(local), capture.h - bottom(local)}) > 48;
    const Box cap{0, 0, capture.w, capture.h};
    const bool delayed = (seed & 1U) != 0;
    const double x1 = local.x + local.w * std::lerp(.275, .345, unit_draw(seed));
    const double x2 = local.x + local.w * std::lerp(.59, .645, unit_draw(seed));
    const double lower_top = local.y + local.h * std::lerp(.50, .63, unit_draw(seed));
    const double receiver_top = local.y + local.h * std::lerp(.58, .64, unit_draw(seed));
    const double middle_top = local.y + local.h * std::lerp(.28, .41, unit_draw(seed));
    const double footing_top = local.y + local.h * std::lerp(.80, .83, unit_draw(seed));
    const double r = right(local), b = bottom(local);
    const bool small = local.w < 590 || local.h < 390;
    const bool wide = local.w >= 1040 && local.h >= 520;
    const unsigned crew_limit = small ? 4 : wide ? 8 : 6;
    const auto add = [&](double left, double top, double right_edge, double bottom_edge) {
        auto &job = m_jobs[m_job_count++];
        job.bay = from_edges(left, top, right_edge, bottom_edge);
        job.source = m_oversized_outsets ? job.bay : capture_source(job.bay, local, cap);
        job.workers = 0;
    };
    add(x2, receiver_top, r, b);
    auto &receive = m_jobs[RECEIVER];
    receive.start = RECEIVING_START + (delayed ? DELAYED_ARRIVAL : 0);
    receive.clear = receive.start + RECEIVING_SPAN;
    receive.seating = receive.start + PANEL_SEAT * RECEIVING_SPAN;
    // An edge-flush window has no apron below the sill. Clamp only the
    // temporary work decks and reach targets, never the facade or its crops.
    const double ground_floor = std::min(b, bottom(area) - 5);
    const Box staging_body = from_edges(local.x, local.y, r, ground_floor);
    const double carrier_x = x2 + 29, guide_x = x2 - 16, fitter_x = r - 26;
    if (guide_x - 13 < area.x || fitter_x + 13 > right(area) ||
        ground_floor < std::max(area.y, receiver_top) + CREW_HEIGHT) return;
    const double carrier_walk = std::clamp(approach_from(receive.bay, area, 34),
        -17.0, std::min(receive.bay.w - 109, right(area) - 13 - carrier_x));
    const double guide_walk = std::max(-28.0, area.x + 13 - guide_x);
    const double fitter_walk = std::max(-26.0, area.x + 13 - fitter_x);

    const double bond = unit_draw(seed), header = unit_draw(seed), cap_bond = unit_draw(seed);
    if (bond < .15) {
        // Two plinths carry one deep upper mass; the seam stops at the lintel.
        add(local.x, lower_top, x1, b);
        add(x1, lower_top, x2, b);
        add(local.x, local.y, x2, lower_top);
    } else if (bond < .45) {
        // A stepped silhouette: the inner tower goes up on a low shoe while
        // the tall outer pier can take its cap without waiting for the tower.
        const double pier_top = header < .40 ? middle_top : lower_top;
        add(local.x, pier_top, x1, b);
        add(x1, footing_top, x2, b);
        add(local.x, local.y, x1, pier_top);
        add(x1, middle_top, x2, footing_top);
        add(x1, local.y, x2, middle_top);
    } else {
        const double base_top = local.y + local.h * std::lerp(.716, .778, unit_draw(seed));
        add(local.x, base_top, x1, b);
        add(x1, base_top, x2, b);
        // Running bond: stagger the seams on successive courses. Occasionally
        // use a stretcher across the entire course instead of two middle bays.
        const double middle_cut = std::lerp(local.x, x2, std::lerp(.33, .45, unit_draw(seed)));
        const double crown_cut = std::lerp(local.x, x2, std::lerp(.56, .67, unit_draw(seed)));
        if (header < .54 || small) {
            add(local.x, middle_top, x2, base_top);
        } else {
            add(local.x, middle_top, middle_cut, base_top);
            add(middle_cut, middle_top, x2, base_top);
        }
        if (small && header >= .54) {
            add(local.x, local.y, x2, middle_top);
        } else {
            add(local.x, local.y, crown_cut, middle_top);
            add(crown_cut, local.y, x2, middle_top);
        }
    }
    if (!small && cap_bond < .38) {
        const double split = std::lerp(x2, r, std::lerp(.42, .58, unit_draw(seed)));
        add(x2, local.y, split, receiver_top);
        add(split, local.y, r, receiver_top);
    } else {
        add(x2, local.y, r, receiver_top);
    }

    using Mask = std::uint16_t;
    static_assert(MAX_JOBS >= 9 && MAX_JOBS <= 16);
    const auto bit = [](std::size_t i) { return static_cast<Mask>(1U << i); };
    struct Ticket {
        Mask bearings;
        double priority, traverse, side, span;
        std::uint64_t pose_seed;
    };
    std::array<Ticket, MAX_JOBS> tickets;
    std::uint64_t route_seed = seed ^ 0xe7037ed1a0b428dbULL;
    for (std::size_t i = 1; i < m_job_count; ++i) {
        const Box bay = m_jobs[i].bay;
        auto &ticket = tickets[i];
        const bool ground = std::abs(bottom(bay) - b) < EDGE_EPS;
        ticket.bearings = 0;
        if (!ground) {
            double bearing_width = 0;
            for (std::size_t k = 0; k < m_job_count; ++k) {
                const Box below = m_jobs[k].bay;
                const double span = overlap_width(bay, below);
                if (span <= EDGE_EPS || std::abs(bottom(bay) - below.y) >= EDGE_EPS) continue;
                ticket.bearings |= bit(k);
                bearing_width += span;
            }
            if (std::abs(bearing_width - bay.w) >= EDGE_EPS) return;
        }
        ticket.priority = unit_draw(route_seed);
        const double mass = smoothstep(18000, 95000, bay.w * bay.h);
        const double distance = unit_draw(route_seed);
        const bool traverse = ground || ((ticket.bearings & bit(RECEIVER)) && distance > .60);
        ticket.traverse = traverse ? std::lerp(41.0, 53.0, distance) * std::lerp(1.12, .84, mass) :
            std::lerp(6, 12, distance);
        ticket.side = unit_draw(route_seed) < .5 ? -1 : 1;
        ticket.span = small ? (ground ? .212 : std::lerp(.166, .178, mass)) :
            (ground ? std::lerp(.268, .318, mass) : std::lerp(.238, .292, mass));
        ticket.pose_seed = splitmix64(route_seed);
    }

    struct Reservation { std::size_t job; double start, end, left, right, y; };
    std::array<Reservation, MAX_JOBS * 4> occupied;
    std::size_t occupied_count = 0;
    const auto reserve_receiver = [&](std::size_t worker, double start, double end,
        Vec3 floor, double facing, double walk) {
        const double left = floor.x + std::min(0.0, walk) - 13, right_edge = floor.x + std::max(0.0, walk) + 13;
        receive.stations[worker] = {.floor = floor, .facing = facing, .walk = walk,
            .deck_left = left, .deck_right = right_edge, .bearing = floor.x,
            .bearing_y = std::min(floor.y + 11, bottom(area)), .guide = worker == 1};
        occupied[occupied_count++] = {RECEIVER, start, end, left, right_edge, floor.y};
    };
    reserve_receiver(0, receive.start, receive.clear, {carrier_x, ground_floor, 9}, RECEIVING_FACING, carrier_walk);
    reserve_receiver(1, GUIDE_ENTERS, receive.start + .918 * RECEIVING_SPAN, {guide_x, ground_floor, 8}, 1, guide_walk);
    reserve_receiver(2, FITTER_ENTERS, receive.start + RECEIVING_CLEAR * RECEIVING_SPAN,
        {fitter_x, ground_floor, 9}, 1, fitter_walk);

    const auto crew_room = [&](std::size_t index, double start, double end) {
        unsigned peak = 0;
        const auto sample = [&](double time) {
            unsigned busy = 0;
            for (std::size_t k = 0; k < occupied_count; ++k) {
                const auto &other = occupied[k];
                busy += other.job != index && other.start <= time && time < other.end;
            }
            peak = std::max(peak, busy);
        };
        sample(start);
        for (std::size_t k = 0; k < occupied_count; ++k)
            if (occupied[k].job != index && occupied[k].start > start && occupied[k].start < end)
                sample(occupied[k].start);
        return crew_limit - peak;
    };
    const auto reserve_stations = [&](std::size_t index, const Job &job) {
        for (std::size_t k = 0; k < job.workers; ++k) {
            const auto &station = job.stations[k];
            occupied[occupied_count++] = {index, job.start, job.clear, station.deck_left,
                station.deck_right, station.floor.y};
        }
    };

    Mask scheduled = bit(RECEIVER);
    const auto plan_stations = [&](std::size_t index, Job &job, unsigned room) {
        const auto &ticket = tickets[index];
        const Box bay = job.bay;
        const bool supported = ticket.bearings != 0;
        const double y = supported ? bottom(bay) : ground_floor;
        if (y - bay.y < 20) return false; // No useful face within reach of this deck.
        std::array<double, 2> spaces{std::max(0.0, bay.x - area.x), std::max(0.0, right(area) - right(bay))};
        for (std::size_t k = 0; k < m_job_count; ++k) {
            if (k == index || !(scheduled & bit(k))) continue;
            const auto &other = m_jobs[k];
            if (other.seating > job.start || bottom(other.bay) <= bay.y + EDGE_EPS ||
                bottom(bay) <= other.bay.y + EDGE_EPS) continue;
            if (right(other.bay) <= bay.x + EDGE_EPS)
                spaces[0] = std::min(spaces[0], std::max(0.0, bay.x - right(other.bay)));
            if (other.bay.x >= right(bay) - EDGE_EPS)
                spaces[1] = std::min(spaces[1], std::max(0.0, other.bay.x - right(bay)));
        }
        const std::size_t preferred = ticket.side < 0 ? 0 : 1;
        const std::size_t side = spaces[preferred] >= ticket.traverse || spaces[preferred] >= spaces[1 - preferred] ?
            preferred : 1 - preferred;
        const double direction = side == 0 ? -1 : 1;
        auto pose_seed = ticket.pose_seed;
        auto &recipe = job.placement;
        const Box station_bay = supported ? bay : from_edges(bay.x, bay.y, right(bay), y);
        recipe = compose_placement(station_bay, supported ? local : staging_body, area,
            direction * ticket.traverse, supported, false, pose_seed, room);
        if (!recipe.staffed) return false;
        recipe.approach_x = direction * std::min(std::abs(recipe.approach_x), spaces[side]);
        job.workers = 0;

        const auto overlaps = [&](const Reservation &other) {
            return other.job != index && job.start < other.end && other.start < job.clear &&
                std::abs(y - other.y) < 32;
        };
        const auto separate = [](double left, double r, double other_left, double other_right) {
            return r + 2 <= other_left || left >= other_right + 2;
        };
        const auto place = [&](double nominal, double facing, double walk, bool guide) {
            const double extent_left = std::min(0.0, walk) - 13, extent_right = std::max(0.0, walk) + 13;
            const double lo = guide ? nominal : std::max(area.x, bay.x) - extent_left;
            const double hi = guide ? nominal : std::min(right(area), right(bay)) - extent_right;
            if (lo > hi) return false;
            double x = 0, distance = INFINITY;
            const auto consider = [&](double candidate) {
                if (candidate < lo || candidate > hi || candidate + extent_left < area.x ||
                    candidate + extent_right > right(area) || std::abs(candidate - nominal) >= distance) return;
                for (std::size_t k = 0; k < occupied_count; ++k) {
                    const auto &other = occupied[k];
                    if (overlaps(other) && !separate(candidate + extent_left, candidate + extent_right,
                        other.left, other.right)) return;
                }
                for (std::size_t k = 0; k < job.workers; ++k) {
                    const auto &other = job.stations[k];
                    if (!separate(candidate + extent_left, candidate + extent_right,
                        other.deck_left, other.deck_right)) return;
                }
                x = candidate;
                distance = std::abs(candidate - nominal);
            };
            consider(std::clamp(nominal, lo, hi));
            if (!std::isfinite(distance) && !guide) {
                // The nearest free walk is at a lane boundary, not at an
                // arbitrary sampled point in a crowded deck.
                const auto beside = [&](double left, double right_edge) {
                    consider(left - extent_right - 2.25);
                    consider(right_edge - extent_left + 2.25);
                };
                for (std::size_t k = 0; k < occupied_count; ++k)
                    if (overlaps(occupied[k])) beside(occupied[k].left, occupied[k].right);
                for (std::size_t k = 0; k < job.workers; ++k)
                    beside(job.stations[k].deck_left, job.stations[k].deck_right);
            }
            if (!std::isfinite(distance)) return false;

            double bearing = std::clamp(x, bay.x + 5, right(bay) - 5);
            double closest = INFINITY;
            for (std::size_t k = 0; k < m_job_count; ++k) {
                if (!(ticket.bearings & bit(k))) continue;
                const Box below = m_jobs[k].bay;
                const double edge = std::max(bay.x, below.x), end = std::min(right(bay), right(below));
                const double inset = std::min(5.0, (end - edge) * .5);
                const double socket = std::clamp(x, edge + inset, end - inset);
                if (std::abs(socket - x) >= closest) continue;
                bearing = socket;
                closest = std::abs(socket - x);
            }
            auto &station = job.stations[job.workers++];
            station = {.floor = {x, y, recipe.approach_y > 10 ? 14.0 : 9.0},
                .facing = facing, .walk = walk, .deck_left = x + extent_left, .deck_right = x + extent_right,
                .bearing = bearing, .bearing_y = std::min(y + 11, bottom(area)), .guide = guide};
            return true;
        };
        const double heel = recipe.roll > 0 ? 1 : -1;
        for (std::size_t k = 0; k < recipe.carriers; ++k) {
            const double facing = k == 1 ? -heel : heel;
            const double nominal = k == 2 ? bay.x + .5 * bay.w : pivot_x(bay, facing) - facing * 29;
            if (place(nominal, facing, recipe.approach_x, false)) continue;
            if (recipe.carriers != 1 || !place(nominal, facing, 0, false)) return false;
            recipe.approach_x = 0;
        }
        if (recipe.carriers == 1) {
            // If the clear lane is across the bay, hinge at the actual carrier
            // instead of asking that worker to hold the high, unsupported end.
            auto &carrier = job.stations[0];
            const double working_heel = carrier.floor.x < bay.x + .5 * bay.w ? -1 : 1;
            if (working_heel != heel) {
                carrier.facing = working_heel;
                recipe.roll = -recipe.roll;
                if (recipe.approach_y > 10) recipe.yaw = -recipe.yaw;
            }
        }
        if (recipe.side_guide) {
            const double access = recipe.access_side, edge = pivot_x(bay, access);
            if (!place(edge + access * 16, -access, 0, true)) return false;
            if (recipe.ladder) {
                auto &guide = job.stations[job.workers - 1];
                guide.ladder_base = {guide.floor.x + access * 3, y, 14};
                guide.ladder_top = {edge + access * 2, y - 34, 6};
            }
        }
        job.seating = std::lerp(job.start, job.clear, std::clamp(.56 * recipe.pace, .51, .63));
        job.strike = std::min(job.clear - .031, job.seating + .062);
        // Keep tool work only when there is room to plant, strike and withdraw.
        if (job.strike - job.seating < .035) recipe.weld = recipe.fasten = false;
        return true;
    };

    // Dispatch the earliest ready job into a real station/crew slot. Equal
    // openings use seeded priority, not geometric iteration order.
    double next_ingress = 0;
    for (std::size_t n = 1; n < m_job_count; ++n) {
        std::size_t selected = MAX_JOBS;
        Job next{};
        for (std::size_t i = 1; i < m_job_count; ++i) {
            const Ticket &ticket = tickets[i];
            if ((scheduled & bit(i)) || (ticket.bearings & ~scheduled)) continue;
            double ready = next_ingress;
            for (std::size_t k = 0; k < m_job_count; ++k)
                if (ticket.bearings & bit(k)) ready = std::max(ready, m_jobs[k].seating + BEARING_GAP);
            Job candidate{};
            candidate.bay = m_jobs[i].bay;
            candidate.source = m_jobs[i].source;
            const auto consider = [&](double start) {
                if (selected != MAX_JOBS && (start > next.start ||
                    (start == next.start && ticket.priority >= tickets[selected].priority))) return;
                const double end = std::min(start + ticket.span, CLEAR_LEDGE);
                if (end - start < .115 || crew_room(i, start, end) == 0) return;
                candidate.start = start;
                candidate.clear = end;
                if (!plan_stations(i, candidate, 1)) return;
                selected = i;
                next = candidate;
            };
            consider(ready);
            // Availability only changes at a booked release; never resample
            // time or leave an unstaffed face in the publication.
            for (std::size_t k = 0; k < occupied_count; ++k)
                if (occupied[k].end > ready) consider(occupied[k].end);
        }
        if (selected == MAX_JOBS) return; // No supported setup: keep native opening.
        m_jobs[selected] = next;
        reserve_stations(selected, next);
        scheduled |= bit(selected);
        next_ingress = next.start + .060;
    }

    // Mandatory carriers own their stations first. Extra grips and guide
    // ladders must fit without bumping anyone or retiming the load.
    for (std::size_t i = 1; i < m_job_count; ++i) {
        auto &job = m_jobs[i];
        const auto room = crew_room(i, job.start, job.clear);
        if (room < 2) continue;
        auto with_helpers = job;
        if (!plan_stations(i, with_helpers, room) || with_helpers.workers <= job.workers) continue;
        for (std::size_t k = 0; k < occupied_count; ++k) {
            if (occupied[k].job != i) continue;
            occupied[k] = occupied[--occupied_count];
            break; // One mandatory reservation per ordinary job.
        }
        job = with_helpers;
        reserve_stations(i, job);
    }
    m_active = true;
    publish(0);
}

void ConstructionSite::step(const animation::Step &step) noexcept {
    if (!m_active) return;
    if (step.stalled || step.dying || !std::isfinite(step.elapsed) || step.elapsed < 0 || step.elapsed > STALL_SECONDS) {
        m_active = false;
    } else if (step.elapsed > 0) {
        const bool before_identity = m_elapsed / m_duration < RETIRE;
        m_elapsed += step.elapsed;
        // If ordinary frame cadence skips the tail, still publish identity once.
        m_active = m_elapsed < m_duration || before_identity;
        if (m_active) publish(m_elapsed / m_duration);
    }
    if (!m_active) {
        m_batch.count = 0;
        m_batch.overflow = false;
        m_bounds = {0, 0, m_capture.w, m_capture.h};
    }
}

void ConstructionSite::publish(double time) noexcept {
    auto &batch = m_batch;
    batch.count = 0;
    batch.overflow = false;
    const Box cap{0, 0, m_capture.w, m_capture.h};
    // Keep rendering through a deliberate exact identity interval before expiring.
    if (time >= RETIRE) {
        identity(batch, cap);
        m_bounds = cap;
        return;
    }

    if (m_oversized_outsets) outsets(batch, cap, m_local_body);
    std::array<FrameJob, MAX_JOBS> frame;
    std::array<std::size_t, MAX_JOBS> moving;
    std::size_t moving_count = 0;
    for (std::size_t i = 0; i < m_job_count; ++i) {
        const auto &job = m_jobs[i];
        const bool receiving = i == RECEIVER;
        if (time < job.start && !receiving) continue;
        const double span = receiving ? RECEIVING_SPAN : job.clear - job.start;
        const double phase = (time - job.start) / span;
        const bool seated = time >= job.seating;
        if (seated) identity(batch, job.source);
        if (!receiving && time >= job.clear) continue;

        auto &current = frame[i];
        if (receiving) {
            const double arrival = walk_phase(phase);
            const double offset = walk_offset(job.stations[0].walk, arrival);
            const double departure = carrier_retreat(phase);
            current = {pose_receiving_panel(job.bay, RECEIVING_FACING, offset, phase, arrival),
                phase, arrival, offset, departure, phase};
        } else {
            const auto &recipe = job.placement;
            const double to_seat = progress(job.start, job.seating, time);
            const double arrival = placement_walk(to_seat, recipe.pace);
            const double work = placement_work(recipe, to_seat, job.strike, job.seating, job.clear, time);
            const double retreat = progress(helping_until(recipe) + .067, PLACEMENT_DONE, work);
            current = {pose_placement(job.bay, recipe, to_seat, arrival), to_seat, arrival, 0, retreat, work};
        }
        if (!seated && phase >= (receiving ? .067 : .048)) {
            // A few overlapping live faces in stable rear-to-front paint order.
            const auto depth = [&](std::size_t index) {
                const auto &p = frame[index].panel;
                const Box bay = m_jobs[index].bay;
                return p.heel.z + p.across.z * (.5 * bay.w - p.pivot) - p.down.z * .5 * bay.h;
            };
            const double z = depth(i);
            std::size_t pos = moving_count++;
            while (pos > 0 && depth(moving[pos - 1]) > z) {
                moving[pos] = moving[pos - 1];
                --pos;
            }
            moving[pos] = i;
        }
    }
    const auto &marker = m_jobs[RECEIVER];
    const auto &receive = frame[RECEIVER];
    const auto &carrier = marker.stations[0];
    const double receive_from = std::min(marker.start, GUIDE_ENTERS);
    if (time >= receive_from) {
        const double departure = progress(WITHDRAW, RECEIVING_CLEAR, receive.phase);
        receiving_ledge(batch, marker.bay, m_work_area, carrier.floor.y,
            smoothstep(receive_from, receive_from + .048, time), receive.offset, departure);
    }
    for (std::size_t i = 1; i < m_job_count; ++i) {
        const auto &job = m_jobs[i];
        if (time < job.start || time >= job.clear) continue;
        const auto &f = frame[i];
        const double placed = job.start == 0 ? 1 : smoothstep(0, .13, f.phase);
        const double opacity = placed * (1 - smoothstep(.38, 1, f.departure));
        for (std::size_t k = 0; k < job.workers; ++k) station_deck(batch, job.stations[k], opacity);
    }
    for (std::size_t k = 0; k < moving_count; ++k) {
        const auto i = moving[k];
        moving_face(batch, m_jobs[i].bay, m_jobs[i].source, frame[i].panel,
            i == RECEIVER ? frame[i].phase / PANEL_SEAT : frame[i].phase);
    }
    if (time >= marker.start && receive.departure < 1) {
        const double facing = carrier.facing, hinge = receive.panel.pivot;
        const double inset_y = carrier.floor.y - bottom(marker.bay);
        draw_crew(batch, {.floor = carrier.floor,
            .far_contact = receive.panel.at(hinge - facing * 13, inset_y - 24),
            .near_contact = receive.panel.at(hinge - facing * 21, inset_y - 32), .job = CrewJob::Carry,
            .facing = facing, .approach = carrier.walk, .arrival = receive.arrival,
            .work = receive.phase, .departure = receive.departure});
    }
    receiving_crew(batch, marker.bay, receive.panel, marker.stations[1], marker.stations[2], time, receive.phase);
    for (std::size_t i = 1; i < m_job_count; ++i) {
        const auto &job = m_jobs[i];
        if (time < job.start || time >= job.clear) continue;
        const std::size_t last = job.workers - 1;
        const std::size_t finisher = job.stations[last].guide ? last : 0;
        for (std::size_t k = 0; k < job.workers; ++k)
            placement_crew(batch, job.bay, job.placement, frame[i], job.stations[k],
                (job.placement.fasten || job.placement.weld) && k == finisher);
    }
    m_bounds = projected_bounds(batch, m_local_body, cap);
}

}  // namespace hyprimate
