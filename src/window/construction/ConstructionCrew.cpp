#include "ConstructionCrew.hpp"

#include <numbers>

namespace hyprimate::construction {
namespace {
constexpr double ARM_UPPER = 10.5, ARM_LOWER = 10.2;
constexpr double THIGH_LENGTH = 11.3, SHIN_LENGTH = 10.8;
constexpr double GLOVE_REACH = 3.5;
constexpr double HIP_HEIGHT = 23.1, TORSO_HEIGHT = 13.2, HEAD_RISE = .9;
constexpr double STANDING_ANKLE = 4.4;

struct BootStep { double progress, lift; };

BootStep footfall(double stride, bool near, double lead, double lift) noexcept {
    const double first = near ? 0 : .25, second = near ? .5 : .75;
    const double landed = lead * smoothstep(first, first + .25, stride) +
        (1 - lead) * smoothstep(second, second + .25, stride);
    const double start = stride < .5 ? first : second;
    const double swing = std::sin(std::numbers::pi * std::clamp((stride - start) * 4, 0.0, 1.0));
    return {landed, lift * swing * swing};
}

constexpr double LADDER_HALF_WIDTH = 7;
constexpr int LADDER_SPACES = 6;

constexpr Vec3 ladder_rung(const CrewLadder &ladder, double index) noexcept {
    return mix(ladder.base, ladder.top, index / LADDER_SPACES);
}

void painted_bar(ConstructionBatch &batch, Vec3 start, Vec3 end, double width, Tint color,
    double opacity) noexcept {
    const Vec3 along = end - start;
    const Vec3 cross = unit({along.y, -along.x, 0}, {1, 0, 0}) * width;
    quad(batch, start - cross * .5, along, cross, tint(color, opacity));
}

void draw_ladder(ConstructionBatch &batch, const CrewLadder &ladder, double opacity, double scale) noexcept {
    const Vec3 side{LADDER_HALF_WIDTH * scale, 0, 0};
    painted_bar(batch, ladder.base - side, ladder.top - side, 2.4 * scale, {67, 82, 98, 255}, opacity);
    painted_bar(batch, ladder.base + side, ladder.top + side, 2.4 * scale, {115, 137, 145, 255}, opacity);
    const double RUNG_THICKNESS = 1.8 * scale;
    for (int i = 1; i < LADDER_SPACES; ++i) {
        // Align the top edge, not the rung's center, to the boot sole sockets.
        const Vec3 rung = ladder_rung(ladder, i) + Vec3{0, RUNG_THICKNESS * .5, 0};
        painted_bar(batch, rung - side, rung + side, RUNG_THICKNESS, {171, 157, 112, 255}, opacity);
    }
}

void draw_torch(ConstructionBatch &batch, Vec3 grip, Vec3 tool, double opacity, double scale) noexcept {
    // Battery/insulated barrel, collar, and short electrode with a shared tip.
    painted_bar(batch, grip - tool * .18, grip + tool * .51, 4.4 * scale, {47, 109, 123, 255}, opacity);
    painted_bar(batch, grip + tool * .51, grip + tool * .73, 3.1 * scale, {36, 47, 58, 255}, opacity);
    painted_bar(batch, grip + tool * .73, grip + tool, 1.5 * scale, {218, 159, 95, 255}, opacity);
}

void weld_flash(ConstructionBatch &batch, Vec3 tip, double opacity, double scale) noexcept {
    painted_bar(batch, tip + Vec3{-2.2 * scale, 0, 0}, tip + Vec3{2.2 * scale, 0, 0}, 1.4 * scale, {176, 225, 255, 255}, opacity);
    painted_bar(batch, tip + Vec3{0, -3.1 * scale, 0}, tip + Vec3{0, 3.1 * scale, 0}, 1.2 * scale, {247, 252, 255, 255}, opacity);
}

struct Limb {
    Vec3 root, joint, end;
};

// Keep authored lengths. Stance and contact planning put targets inside this reach envelope.
Limb solve(Vec3 root, Vec3 target, Vec3 bend_hint, double upper, double lower) noexcept {
    const Vec3 delta = target - root;
    const Vec3 direction = unit(delta, {1, 0, 0});
    const double epsilon = .001 * upper / ARM_UPPER;
    const double distance = std::clamp(length(delta), std::abs(upper - lower) + epsilon, upper + lower - epsilon);
    const double along = (upper * upper + distance * distance - lower * lower) / (2 * distance);
    const Vec3 bend = unit(bend_hint - direction * dot(bend_hint, direction), {0, 0, 1});
    return {root, root + direction * along + bend * std::sqrt(std::max(0.0, upper * upper - along * along)),
        root + direction * distance};
}

void draw_leg(ConstructionBatch &batch, const Limb &leg, Part thigh, Part shin, Part boot,
    double facing, double opacity, double scale) noexcept {
    stamp_link(batch, thigh, leg.root, leg.joint, opacity);
    stamp_link(batch, shin, leg.joint, leg.end, opacity);
    stamp(batch, boot, leg.end, {.180 * facing * scale, 0, 0},
        {0, STANDING_ANKLE * scale / (boot.tip.y - boot.pivot.y), 0}, opacity);
}

void draw_arm(ConstructionBatch &batch, const Limb &arm, Part sleeve, Part forearm, double opacity) noexcept {
    stamp_link(batch, sleeve, arm.root, arm.joint, opacity);
    stamp_link(batch, forearm, arm.joint, arm.end, opacity);
}
}  // namespace

void draw_crew(ConstructionBatch &batch, const CrewCues &c) noexcept {
    const double scale = c.scale;
    if (!std::isfinite(scale) || scale <= 0) return;
    const double opacity = smoothstep(0, .15, c.arrival) * (1 - smoothstep(.28, 1, c.departure));
    if (opacity <= 0) return;

    const double f = c.facing, t = c.work;
    const auto facing = [f, scale](double x, double y, double z = 0) { return Vec3{f * x, y, z} * scale; };
    const double distance = c.departure > 0 ? c.approach * .65 : c.approach;
    const double stride = c.departure > 0 ? 1 - c.departure : c.arrival;
    const Vec3 ladder_axis = c.ladder.top - c.ladder.base;
    const bool on_ladder = dot(ladder_axis, ladder_axis) > 0;
    if (on_ladder) {
        const double rise = -ladder_axis.y;
        const double lean_sq = ladder_axis.x * ladder_axis.x + ladder_axis.z * ladder_axis.z;
        if (!(rise >= 24 * scale && rise <= 80 * scale &&
            lean_sq >= 16 * scale * scale && lean_sq <= rise * rise)) return;
    }
    // Bias footfalls, not the shared walk offset. Both feet still plant at arrival 1.
    const double beat = stride + stride * (1 - stride) * (std::clamp(c.cadence, .7, 1.3) - 1) * 1.8;
    const auto near = footfall(beat, true, on_ladder ? 1.0 / 3 : .37, on_ladder ? 4.1 : 5.3);
    const auto far = footfall(beat, false, on_ladder ? .5 : .65, on_ladder ? 4.6 : 7.2);
    const double rooted_x = on_ladder ? 0 : walk_offset(distance, stride);
    const Vec3 far_sole = on_ladder ? ladder_rung(c.ladder, far.progress * 4) + facing(4, 0) :
        c.floor + facing(6, 0, -2.7) + Vec3{rooted_x + (distance * (1 - far.progress) - rooted_x) * scale, 0, 0};
    const Vec3 near_sole = on_ladder ? ladder_rung(c.ladder, near.progress * 3) + facing(-4, 0) :
        c.floor + facing(-6, 0, 2.9) + Vec3{rooted_x + (distance * (1 - near.progress) - rooted_x) * scale, 0, 0};
    const Vec3 far_ankle = far_sole + Vec3{0, (-STANDING_ANKLE - far.lift) * scale, 0};
    const Vec3 near_ankle = near_sole + Vec3{0, (-STANDING_ANKLE - near.lift) * scale, 0};
    const Vec3 floor = on_ladder ? mix(far_sole, near_sole, .5) : c.floor;
    const double twist = (near.lift - far.lift) * .31;

    const bool carrying = c.job == CrewJob::Carry, guiding = c.job == CrewJob::Guide;
    const bool bracing = c.job == CrewJob::Brace, welding = c.job == CrewJob::Weld;
    const bool fastening = c.job == CrewJob::Fasten;
    const bool steadying = c.steady_during_finish && (fastening || welding);
    const double lower = 1 - smoothstep(c.hold_until, c.hold_until + .060, t);
    const double grip = carrying ? smoothstep(.025, .11, t) * lower :
        (bracing ? smoothstep(.26, .42, t) * lower : 0);
    const double latch = (guiding || steadying) ? smoothstep(.395, GUIDE_LATCH, t) * lower : 0;
    const double ready = guiding ? smoothstep(.7, 1, c.arrival) * lower : 0;
    const double weld_press = welding ? smoothstep(PANEL_SEAT, FASTENER_STRIKE, t) *
        (1 - smoothstep(WELD_COMPLETE, WELD_COMPLETE + .065, t)) : 0;
    const double hit = fastening ?
        smoothstep(FASTENER_STRIKE, FASTENER_STRIKE + .018, t) -
            smoothstep(FASTENER_STRIKE + .018, FASTENER_STRIKE + .055, t) : 0;
    const double lean = 1.5 + twist + (bracing ? 3.5 : 2.6) * grip + 1.3 * latch - 1.3 * hit;
    Vec3 hips = floor + facing(1.5 * grip - 1.6 * hit,
        -HIP_HEIGHT - .14 * (near.lift + far.lift) + 2.4 * grip + 1.5 * latch) + Vec3{rooted_x, 0, 0};
    // Drop the pelvis to keep both authored leg chains inside the reach envelope.
    const double reach = (THIGH_LENGTH + SHIN_LENGTH - .16) * scale;
    const auto reachable_hip = [reach](Vec3 ankle, Vec3 root) {
        const double dx = ankle.x - root.x, dz = ankle.z - root.z;
        return ankle.y - std::sqrt(std::max(0.0, reach * reach - dx * dx - dz * dz));
    };
    hips.y = std::max({hips.y, reachable_hip(far_ankle, hips + facing(-1.8, 0, -2.7)),
        reachable_hip(near_ankle, hips + facing(1.5, 0, 2.9))});
    const Vec3 far_socket = facing(-2.5 - twist * .42, 3, -2.8);
    const Vec3 near_socket = facing(2 + twist * .42, 3, 2.8);
    Vec3 torso = facing(lean, -TORSO_HEIGHT, .5);
    const bool aiming = c.aim_contacts || bracing || on_ladder || steadying;
    if (aiming && (grip + latch + weld_press > 0)) {
        // Lean the trunk toward distant sockets; don't move the soles or lengthen limbs.
        const auto reach_bias = [&](Vec3 contact, Vec3 shoulder, double weight) {
            if (weight <= 0) return Vec3{};
            const Vec3 delta = contact - (hips + torso + shoulder);
            const double ease = smoothstep(16 * scale, 26 * scale, length(delta));
            return unit(delta, facing(1, 0)) * (3.0 * scale * ease * weight);
        };
        const Vec3 nudge = reach_bias(c.far_contact, far_socket, grip + latch) +
            reach_bias(c.near_contact, near_socket, grip + weld_press);
        torso = unit(torso + nudge, torso) * length(torso);
    }
    const Vec3 neck = hips + torso;
    const Vec3 far_shoulder = neck + far_socket;
    const Vec3 near_shoulder = neck + near_socket;
    const Limb far_leg = solve(hips + facing(-1.8, 0, -2.7), far_ankle, facing(1, 0, -.2),
        THIGH_LENGTH * scale, SHIN_LENGTH * scale);
    const Limb near_leg = solve(hips + facing(1.5, 0, 2.9), near_ankle, facing(1, 0, .28),
        THIGH_LENGTH * scale, SHIN_LENGTH * scale);

    const double far_grip = grip + latch;
    Vec3 far_hand = unit(facing(.68 + .32 * far_grip, .72 * (1 - far_grip), 0), facing(1, 0));
    Vec3 near_hand = unit(facing(.77 + .23 * grip, .64 * (1 - grip), 0), facing(1, 0));
    const Vec3 far_idle = far_shoulder + facing(3.2 - 2.1 * twist + 7.4 * ready, 13.6 - 10.5 * ready, -.2);
    const Vec3 near_idle = near_shoulder + facing(4.0 + 2.5 * twist, 13.7, 1.0);
    Vec3 far_rest = far_idle, near_rest = near_idle;
    if (on_ladder) {
        const auto rail_grip = [&](Vec3 shoulder, double side) {
            const double u = std::clamp((shoulder.y + 5 * scale - c.ladder.base.y) / ladder_axis.y, .12, .98);
            return c.ladder.base + ladder_axis * u + facing(side, 0);
        };
        const Vec3 far_rail = rail_grip(far_shoulder, LADDER_HALF_WIDTH);
        const Vec3 near_rail = rail_grip(near_shoulder, -LADDER_HALF_WIDTH);
        far_hand = unit(far_rail - far_shoulder, far_hand);
        near_hand = unit(near_rail - near_shoulder, near_hand);
        far_rest = far_rail - far_hand * (GLOVE_REACH * scale);
        near_rest = near_rail - near_hand * (GLOVE_REACH * scale);
    }
    if (aiming && far_grip > 0)
        far_hand = unit(mix(far_hand, unit(c.far_contact - far_shoulder, far_hand), far_grip), far_hand);
    if (aiming && grip > 0)
        near_hand = unit(mix(near_hand, unit(c.near_contact - near_shoulder, near_hand), grip), near_hand);
    const Vec3 far_wrist = mix(far_rest, c.far_contact - far_hand * (GLOVE_REACH * scale), far_grip);
    Vec3 near_wrist = mix(near_rest, c.near_contact - near_hand * (GLOVE_REACH * scale), grip);

    Vec3 tool{};
    if (c.job == CrewJob::Fasten) {
        const Vec3 tucked_tool = facing(3.8, -8.7, .25);
        const Vec3 raised_tool = facing(1.7, -9.25, -.25);
        const Vec3 striking_tool = facing(9.1, 2.6, -1.1);
        const Vec3 raised_tip = c.near_contact + facing(-13, -17, 1.2);
        const Vec3 bounced_tip = c.near_contact + facing(-4, -7, 1.2);
        constexpr double WINDUP = std::lerp(PANEL_SEAT, FASTENER_STRIKE, 4.0 / 9);
        constexpr double REBOUND = FASTENER_STRIKE + .03, TUCK = FASTENER_STRIKE + .10;
        if (t < WINDUP) {
            const double wind = smoothstep(PANEL_SEAT, WINDUP, t);
            near_wrist = mix(near_idle, raised_tip - raised_tool, wind);
            tool = mix(tucked_tool, raised_tool, wind);
        } else if (t < FASTENER_STRIKE) {
            const double swing = smoothstep(WINDUP, FASTENER_STRIKE, t);
            tool = mix(raised_tool, striking_tool, swing);
            near_wrist = mix(raised_tip, c.near_contact, swing) - tool;
        } else {
            const double rebound = smoothstep(FASTENER_STRIKE, REBOUND, t);
            const double tuck = smoothstep(REBOUND, TUCK, t);
            tool = mix(mix(striking_tool, raised_tool, rebound), tucked_tool, tuck);
            near_wrist = mix(mix(c.near_contact, bounced_tip, rebound) - tool, near_idle, tuck);
        }
    } else if (welding) {
        const Vec3 tucked = unit(facing(.38, -.92, .1), facing(1, 0));
        const Vec3 nozzle = unit(c.near_contact - near_shoulder, tucked);
        const Vec3 direction = unit(mix(tucked, nozzle, weld_press), nozzle);
        tool = direction * (10.8 * scale);
        near_wrist = mix(near_rest, c.near_contact - tool, weld_press);
        near_hand = direction;
    }
    const Limb far_arm = solve(far_shoulder, far_wrist, facing(-.4, .82, -.52), ARM_UPPER * scale, ARM_LOWER * scale);
    const Limb near_arm = solve(near_shoulder, near_wrist, facing(-.52, .92, .65), ARM_UPPER * scale, ARM_LOWER * scale);

    if (on_ladder) draw_ladder(batch, c.ladder, opacity, scale);
    draw_leg(batch, far_leg, FAR_THIGH, FAR_SHIN, FAR_BOOT, f, opacity, scale);
    draw_arm(batch, far_arm, FAR_SLEEVE, FAR_FOREARM, opacity);
    stamp_link(batch, GLOVE, far_arm.end, far_arm.end + far_hand * (GLOVE_REACH * scale), opacity);
    draw_leg(batch, near_leg, NEAR_THIGH, NEAR_SHIN, NEAR_BOOT, f, opacity, scale);
    stamp_link(batch, TORSO, hips, neck, opacity);
    const Vec3 down = unit({-torso.x / scale * .23, 1, aiming ? -torso.z / scale * .12 : 0}, {0, 1, 0});
    constexpr double crown = (CREW_HEIGHT - HIP_HEIGHT - TORSO_HEIGHT - HEAD_RISE) / (HEAD.pivot.y - HEAD.tip.y);
    stamp(batch, HEAD, neck + facing(1.4, -HEAD_RISE, 1),
        Vec3{down.y, -down.x, 0} * (.20 * f * scale), down * (crown * scale), opacity);
    draw_arm(batch, near_arm, NEAR_SLEEVE, NEAR_FOREARM, opacity);
    if (c.job == CrewJob::Fasten) stamp_link(batch, HAMMER, near_arm.end, near_arm.end + tool, opacity);
    else if (welding) draw_torch(batch, near_arm.end, tool, opacity, scale);
    stamp_link(batch, GLOVE, near_arm.end, near_arm.end + near_hand * (GLOVE_REACH * scale), opacity);
    if (welding && t >= FASTENER_STRIKE && t < WELD_COMPLETE) {
        const Vec3 tip = near_arm.end + tool;
        const Vec3 gap = tip - c.near_contact;
        if (dot(gap, gap) <= .01 * .01 * scale * scale) {
            const double flash = 1 - smoothstep(FASTENER_STRIKE + .024, WELD_COMPLETE, t);
            weld_flash(batch, tip, opacity * flash, scale);
        }
    }
}

}  // namespace hyprimate::construction
