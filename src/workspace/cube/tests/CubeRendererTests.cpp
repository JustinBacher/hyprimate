#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "Check.hpp"
#include "GlHarness.hpp"
#include "production_gl.hpp"
#include "workspace/WorkspaceTransition.hpp"

namespace {
    using namespace hyprimate::workspace;
    using cube::Material;
    using cube::Settings;
    using cube::Variant;
    using cube_offscreen::Frame;
    using cube_offscreen::Pixel;
    using Image = std::vector<Pixel>;

    constexpr std::array VARIANTS{Variant::Solid, Variant::InsideOut};
    constexpr std::array DIRECTIONS{Direction::Forward, Direction::Backward};
    constexpr std::array<GLfloat, 4> CANARY{1.F, 0.F, 1.F, 1.F};
    constexpr Pixel CANARY_PIXEL{255, 0, 255, 255};

    struct Size {
        int width;
        int height;

        std::size_t area() const { return static_cast<std::size_t>(width) * height; }
        std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * width + x; }
    };

    Image desktop(Size size, bool second) {
        Image pixels(size.area());
        for (int y = 0; y < size.height; ++y) {
            for (int x = 0; x < size.width; ++x) {
                // Native workspace captures use row zero as the top of the desktop.
                const double u = (x + .5) / size.width, v = (y + .5) / size.height;
                const auto across = static_cast<std::uint8_t>(30 + 115 * u);
                const auto down = static_cast<std::uint8_t>(25 + 105 * v);
                // Visible orientation witness: a left stem, long upper arm, shorter middle arm.
                const bool letter = u >= .36 && v >= .24 && v < .75 &&
                    (u < .42 || (u < .65 && v < .31) || (u < .55 && v >= .43 && v < .50));
                pixels[size.index(x, y)] = letter ? Pixel{24, 239, 24, 255}
                    : second                     ? Pixel{down, across, 224, 255}
                                                 : Pixel{224, across, down, 255};
            }
        }
        return pixels;
    }

    class Fixture {
      public:
        explicit Fixture(Size size)
            : size(size), frame(size.width, size.height), captures{desktop(size, false), desktop(size, true)},
              actual(size.area()) {
            if (!frame.valid()) return;
            frame.upload(false, captures[0]);
            frame.upload(true, captures[1]);
        }

        bool render(Material &material, double progress, Direction direction, const Settings &settings) {
            frame.clear(CANARY);
            const bool drawn = frame.draw(material, progress, direction, settings);
            frame.read(actual);
            const bool no_gl_error = glGetError() == GL_NO_ERROR;
            CHECK(no_gl_error);
            CHECK(drawn);
            return drawn && no_gl_error;
        }

        Pixel pixel(double x, double y) const {
            return actual[size.index(static_cast<int>(x * size.width), static_cast<int>(y * size.height))];
        }

        Size size;
        Frame frame;
        std::array<Image, 2> captures;
        Image actual;
    };

    void check_exact_endpoint(const Image &actual, const Image &expected) {
        const auto first = std::mismatch(actual.begin(), actual.end(), expected.begin());
        if (first.first != actual.end()) {
            std::fprintf(stderr, "first endpoint mismatch at pixel %td: %u/%u/%u/%u vs %u/%u/%u/%u\n",
                first.first - actual.begin(), unsigned((*first.first)[0]), unsigned((*first.first)[1]),
                unsigned((*first.first)[2]), unsigned((*first.first)[3]), unsigned((*first.second)[0]),
                unsigned((*first.second)[1]), unsigned((*first.second)[2]), unsigned((*first.second)[3]));
        }
        CHECK(first.first == actual.end());
    }

    void check_near_endpoint(const Image &actual, const Image &expected) {
        std::uint64_t total_error = 0;
        std::size_t outliers = 0;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            int worst = 0;
            for (int channel = 0; channel < 3; ++channel) {
                const int delta = std::abs(actual[i][channel] - expected[i][channel]);
                total_error += delta;
                worst = std::max(worst, delta);
            }
            if (worst > 24 || actual[i][3] != 255) ++outliers;
        }
        const auto mean = static_cast<double>(total_error) / (3 * actual.size());
        // Allow subpixel resampling; reject even a localized, high-contrast lining flash.
        if (mean > 1.25 || outliers * 100 > actual.size()) {
            std::fprintf(stderr, "near endpoint: mean RGB error %.3f, %zu/%zu visibly different pixels\n",
                mean, outliers, actual.size());
        }
        CHECK(mean <= 1.25);
        CHECK(outliers * 100 <= actual.size());
    }

    void endpoints_are_full_workspace_at_extrema(Material &material) {
        for (const auto size : {Size{320, 180}, Size{297, 127}, Size{131, 209}}) {
            Fixture fixture(size);
            CHECK(fixture.frame.valid());
            if (!fixture.frame.valid()) continue;
            for (const auto variant : VARIANTS) {
                for (const auto zoom : {0.F, 1.F}) {
                    for (const auto tilt : {-20.F, 20.F}) {
                        for (const auto direction : DIRECTIONS) {
                            const Settings settings{variant, tilt, zoom};
                            for (int endpoint = 0; endpoint <= 1; ++endpoint) {
                                const int failures = g_failures;
                                const auto &expected = fixture.captures[endpoint ^ (direction == Direction::Backward)];
                                if (fixture.render(material, endpoint, direction, settings)) {
                                    check_exact_endpoint(fixture.actual, expected);
                                }
                                if (fixture.render(material, endpoint ? .9999 : .0001, direction, settings)) {
                                    check_near_endpoint(fixture.actual, expected);
                                }
                                if (g_failures != failures) {
                                    std::fprintf(stderr, "%dx%d variant=%d zoom=%.1f tilt=%.1f dir=%d endpoint=%d\n",
                                        size.width, size.height, static_cast<int>(variant), zoom, tilt,
                                        static_cast<int>(direction), endpoint);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    bool belongs_to(Pixel pixel, bool second) {
        const auto owner = pixel[second ? 2 : 0];
        return owner > std::max(pixel[1], pixel[second ? 0 : 2]) + 50;
    }

    struct Coverage {
        std::size_t count = 0;
        double x = 0, y = 0;
    };

    Coverage coverage(const Fixture &fixture, bool second) {
        Coverage result;
        for (int y = 0; y < fixture.size.height; ++y) {
            for (int x = 0; x < fixture.size.width; ++x) {
                if (!belongs_to(fixture.actual[fixture.size.index(x, y)], second)) continue;
                ++result.count;
                result.x += x;
                result.y += y;
            }
        }
        return result;
    }

    void check_owner_order(const Fixture &fixture) {
        // Backward travel both swaps source/destination and reflects the geometry.
        // The original left and right desktops therefore stay on their own sides.
        const auto left = coverage(fixture, false), right = coverage(fixture, true);
        CHECK(left.count > fixture.size.area() / 20);
        CHECK(right.count > fixture.size.area() / 20);
        if (!left.count || !right.count) return;
        CHECK(left.x / left.count + fixture.size.width / 10. < right.x / right.count);
    }

    bool is_letter(Pixel pixel) { return pixel[1] > std::max(pixel[0], pixel[2]) + 65; }

    void check_upright_letter(const Fixture &fixture, bool right_half) {
        // Both visible faces carry the same F. Its long vertical stem must be on
        // the left of its raised arm, even when the direction mirrors the turn.
        const int begin = right_half ? fixture.size.width / 2 : 0;
        const int end = begin + fixture.size.width / 2;
        int left = end, right = begin;
        for (int y = 0; y < fixture.size.height; ++y) {
            for (int x = begin; x < end; ++x) {
                if (!is_letter(fixture.actual[fixture.size.index(x, y)])) continue;
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        CHECK(right - left >= 12);
        if (right - left < 12) return;
        struct Slice {
            int top;
            int bottom = 0;
            int count = 0;
            int sum_y = 0;
        } stem{fixture.size.height}, arm{fixture.size.height};
        const int edge = (right - left) / 6;
        for (int y = 0; y < fixture.size.height; ++y) {
            for (int x = left; x <= right; ++x) {
                if (x > left + edge && x < right - edge) continue;
                if (!is_letter(fixture.actual[fixture.size.index(x, y)])) continue;
                auto &slice = x <= left + edge ? stem : arm;
                slice.top = std::min(slice.top, y);
                slice.bottom = std::max(slice.bottom, y);
                ++slice.count;
                slice.sum_y += y;
            }
        }
        CHECK(stem.count > 0 && arm.count > 0);
        if (!stem.count || !arm.count) return;
        CHECK(stem.bottom - stem.top > 1.4 * (arm.bottom - arm.top));
        CHECK(static_cast<double>(arm.sum_y) / arm.count + 3 < static_cast<double>(stem.sum_y) / stem.count);
    }

    void midpoint_distinguishes_corner_and_room(Material &material, Fixture &fixture) {
        for (const auto direction : DIRECTIONS) {
            if (!fixture.render(material, .5, direction, {Variant::Solid, 0.F, .25F})) continue;
            check_owner_order(fixture);
            check_upright_letter(fixture, false);
            check_upright_letter(fixture, true);
            const auto solid_above = fixture.pixel(.5, .08), solid_below = fixture.pixel(.5, .92);
            const auto bevel = fixture.pixel(.5, .5);
            // The outside corner floats against sky, with a lit metal seam rather than a gap.
            CHECK(solid_above[0] < 36 && solid_below[0] < 36);
            CHECK(!belongs_to(bevel, false) && !belongs_to(bevel, true));
            CHECK(bevel[0] > solid_below[0] + 10);
            if (!fixture.render(material, .5, direction, {Variant::InsideOut, 0.F, .45F})) continue;
            check_owner_order(fixture);
            check_upright_letter(fixture, false);
            check_upright_letter(fixture, true);
            const auto ceiling = fixture.pixel(.5, .08), floor = fixture.pixel(.5, .92);
            // The room's walls meet without an exterior bevel or a sky-colored gap.
            CHECK(belongs_to(fixture.pixel(.5 - 1. / fixture.size.width, .5), false));
            CHECK(belongs_to(fixture.pixel(.5, .5), true));
            CHECK(!belongs_to(ceiling, false) && !belongs_to(ceiling, true));
            CHECK(!belongs_to(floor, false) && !belongs_to(floor, true));
            CHECK(floor[0] > solid_below[0] + 20);
            CHECK(floor[0] > ceiling[0] + 15);
        }
    }

    void direction_controls_which_workspace_crosses_the_center(Material &material, Fixture &fixture) {
        for (const auto variant : VARIANTS) {
            const Settings settings{variant, 0.F, variant == Variant::Solid ? .25F : .45F};
            for (const auto direction : DIRECTIONS) {
                const bool source_is_blue = direction == Direction::Backward;
                if (fixture.render(material, .35, direction, settings)) {
                    CHECK(belongs_to(fixture.pixel(.5, .5), source_is_blue));
                }
                if (fixture.render(material, .65, direction, settings)) {
                    CHECK(belongs_to(fixture.pixel(.5, .5), !source_is_blue));
                }
            }
        }
    }

    void zoom_retreats_and_tilt_is_signed(Material &material, Fixture &fixture) {
        for (const auto variant : VARIANTS) {
            if (!fixture.render(material, .5, Direction::Forward, {variant, 0.F, 0.F})) continue;
            const auto large = coverage(fixture, false).count + coverage(fixture, true).count;
            if (!fixture.render(material, .5, Direction::Forward, {variant, 0.F, 1.F})) continue;
            const auto small = coverage(fixture, false).count + coverage(fixture, true).count;
            CHECK(small > fixture.size.area() / 8);
            CHECK(large > small + fixture.size.area() / 30);

            std::array<double, 3> center_y{};
            const float zoom = variant == Variant::Solid ? .25F : .45F;
            for (std::size_t i = 0; i < center_y.size(); ++i) {
                const float tilt = (static_cast<float>(i) - 1.F) * 20.F;
                if (!fixture.render(material, .5, Direction::Forward, {variant, tilt, zoom})) break;
                const auto left = coverage(fixture, false), right = coverage(fixture, true);
                if (left.count + right.count) center_y[i] = (left.y + right.y) / (left.count + right.count);
                CHECK(center_y[i] > 0.);
            }
            // Nose-up lifts the solid corner; looking up inside the room reveals more
            // ceiling and moves the desktop walls down in the image.
            const double step = fixture.size.height * .02 * (variant == Variant::Solid ? -1. : 1.);
            CHECK((center_y[1] - center_y[0]) / step > 1.);
            CHECK((center_y[2] - center_y[1]) / step > 1.);
        }
    }

    void reversal_keeps_the_presented_pose_and_capture_owners(Material &material, Fixture &fixture) {
        for (const auto variant : VARIANTS) {
            const Settings settings{variant, 0.F, variant == Variant::Solid ? .25F : .45F};
            for (const auto direction : DIRECTIONS) {
                constexpr double DURATION = .6;
                Transition path;
                CHECK(path.begin(direction, DURATION));
                path.advance(DURATION / 2);
                const WorkspaceMotion presented = path.motion();
                if (!fixture.render(material, presented.position, path.direction(), settings)) continue;
                const Image before_return = fixture.actual;
                path.retarget(false, DURATION, presented);
                CHECK(path.direction() == direction);
                CHECK(path.progress() == presented.position);
                if (fixture.render(material, path.progress(), path.direction(), settings)) {
                    CHECK(fixture.actual == before_return);
                }
                bool checked_returning_owners = false;
                for (int i = 0; i < 120 && path.active(); ++i) {
                    path.advance(1. / 120.);
                    if (!checked_returning_owners && path.progress() < .45 && path.progress() > .35 &&
                        path.velocity() < 0.) {
                        if (fixture.render(material, path.progress(), path.direction(), settings)) {
                            check_owner_order(fixture);
                            CHECK(belongs_to(fixture.pixel(.5, .5), direction == Direction::Backward));
                        }
                        checked_returning_owners = true;
                    }
                }
                CHECK(checked_returning_owners);
                CHECK(!path.active());
                CHECK(path.progress() == 0.);
                if (fixture.render(material, path.progress(), path.direction(), settings)) {
                    check_exact_endpoint(fixture.actual, fixture.captures[direction == Direction::Backward]);
                }
            }
        }
    }

    void invalid_draws_do_not_touch_the_framebuffer(Material &material, Fixture &fixture) {
        const Settings settings{Variant::Solid, 0.F, .25F};
        const auto a = fixture.frame.capture(false), b = fixture.frame.capture(true), out = fixture.frame.output();
        const auto size = fixture.size;
        const auto &frame = fixture.frame;
        const auto reject = [&](GLuint source, GLuint destination, GLuint output, int width, int height, double p) {
            frame.clear(CANARY);
            CHECK(!frame.draw(material, source, destination, output, width, height, p, Direction::Forward, settings));
            frame.read(fixture.actual);
            CHECK(std::ranges::all_of(fixture.actual, [](Pixel pixel) { return pixel == CANARY_PIXEL; }));
            CHECK(glGetError() == GL_NO_ERROR);
        };
        reject(0, b, out, size.width, size.height, .5);
        reject(a, 0, out, size.width, size.height, .5);
        reject(a, b, 0, size.width, size.height, .5);
        reject(a, b, out, 0, size.height, .5);
        reject(a, b, out, size.width, -1, .5);
        reject(a, b, out, size.width, size.height, std::numeric_limits<double>::quiet_NaN());
        reject(a, b, out, size.width, size.height, std::numeric_limits<double>::infinity());
        reject(a, b, out, size.width, size.height, -.01);
        reject(a, b, out, size.width, size.height, 1.01);
        reject(out, b, out, size.width, size.height, .5);
        reject(a, out, out, size.width, size.height, .5);
        reject(out, b, out, size.width, size.height, 0.);
        reject(a, out, out, size.width, size.height, 1.);
        material.release();
        reject(a, b, out, size.width, size.height, .5);
        reject(a, b, out, size.width, size.height, 0.);
        const bool reinitialized = material.initialize();
        CHECK(reinitialized);
        if (reinitialized && fixture.render(material, 1., Direction::Forward, settings)) {
            check_exact_endpoint(fixture.actual, fixture.captures[1]);
        }
    }
}  // namespace

int main() {
    const gl_harness::CContext context(1, 1);
    if (!context.valid()) return 77;
    Material material;
    if (!material.initialize()) {
        std::fprintf(stderr, "cube material initialization failed\n");
        return 1;
    }
    endpoints_are_full_workspace_at_extrema(material);
    Fixture fixture({320, 180});
    CHECK(fixture.frame.valid());
    if (fixture.frame.valid()) {
        midpoint_distinguishes_corner_and_room(material, fixture);
        direction_controls_which_workspace_crosses_the_center(material, fixture);
        zoom_retreats_and_tilt_is_signed(material, fixture);
        reversal_keeps_the_presented_pose_and_capture_owners(material, fixture);
        invalid_draws_do_not_touch_the_framebuffer(material, fixture);
    }
    material.release();
    return g_failures ? 1 : 0;
}
