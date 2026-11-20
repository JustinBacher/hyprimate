#include <algorithm>
#include <cmath>
#include <limits>

#include "Check.hpp"
#include "animations/paper/PaperInteraction.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;

namespace {
    double distance(Vec3 first, Vec3 second) {
        return std::sqrt((first.x - second.x) * (first.x - second.x) +
                         (first.y - second.y) * (first.y - second.y) +
                         (first.z - second.z) * (first.z - second.z));
    }
    Vec3 native(const PaperSurface &surface, Vec2 uv) {
        const double scale = std::sqrt(surface.size.x * surface.size.y);
        return {(uv.x - 0.5) * surface.size.x / scale, (uv.y - 0.5) * surface.size.y / scale, 0};
    }
    double height(const PaperSurface &surface) {
        double result = 0;
        for (const auto &point : surface.points) result = std::max(result, point.position.z);
        return result;
    }
    double difference(const PaperSurface &first, const PaperSurface &second) {
        double result = 0;
        CHECK(first.points.size() == second.points.size());
        for (std::size_t index = 0; index < first.points.size(); ++index)
            result = std::max(
                result, distance(first.points[index].position, second.points[index].position));
        return result;
    }
    void flat(const PaperSurface &surface) {
        for (const auto &point : surface.points)
            CHECK(distance(point.position, native(surface, point.uv)) < 1e-10);
        CHECK(surface.shadow_strength == 0);
    }
    void valid(const PaperSurface &surface) {
        CHECK(surface.interaction_mesh);
        CHECK(!surface.points.empty() && !surface.faces.empty());
        CHECK(surface.points.size() <= 4096);
        CHECK(surface.shadow_strength >= 0 && surface.shadow_strength <= 1);
        CHECK(height(surface) < 1);
        const double scale = std::sqrt(surface.size.x * surface.size.y);
        for (const auto &point : surface.points) {
            CHECK(std::isfinite(point.position.x) && std::isfinite(point.position.y));
            CHECK(std::isfinite(point.position.z) && point.position.z >= 0);
            CHECK(point.uv.x >= 0 && point.uv.x <= 1);
            CHECK(point.uv.y >= 0 && point.uv.y <= 1);
        }
        for (const auto &face : surface.faces)
            for (unsigned edge = 0; edge < 3; ++edge) {
                CHECK(face.vertex[edge] < surface.points.size());
                const auto &first = surface.points[face.vertex[edge]];
                const auto &second = surface.points[face.vertex[(edge + 1) % 3]];
                const double rest = std::hypot((first.uv.x - second.uv.x) * surface.size.x,
                                        (first.uv.y - second.uv.y) * surface.size.y) /
                                    scale;
                CHECK(std::abs(distance(first.position, second.position) / rest - 1) < 0.015);
            }
    }
    struct Harness {
        PaperInteraction interaction;
        PaperSurface surface;
        PaperInteractionParams params;
        Step step{};

        explicit Harness(Box body = {100, 100, 800, 800}) {
            step.body = step.capture = body;
            step.elapsed = 1.0 / 60;
            step.movement_enabled = true;
            CHECK(!interaction.advance(surface, params, step));
        }
        bool move(Box body, double seconds = 1.0 / 60, double elapsed = -1) {
            Motion motion{};
            motion.from_body = motion.from_capture = step.body;
            motion.body = motion.capture = body;
            motion.translated = body.x != step.body.x || body.y != step.body.y;
            motion.resized = body.w != step.body.w || body.h != step.body.h;
            motion.sample_seconds = seconds;
            step.body = step.capture = body;
            step.elapsed = elapsed < 0 ? seconds : elapsed;
            step.stalled = false;
            step.motion = motion;
            return interaction.advance(surface, params, step);
        }
        bool idle(double seconds = 1.0 / 60, bool stalled = false) {
            step.motion.reset();
            step.elapsed = seconds;
            step.stalled = stalled;
            return interaction.advance(surface, params, step);
        }
        unsigned settle() {
            bool active = true;
            unsigned frames = 0;
            while (active && frames < 600) {
                active = idle();
                ++frames;
            }
            CHECK(!active);
            flat(surface);
            return frames;
        }
    };
    void speeds_boundaries_aspects() {
        for (const auto size : {Vec2{800, 800}, Vec2{1600, 200}, Vec2{200, 1600}})
            for (const double speed : {30.0, 400.0, 2400.0})
                for (unsigned boundary = 0; boundary < 8; ++boundary) {
                    Harness test({100, 100, size.x, size.y});
                    test.params.stiffness = 0;
                    test.params.resize.fold_intensity = test.params.resize.curl_intensity = 1;
                    test.params.resize.crease_density = 1;
                    const auto points = test.surface.points.capacity();
                    const auto faces = test.surface.faces.capacity();
                    for (unsigned sample = 0; sample < 8; ++sample) {
                        auto body = test.step.body;
                        const double delta = speed / 60 * (sample < 4 ? 1 : -1);
                        if (boundary == 0 || boundary == 4 || boundary == 6) {
                            body.x -= delta;
                            body.w += delta;
                        }
                        if (boundary == 1 || boundary == 5 || boundary == 7) body.w += delta;
                        if (boundary == 2 || boundary == 4 || boundary == 5) {
                            body.y -= delta;
                            body.h += delta;
                        }
                        if (boundary == 3 || boundary == 6 || boundary == 7) body.h += delta;
                        CHECK(test.move(body));
                        valid(test.surface);
                        CHECK(test.surface.points.capacity() == points);
                        CHECK(test.surface.faces.capacity() == faces);
                    }
                }
    }
    void resize_locality_overlap_and_styles() {
        Harness grow, shrink, opposing, flutter_style, glide_style;
        glide_style.params.style = PaperMoveStyle::Glide;
        glide_style.params.glide.shadow_separation = 0;
        for (unsigned sample = 0; sample < 20; ++sample) {
            auto body = grow.step.body;
            body.w += 10;
            CHECK(grow.move(body));
            body = shrink.step.body;
            body.w -= 24;
            CHECK(shrink.move(body));
            body = opposing.step.body;
            body.x -= 5;
            body.w += 10;
            CHECK(opposing.move(body));
            body = flutter_style.step.body;
            body.w += 8;
            body.h -= 5;
            CHECK(flutter_style.move(body));
            CHECK(glide_style.move(body));
            CHECK(difference(flutter_style.surface, glide_style.surface) == 0);
            CHECK(flutter_style.surface.shadow_strength == glide_style.surface.shadow_strength);
        }
        valid(grow.surface);
        valid(shrink.surface);
        valid(opposing.surface);
        // A revealed right edge rises; stationary/central material stays native.
        CHECK(PaperInteraction::sample(grow.surface, {1, 0.5}).z > 0);
        for (const Vec2 uv : {Vec2{0, 0}, Vec2{0, 0.5}, Vec2{0.4, 0.5}, Vec2{0.5, 1}})
            CHECK(distance(PaperInteraction::sample(grow.surface, uv), native(grow.surface, uv)) <
                  1e-10);
        for (const double coordinate : {0.0, 0.1, 0.25, 0.4}) {
            const auto left = PaperInteraction::sample(opposing.surface, {coordinate, 0.5});
            const auto right = PaperInteraction::sample(opposing.surface, {1 - coordinate, 0.5});
            CHECK(std::abs(left.x + right.x) < 1e-10);
            CHECK(std::abs(left.z - right.z) < 1e-10);
        }
        bool overlaps = false;
        for (const auto &face : shrink.surface.faces)
            for (unsigned edge = 0; edge < 3; ++edge) {
                const auto &first = shrink.surface.points[face.vertex[edge]];
                const auto &second = shrink.surface.points[face.vertex[(edge + 1) % 3]];
                if (first.uv.y == second.uv.y && first.uv.x < second.uv.x &&
                    second.position.x < first.position.x && first.position.z > 0)
                    overlaps = true;
            }
        CHECK(overlaps);  // > pi/2 physical tangents, not just surface shading.
        CHECK(shrink.settle() > 1);
        CHECK(grow.settle() > 1);
    }
    void movement_styles_and_residuals() {
        for (const auto style : {PaperMoveStyle::Flutter, PaperMoveStyle::Glide}) {
            Harness test, reverse;
            test.params.style = reverse.params.style = style;
            test.surface.shading = 0.37F;
            for (unsigned sample = 0; sample < 24; ++sample) {
                auto body = test.step.body;
                body.x += 12;
                body.y += 4;
                const auto previous = test.surface;
                CHECK(test.move(body));
                body = reverse.step.body;
                body.x -= 12;
                body.y -= 4;
                CHECK(reverse.move(body));
                CHECK(difference(previous, test.surface) < 0.08);
                valid(test.surface);
            }
            CHECK(height(test.surface) > 1e-5);
            CHECK(test.surface.shadow_strength > 0);
            CHECK(test.surface.shading == 0.37F);
            if (style == PaperMoveStyle::Glide) {
                CHECK(PaperInteraction::sample(test.surface, {0.5, 0.5}).z < 1e-6);
                CHECK(PaperInteraction::sample(test.surface, {0.95, 0.5}).z >
                      PaperInteraction::sample(test.surface, {0.05, 0.5}).z);
                CHECK(PaperInteraction::sample(reverse.surface, {0.05, 0.5}).z >
                      PaperInteraction::sample(reverse.surface, {0.95, 0.5}).z);
                test.params.glide.shadow_separation = 0;
                CHECK(test.idle());
                CHECK(test.surface.shadow_strength == 0);
            }
            CHECK(test.settle() > 1);
            CHECK(test.surface.shading == 0.37F);
        }
        Harness low_friction, high_friction;
        low_friction.params.style = high_friction.params.style = PaperMoveStyle::Glide;
        low_friction.params.glide.friction = 0;
        high_friction.params.glide.friction = 1;
        for (unsigned sample = 0; sample < 24; ++sample) {
            auto body = low_friction.step.body;
            body.x += 12;
            low_friction.move(body);
            high_friction.move(body);
        }
        CHECK(high_friction.settle() < low_friction.settle());
        Harness residual;
        residual.params.style = PaperMoveStyle::Glide;
        residual.params.glide.corner_lift = 0;
        for (unsigned sample = 0; sample < 24; ++sample) {
            auto body = residual.step.body;
            body.x += 12;
            residual.move(body);
        }
        for (unsigned frame = 0; frame < 20; ++frame) residual.idle();
        double previous_height = height(residual.surface);
        for (unsigned frame = 0; frame < 80; ++frame) {
            residual.idle();
            CHECK(height(residual.surface) <= previous_height + 1e-9);
            previous_height = height(residual.surface);
        }
    }
    void flutter_frequency_acceleration_and_isolation() {
        Harness slow, fast, no_air, glide, glide_other;
        slow.params.flutter.frequency = 0.1;
        fast.params.flutter.frequency = 4;
        no_air.params.flutter.air_resistance = 0;
        glide.params.style = glide_other.params.style = PaperMoveStyle::Glide;
        glide_other.params.flutter = {0, 0, 4, 0, 0};
        for (unsigned sample = 0; sample < 24; ++sample) {
            auto body = slow.step.body;
            body.x += 10;
            slow.move(body);
            fast.move(body);
            no_air.move(body);
            glide.move(body);
            glide_other.move(body);
        }
        CHECK(difference(slow.surface, fast.surface) > 1e-5);
        CHECK(difference(glide.surface, glide_other.surface) == 0);
        flat(no_air.surface);
        Harness uniform, accelerated, untwisted;
        untwisted.params.flutter.torsion = 0;
        for (unsigned sample = 0; sample < 12; ++sample) {
            auto body = uniform.step.body;
            body.x += 10;
            body.y += 3;
            uniform.move(body);
            auto faster = accelerated.step.body;
            faster.x += sample < 6 ? 5 : 15;
            faster.y += 3;
            accelerated.move(faster);
            untwisted.move(body);
        }
        CHECK(difference(uniform.surface, accelerated.surface) > 1e-5);
        CHECK(difference(uniform.surface, untwisted.surface) > 1e-7);
    }
    void timing_rebasing_deadband_and_sampling() {
        for (const double seconds : {1.0 / 30, 1.0 / 60, 1.0 / 144, 1.0 / 240, 0.75}) {
            Harness first, second;
            auto body = first.step.body;
            body.x += 100;
            CHECK(first.move(body, seconds, 0));
            CHECK(second.move(body, seconds, 0));
            CHECK(height(first.surface) > 0);
            CHECK(difference(first.surface, second.surface) == 0);
            const auto before = first.surface;
            CHECK(first.idle(0));
            CHECK(difference(before, first.surface) == 0);
            CHECK(first.idle(1, true));
            CHECK(height(first.surface) > 0);  // decay, not a hard-reset flat pose.
            first.settle();
        }
        Harness rebase;
        rebase.step.body.x += 2000;
        rebase.step.capture = rebase.step.body;
        CHECK(!rebase.idle());
        flat(rebase.surface);
        rebase.step.capture = {2090, 90, 820, 820};
        CHECK(!rebase.idle());
        flat(rebase.surface);
        Harness rounding;
        auto body = rounding.step.body;
        body.x += 0.01;
        CHECK(!rounding.move(body));
        flat(rounding.surface);
        rebase.step.body.w = std::numeric_limits<double>::quiet_NaN();
        rebase.step.capture.w = -1;
        CHECK(!rebase.idle());
        Harness sample;
        body = sample.step.body;
        body.w -= 80;
        sample.move(body);
        for (const auto &point : sample.surface.points)
            CHECK(distance(PaperInteraction::sample(sample.surface, point.uv), point.position) <
                  1e-12);
        CHECK(distance(PaperInteraction::sample(sample.surface, {-1, -1}),
                  PaperInteraction::sample(sample.surface, {0, 0})) == 0);
        CHECK(distance(PaperInteraction::sample(sample.surface, {2, 2}),
                  PaperInteraction::sample(sample.surface, {1, 1})) == 0);
        CHECK(distance(PaperInteraction::sample(sample.surface, {0.52 - 1e-8, 0.52}),
                  PaperInteraction::sample(sample.surface, {0.52 + 1e-8, 0.52})) < 1e-6);
    }
    void parameter_boundaries() {
        for (const double value : {0.0, 1.0}) {
            Harness test;
            test.params.stiffness = value;
            test.params.inertia = value;
            test.params.settle_damping = value;
            test.params.resize.fold_intensity = test.params.resize.curl_intensity = value;
            test.params.resize.crease_density = value;
            for (unsigned sample = 0; sample < 12; ++sample) {
                auto body = test.step.body;
                body.w -= 8;
                body.h += 3;
                CHECK(test.move(body));
            }
            valid(test.surface);
            CHECK(test.settle() > 1);
        }
    }
}  // namespace

int main() {
    speeds_boundaries_aspects();
    resize_locality_overlap_and_styles();
    movement_styles_and_residuals();
    flutter_frequency_acceleration_and_isolation();
    timing_rebasing_deadband_and_sampling();
    parameter_boundaries();
    return g_failures != 0;
}
