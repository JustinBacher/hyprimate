#include <array>
#include <cmath>
#include <limits>
#include "Check.hpp"
#include "workspace/rotation/RotationGeometry.hpp"
using hyprimate::workspace::Direction;
using namespace hyprimate::workspace::rotation;
namespace {
    bool near(double a, double b, double epsilon = 1e-4) { return std::abs(a - b) < epsilon; }
    std::array<double, 2> apply(const std::array<float, 9> &m, double x, double y) {
        return {m[0] * x + m[3] * y + m[6], m[1] * x + m[4] * y + m[7]};
    }
    void run() {
        constexpr int width = 800, height = 600;
        const double step = 2 * std::atan(0.25);
        for (int i = 0; i <= 100; ++i) {
            double t = i / 100.0;
            auto f = make_frame(width, height, t, Direction::Forward);
            auto b = make_frame(width, height, t, Direction::Backward);
            CHECK(near(f.source_angle, -b.source_angle));
            CHECK(near(f.destination_angle, -b.destination_angle));
            CHECK(near(f.source_angle, -step * t));
            CHECK(near(f.destination_angle, step * (1 - t)));
            for (const auto &m : {f.source, f.destination, b.source, b.destination}) {
                auto a = apply(m, 100, 100), c = apply(m, 400, 500);
                CHECK(near(std::hypot(a[0] - c[0], a[1] - c[1]), 500, 0.001));
                auto center = apply(m, width / 2.0, height + 2.0 * width);
                CHECK(near(center[0], width / 2.0, 0.001));
                CHECK(near(center[1], height + 2.0 * width, 0.001));
            }
        }
        for (const auto extent : {std::array{1920, 1080}, std::array{1080, 1920},
                 std::array{3440, 1440}}) {
            const double radius = 2.0 * extent[0], cx = extent[0] / 2.0, cy = extent[1] + radius;
            for (int i = 0; i <= 20; ++i) {
                const auto frame = make_frame(extent[0], extent[1], i / 20.0, Direction::Forward);
                for (const auto &pair : {std::pair{frame.source, frame.source_angle},
                         std::pair{frame.destination, frame.destination_angle}}) {
                    const auto &m = pair.first;
                    const double angle = pair.second;
                    const double x = cx + radius * std::sin(angle);
                    const double y = cy - radius * std::cos(angle);
                    const auto midpoint = apply(m, x, y);
                    CHECK(near(midpoint[0], cx, 0.002));
                    CHECK(near(midpoint[1], extent[1], 0.002));
                    // The tangent maps to the canonical horizontal bottom edge, with unit scale.
                    const auto tangent = apply(m, x + std::cos(angle), y + std::sin(angle));
                    CHECK(near(tangent[0] - midpoint[0], 1.0));
                    CHECK(near(tangent[1] - midpoint[1], 0.0));
                    CHECK(near(std::hypot(x - cx, y - cy), radius));
                }
            }
        }
        auto first = make_frame(width, height, 0, Direction::Forward);
        auto last = make_frame(width, height, 1, Direction::Forward);
        auto a = apply(first.source, 123, 456), b = apply(last.destination, 123, 456);
        CHECK(near(a[0], 123) && near(a[1], 456));
        CHECK(near(b[0], 123) && near(b[1], 456));
        CHECK(make_frame(width, height, -1, Direction::Forward).source_angle == 0);
        CHECK(make_frame(width, height, std::numeric_limits<double>::quiet_NaN(), Direction::Forward).source_angle == 0);
    }
}
int main() { run(); return g_failures == 0 ? 0 : 1; }
