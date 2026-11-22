#pragma once

#include "../shared/Core.hpp"

namespace hyprimate::construction {

struct PlacementRecipe {
    // World-space angles, not multiplied by walk direction. Positive roll uses the right
    // heel as its pivot, negative roll the left, so the opposite end rises.
    double roll = 0, yaw = 0;
    double approach_y = 0; // Initial heel offset from the seat, positive down.
    double nudge = 0; // Signed horizontal correction during alignment; zero at the seat.
    double pace = 1; // Multiplier for the job span.
    double approach_x = 0; // Requested signed walk shortened to fit the reserved stations.
    double access_side = 0; // -1 left, +1 right, 0 no side guide.
    std::size_t carriers = 1;
    bool staffed = false, ladder = false, side_guide = false, weld = false, fasten = false;
};

// All boxes are capture-local logical pixels. Supported means a predecessor has seated
// underneath the bay and can support its work deck. Availability excludes workers already
// reserved by overlapping jobs. Access still requires the scene's actual deck/ladder geometry.
[[nodiscard]] inline PlacementRecipe compose_placement(
    Box bay, Box body, Box area, double approach_x, bool supported, bool receiving,
    std::uint64_t &seed, unsigned available) noexcept {
    PlacementRecipe recipe;
    if (receiving) {
        // The dedicated receiving beat owns its original motion, crew and seed delay.
        recipe.approach_x = approach_x;
        recipe.staffed = available > 0;
        return recipe;
    }

    // Draw once per scheduled bay, including unstaffed bays. Geometry or crew availability
    // changing an optional role must not change the draws for later bays.
    const double heel_draw = unit_draw(seed), yaw_draw = unit_draw(seed), lift_draw = unit_draw(seed);
    const double align_draw = unit_draw(seed), finish_draw = unit_draw(seed);

    constexpr double STANCE_INSET = 29, BOOT_ROOM = 14, HEAD_ROOM = 44, DECK_THICKNESS = 5;
    constexpr double GUIDE_OUTSET = 16, GUIDE_GAP = 26, ACCESS_APRON = 36, LADDER_APRON = 46;
    const double right = bay.x + bay.w, floor = bay.y + bay.h;
    const double below = body.y + body.h - floor;
    const double area_right = area.x + area.w, area_bottom = area.y + area.h;
    const double left_room = bay.x - area.x, right_room = area_right - right;
    const double mass = smoothstep(15000, 78000, bay.w * bay.h);
    const double broad = smoothstep(130, 300, bay.w);
    const double upright = smoothstep(.38, 1.06, bay.h / bay.w);
    const double inset = std::min(STANCE_INSET, bay.w * .5);
    const double left_stance = bay.x + inset, right_stance = right - inset;
    const double inside_left = area.x + BOOT_ROOM, inside_right = area_right - BOOT_ROOM;
    const bool left_fits = left_stance >= inside_left && left_stance <= inside_right;
    const bool right_fits = right_stance >= inside_left && right_stance <= inside_right;
    recipe.staffed = available > 0 && (supported || below <= 1) && bay.w >= 46 &&
        floor >= area.y + HEAD_ROOM && floor + DECK_THICKNESS <= area_bottom && (left_fits || right_fits);

    // Only count the exterior apron as known clear. An interior side may already be
    // occupied by a neighbor; the predecessor under this bay doesn't establish side access.
    const bool outer_left = bay.x <= body.x + 1, outer_right = right >= body.x + body.w - 1;
    const bool outer = outer_left || outer_right;
    const bool left_access = outer_left && left_room >= ACCESS_APRON;
    const bool right_access = outer_right && right_room >= ACCESS_APRON;
    // An intentionally short traverse leaves room for a vertical placement. Losing
    // horizontal clearance at an output edge does not turn a lateral job into one.
    const double vertical = supported ? 1 - smoothstep(13, 24, std::abs(approach_x)) : 0;
    const bool wants_guide = recipe.staffed && supported && below >= 36 && (left_access || right_access) &&
        (mass >= .43 || upright >= .60);
    const bool guide_left = left_access && (!right_access || left_room >= right_room);
    // Require room for the upper rungs and the braced approach below the deck.
    const bool ladder_access = wants_guide && below >= 52 && floor - area.y >= HEAD_ROOM + 26 &&
        area_bottom - floor >= 40 && (guide_left ? left_room : right_room) >= LADDER_APRON;
    // On a clear traverse, one worker can take the ladder while the other brings
    // the load across. A short or stationary job still varies its second role.
    const bool prefers_guide = wants_guide && available == 2 &&
        ((ladder_access && std::abs(approach_x) >= 24) || finish_draw < .72);
    // Outer vertical loads steer from their low heel; a partner at the opposite
    // end would limit the yaw too much. Extra-wide jobs can trade yaw for support.
    const bool needs_steering_room = outer && vertical > .65 && bay.w < 304;
    if (recipe.staffed && !prefers_guide && !needs_steering_room && available >= 2 &&
        left_fits && right_fits && bay.w >= 100 &&
        .69 * mass + .59 * broad + .16 * lift_draw >= .57) {
        recipe.carriers = 2;
        if (available >= 3 && bay.w >= 304 && body.w >= 1040 && body.h >= 520 && mass > .88)
            recipe.carriers = 3;
    }
    if (wants_guide && available > recipe.carriers) {
        recipe.side_guide = true;
        recipe.access_side = guide_left ? -1 : 1;
        recipe.ladder = ladder_access;
    }

    const double walk_left = recipe.access_side < 0
        ? std::max(inside_left, bay.x - GUIDE_OUTSET + GUIDE_GAP) : inside_left;
    const double walk_right = recipe.access_side > 0
        ? std::min(inside_right, right + GUIDE_OUTSET - GUIDE_GAP) : inside_right;
    const auto walk_room = [&](double leftmost, double rightmost) {
        return std::max(0.0, approach_x < 0 ? leftmost - walk_left : walk_right - rightmost);
    };
    bool heel_right = heel_draw >= .5;
    if (recipe.staffed) {
        if (left_fits != right_fits) {
            heel_right = right_fits;
        } else if (recipe.carriers == 1) {
            const double same = heel_right ? right_stance : left_stance;
            const double other = heel_right ? left_stance : right_stance;
            if (walk_room(same, same) < std::min(12.0, std::abs(approach_x)) &&
                walk_room(other, other) > walk_room(same, same) + 8)
                heel_right = !heel_right;
        }
        const double heel = heel_right ? right_stance : left_stance;
        const double room = recipe.carriers == 1 ? walk_room(heel, heel) : walk_room(left_stance, right_stance);
        recipe.approach_x = std::copysign(std::min(std::abs(approach_x), room), approach_x);
    } else {
        // The load can still come in from the visible apron without inventing a foothold.
        const double room = std::max(0.0, approach_x < 0 ? left_room : right_room);
        recipe.approach_x = std::copysign(std::min(std::abs(approach_x), room), approach_x);
    }

    const bool paired = recipe.carriers > 1;
    const double vertical_height = 28 + 4 * upright + 2 * lift_draw;
    const double lateral_y = (supported ? -1 : 1) * (.7 + 3 * upright) * (.6 + .4 * lift_draw);
    const double vertical_y = outer ? -vertical_height : vertical_height;
    const double y_intent = (1 - vertical) * lateral_y + vertical * vertical_y;
    const double y_limit = paired ? 29 : 34;
    const double overhang_room = left_access || right_access ? 12 : 0;
    const double above_room = std::clamp(bay.y - area.y + overhang_room, 0.0, y_limit);
    const double below_room = std::clamp(area_bottom - floor - DECK_THICKNESS, 0.0, y_limit);
    recipe.approach_y = std::clamp(y_intent, -above_room, below_room);
    // Roll supplies the large visible rise around a low heel, rather than carrying the
    // actors' feet upward. Opposite-end gloves have a smaller rise/depth budget than
    // the free end of a single-worker load. Leave slack for an already-raised heel.
    const double tip_rise = paired ? 14 - .25 * std::max(0.0, -recipe.approach_y) : 22 + 21 * vertical;
    const double shape_roll = (.022 + (.15 + .118 * vertical) * upright) * (.66 + .34 * align_draw);
    recipe.roll = (heel_right ? 1 : -1) * std::min({shape_roll, tip_rise / bay.w, 44 / bay.h});
    const double shape_yaw = (.045 + .145 * upright) * (.65 + .35 * lift_draw) +
        vertical * (.24 + .055 * yaw_draw);
    const double yaw_limit = (paired ? 8 : 48 + 60 * vertical) / bay.w;
    // In a rise from below the ledge, turn the free end toward the viewer so it
    // clears the supporting deck. A lateral/descending job can turn either way.
    const double yaw_sign = vertical > .5 && recipe.approach_y > 0
        ? (heel_right ? 1 : -1) : (yaw_draw < .5 ? -1 : 1);
    recipe.yaw = yaw_sign * std::min({shape_yaw, yaw_limit, .44});

    const double correction = 2.0 + 3.5 * std::max(mass, upright);
    recipe.nudge = (align_draw < .48 ? -1 : 1) * correction;
    recipe.pace = .90 + .19 * mass + .036 * broad + .035 * lift_draw;
    const bool finish_joint = recipe.staffed && (recipe.side_guide || paired || mass > .56 ||
        (supported && bay.w >= 108 && finish_draw > .38));
    recipe.weld = finish_joint && .66 * mass + .20 * upright + .26 * finish_draw > .63;
    recipe.fasten = finish_joint && !recipe.weld;
    return recipe;
}

}  // namespace hyprimate::construction
