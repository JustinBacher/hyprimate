#include "ConstructionClosePrivate.hpp"

namespace hyprimate::construction::close_detail {
    namespace {
        // Use the same texel anchors as opening; keep a stable cutout width as limbs
        // bend between the planted soles, pelvis, shoulder, and evaluated hand contacts.
        void link(ConstructionBatch &effects,
            Part part,
            Vec3 a,
            Vec3 b,
            double facing,
            double pixel,
            double opacity) noexcept {
            const double dx = part.tip.x - part.pivot.x, dy = part.tip.y - part.pivot.y;
            const double length_squared = dx * dx + dy * dy;
            const Vec3 delta = b - a;
            const Vec3 side = unit({delta.y, -delta.x, 0}, {1, 0, 0}) *
                              (std::sqrt(length_squared) * pixel * facing);
            stamp(effects,
                part,
                a,
                (delta * dx + side * dy) * (1 / length_squared),
                (delta * dy - side * dx) * (1 / length_squared),
                opacity);
        }

        void worker_art(ConstructionBatch &effects, const Regional::Worker &worker) noexcept {
            constexpr std::array thigh{NEAR_THIGH, FAR_THIGH}, shin{NEAR_SHIN, FAR_SHIN};
            constexpr std::array boot{NEAR_BOOT, FAR_BOOT}, sleeve{NEAR_SLEEVE, FAR_SLEEVE};
            constexpr std::array forearm{NEAR_FOREARM, FAR_FOREARM};
            const double h = worker.height, facing = worker.facing, opacity = worker.opacity;
            const auto joint = [&](Part part, Vec3 a, Vec3 b, double pixel) {
                link(effects, part, a, b, facing, h * pixel, opacity);
            };
            const auto leg = [&](std::size_t i) {
                const double side = i == 0 ? 1 : -1;
                const Vec3 hip = worker.hip + Vec3{side * facing * .045 * h, 0, side * .026 * h};
                const Vec3 ankle =
                    worker.soles[i] - Vec3{0, (boot[i].tip.y - boot[i].pivot.y) * .0043 * h, 0};
                const Vec3 knee = mix(hip, ankle, .51) + Vec3{facing * .085 * h, 0, 0};
                joint(thigh[i], hip, knee, .0049);
                joint(shin[i], knee, ankle, .0044);
                stamp(effects,
                    boot[i],
                    ankle,
                    {facing * .0043 * h, 0, 0},
                    {0, .0043 * h, 0},
                    opacity);
            };
            const auto arm = [&](std::size_t i, Vec3 hand) {
                const double side = i == 0 ? 1 : -1;
                const Vec3 shoulder =
                    worker.shoulder + Vec3{side * facing * .044 * h, 0, side * .027 * h};
                const Vec3 elbow = mix(shoulder, hand, .46) + Vec3{-facing * .070 * h, .060 * h, 0};
                const Vec3 wrist = hand - unit(hand - elbow, {facing, 0, 0}) *
                                              std::min(.079 * h, length(hand - elbow) / 2);
                joint(sleeve[i], shoulder, elbow, .0045);
                joint(forearm[i], elbow, wrist, .0041);
                joint(GLOVE, wrist, hand, .0038);
            };
            leg(1);
            arm(1, worker.far_hand);
            leg(0);
            joint(TORSO, worker.hip, mix(worker.shoulder, worker.head, .46), .0072);
            const Vec3 chin = worker.head + Vec3{0, .092 * h, 0};
            stamp(effects, HEAD, chin, {facing * .0046 * h, 0, 0}, {0, .0046 * h, 0}, opacity);
            arm(0, worker.near_hand);
        }

        void glow(ConstructionBatch &effects,
            Vec3 center,
            Vec2 radius,
            Tint opaque,
            double opacity) noexcept {
            // The shared Shadow surface is an untethered soft radial mask; the tint can
            // be colored and does not sample the snapshot or atlas.
            quad(effects,
                {center.x - radius.x, center.y - radius.y, center.z},
                {2 * radius.x, 0, 0},
                {0, 2 * radius.y, 0},
                tint(opaque, opacity),
                ConstructionSurface::Shadow);
        }
    }  // namespace

    void emit_close_effects(const Regional::Frame &frame,
        ConstructionBatch &effects,
        bool departing,
        std::span<const ConstructionQuad> saved,
        double elapsed,
        double seconds,
        double distance,
        bool blank) noexcept {
        if (!departing) {
            for (const auto &worker : frame.crew()) worker_art(effects, worker);
        } else if (elapsed < seconds && (blank || !frame.crew().empty())) {
            const double progress = elapsed / seconds;
            if (progress == 0) {
                for (const auto &q : saved) effects.add(q);
            } else {
                const double displacement = distance * smoothstep(0, 1, progress);
                const double opacity = 1 - smoothstep(.16, 1, progress);
                for (auto q : saved) {
                    q.origin[1] = static_cast<float>(q.origin[1] + displacement);
                    q.tint = tint(q.tint, opacity);
                    if (q.tint[3] != 0) effects.add(q);
                }
            }
        }
        for (const auto &dust : frame.dust()) {
            const Vec3 center{dust.center.x, dust.center.y, 0};
            glow(effects, center, dust.radii, {183, 163, 142, 255}, dust.opacity);
            glow(effects,
                center + Vec3{dust.radii.x * .45, -dust.radii.y * .32, 0},
                {dust.radii.x * .64, dust.radii.y * .82},
                {222, 205, 180, 255},
                dust.opacity * .62);
        }
        if (const auto &flash = frame.flash) {
            const auto [center, radius, intensity] = *flash;
            glow(effects, center, {radius, radius}, {255, 161, 60, 255}, intensity * .72);
            glow(effects, center, {radius * .37, radius * .37}, {255, 247, 214, 255}, intensity);
        }
    }
}  // namespace hyprimate::construction::close_detail
