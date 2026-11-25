#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <stop_token>

#include "Check.hpp"
#include "window/paper/PaperCrumple.hpp"

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{1000, 700};

    PaperSurface frame(const PaperCrumple &model) {
        PaperSurface result;
        model.frame(result);
        return result;
    }
    double distance(Vec3 a, Vec3 b) {
        return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
    }
    Vec3 subtract(Vec3 a, Vec3 b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    Vec3 cross(Vec3 a, Vec3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    double dot(Vec3 a, Vec3 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    bool pierces(Vec3 p, Vec3 q, Vec3 a, Vec3 b, Vec3 c) {
        const auto e1 = subtract(b, a), e2 = subtract(c, a), direction = subtract(q, p);
        const auto h = cross(direction, e2);
        const double determinant = dot(e1, h);
        if (std::abs(determinant) < 1e-11) return false;
        const auto offset = subtract(p, a), side = cross(offset, e1);
        const double u = dot(offset, h) / determinant, v = dot(direction, side) / determinant;
        const double t = dot(e2, side) / determinant;
        return t > 1e-5 && t < 1 - 1e-5 && u > 1e-5 && v > 1e-5 && u + v < 1 - 1e-5;
    }
    void no_piercing(const PaperSurface &surface) {
        std::size_t pairs = 0;
        const auto cuts = [&](const PaperTriangle &a, const PaperTriangle &b) {
            for (unsigned e = 0; e < 3; ++e)
                if (pierces(surface.points[a.vertex[e]].position,
                        surface.points[a.vertex[(e + 1) % 3]].position,
                        surface.points[b.vertex[0]].position,
                        surface.points[b.vertex[1]].position,
                        surface.points[b.vertex[2]].position))
                    return true;
            return false;
        };
        for (std::size_t i = 0; i < surface.faces.size(); ++i) {
            const auto &a = surface.faces[i];
            for (std::size_t j = 0; j < i; ++j) {
                const auto &b = surface.faces[j];
                bool adjacent = false;
                for (const auto x : a.vertex)
                    for (const auto y : b.vertex) adjacent = adjacent || x == y;
                if (!adjacent && (cuts(a, b) || cuts(b, a))) ++pairs;
            }
        }
        CHECK(pairs == 0);
    }
    Vec3 rest(const PaperPoint &p, Vec2 size) {
        return {(p.uv.x - 0.5) * std::sqrt(size.x / size.y),
            (p.uv.y - 0.5) * std::sqrt(size.y / size.x),
            0};
    }
    void run(PaperCrumple &model, double seconds) {
        while (seconds > 1e-12) {
            const double dt = std::min(seconds, 0.01);
            model.advance(dt);
            seconds -= dt;
        }
    }
    double delta(const PaperSurface &a, const PaperSurface &b) {
        if (a.points.size() != b.points.size()) return INFINITY;
        double result = 0;
        for (std::size_t i = 0; i < a.points.size(); ++i)
            result = std::max(result, distance(a.points[i].position, b.points[i].position));
        return result;
    }
    double radius(const PaperSurface &surface) {
        Vec3 centre{};
        for (const auto &p : surface.points) {
            centre.x += p.position.x;
            centre.y += p.position.y;
            centre.z += p.position.z;
        }
        const double count = static_cast<double>(surface.points.size());
        centre.x /= count;
        centre.y /= count;
        centre.z /= count;
        double sum = 0;
        for (const auto &p : surface.points) {
            const double r = distance(p.position, centre);
            sum += r * r;
        }
        return std::sqrt(sum / count);
    }
    void valid(const PaperSurface &surface) {
        CHECK(!surface.points.empty());
        CHECK(!surface.faces.empty());
        CHECK(std::isfinite(surface.opacity));
        for (const auto &p : surface.points) {
            CHECK(std::isfinite(p.position.x));
            CHECK(std::isfinite(p.position.y));
            CHECK(std::isfinite(p.position.z));
            CHECK(p.uv.x >= 0 && p.uv.x <= 1 && p.uv.y >= 0 && p.uv.y <= 1);
        }
        for (const auto &face : surface.faces) {
            for (unsigned e = 0; e < 3; ++e) {
                CHECK(face.vertex[e] < surface.points.size());
                CHECK(face.crease[e] >= 0 && face.crease[e] <= 1);
            }
            CHECK(face.vertex[0] != face.vertex[1]);
            CHECK(face.vertex[1] != face.vertex[2]);
            CHECK(face.vertex[2] != face.vertex[0]);
        }
    }
    void identity(const PaperSurface &surface) {
        valid(surface);
        CHECK(surface.opacity == 1);
        for (const auto &p : surface.points)
            CHECK(distance(p.position, rest(p, surface.size)) < 1e-12);
        for (const auto &face : surface.faces)
            for (const float crease : face.crease) CHECK(crease == 0);
    }

    void test_aspect_and_endpoints() {
        for (const Vec2 size : {SIZE, Vec2{600, 1000}, Vec2{3000, 800}, Vec2{900, 900}}) {
            PaperCrumple model;
            PaperParams params;
            params.simulation_quality = PaperQuality::Low;
            model.begin(Transition::Close, params, 7, size);
            identity(frame(model));
            run(model, params.duration);
            CHECK(model.closed() && !model.active());
            CHECK(frame(model).opacity == 0);
            model.begin(Transition::Open, params, 99, size);
            run(model, params.duration);
            CHECK(!model.closed() && !model.active());
            identity(frame(model));
        }
        for (const double duration : {0.1, 0.6, 3.0}) {
            PaperParams params;
            params.duration = duration;
            params.simulation_quality = PaperQuality::Low;
            PaperCrumple model;
            model.begin(Transition::Close, params, 4, SIZE);
            run(model, duration * 0.5);
            CHECK(model.active());
            run(model, duration * 0.5 + 0.001);
            CHECK(model.closed());
        }
    }

    void test_force_entropy_and_determinism() {
        PaperParams params;
        params.simulation_quality = PaperQuality::Low;
        PaperCrumple a, b, other;
        a.begin(Transition::Close, params, 2, SIZE);
        b.begin(Transition::Close, params, 2, SIZE);
        other.begin(Transition::Close, params, 9, SIZE);
        CHECK(delta(frame(a), frame(other)) == 0);  // Never randomize material geometry.
        run(a, 0.21);
        run(b, 0.21);
        run(other, 0.21);
        CHECK(delta(frame(a), frame(b)) == 0);
        // Remeshing may give different vertex counts; compare an observable instead.
        CHECK(std::abs(radius(frame(a)) - radius(frame(other))) > 1e-7);
        params.variation = 0;
        a.reset();
        b.reset();
        a.begin(Transition::Close, params, 2, {1001, 700});
        b.begin(Transition::Close, params, 9, {1001, 700});
        run(a, 0.21);
        run(b, 0.21);
        CHECK(delta(frame(a), frame(b)) >
              1e-7);  // Minimum force entropy remains even at zero variation.
    }

    void test_continuity_and_fractional_publication() {
        PaperParams params;
        params.simulation_quality = PaperQuality::Low;
        PaperCrumple model;
        model.begin(Transition::Close, params, 5, SIZE);
        const double tick = 1.0 / 240;
        model.advance(tick * 8.25);
        const auto first = frame(model);
        model.advance(tick * 0.25);
        const auto middle = frame(model);
        model.advance(tick * 0.25);
        const auto last = frame(model);
        CHECK(first.points.size() == last.points.size());
        double motion = 0;
        for (std::size_t i = 0; i < first.points.size(); ++i) {
            const auto &a = first.points[i].position;
            const auto &b = last.points[i].position;
            CHECK(distance(middle.points[i].position,
                      {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5}) < 1e-12);
            motion = std::max(motion, distance(a, b));
        }
        CHECK(motion > 1e-8);
        model.begin(Transition::Open, params, 17, {SIZE.x + 5, SIZE.y + 8});
        const auto reversed = frame(model);
        CHECK(delta(last, reversed) == 0);
        CHECK(last.opacity == reversed.opacity);
        run(model, 0.008);
        const auto opening = frame(model);
        model.begin(Transition::Close, params, 25, SIZE);
        CHECK(delta(opening, frame(model)) == 0);
    }

    void test_physical_packet() {
        PaperParams params;
        params.duration = 0.6;
        PaperCrumple model;
        model.begin(Transition::Open, params, 42, SIZE);
        const auto packet = frame(model);
        valid(packet);
        no_piercing(packet);
        CHECK(packet.opacity == 0);
        double min_z = INFINITY, max_z = -INFINITY, strain_sum = 0, strain_max = 0;
        std::size_t edges = 0;
        float sharpest = 0;
        for (const auto &p : packet.points) {
            min_z = std::min(min_z, p.position.z);
            max_z = std::max(max_z, p.position.z);
        }
        for (const auto &f : packet.faces) {
            for (unsigned e = 0; e < 3; ++e) {
                const auto &a = packet.points[f.vertex[e]];
                const auto &b = packet.points[f.vertex[(e + 1) % 3]];
                const double metric = distance(rest(a, SIZE), rest(b, SIZE));
                const double strain = std::abs(distance(a.position, b.position) / metric - 1);
                strain_sum += strain;
                strain_max = std::max(strain_max, strain);
                sharpest = std::max(sharpest, f.crease[e]);
                ++edges;
            }
        }
        std::printf(
            "[paper] packet rms-radius=%g thickness=%g mean-strain=%g max-strain=%g crease=%g\n",
            radius(packet),
            max_z - min_z,
            strain_sum / edges,
            strain_max,
            sharpest);
        CHECK(radius(packet) < 0.38);
        CHECK(max_z - min_z > 0.08);  // A layered object, not planar uniform shrink.
        CHECK(strain_sum / edges < 0.08);
        CHECK(strain_max < 0.4);
        CHECK(sharpest > 0.15);
        for (unsigned sample = 0; sample < 5; ++sample) {
            run(model, 0.1);
            valid(frame(model));
            no_piercing(frame(model));
        }
        CHECK(frame(model).opacity > 0.9);
        run(model, 0.095);
        const auto penultimate = frame(model);
        no_piercing(penultimate);
        double residual = 0;
        for (const auto &p : penultimate.points)
            residual = std::max(residual, distance(p.position, rest(p, penultimate.size)));
        CHECK(residual < 0.03);
        run(model, 0.005);
        identity(frame(model));
    }
    void test_duration_scaled_flat_handoff() {
        for (const double duration : {0.1, 0.6, 1.2}) {
            for (const auto size : {Vec2{1280, 720}, Vec2{2400, 400}, Vec2{400, 2400}}) {
                PaperParams params;
                params.duration = duration;
                params.simulation_quality = PaperQuality::Low;
                PaperCrumple model;
                model.begin(Transition::Open, params, 441, size);
                no_piercing(frame(model));
                run(model, duration - 1.0 / 480);
                const auto last = frame(model);
                no_piercing(last);
                double gap = 0;
                for (const auto &p : last.points)
                    gap = std::max(gap, distance(p.position, rest(p, last.size)));
                std::printf("[paper] open duration=%g aspect=%g penultimate-gap=%g\n",
                    duration,
                    size.x / size.y,
                    gap);
                CHECK(gap < 0.03);
                run(model, 1.0 / 480 + 1e-5);
                identity(frame(model));
            }
        }
    }

    void test_close_compaction_and_opposing_layers() {
        PaperParams params;
        params.fold_density = 1;
        PaperCrumple model;
        model.begin(Transition::Close, params, 441, {1280, 720});
        run(model, params.duration);
        const auto surface = frame(model);
        valid(surface);
        no_piercing(surface);
        double left = INFINITY, right = -INFINITY, top = INFINITY, bottom = -INFINITY;
        double front = 0, back = 0, strain = 0;
        std::size_t edges = 0;
        for (const auto &p : surface.points) {
            left = std::min(left, p.position.x);
            right = std::max(right, p.position.x);
            top = std::min(top, p.position.y);
            bottom = std::max(bottom, p.position.y);
        }
        for (const auto &f : surface.faces) {
            const auto &a = surface.points[f.vertex[0]].position;
            const auto &b = surface.points[f.vertex[1]].position;
            const auto &c = surface.points[f.vertex[2]].position;
            const double normal_z = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            front += std::max(normal_z, 0.0);
            back += std::max(-normal_z, 0.0);
            for (unsigned e = 0; e < 3; ++e) {
                const auto &p = surface.points[f.vertex[e]];
                const auto &q = surface.points[f.vertex[(e + 1) % 3]];
                strain += std::abs(distance(p.position, q.position) /
                                       distance(rest(p, surface.size), rest(q, surface.size)) -
                                   1);
                ++edges;
            }
        }
        const double area = (right - left) * (bottom - top);
        std::printf("[paper] close box-area=%g mean-strain=%g opposing-projection=%g/%g\n",
            area,
            strain / edges,
            front,
            back);
        // Finite-thickness adjacent folds retain at least 84% projected compaction.
        CHECK(area <= 0.16);
        CHECK(strain / edges < 0.08);
        CHECK(front > 0.015 && back > 0.015);
    }

    void test_parameter_coupling() {
        PaperParams base;
        base.simulation_quality = PaperQuality::Low;
        PaperCrumple reference;
        reference.begin(Transition::Close, base, 11, SIZE);
        run(reference, 0.3);
        const auto expected = frame(reference);
        // Each mechanical setting must reach the physical state, not just a shader.
        for (unsigned setting = 0; setting < 10; ++setting) {
            auto changed = base;
            switch (setting) {
                case 0:
                    changed.crumpliness = 0;
                    break;
                case 1:
                    changed.fold_density = 0;
                    break;
                case 2:
                    changed.paper_stiffness = 0;
                    break;
                case 3:
                    changed.crumple_size = 0.4;
                    break;
                case 4:
                    changed.rotation_intensity = 1;
                    break;
                case 5:
                    changed.settling = 1;
                    break;
                case 6:
                    changed.variation = 1;
                    break;
                case 7:
                    changed.crumple_origin = PaperOrigin::Left;
                    break;
                case 8:
                    changed.force_direction = PaperForce::Rotational;
                    break;
                case 9:
                    changed.force_direction = PaperForce::Directional;
                    break;
            }
            PaperCrumple model;
            model.begin(Transition::Close, changed, 11, SIZE);
            run(model, 0.3);
            const auto actual = frame(model);
            valid(actual);
            CHECK(delta(expected, actual) > 1e-7);
        }
        auto unlit = base;
        unlit.shading = 0;
        PaperCrumple model;
        model.begin(Transition::Close, unlit, 11, SIZE);
        run(model, 0.3);
        CHECK(frame(model).shading == 0);
        CHECK(delta(frame(model), expected) == 0);
        auto no_retention = base;
        no_retention.crease_persistence = 0;
        model.reset();
        model.begin(Transition::Close, no_retention, 11, SIZE);
        run(model, 0.3);
        CHECK(delta(frame(model), expected) == 0);  // Retention is an opening-only control.

        PaperCrumple slow, taut;
        base.unfold_tension = 0;
        slow.begin(Transition::Open, base, 11, SIZE);
        base.unfold_tension = 1;
        taut.begin(Transition::Open, base, 11, SIZE);
        run(slow, 0.22);
        run(taut, 0.22);
        CHECK(delta(frame(slow), frame(taut)) > 1e-7);
    }
    void test_local_crease_history() {
        PaperParams params;
        params.simulation_quality = PaperQuality::Low;
        params.crease_persistence = 0;
        PaperCrumple released, retained;
        released.begin(Transition::Open, params, 7, SIZE);
        params.crease_persistence = 1;
        retained.begin(Transition::Open, params, 7, SIZE);
        CHECK(delta(frame(released), frame(retained)) == 0);
        run(released, 0.23);
        run(retained, 0.23);
        const auto before = frame(retained);
        CHECK(delta(frame(released), before) > 1e-7);
        retained.begin(Transition::Close, {}, 99, SIZE);
        CHECK(delta(before, frame(retained)) == 0);
        run(retained, 0.04);
        no_piercing(frame(retained));

        run(released, 1);
        identity(frame(released));
        released.begin(Transition::Open, params, 11, SIZE);
        CHECK(released.active() && frame(released).opacity == 0);
        CHECK(radius(frame(released)) < 0.38);
    }
    void test_crushed_packet_contact_corpus() {
        PaperParams params;
        params.duration = 0.6;
        constexpr Vec2 CAPTURE{1280, 720};
        const double pixels = std::sqrt(CAPTURE.x * CAPTURE.y);
        for (std::uint64_t seed = 433; seed <= 444; ++seed) {
            PaperCrumple model;
            model.begin(Transition::Open, params, seed, CAPTURE);
            no_piercing(frame(model));
            for (unsigned sample = 0; sample < 6; ++sample) {
                run(model, 0.1);
                no_piercing(frame(model));
            }
            // Nominal duration ends the tool motion, not the physical settling.
            // Keep checking contact and subpixel geometry until the quiet handoff.
            for (unsigned tick = 0; model.active() && tick < 8; ++tick) {
                const auto settling = frame(model);
                CHECK(settling.opacity == 1);
                for (const auto &point : settling.points)
                    CHECK(pixels * distance(point.position, rest(point, settling.size)) <= 0.25);
                run(model, 1.0 / 240);
                no_piercing(frame(model));
            }
            CHECK(!model.active() && !model.closed());
            identity(frame(model));
        }
        PaperCrumple close;
        params.duration = 0.5;
        close.begin(Transition::Close, params, 437, {1280, 720});
        run(close, 0.5);
        no_piercing(frame(close));
    }
    void test_directional_opening_shape_release() {
        constexpr double PIXELS = 620;
        constexpr double DT = 1.0 / 120;
        using MaterialKey = std::pair<double, double>;
        const std::array<MaterialKey, 4> corners{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        struct Scenario {
            std::uint64_t seed;
            double duration;
            PaperOrigin origin;
            PaperForce force;
        };
        const std::array<Scenario, 10> scenarios{{
            {1, 1.2, PaperOrigin::Left, PaperForce::Directional},
            {5120, 1.2, PaperOrigin::Left, PaperForce::Directional},
            {5126, 1.2, PaperOrigin::Left, PaperForce::Directional},
            {5119, 1.2, PaperOrigin::Left, PaperForce::Directional},
            {442, 1.2, PaperOrigin::Left, PaperForce::Directional},
            {38515989, 0.6, PaperOrigin::Right, PaperForce::Rotational},
            {5115, 1.2, PaperOrigin::Left, PaperForce::Directional},
            {718219966499ULL, 1.2, PaperOrigin::Left, PaperForce::Directional},
            {440, 1.2, PaperOrigin::Left, PaperForce::Directional},
            {38504381, 0.6, PaperOrigin::Bottom, PaperForce::Inward},
        }};
        for (const auto &scenario : scenarios) {
            const auto seed = scenario.seed;
            PaperParams params;
            params.duration = scenario.duration;
            params.crumple_origin = scenario.origin;
            params.force_direction = scenario.force;
            PaperCrumple model;
            model.begin(Transition::Open, params, seed, {PIXELS, PIXELS});
            std::map<MaterialKey, Vec3> previous;
            double held = 0, longest = 0, penultimate = 0;
            for (const auto &point : frame(model).points) {
                previous[{point.uv.x, point.uv.y}] = point.position;
                penultimate = std::max(
                    penultimate, PIXELS * distance(point.position, rest(point, {PIXELS, PIXELS})));
            }
            const auto ticks = static_cast<unsigned>(std::lround(params.duration / DT));
            unsigned finished = 0;
            for (unsigned tick = 1; tick <= ticks + 60; ++tick) {
                model.advance(DT);
                const auto surface = frame(model);
                std::map<MaterialKey, Vec3> current;
                double flat_error = 0;
                for (const auto &point : surface.points) {
                    current[{point.uv.x, point.uv.y}] = point.position;
                    flat_error = std::max(
                        flat_error, PIXELS * distance(point.position, rest(point, surface.size)));
                }
                double shape_change = 0, shape_error = 0;
                for (const auto &[uv, point] : current) {
                    const auto old = previous.find(uv);
                    if (old == previous.end()) continue;
                    for (const auto &corner : corners) {
                        const Vec3 flat_point{uv.first - 0.5, uv.second - 0.5, 0};
                        const Vec3 flat_corner{corner.first - 0.5, corner.second - 0.5, 0};
                        shape_error = std::max(shape_error,
                            PIXELS * std::abs(distance(point, current.at(corner)) -
                                              distance(flat_point, flat_corner)));
                        // Material chord lengths remove both shared translation and
                        // rigid rotation: moving a glued wad cannot pass this gate.
                        shape_change = std::max(shape_change,
                            PIXELS * std::abs(distance(point, current.at(corner)) -
                                              distance(old->second, previous.at(corner))));
                    }
                }
                const double time = tick * DT;
                held = time > 0.1 && shape_error > 1 && shape_change < 0.05 ? held + DT : 0;
                longest = std::max(longest, held);
                if (!model.active()) {
                    finished = tick;
                    break;
                }
                penultimate = flat_error;
                previous = std::move(current);
            }
            std::printf(
                "[paper] release seed=%llu rigid-invariant-held=%g penultimate-px=%g "
                "completion=%g\n",
                static_cast<unsigned long long>(seed),
                longest,
                penultimate,
                finished * DT);
            CHECK(longest < 0.1);
            CHECK(penultimate < 1);
            CHECK(finished > 0 && finished <= ticks + 12);
        }
    }

    // Evaluate immutable material coordinates, not array indices: adaptive remeshing
    // may insert/remove nodes while the same piece of paper must move continuously.
    Vec3 material_sample(const PaperSurface &surface, Vec2 uv) {
        for (const auto &face : surface.faces) {
            const auto &a = surface.points[face.vertex[0]];
            const auto &b = surface.points[face.vertex[1]];
            const auto &c = surface.points[face.vertex[2]];
            const double bx = b.uv.x - a.uv.x, by = b.uv.y - a.uv.y;
            const double cx = c.uv.x - a.uv.x, cy = c.uv.y - a.uv.y;
            const double dx = uv.x - a.uv.x, dy = uv.y - a.uv.y;
            const double determinant = bx * cy - by * cx;
            if (std::abs(determinant) < 1e-15) continue;
            const double u = (dx * cy - dy * cx) / determinant;
            const double v = (bx * dy - by * dx) / determinant;
            if (u < -1e-9 || v < -1e-9 || u + v > 1 + 1e-9) continue;
            return {a.position.x + u * (b.position.x - a.position.x) +
                        v * (c.position.x - a.position.x),
                a.position.y + u * (b.position.y - a.position.y) +
                    v * (c.position.y - a.position.y),
                a.position.z + u * (b.position.z - a.position.z) +
                    v * (c.position.z - a.position.z)};
        }
        CHECK(false);  // A published surface must cover the original UV domain.
        return {};
    }

    double face_residual_angle(const PaperSurface &surface) {
        double result = 0;
        for (const auto &face : surface.faces) {
            const auto &a = surface.points[face.vertex[0]].position;
            const auto normal = cross(subtract(surface.points[face.vertex[1]].position, a),
                subtract(surface.points[face.vertex[2]].position, a));
            const double length = distance(normal, {});
            CHECK(length > 1e-15);
            result = std::max(
                result, std::acos(std::clamp(normal.z / std::max(length, 1e-15), -1.0, 1.0)));
        }
        return result;
    }

    void test_opening_material_trajectory() {
        struct Scenario {
            std::uint64_t seed;
            double duration;
            Vec2 size;
            double density;
            PaperQuality quality;
            PaperOrigin origin = PaperOrigin::Left;
            PaperForce force = PaperForce::Directional;
        };
        const std::array<Scenario, 12> scenarios{{
            {5120, 1.2, {620, 620}, 0.55, PaperQuality::Adaptive},
            {440, 1.2, {620, 620}, 0.55, PaperQuality::Adaptive},
            {5120,
                3.0,
                {1280, 720},
                0.55,
                PaperQuality::Adaptive,
                PaperOrigin::Center,
                PaperForce::Inward},
            {440,
                3.0,
                {620, 620},
                0.55,
                PaperQuality::Adaptive,
                PaperOrigin::Center,
                PaperForce::Inward},
            {441, 0.1, {2400, 400}, 0.55, PaperQuality::Low},
            {441, 0.1, {400, 2400}, 0.55, PaperQuality::Low},
            {5120, 0.6, {2400, 400}, 0.2, PaperQuality::Adaptive},
            {440, 0.6, {400, 2400}, 0.85, PaperQuality::Adaptive},
            {1, 1.2, {1280, 720}, 0.2, PaperQuality::Low},
            {442, 1.2, {720, 1280}, 0.85, PaperQuality::High},
            {38515989,
                0.6,
                {620, 620},
                0.55,
                PaperQuality::Adaptive,
                PaperOrigin::Right,
                PaperForce::Rotational},
            {38504381,
                0.6,
                {620, 620},
                0.55,
                PaperQuality::Adaptive,
                PaperOrigin::Bottom,
                PaperForce::Inward},
        }};
        constexpr double DT = 1.0 / 240;
        for (const auto &scenario : scenarios) {
            PaperParams params;
            params.duration = scenario.duration;
            params.fold_density = scenario.density;
            params.simulation_quality = scenario.quality;
            params.crumple_origin = scenario.origin;
            params.force_direction = scenario.force;
            PaperCrumple model;
            model.begin(Transition::Open, params, scenario.seed, scenario.size);
            const double pixels = std::sqrt(scenario.size.x * scenario.size.y);
            std::array<Vec3, 25> previous{}, velocity{};
            std::array<double, 25> residual{}, rebound{}, travel{}, initial_error{};
            auto surface = frame(model);
            for (unsigned i = 0; i < previous.size(); ++i) {
                const Vec2 uv{(i % 5) / 4.0, (i / 5) / 4.0};
                previous[i] = material_sample(surface, uv);
                residual[i] = pixels * distance(previous[i], rest({{}, uv}, surface.size));
            }
            double angle = face_residual_angle(surface);
            double angle_rebound = 0, peak_speed = 0, peak_kick = 0;
            double early_angle = 0, early_chord_error = 0, late_angle = 0, penultimate = 0;
            unsigned finished = 0;
            bool sampled_late = false;
            const unsigned ticks = static_cast<unsigned>(std::ceil(params.duration / DT));
            for (unsigned tick = 1; tick <= ticks + 24; ++tick) {
                model.advance(DT);
                model.frame(surface);
                const double fraction = tick * DT / params.duration;
                const double next_angle = face_residual_angle(surface);
                if (fraction >= 0.2 && fraction <= 0.4)
                    early_angle = std::max(early_angle, next_angle);
                if (fraction >= 0.9) late_angle = std::max(late_angle, next_angle);
                if (fraction >= 0.65) angle_rebound += std::max(0.0, next_angle - angle);
                angle = next_angle;
                const Vec3 corner =
                    fraction >= 0.2 && fraction <= 0.4 ? material_sample(surface, {0, 0}) : Vec3{};
                const Vec3 flat_corner = rest({{}, {0, 0}}, surface.size);
                double error = 0;
                for (unsigned i = 0; i < previous.size(); ++i) {
                    const Vec2 uv{(i % 5) / 4.0, (i / 5) / 4.0};
                    const Vec3 point = material_sample(surface, uv);
                    const Vec3 displacement = subtract(point, previous[i]);
                    const double next = pixels * distance(point, rest({{}, uv}, surface.size));
                    if (fraction >= 0.2 && fraction <= 0.4) {
                        const Vec3 flat = rest({{}, uv}, surface.size);
                        early_chord_error = std::max(early_chord_error,
                            pixels *
                                std::abs(distance(point, corner) - distance(flat, flat_corner)));
                    }
                    if (fraction >= 0.65) {
                        if (!sampled_late) initial_error[i] = residual[i];
                        rebound[i] += std::max(0.0, next - residual[i]);
                        travel[i] += pixels * distance(displacement, {});
                        peak_speed = std::max(peak_speed, pixels * distance(displacement, {}) / DT);
                        peak_kick =
                            std::max(peak_kick, pixels * distance(displacement, velocity[i]) / DT);
                    }
                    error = std::max(error, next);
                    previous[i] = point;
                    velocity[i] = displacement;
                    residual[i] = next;
                }
                sampled_late = fraction >= 0.65;
                if (!model.active()) {
                    finished = tick;
                    break;
                }
                penultimate = error;
            }
            const auto worst = static_cast<unsigned>(
                std::max_element(rebound.begin(), rebound.end()) - rebound.begin());
            double detour = 0;
            for (unsigned i = 0; i < travel.size(); ++i)
                detour = std::max(detour, travel[i] - initial_error[i]);
            std::printf(
                "[paper] motion seed=%llu duration=%g size=%gx%g density=%g "
                "rebound-px=%g uv=(%g,%g) detour-px=%g speed=%g kick=%g "
                "angle-rebound=%g late-angle=%g early-angle=%g early-chord-px=%g completion=%g\n",
                static_cast<unsigned long long>(scenario.seed),
                scenario.duration,
                scenario.size.x,
                scenario.size.y,
                scenario.density,
                rebound[worst],
                (worst % 5) / 4.0,
                (worst / 5) / 4.0,
                detour,
                peak_speed,
                peak_kick,
                angle_rebound,
                late_angle,
                early_angle,
                early_chord_error,
                finished * DT);
            // The old affine contraction feeds its own position corrections into
            // inertial velocity: seed 5120 UV(.25,.25) retreats 2.3 -> 10.8px
            // at .719 -> .743, accumulating 39px of outward travel despite a
            // subpixel final pose.
            // Permit a small contact/mesh adjustment, not repeated visible springs.
            CHECK(rebound[worst] <= std::max(1.0, pixels * 0.005));
            CHECK(detour <= std::max(4.0, pixels * 0.02));
            CHECK(late_angle < 0.25);
            CHECK(
                early_angle > 0.1);  // Do not pass by replacing the folded opening with flat paper.
            CHECK(early_chord_error > pixels * 0.01);  // Rigid rotation/translation is not a fold.
            CHECK(penultimate <= 0.5);
            CHECK(finished >= ticks && finished <= ticks + 1);
            identity(surface);
        }
    }

    // Curvature, unlike the angle to the screen, cannot be supplied by translating
    // or tilting an already-flat sheet. Area weighting makes the reference normal
    // independent of how many adaptive triangles cover each patch.
    double material_normal_spread(const PaperSurface &surface, double *area_rms = nullptr) {
        Vec3 mean{};
        const auto normal = [&](const PaperTriangle &face) {
            const auto &a = surface.points[face.vertex[0]].position;
            return cross(subtract(surface.points[face.vertex[1]].position, a),
                subtract(surface.points[face.vertex[2]].position, a));
        };
        for (const auto &face : surface.faces) {
            const auto n = normal(face);
            mean.x += n.x;
            mean.y += n.y;
            mean.z += n.z;
        }
        const double mean_length = distance(mean, {});
        double spread = 0, weighted_squared = 0, total_area = 0;
        for (const auto &face : surface.faces) {
            const auto n = normal(face);
            const double area = distance(n, {});
            const double denominator = area * mean_length;
            CHECK(denominator > 1e-15);
            const double angle =
                std::acos(std::clamp(dot(n, mean) / std::max(denominator, 1e-15), -1.0, 1.0));
            spread = std::max(spread, angle);
            weighted_squared += area * angle * angle;
            total_area += area;
        }
        if (area_rms) *area_rms = std::sqrt(weighted_squared / std::max(total_area, 1e-15));
        return spread;
    }

    void test_late_material_fold_progression() {
        PaperParams params;
        params.duration = 3.0;
        PaperCrumple model;
        model.begin(Transition::Open, params, 440, {1280, 720});
        constexpr std::array<double, 4> fractions{0.7, 0.8, 0.9, 0.95};
        std::array<double, 4> curvature{}, area_rms{}, height_pixels{};
        double previous_time = 0;
        for (unsigned i = 0; i < fractions.size(); ++i) {
            const double time = params.duration * fractions[i];
            run(model, time - previous_time);
            const auto surface = frame(model);
            curvature[i] = material_normal_spread(surface, &area_rms[i]);
            double minimum_z = std::numeric_limits<double>::infinity();
            double maximum_z = -minimum_z;
            for (const auto &point : surface.points) {
                minimum_z = std::min(minimum_z, point.position.z);
                maximum_z = std::max(maximum_z, point.position.z);
            }
            height_pixels[i] = (maximum_z - minimum_z) * std::sqrt(surface.size.x * surface.size.y);
            previous_time = time;
        }
        std::printf(
            "[paper] late-fold seed=440 duration=3 curvature(.7,.8,.9,.95)="
            "(%g,%g,%g,%g) area-rms=(%g,%g,%g,%g) z-range-px=(%g,%g,%g,%g)\n",
            curvature[0],
            curvature[1],
            curvature[2],
            curvature[3],
            area_rms[0],
            area_rms[1],
            area_rms[2],
            area_rms[3],
            height_pixels[0],
            height_pixels[1],
            height_pixels[2],
            height_pixels[3]);
        // A representative long opening must still unfold small material folds
        // in its final quarter. Broad bounds reject the opposite degeneracy:
        // overdamping everything flat early and merely aligning a rigid rectangle.
        CHECK(curvature[0] > 0.04);
        CHECK(curvature[1] > 0.02);
        CHECK(curvature[1] + 0.005 < curvature[0]);
        CHECK(curvature[2] < curvature[1] * 0.95);
        CHECK(curvature[3] < curvature[2] * 0.95);
        run(model, params.duration - previous_time - 1.0 / 240);
        const auto before = frame(model);
        CHECK(material_normal_spread(before) < 0.01);
        run(model, 1.0 / 240 + 1e-6);
        const auto after = frame(model);
        const double pixels = std::sqrt(after.size.x * after.size.y);
        for (unsigned i = 0; i < 25; ++i) {
            const Vec2 uv{(i % 5) / 4.0, (i / 5) / 4.0};
            CHECK(
                pixels * distance(material_sample(before, uv), material_sample(after, uv)) <= 0.5);
        }
        CHECK(!model.active());
        identity(after);
    }

    void test_opening_tool_reach_and_dwell() {
        // A fixed travel radius starved the far end of the strip; scaling the final
        // dwell only with duration left the 100 ms, zero-tension sheet still moving.
        for (bool strip : {false, true}) {
            PaperParams p;
            p.duration = strip ? 0.6 : 0.1;
            p.fold_density = 0.95;
            p.simulation_quality = PaperQuality::Low;
            p.crumple_origin = strip ? PaperOrigin::Bottom : PaperOrigin::Top;
            p.force_direction = strip ? PaperForce::Directional : PaperForce::Rotational;
            if (!strip) p.unfold_tension = 0;
            const Vec2 size = strip ? Vec2{240, 2600} : Vec2{1280, 720};
            PaperCrumple model;
            model.begin(Transition::Open, p, strip ? 82962 : 74986, size);
            constexpr double SAMPLE = 1.0 / 960;
            run(model, p.duration - SAMPLE);
            CHECK(model.active());
            for (const auto &point : frame(model).points)
                CHECK(std::sqrt(size.x * size.y) * distance(point.position, rest(point, size)) <
                      0.25);
            run(model, SAMPLE + 1e-7);
            CHECK(!model.active());
            identity(frame(model));
        }
    }

    void test_short_opening_schedule() {
        for (const double duration : {0.001, 0.010, 0.025, 0.040, 0.080}) {
            PaperParams params;
            params.duration = duration;
            params.simulation_quality = PaperQuality::Low;
            PaperCrumple model;
            model.begin(Transition::Open, params, 42, SIZE);
            CHECK(model.active());
            const auto packet = frame(model);
            valid(packet);

            run(model, duration * 0.5);
            const auto halfway = frame(model);
            valid(halfway);
            // At 1 ms the opening is shorter than one integration tick; there
            // is no intermediate simulated pose to compare.
            if (duration > 0.001) CHECK(std::abs(radius(halfway) - radius(packet)) > 1e-7);

            run(model, duration * 0.5);
            // A completed opening is the public evidence that flat_and_quiet()
            // succeeded; bound extra settling time so an inverted schedule fails.
            for (unsigned i = 0; i < 20 && model.active(); ++i) run(model, 0.01);
            std::printf("[paper] short-opening duration=%g active=%d\n", duration, model.active());
            CHECK(!model.active());
            CHECK(!model.closed());
            identity(frame(model));
        }
    }

    void test_time_validation_and_reset() {
        PaperParams params;
        params.simulation_quality = PaperQuality::Low;
        PaperCrumple a, b;
        a.begin(Transition::Close, params, 5, SIZE);
        b.begin(Transition::Close, params, 5, SIZE);
        a.advance(0.2);
        b.advance(1.0 / 30);
        CHECK(delta(frame(a), frame(b)) == 0);
        a.advance(std::numeric_limits<double>::quiet_NaN());
        CHECK(a.closed() && !a.active());
        CHECK(frame(a).opacity == 0);
        b.reset();
        const std::stop_source cancelled;
        cancelled.request_stop();
        b.begin(Transition::Open, {}, 1, SIZE, {0.5, 0.5}, cancelled.get_token());
        CHECK(!b.active() && !b.closed());
        identity(frame(b));
        params.fold_density = std::numeric_limits<double>::infinity();
        b.begin(Transition::Close, params, 5, SIZE);
        CHECK(!b.active());
        identity(frame(b));
        params = {};
        params.duration = 0;
        b.begin(Transition::Close, params, 5, SIZE);
        CHECK(!b.active());
        b.begin(Transition::Close, {}, 5, {0, 700});
        CHECK(!b.active());
    }

}  // namespace

int main() {
    test_aspect_and_endpoints();
    test_force_entropy_and_determinism();
    test_continuity_and_fractional_publication();
    test_physical_packet();
    test_duration_scaled_flat_handoff();
    test_close_compaction_and_opposing_layers();
    test_parameter_coupling();
    test_local_crease_history();
    test_time_validation_and_reset();
    test_crushed_packet_contact_corpus();
    test_directional_opening_shape_release();
    test_opening_material_trajectory();
    test_late_material_fold_progression();
    test_opening_tool_reach_and_dwell();
    test_short_opening_schedule();
    return g_failures == 0 ? 0 : 1;
}
