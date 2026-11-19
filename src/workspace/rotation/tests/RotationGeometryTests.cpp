#include <array>
#include <cmath>
#include <limits>
#include "Check.hpp"
#include "workspace/rotation/RotationGeometry.hpp"
using hyprimate::workspace::Direction;
using namespace hyprimate::workspace::rotation;
namespace {
    bool near(double first, double second, double epsilon = 1e-4) { return std::abs(first - second) < epsilon; }
    std::array<double, 2> apply(const std::array<float, 9> &matrix, double pos_x, double pos_y) {
        return {(matrix[0] * pos_x) + (matrix[3] * pos_y) + matrix[6],
            (matrix[1] * pos_x) + (matrix[4] * pos_y) + matrix[7]};
    }
    void run() {
        constexpr int WIDTH = 800;
        constexpr int HEIGHT = 600;
        const double STEP = 2 * std::atan(0.25);
        for (int i = 0; i <= 100; ++i) {
            const double PROGRESS = i / 100.0;
            const auto FORWARD = make_frame({.width = WIDTH, .height = HEIGHT}, PROGRESS, Direction::Forward);
            const auto BACKWARD = make_frame({.width = WIDTH, .height = HEIGHT}, PROGRESS, Direction::Backward);
            CHECK(near(FORWARD.source_angle, -BACKWARD.source_angle));
            CHECK(near(FORWARD.destination_angle, -BACKWARD.destination_angle));
            CHECK(near(FORWARD.source_angle, -STEP * PROGRESS));
            CHECK(near(FORWARD.destination_angle, STEP * (1 - PROGRESS)));
            for (const auto &matrix : {FORWARD.source, FORWARD.destination, BACKWARD.source, BACKWARD.destination}) {
                const auto FIRST = apply(matrix, 100, 100);
                const auto SECOND = apply(matrix, 400, 500);
                CHECK(near(std::hypot(FIRST[0] - SECOND[0], FIRST[1] - SECOND[1]), 500, 0.001));
                const auto CENTER = apply(matrix, WIDTH / 2.0, HEIGHT + (2.0 * WIDTH));
                CHECK(near(CENTER[0], WIDTH / 2.0, 0.001));
                CHECK(near(CENTER[1], HEIGHT + (2.0 * WIDTH), 0.001));
            }
        }
        for (const auto EXTENT : {std::array{1920, 1080}, std::array{1080, 1920},
                 std::array{3440, 1440}}) {
            const double RADIUS = 2.0 * EXTENT[0];
            const double CENTER_X = EXTENT[0] / 2.0;
            const double CENTER_Y = EXTENT[1] + RADIUS;
            for (int i = 0; i <= 20; ++i) {
                const auto FRAME = make_frame({.width = EXTENT[0], .height = EXTENT[1]}, i / 20.0, Direction::Forward);
                for (const auto &pair : {std::pair{FRAME.source, FRAME.source_angle},
                         std::pair{FRAME.destination, FRAME.destination_angle}}) {
                    const auto &matrix = pair.first;
                    const double ANGLE = pair.second;
                    const double POS_X = CENTER_X + (RADIUS * std::sin(ANGLE));
                    const double POS_Y = CENTER_Y - (RADIUS * std::cos(ANGLE));
                    const auto MIDPOINT = apply(matrix, POS_X, POS_Y);
                    CHECK(near(MIDPOINT[0], CENTER_X, 0.002));
                    CHECK(near(MIDPOINT[1], EXTENT[1], 0.002));
                    // The tangent maps to the canonical horizontal bottom edge, with unit scale.
                    const auto TANGENT = apply(matrix, POS_X + std::cos(ANGLE), POS_Y + std::sin(ANGLE));
                    CHECK(near(TANGENT[0] - MIDPOINT[0], 1.0));
                    CHECK(near(TANGENT[1] - MIDPOINT[1], 0.0));
                    CHECK(near(std::hypot(POS_X - CENTER_X, POS_Y - CENTER_Y), RADIUS));
                }
            }
        }
        const auto FIRST = make_frame({.width = WIDTH, .height = HEIGHT}, 0, Direction::Forward);
        const auto LAST = make_frame({.width = WIDTH, .height = HEIGHT}, 1, Direction::Forward);
        const auto START = apply(FIRST.source, 123, 456);
        const auto END = apply(LAST.destination, 123, 456);
        CHECK(near(START[0], 123) && near(START[1], 456));
        CHECK(near(END[0], 123) && near(END[1], 456));
        CHECK((make_frame({.width = WIDTH, .height = HEIGHT}, -1, Direction::Forward).source_angle == 0));
        CHECK((make_frame({.width = WIDTH, .height = HEIGHT}, std::numeric_limits<double>::quiet_NaN(), Direction::Forward).source_angle == 0));
    }
}
int main() { run(); return g_failures == 0 ? 0 : 1; }
