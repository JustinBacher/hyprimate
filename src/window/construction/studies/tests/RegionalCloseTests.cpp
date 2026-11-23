#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "Check.hpp"
#include "window/construction/studies/RegionalClose.hpp"

using namespace hyprimate;
using namespace hyprimate::construction::studies;

namespace {

    using Close = RegionalClose;
    constexpr Box LANDSCAPE{37.5, 19.25, 654, 402};
    constexpr Box TALL{64.25, 37.5, 336, 564};
    constexpr Close::SourceView authorized(Box crop) {
        return {.crop = crop, .authorized = true};
    }

    Vec2 point(Box body, double x, double y) {
        return {body.x + x * body.w, body.y + y * body.h};
    }

    std::optional<Vec3> print_at(const Close::Frame &frame, Vec2 mark) {
        const auto vertices = frame.source_vertices();
        for (const auto &face : frame.source_faces()) {
            const auto &a = vertices[face[0]];
            const auto &b = vertices[face[1]];
            const auto &c = vertices[face[2]];
            const double ux = b.source.x - a.source.x, uy = b.source.y - a.source.y;
            const double vx = c.source.x - a.source.x, vy = c.source.y - a.source.y;
            const double area = ux * vy - uy * vx;
            if (area == 0) continue;
            const double dx = mark.x - a.source.x, dy = mark.y - a.source.y;
            const double u = (dx * vy - dy * vx) / area, v = (ux * dy - uy * dx) / area;
            if (u < -1e-9 || v < -1e-9 || u + v > 1 + 1e-9) continue;
            return Vec3{
                std::lerp(a.position.x, b.position.x, u) + v * (c.position.x - a.position.x),
                std::lerp(a.position.y, b.position.y, u) + v * (c.position.y - a.position.y),
                std::lerp(a.position.z, b.position.z, u) + v * (c.position.z - a.position.z)};
        }
        return std::nullopt;
    }

    bool near(Vec3 a, Vec3 b) {
        return std::abs(a.x - b.x) < 1e-7 && std::abs(a.y - b.y) < 1e-7 &&
               std::abs(a.z - b.z) < 1e-7;
    }

    struct Take {
        std::uint64_t seed = 0;
        Vec2 cue;
    };

    std::optional<Take> find_take(Box body, double left, double right) {
        // Seek approved framing categories, not the Python preview's random stream.
        const auto face = Close::VisibleFace::flat(body);
        Close model;
        Close::Frame frame;
        for (std::uint64_t seed = 0; seed < 64; ++seed) {
            CHECK(model.start(body, std::span{&face, 1}, seed, 1));
            model.advance(.16);
            model.advance(.16);
            model.emit(authorized(body), frame, false);
            if (!frame.flash) continue;
            const auto origin = frame.flash->position;
            const double u = (origin.x - body.x) / body.w;
            if (left < u && u < right) return Take{seed, {origin.x, origin.y}};
        }
        CHECK(false);
        return std::nullopt;
    }

    void check_joined_sheet(const Close::Frame &frame) {
        // Neighbors use the same vertices, not coincident copies of independent panel edges.
        std::size_t joined_edges = 0;
        const auto faces = frame.source_faces();
        for (std::size_t i = 0; i < faces.size(); ++i) {
            for (std::size_t j = i + 1; j < faces.size(); ++j) {
                int common = 0;
                for (const auto index : faces[i])
                    common += std::ranges::find(faces[j], index) != faces[j].end();
                joined_edges += common == 2;
            }
        }
        CHECK(joined_edges > 6);
    }

    void trace_dustless(Box body, Take take, Vec2 held, bool lower_bite, double duration) {
        const auto face = Close::VisibleFace::flat(body);
        const Vec2 first{take.cue.x, body.y + (lower_bite ? .78 : .54) * body.h};
        const Vec2 roof{take.cue.x, body.y + .14 * body.h};
        Close model;
        Close::Frame frame;
        CHECK(model.start(body, std::span{&face, 1}, take.seed, duration));
        double first_lead = -1, neighbor_moves = -1;
        bool roof_holds = false, neighbor_follows_inward = false, joined = false;
        bool print_gone = false;
        double last_print = 0;
        for (int tick = 0; tick <= 120; ++tick) {
            if (tick != 0) model.advance(duration / 120);
            model.emit(authorized(body), frame, false);
            CHECK(frame.dust().empty());
            if (frame.source_faces().empty())
                print_gone = true;
            else {
                CHECK(!print_gone);
                last_print = double(tick) / 120;
            }
            const auto a = print_at(frame, first), b = print_at(frame, held),
                       crown = print_at(frame, roof);
            if (tick <= 32) {
                CHECK(a && near(*a, {first.x, first.y, 0}));
                CHECK(b && near(*b, {held.x, held.y, 0}));
            }
            if (a && b && first_lead < 0 && a->y > first.y + .055 * std::min(body.h, body.w) &&
                std::abs(b->y - held.y) < 1) {
                first_lead = double(tick) / 120;
                check_joined_sheet(frame);
                joined = true;
            }
            if (a && crown && a->y > first.y + .055 * std::min(body.h, body.w) &&
                std::abs(crown->y - roof.y) < 2)
                roof_holds = true;
            if (b && b->y > held.y + .055 * std::min(body.h, body.w)) {
                if (neighbor_moves < 0) neighbor_moves = double(tick) / 120;
                neighbor_follows_inward |=
                    (b->x - held.x) * (take.cue.x - held.x) > 2 * std::abs(take.cue.x - held.x);
            }
        }
        CHECK(joined);
        CHECK(first_lead > .33 && first_lead < .50);
        CHECK(neighbor_moves > first_lead + .05 && neighbor_moves < .81);
        CHECK(neighbor_follows_inward);
        if (lower_bite) CHECK(roof_holds);
        CHECK(print_gone && last_print < .92);
        CHECK(!model.active());
        CHECK(frame.source_faces().empty() && frame.crew().empty() && frame.dust().empty() &&
              !frame.flash);
    }

    void seed_and_shape_change_the_first_region() {
        const auto center = find_take(LANDSCAPE, .43, .57);
        const auto side = find_take(LANDSCAPE, .82, .94);
        const auto low = find_take(TALL, .055, .17);
        if (center) trace_dustless(LANDSCAPE, *center, point(LANDSCAPE, .16, .54), false, .85);
        if (side) trace_dustless(LANDSCAPE, *side, point(LANDSCAPE, .49, .54), false, 1.65);
        if (low) {
            CHECK(low->cue.y < TALL.y + .84 * TALL.h);
            trace_dustless(TALL, *low, point(TALL, .79, .75), true, 1.4);
        }
    }

    void preserves_only_the_interrupted_print() {
        const Box raised_crop{LANDSCAPE.x - 9, LANDSCAPE.y + 61, 143, 175};
        const Box seated_crop{LANDSCAPE.x + 221, LANDSCAPE.y + 189, 263, 169};
        constexpr Vec3 RAISED{109, 36, -3}, U{121, 19, 5}, V{-17, 163, 11};
        const std::array visible{
            Close::VisibleFace{raised_crop, RAISED, U, V}, Close::VisibleFace::flat(seated_crop)};
        const Box authorized_crop{raised_crop.x + 11, raised_crop.y + 7, 102, 111};
        const auto mark = point(raised_crop, .5, .5);
        const auto hole = point(LANDSCAPE, .28, .47);
        Close model;
        Close::Frame frame;
        CHECK(model.start(LANDSCAPE, visible, 51, 1));
        model.emit(authorized(LANDSCAPE), frame);
        const auto initial = print_at(frame, mark);
        CHECK(initial && near(*initial,
                             {RAISED.x + (U.x + V.x) / 2,
                                 RAISED.y + (U.y + V.y) / 2,
                                 RAISED.z + (U.z + V.z) / 2}));
        CHECK(!print_at(frame, hole));
        CHECK(!print_at(frame,
            point(raised_crop, .02, .50)));  // Outside the body even though input covered it.
        CHECK(print_at(frame, point(seated_crop, .5, .5)));
        for (int i = 0; i < 40; ++i) model.advance(.01);
        model.emit(authorized(authorized_crop), frame);
        CHECK(!frame.source_faces().empty());
        for (const auto &v : frame.source_vertices()) {
            CHECK(v.source.x >= authorized_crop.x &&
                  v.source.x <= authorized_crop.x + authorized_crop.w);
            CHECK(v.source.y >= authorized_crop.y &&
                  v.source.y <= authorized_crop.y + authorized_crop.h);
            CHECK(std::isfinite(v.position.x) && std::isfinite(v.position.y) &&
                  std::isfinite(v.position.z));
        }
        CHECK(!print_at(frame, point(seated_crop, .5, .5)));
        CHECK(!print_at(frame, hole));
        model.emit({.crop = LANDSCAPE, .authorized = false}, frame);
        CHECK(frame.source_faces().empty() &&
              frame.source_vertices().empty());  // Revoke without advancing.
        model.emit(
            authorized({.x = std::numeric_limits<double>::quiet_NaN(), .w = 20, .h = 20}), frame);
        CHECK(frame.source_faces().empty());
        model.emit(authorized({.x = -100, .y = -100, .w = 20, .h = 20}), frame);
        CHECK(frame.source_faces().empty());
        model.emit(authorized(LANDSCAPE), frame);
        CHECK(!frame.source_faces().empty() && !print_at(frame, hole));
        model.cancel();
        model.emit(authorized(LANDSCAPE), frame);
        CHECK(!model.active() && frame.source_faces().empty() && frame.crew().empty() &&
              frame.dust().empty() && !frame.flash);
        CHECK(!model.start(LANDSCAPE, {}, 51, 1));
        model.emit(authorized(LANDSCAPE), frame);
        CHECK(frame.source_faces().empty() && frame.crew().empty());
    }

    void crew_clears_before_flash_and_dust_stays_local() {
        const auto face = Close::VisibleFace::flat(LANDSCAPE);
        Close model;
        Close::Frame frame;
        CHECK(model.start(LANDSCAPE, std::span{&face, 1}, 21, 1));
        model.emit(authorized(LANDSCAPE), frame);
        CHECK(frame.crew().size() >= 2);
        if (frame.crew().size() < 2) return;
        const auto foot = frame.crew().front().floor;
        const auto planted = frame.crew().front().soles;
        bool checked = false, signaled = false, retreated = false, flagged = false,
             saw_dust = false;
        for (int i = 1; i <= 101; ++i) {
            model.advance(.01);
            model.emit(authorized(LANDSCAPE), frame);
            if (frame.crew().size() >= 2) {
                const auto &checker = frame.crew()[0], &flagger = frame.crew()[1];
                checked |= checker.near_hand.x >= LANDSCAPE.x &&
                           checker.near_hand.x <= LANDSCAPE.x + LANDSCAPE.w;
                signaled |= flagger.near_hand.y < flagger.head.y - 1;
                retreated |= checker.facing * (checker.floor.x - foot.x) > checker.height;
                if (i <= 2) {
                    CHECK(near(checker.soles[0], planted[0]));
                    CHECK(near(checker.soles[1], planted[1]));
                }
            }
            if (frame.flash) {
                CHECK(i > 26 && i < 40);
                CHECK(frame.crew().empty());
                CHECK(frame.flash->radius < .05 * LANDSCAPE.h);
                flagged = true;
            }
            for (const auto &puff : frame.dust()) {
                CHECK(flagged && i >= 55);
                CHECK(puff.radii.x < .10 * LANDSCAPE.w);
                CHECK(puff.center.y > LANDSCAPE.y + .90 * LANDSCAPE.h);
                saw_dust = true;
            }
        }
        CHECK(checked && signaled && retreated && flagged && saw_dust);
        CHECK(!model.active() && frame.crew().empty() && frame.dust().empty() &&
              frame.source_faces().empty() && !frame.flash);
    }

    void clipping_many_patches_keeps_the_same_seeded_clock() {
        std::array<Close::VisibleFace, Close::MAX_VISIBLE_FACES> visible;
        for (std::size_t i = 0; i < visible.size(); ++i) {
            visible[i] = Close::VisibleFace::flat(
                {LANDSCAPE.x, LANDSCAPE.y + 15 * double(i), LANDSCAPE.w, 9});
        }
        Close regular, jittered;
        Close::Frame a, b;
        CHECK(regular.start(LANDSCAPE, visible, 1337, 1));
        CHECK(jittered.start(LANDSCAPE, visible, 1337, 2));
        for (int i = 0; i < 24; ++i) {
            regular.advance(1.0 / 60);
            jittered.advance(1.0 / 120);
            jittered.advance(3.0 / 120);
        }
        regular.emit(authorized(LANDSCAPE), a, false);
        jittered.emit(authorized(LANDSCAPE), b, false);
        CHECK(a.source_faces().size() > 10 * visible.size());
        CHECK(a.source_vertices().size() == b.source_vertices().size());
        CHECK(std::ranges::equal(a.source_faces(), b.source_faces()));
        if (a.source_vertices().size() != b.source_vertices().size()) return;
        for (std::size_t i = 0; i < a.source_vertices().size(); ++i) {
            const auto &lhs = a.source_vertices()[i], &rhs = b.source_vertices()[i];
            CHECK(lhs.source.x == rhs.source.x && lhs.source.y == rhs.source.y);
            CHECK(near(lhs.position, rhs.position));
        }
        CHECK(!print_at(a, {LANDSCAPE.x + 200, LANDSCAPE.y + 11}));
        const Box crop{LANDSCAPE.x + .83, LANDSCAPE.y + 1.7, LANDSCAPE.w - 2.16, 350.8};
        regular.emit(authorized(crop), a, false);
        CHECK(a.source_faces().size() > 10 * visible.size());
        for (const auto &face : a.source_faces())
            for (const auto index : face) CHECK(index < a.source_vertices().size());
        visible.back() =
            visible.front();  // Ambiguous overlapping source cannot silently duplicate print.
        CHECK(!regular.start(LANDSCAPE, visible, 1337, 1));
        regular.emit(authorized(LANDSCAPE), a);
        CHECK(a.source_faces().empty() && a.crew().empty());
    }

    void authorized_crop_does_not_repose_or_retriangulate_the_print() {
        const auto take = find_take(TALL, .055, .17);
        if (!take) return;
        const auto face = Close::VisibleFace::flat(TALL);
        const std::array marks{point(TALL, .249, .346),
            point(TALL, .323, .510),
            point(TALL, .360, .613),
            point(TALL, .492, .582)};
        Close model;
        Close::Frame frame;
        CHECK(model.start(TALL, std::span{&face, 1}, take->seed, 1));
        for (int tick = 0; tick < 48; ++tick) model.advance(.01);
        model.emit(authorized(TALL), frame, false);
        std::array<std::optional<Vec3>, 4> full;
        bool displaced = false;
        for (std::size_t i = 0; i < marks.size(); ++i) {
            full[i] = print_at(frame, marks[i]);
            displaced |= full[i] && full[i]->y > marks[i].y + 5;
        }
        CHECK(displaced);
        for (std::size_t i = 0; i < marks.size(); ++i) {
            const auto mark = marks[i];
            const Box crop{mark.x - 5.31, mark.y - 7.49, 10.72, 15.23};
            model.emit(authorized(crop), frame, false);
            const auto clipped = print_at(frame, mark);
            CHECK(full[i] && clipped && near(*full[i], *clipped));
            for (const auto &v : frame.source_vertices()) {
                CHECK(v.source.x >= crop.x && v.source.x <= crop.x + crop.w);
                CHECK(v.source.y >= crop.y && v.source.y <= crop.y + crop.h);
            }
        }
    }

    void meeting_source_patches_keep_their_t_junction_joined() {
        const auto split = point(TALL, .465, .413);
        const Box left{TALL.x, TALL.y, split.x - TALL.x, TALL.h};
        const Box upper{split.x, TALL.y, TALL.x + TALL.w - split.x, split.y - TALL.y};
        const Box lower{split.x, split.y, upper.w, TALL.y + TALL.h - split.y};
        // The right half is already tilted independently of the left, but their
        // posed coordinates agree on the shared seam and both branches of the T.
        const Vec3 slant{upper.w * .92, 3, 7};
        const std::array visible{Close::VisibleFace::flat(left),
            Close::VisibleFace{upper, {upper.x, upper.y, 0}, slant, {0, upper.h, 0}},
            Close::VisibleFace{lower, {lower.x, lower.y, 0}, slant, {0, lower.h, 0}}};
        Close model;
        Close::Frame frame;
        CHECK(model.start(TALL, visible, 1, 1));
        for (int tick = 0; tick < 46; ++tick) model.advance(.01);
        model.emit(authorized({TALL.x + 9, TALL.y + 20.37, TALL.w - 18, 329.82}), frame, false);
        const auto vertices = frame.source_vertices();
        const auto faces = frame.source_faces();
        std::size_t matched = 0;
        bool met_junction = false;
        for (const auto &face : faces) {
            for (std::size_t edge = 0; edge < face.size(); ++edge) {
                const auto &a = vertices[face[edge]], &b = vertices[face[(edge + 1) % 3]];
                const auto &other = vertices[face[(edge + 2) % 3]];
                if (a.source.x != split.x || b.source.x != split.x || other.source.x >= split.x)
                    continue;
                bool joined = false;
                for (const auto &neighbor : faces) {
                    for (std::size_t next = 0; next < neighbor.size(); ++next) {
                        const auto &c = vertices[neighbor[next]],
                                   &d = vertices[neighbor[(next + 1) % 3]];
                        if (c.source.x != split.x || d.source.x != split.x ||
                            a.source.y != d.source.y || b.source.y != c.source.y)
                            continue;
                        if (vertices[neighbor[(next + 2) % 3]].source.x <= split.x) continue;
                        CHECK(near(a.position, d.position) && near(b.position, c.position));
                        joined = true;
                    }
                }
                CHECK(joined);
                matched += joined;
                met_junction |= joined && (a.source.y == split.y || b.source.y == split.y);
            }
        }
        CHECK(matched > 2 && met_junction);
    }

    void cue_requires_its_source_point_inside_the_draw_crop() {
        const auto take = find_take(LANDSCAPE, .43, .57);
        if (!take) return;
        auto moved = Close::VisibleFace::flat(LANDSCAPE);
        moved.origin.x += 115;
        moved.origin.y -= 45;
        moved.origin.z = 6;
        Close model;
        Close::Frame frame;
        CHECK(model.start(LANDSCAPE, std::span{&moved, 1}, take->seed, 1));
        model.advance(.16);
        model.advance(.15);
        model.emit(authorized({take->cue.x - 5, take->cue.y - 5, 10, 10}), frame);
        CHECK(frame.flash && near(frame.flash->position, {take->cue.x + 115, take->cue.y - 45, 6}));
        model.emit(authorized({take->cue.x + 110, take->cue.y - 50, 10, 10}), frame);
        CHECK(!frame.source_faces().empty());
        CHECK(!frame.flash);  // Covers the moved flash location, but not its source.
    }

    void narrow_tall_faces_clear_and_nonfinite_input_is_denied() {
        constexpr Box BODY{11.3, -3.5, 2.625, 159.75};
        auto face = Close::VisibleFace::flat(BODY);
        Close model;
        Close::Frame frame;
        CHECK(model.start(BODY, std::span{&face, 1}, 83, 1));
        bool posed = false;
        for (int tick = 0; tick <= 100; ++tick) {
            model.emit(authorized({BODY.x + .07, BODY.y, BODY.w - .15, BODY.h}), frame);
            for (const auto &v : frame.source_vertices()) {
                CHECK(std::isfinite(v.position.x) && std::isfinite(v.position.y) &&
                      std::isfinite(v.position.z));
                posed |= v.position.y > v.source.y + 1;
            }
            if (tick >= 95) CHECK(frame.source_faces().empty() && frame.source_vertices().empty());
            if (tick != 100) model.advance(.01);
        }
        CHECK(posed && !model.active());
        CHECK(frame.crew().empty() && frame.dust().empty() && !frame.flash);
        CHECK(model.start(BODY, std::span{&face, 1}, 83, 1));
        model.emit(authorized(BODY), frame);
        face.v.z = std::numeric_limits<double>::quiet_NaN();
        CHECK(!model.start(BODY, std::span{&face, 1}, 83, 1));
        model.emit(authorized(BODY), frame);
        CHECK(frame.source_faces().empty() && frame.source_vertices().empty() &&
              frame.crew().empty() && !frame.flash);
    }

    void stalls_and_bad_facts_clear_every_output() {
        const auto face = Close::VisibleFace::flat(LANDSCAPE);
        Close model;
        Close::Frame frame;
        for (const double elapsed :
            {-1., std::numeric_limits<double>::infinity(), STALL_SECONDS + .01}) {
            CHECK(model.start(LANDSCAPE, std::span{&face, 1}, 17, .82));
            model.emit(authorized(LANDSCAPE), frame);
            CHECK(!frame.source_faces().empty());
            model.advance(elapsed);
            model.emit(authorized(LANDSCAPE), frame);
            CHECK(!model.active() && frame.source_faces().empty() && frame.crew().empty() &&
                  frame.dust().empty() && !frame.flash);
        }
        CHECK(model.start(LANDSCAPE, std::span{&face, 1}, 17, .82));
        model.advance(0, true);
        CHECK(!model.active());
        CHECK(!model.start(LANDSCAPE, std::span{&face, 1}, 17, 0));
        const std::array<Close::VisibleFace, Close::MAX_VISIBLE_FACES + 1> oversized{};
        CHECK(!model.start(LANDSCAPE, oversized, 17, .82));
    }

    void worker_scale_tracks_window_not_source_crop() {
        Close model;
        Close::Frame frame;
        for (double span : {400., 800.}) {
            const Box body{0, 0, 2 * span, span};
            const auto face = Close::VisibleFace::flat(body);
            std::array<double, 2> heights{};
            CHECK(model.start(body, std::span{&face, 1}, 19, 1));
            model.emit(authorized(body), frame);
            CHECK(frame.crew().size() == 2);
            for (std::size_t i = 0; i < frame.crew().size(); ++i) {
                heights[i] = frame.crew()[i].height;
                CHECK(heights[i] > .067 * span && heights[i] < .081 * span);
            }
            CHECK(model.start(body, std::span{&face, 1}, 19, 1, 1, {0, 0, span, span / 2}));
            model.emit(authorized(body), frame);
            for (std::size_t i = 0; i < frame.crew().size(); ++i)
                CHECK(std::abs(frame.crew()[i].height - .5 * heights[i]) < 1e-7);
            CHECK(model.start(body, std::span{&face, 1}, 19, 1, 2));
            model.emit(authorized(body), frame);
            for (std::size_t i = 0; i < frame.crew().size(); ++i)
                CHECK(std::abs(frame.crew()[i].height - 2 * heights[i]) < 1e-7);
            CHECK(model.start(body, std::span{&face, 1}, 19, 1, 0));
            model.emit(authorized(body), frame);
            CHECK(frame.crew().empty() && !frame.source_faces().empty());
            bool saw_flash = false;
            for (int tick = 0; tick < 35; ++tick) {
                model.advance(.01);
                model.emit(authorized(body), frame);
                CHECK(frame.crew().empty());
                saw_flash |= frame.flash.has_value();
            }
            CHECK(saw_flash);
            for (const auto &v : frame.source_vertices())
                CHECK(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z));
        }
    }

}  // namespace

int main() {
    worker_scale_tracks_window_not_source_crop();
    seed_and_shape_change_the_first_region();
    preserves_only_the_interrupted_print();
    crew_clears_before_flash_and_dust_stays_local();
    clipping_many_patches_keeps_the_same_seeded_clock();
    authorized_crop_does_not_repose_or_retriangulate_the_print();
    meeting_source_patches_keep_their_t_junction_joined();
    cue_requires_its_source_point_inside_the_draw_crop();
    narrow_tall_faces_clear_and_nonfinite_input_is_denied();
    stalls_and_bad_facts_clear_every_output();
    return g_failures == 0 ? 0 : 1;
}
