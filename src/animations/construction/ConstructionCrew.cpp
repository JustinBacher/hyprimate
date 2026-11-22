#include "ConstructionCrew.hpp"

#include <numbers>

namespace hyprimate::construction {
namespace {
constexpr double STEP_CHANGE = .091;
constexpr double ARM_UPPER = 10.5, ARM_LOWER = 10.2;
constexpr double THIGH_LENGTH = 11.3, SHIN_LENGTH = 10.8;
constexpr double GLOVE_REACH = 3.5;
constexpr double HIP_HEIGHT = 23.1, TORSO_HEIGHT = 13.2, HEAD_RISE = .9;

// Both pivots retain their authored lengths. Only unreachable targets are clipped;
// the contact cues (guide and strike) put the rigger inside the reach envelope.
Limb solve(Vec3 root, Vec3 target, Vec3 bend_hint, double upper, double lower) noexcept {
    const Vec3 delta = target - root;
    const Vec3 direction = unit(delta, {1, 0, 0});
    const double distance = std::clamp(length(delta), std::abs(upper - lower) + .001, upper + lower - .001);
    const double along = (upper * upper + distance * distance - lower * lower) / (2 * distance);
    const Vec3 bend = unit(bend_hint - direction * dot(bend_hint, direction), {0, 0, 1});
    return {root, root + direction * along + bend * std::sqrt(std::max(0.0, upper * upper - along * along)),
        root + direction * distance};
}

struct StepPose {
    double x, lift;
};

StepPose boot_step(double time, double from, double to, double initial, double planted, double rise) noexcept {
    const double phase = std::clamp((time - from) / (to - from), 0.0, 1.0);
    const double arc = std::sin(std::numbers::pi * phase);
    return {std::lerp(initial, planted, smoothstep(0, 1, phase)), rise * arc * arc};
}

void draw_leg(ConstructionBatch &batch, const Limb &leg, Part thigh, Part shin, Part boot,
    double scale, double opacity) noexcept {
    stamp_link(batch, thigh, leg.root, leg.joint, opacity);
    stamp_link(batch, shin, leg.joint, leg.end, opacity);
    stamp(batch, boot, leg.end, {scale * .180, 0, 0}, {0, scale * 4.4 / (boot.tip.y - boot.pivot.y), 0}, opacity);
}

void draw_arm(ConstructionBatch &batch, const Limb &arm, Part sleeve, Part forearm, double opacity) noexcept {
    stamp_link(batch, sleeve, arm.root, arm.joint, opacity);
    stamp_link(batch, forearm, arm.joint, arm.end, opacity);
}
}  // namespace

CrewPose evaluate_crew(const CrewCues &c) noexcept {
    const double t = c.time;
    const double stride_time = t < WALK_END ? t : WALK_END * (1 - smoothstep(c.release, c.retire, t));
    const auto near_boot = boot_step(stride_time, 0, STEP_CHANGE, -55, -27, 5.3);
    const auto far_boot = boot_step(stride_time, STEP_CHANGE, WALK_END, -45, -9, 7.2);
    const double stride = smoothstep(0, WALK_END, stride_time);
    const double counterturn = (near_boot.lift - far_boot.lift) * .37;
    const double collecting = smoothstep(WALK_END, TOOL_PICKUP, t) - smoothstep(TOOL_PICKUP, .266, t);
    const double guide = smoothstep(.485, GUIDE_LATCH, t) * (1 - smoothstep(c.seat, c.seat + .020, t));
    const double recoil = smoothstep(c.strike, c.strike + .010, t) - smoothstep(c.strike + .010, c.strike + .030, t);
    const double lean = 1.4 + counterturn + collecting * 2.6 + guide * 1.5 - recoil * 2.1;
    const double scale = c.scale;
    const auto delta = [scale](double x, double y, double z = 0) { return Vec3{x * scale, y * scale, z * scale}; };
    const Vec3 hips = c.floor + delta(std::lerp(-49.0, -18.0, stride) + guide * 1.8 - recoil * 1.8,
        -HIP_HEIGHT - (near_boot.lift + far_boot.lift) * .20 + collecting * 3.7 + guide * 1.0);
    const Vec3 neck = hips + delta(lean, -TORSO_HEIGHT, .5);
    const Vec3 far_shoulder = neck + delta(-2.5 - counterturn * .42, 3.0, -2.8);
    const Vec3 near_shoulder = neck + delta(2.0 + counterturn * .42, 3.0, 2.8);
    const Vec3 far_ankle = c.floor + delta(far_boot.x, -4.4 - far_boot.lift, -2.7);
    const Vec3 near_ankle = c.floor + delta(near_boot.x, -4.4 - near_boot.lift, 2.9);
    const Limb far_leg = solve(hips + delta(-1.8, 0, -2.7), far_ankle, {1, 0, -.2}, THIGH_LENGTH * scale, SHIN_LENGTH * scale);
    const Limb near_leg = solve(hips + delta(1.5, 0, 2.9), near_ankle, {1, 0, .28}, THIGH_LENGTH * scale, SHIN_LENGTH * scale);

    const double signal = smoothstep(TOOL_PICKUP + .003, .254, t) - smoothstep(.275, .326, t);
    const double wave = std::sin(89 * (t - .255)) * signal;
    const Vec3 hand_direction = unit(mix({.72, .60, 0}, {.78, -.62, 0}, signal), {1, 0, 0});
    const Vec3 guide_direction = unit(mix(hand_direction, {1, 0, 0}, guide), {1, 0, 0});
    const Vec3 idle = far_shoulder + delta(4.3 - counterturn * 2.1, 13.6, -.2);
    const Vec3 signaled = idle + delta(signal * (6.3 + 1.0 * wave), -signal * (22.2 + 1.2 * wave), -.8 * signal);
    const Vec3 far_wrist = mix(signaled, c.guide - guide_direction * (GLOVE_REACH * scale), guide);
    const Limb far_arm = solve(far_shoulder, far_wrist, {-.40, .82, -.52}, ARM_UPPER * scale, ARM_LOWER * scale);

    const Vec3 bench_grip = c.floor + delta(-8.5, -18.0, 3.5);
    const Vec3 bench_tool = delta(-1, -9.5, .25);
    const Vec3 carry_wrist = near_shoulder + delta(6.2, 11.8, 2.1);
    const Vec3 carry_tool = delta(3.8, -8.7, .25);
    const Vec3 striking_tool = delta(9.1, 2.6, -1.1);
    const Vec3 raised_tool = delta(1.7, -9.25, -.25);
    const double take = smoothstep(WALK_END + .005, TOOL_PICKUP, t);
    const double carry = smoothstep(TOOL_PICKUP, .274, t);
    Vec3 near_wrist = mix(mix(near_shoulder + delta(3.3 + counterturn * 2.5, 14.1, 1.0), bench_grip, take), carry_wrist, carry);
    Vec3 tool = mix(bench_tool, carry_tool, carry);

    if (t >= c.seat) {
        const double wind_up = c.seat + .025;
        const Vec3 raised_tip = c.bolt + delta(-13.0, -19.0, 1.2);
        const Vec3 bounced_tip = c.bolt + delta(-4.0, -6.5, .6);
        if (t < wind_up) {
            const double wind = smoothstep(c.seat, wind_up, t);
            near_wrist = mix(carry_wrist, raised_tip - raised_tool, wind);
            tool = mix(carry_tool, raised_tool, wind);
        } else if (t < c.strike) {
            const double down = smoothstep(wind_up, c.strike, t);
            tool = mix(raised_tool, striking_tool, down);
            near_wrist = mix(raised_tip, c.bolt, down) - tool;
        } else {
            const double bounce = smoothstep(c.strike, c.strike + .014, t);
            const double tuck = smoothstep(c.strike + .014, c.release, t);
            tool = mix(mix(striking_tool, raised_tool, bounce), carry_tool, tuck);
            near_wrist = mix(mix(c.bolt, bounced_tip, bounce) - tool, carry_wrist, tuck);
        }
    }
    const Limb near_arm = solve(near_shoulder, near_wrist, {-.52, .92, .65}, ARM_UPPER * scale, ARM_LOWER * scale);
    const Vec3 grip = t < TOOL_PICKUP ? bench_grip : near_arm.end;
    return {.far_leg = far_leg, .near_leg = near_leg, .far_arm = far_arm, .near_arm = near_arm,
        .hips = hips, .neck = neck, .far_fingers = far_arm.end + guide_direction * (GLOVE_REACH * scale),
        .near_fingers = near_arm.end + delta(2.9, .6, 0), .hammer_grip = grip, .hammer_tip = grip + tool,
        .scale = scale, .lean = lean, .opacity = 1 - smoothstep(c.release + .022, c.retire, t)};
}

void draw_crew_far(ConstructionBatch &batch, const CrewPose &p) noexcept {
    draw_leg(batch, p.far_leg, FAR_THIGH, FAR_SHIN, FAR_BOOT, p.scale, p.opacity);
    draw_arm(batch, p.far_arm, FAR_SLEEVE, FAR_FOREARM, p.opacity);
    stamp_link(batch, GLOVE, p.far_arm.end, p.far_fingers, p.opacity);
}

void draw_crew_near(ConstructionBatch &batch, const CrewPose &p) noexcept {
    draw_leg(batch, p.near_leg, NEAR_THIGH, NEAR_SHIN, NEAR_BOOT, p.scale, p.opacity);
    stamp_link(batch, TORSO, p.hips, p.neck, p.opacity);
    const Vec3 down = unit({-p.lean * .23, 1, 0}, {0, 1, 0});
    constexpr double crown = (CREW_HEIGHT - HIP_HEIGHT - TORSO_HEIGHT - HEAD_RISE) / (HEAD.pivot.y - HEAD.tip.y);
    stamp(batch, HEAD, p.neck + Vec3{p.scale * 1.4, -p.scale * HEAD_RISE, p.scale},
        Vec3{down.y, -down.x, 0} * (.20 * p.scale), down * (crown * p.scale), p.opacity);
    draw_arm(batch, p.near_arm, NEAR_SLEEVE, NEAR_FOREARM, p.opacity);
    stamp_link(batch, HAMMER, p.hammer_grip, p.hammer_tip, p.opacity);
    stamp_link(batch, GLOVE, p.near_arm.end, p.near_fingers, p.opacity);
}

}  // namespace hyprimate::construction
