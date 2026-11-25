#include <algorithm>
#include <cmath>
#include <numbers>

#include "CubeMesh.hpp"

namespace hyprimate::workspace::cube::detail {
    namespace {

        struct Point2 {
            double x;
            double z;
        };

        struct CameraPoint {
            double x;
            double y;
            double depth;
        };

        class Projection {
          public:
            Projection(double focal_length, double half_height, Direction direction)
                : m_x(focal_length * (direction == Direction::Forward ? 1.0 : -1.0)),
                  m_y(-focal_length / half_height) {
            }  // Visual top is framebuffer row 0.

            Clip operator()(const CameraPoint &point) const {
                return {static_cast<GLfloat>(m_x * point.x),
                    static_cast<GLfloat>(m_y * point.y),
                    static_cast<GLfloat>(point.depth)};
            }

          private:
            double m_x;
            double m_y;
        };


        struct Angle {
            double sine;
            double cosine;
        };

        Angle angle(double radians) {
            return {std::sin(radians), std::cos(radians)};
        }

        // Work from the nearer endpoint, maintaining exact head-on poses and avoiding
        // cancellation in the room's quintic when approaching its destination.
        Angle quarter_turn(double amount, double progress) {
            const auto ROTATION = angle(amount * std::numbers::pi / 2.0);
            return progress <= 0.5 ? ROTATION : Angle{ROTATION.cosine, ROTATION.sine};
        }

        constexpr std::array<Point2, 4> SOLID_CORNERS{{{1, 1}, {1, -1}, {-1, -1}, {-1, 1}}};
        constexpr double SIN_PI_8 = 0.38268343236508977;
        constexpr double COS_PI_8 = 0.92387953251128676;
        constexpr double DIAGONAL = std::numbers::sqrt2 / 2.0;
        // Unit directions every pi/8. Exact compass axes weld the quarter arcs to the flats.
        constexpr std::array<Point2, 16> ARC{{{0, 1},
            {SIN_PI_8, COS_PI_8},
            {DIAGONAL, DIAGONAL},
            {COS_PI_8, SIN_PI_8},
            {1, 0},
            {COS_PI_8, -SIN_PI_8},
            {DIAGONAL, -DIAGONAL},
            {SIN_PI_8, -COS_PI_8},
            {0, -1},
            {-SIN_PI_8, -COS_PI_8},
            {-DIAGONAL, -DIAGONAL},
            {-COS_PI_8, -SIN_PI_8},
            {-1, 0},
            {-COS_PI_8, SIN_PI_8},
            {-DIAGONAL, DIAGONAL},
            {-SIN_PI_8, COS_PI_8}}};
        constexpr double SOLID_FOCAL = 3.4;

        Point2 solid_stop(std::size_t index, double radius) {
            const Point2 CORNER = SOLID_CORNERS[index / 5];
            const Point2 OUTWARD = ARC[(index - (index / 5)) % ARC.size()];
            return {(CORNER.x * (1.0 - radius)) + (radius * OUTWARD.x),
                (CORNER.z * (1.0 - radius)) + (radius * OUTWARD.z)};
        }

        class SolidCamera {
          public:
            SolidCamera(const Pose &pose, double edge_progress, double envelope)
                : m_yaw(quarter_turn(edge_progress, pose.progress)),
                  m_pitch(angle(pose.tilt * envelope)) {
                double retreat = 1.42 * envelope;
                if (m_pitch.sine != 0.0) {
                    // Fit the unrounded silhouette before adding the level dolly.
                    for (const auto &corner : SOLID_CORNERS) {
                        for (const double HEIGHT : {-pose.half_height, pose.half_height}) {
                            const auto TURNED = rotated(corner, HEIGHT);
                            const double EXTENT =
                                std::max(std::abs(TURNED.x), std::abs(TURNED.y) / pose.half_height);
                            retreat = std::max(
                                retreat, -TURNED.depth - 1.0 + (SOLID_FOCAL * (EXTENT - 1.0)));
                        }
                    }
                }
                m_eye_distance = SOLID_FOCAL + 1.0 + retreat + (2.0 * pose.zoom * envelope);
            }

            [[nodiscard]] CameraPoint view(const Point2 &position, double height) const {
                auto point = rotated(position, height);
                point.depth += m_eye_distance;
                return point;
            }

            [[nodiscard]] Point2 eye_plan() const {
                const double TOWARD = m_pitch.cosine * m_eye_distance;
                return {m_yaw.sine * TOWARD, m_yaw.cosine * TOWARD};
            }

            [[nodiscard]] double eye_height() const {
                return -m_pitch.sine * m_eye_distance;
            }
            [[nodiscard]] const Angle &yaw() const {
                return m_yaw;
            }

          private:
            [[nodiscard]] CameraPoint rotated(const Point2 &position, double height) const {
                const double ACROSS = (m_yaw.cosine * position.x) - (m_yaw.sine * position.z);
                const double TOWARD = (m_yaw.sine * position.x) + (m_yaw.cosine * position.z);
                return {ACROSS,
                    (m_pitch.cosine * height) + (m_pitch.sine * TOWARD),
                    (m_pitch.sine * height) - (m_pitch.cosine * TOWARD)};
            }

            Angle m_yaw;
            Angle m_pitch;
            double m_eye_distance = 0;
        };

        class BevelLight {
          public:
            explicit BevelLight(double progress)
                : m_key(angle(
                      (progress * std::numbers::pi / 2.0) - (std::numbers::pi / 16.0) - 0.55)),
                  m_glint(angle(
                      (progress * std::numbers::pi / 2.0) - (std::numbers::pi / 16.0) - 0.26)) {
            }

            [[nodiscard]] Rgb shade(std::size_t index) const {
                const Point2 ARC_START = ARC[(index - (index / 5)) % ARC.size()];
                const double KEY =
                    std::max(0.0, (ARC_START.x * m_key.sine) + (ARC_START.z * m_key.cosine));
                const double GLINT =
                    std::max(0.0, (ARC_START.x * m_glint.sine) + (ARC_START.z * m_glint.cosine));
                const double GLINT2 = GLINT * GLINT;
                const double GLINT4 = GLINT2 * GLINT2;
                const double GLINT8 = GLINT4 * GLINT4;
                const double LIGHT = 0.15 + (0.19 * KEY) + (0.32 * GLINT8 * GLINT4 * GLINT2);
                return {static_cast<GLfloat>(LIGHT * 0.86),
                    static_cast<GLfloat>(LIGHT * 0.95),
                    static_cast<GLfloat>(LIGHT)};
            }

          private:
            Angle m_key;
            Angle m_glint;
        };

        void solid(Mesh &mesh, const Pose &pose) {
            const double EDGE_PROGRESS = std::min(pose.progress, 1.0 - pose.progress);
            const double ENVELOPE = std::sin(std::numbers::pi * EDGE_PROGRESS);
            const SolidCamera CAMERA(pose, EDGE_PROGRESS, ENVELOPE);
            const Projection PROJECT(SOLID_FOCAL, pose.half_height, pose.direction);
            const double RADIUS = 0.012 * ENVELOPE;
            std::array<Point2, STOPS> plan{};
            std::array<Clip, STOPS> top{};
            std::array<Clip, STOPS> bottom{};
            for (std::size_t i = 0; i < STOPS; ++i) {
                plan[i] = solid_stop(i, RADIUS);
                top[i] = PROJECT(CAMERA.view(plan[i], pose.half_height));
                bottom[i] = PROJECT(CAMERA.view(plan[i], -pose.half_height));
            }

            if (ENVELOPE != 0.0) {
                mesh.sky();
            }
            const Point2 EYE = CAMERA.eye_plan();
            const BevelLight ALUMINIUM(pose.progress);
            for (std::size_t i = 0; i < STOPS; ++i) {
                const std::size_t NEXT = (i + 1) % STOPS;
                const auto &left = plan[i];
                const auto &right = plan[NEXT];
                // Outward normal of a clockwise section edge is (-dz, dx). The
                // visible faces of a convex opaque solid never overlap in projection.
                if ((right.x - left.x) * (EYE.z - left.z) <=
                    (right.z - left.z) * (EYE.x - left.x)) {
                    continue;
                }
                const Quad SIDE{top[i], top[NEXT], bottom[NEXT], bottom[i]};
                if (i == 19) {
                    const auto LIGHT =
                        static_cast<GLfloat>(1.0 - (0.24 * (1.0 - CAMERA.yaw().cosine)));
                    mesh.quad(SIDE, Chart::Source, {LIGHT, LIGHT, LIGHT});
                } else if (i == 4) {
                    const auto LIGHT =
                        static_cast<GLfloat>(1.0 - (0.29 * (1.0 - CAMERA.yaw().sine)));
                    mesh.quad(SIDE, Chart::Destination, {LIGHT, LIGHT, LIGHT});
                } else {
                    mesh.quad(SIDE, Chart::Paint, ALUMINIUM.shade(i));
                }
            }

            const double EYE_Y = CAMERA.eye_height();
            if (std::abs(EYE_Y) > pose.half_height) {
                const bool CROWN = EYE_Y > 0;
                const auto &ring = CROWN ? top : bottom;
                const Rgb COLOR = CROWN ? Rgb{0.19F, 0.23F, 0.26F} : Rgb{0.14F, 0.17F, 0.20F};
                const Clip CENTER =
                    PROJECT(CAMERA.view({0, 0}, CROWN ? pose.half_height : -pose.half_height));
                for (std::size_t i = 0; i < STOPS; ++i) {
                    mesh.triangle({CENTER, ring[i], ring[(i + 1) % STOPS]}, COLOR);
                }
            }
        }

        constexpr std::array<Point2, 4> ROOM_CORNERS{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
        constexpr double ROOM_FOCAL = 0.8125;

        void inside_out(Mesh &mesh, const Pose &pose) {
            const double EDGE = std::min(pose.progress, 1.0 - pose.progress);
            const double TURN = EDGE * EDGE * EDGE * (10.0 + (EDGE * (-15.0 + (6.0 * EDGE))));
            const Angle YAW = quarter_turn(TURN, pose.progress);
            const double ARC_ENVELOPE = 4.0 * pose.progress * (1.0 - pose.progress);
            const double REVEAL = ARC_ENVELOPE * ARC_ENVELOPE;
            const double SQUARE_SINE = YAW.sine * YAW.sine;
            const double SQUARE_COSINE = YAW.cosine * YAW.cosine;
            // The fourth norm bounds each component of the eye displacement. Even
            // at full retreat the eye stays strictly inside all six room planes.
            const double MAX_RETREAT =
                0.90 /
                std::sqrt(std::sqrt((SQUARE_SINE * SQUARE_SINE) + (SQUARE_COSINE * SQUARE_COSINE)));
            const double REAR = std::lerp(0.22, MAX_RETREAT, pose.zoom);
            const double RADIUS = std::lerp(1.0 - ROOM_FOCAL, -REAR, REVEAL);
            const Point2 EYE{YAW.sine * RADIUS, -YAW.cosine * RADIUS};
            const Angle PITCH = angle(pose.tilt * REVEAL);
            const Projection PROJECT(ROOM_FOCAL, pose.half_height, pose.direction);
            const auto VIEW = [&](const Point2 &position, double height) {
                const double across = position.x - EYE.x;
                const double toward = position.z - EYE.z;
                const double DEPTH = (YAW.sine * across) - (YAW.cosine * toward);
                return PROJECT({(YAW.cosine * across) + (YAW.sine * toward),
                    (PITCH.cosine * height) - (PITCH.sine * DEPTH),
                    (PITCH.cosine * DEPTH) + (PITCH.sine * height)});
            };
            Quad lower{};
            Quad upper{};
            for (std::size_t i = 0; i < ROOM_CORNERS.size(); ++i) {
                lower[i] = VIEW(ROOM_CORNERS[i], -pose.half_height);
                upper[i] = VIEW(ROOM_CORNERS[i], pose.half_height);
            }
            const auto WALL = [&](std::size_t left, std::size_t right) -> Quad {
                return {upper[left], upper[right], lower[right], lower[left]};
            };
            // Each view ray exits exactly one room face. Signed-w clipping discards
            // the portions behind the eye without a far wall painting over a near one.
            mesh.quad(WALL(0, 1), Chart::Source, WHITE);
            mesh.quad(WALL(1, 2), Chart::Destination, WHITE);
            mesh.quad(
                WALL(2, 3), Chart::Paint, {0.24F, 0.26F, 0.28F}, {0.92F, 0.77F, 0.77F, 0.92F});
            mesh.quad(
                WALL(3, 0), Chart::Paint, {0.28F, 0.29F, 0.30F}, {0.77F, 0.92F, 0.92F, 0.77F});
            mesh.quad(lower, Chart::Paint, {0.31F, 0.33F, 0.35F}, {0.92F, 0.65F, 0.82F, 1.09F});
            mesh.quad({upper[3], upper[2], upper[1], upper[0]},
                Chart::Paint,
                {0.16F, 0.18F, 0.20F},
                {0.96F, 0.79F, 0.65F, 0.82F});
        }
    }
    void build_mesh(Mesh &mesh, const Pose &pose, Variant variant) {
        switch (variant) {
            case Variant::Solid:
                solid(mesh, pose);
                break;
            case Variant::InsideOut:
                inside_out(mesh, pose);
                break;
        }
    }
}
