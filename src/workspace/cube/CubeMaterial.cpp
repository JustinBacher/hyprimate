#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>

#include "CubeMaterial.hpp"
#include "gl/Storage.hpp"

namespace hyprimate::workspace::cube {
    namespace {
        constexpr const char *VERTEX_SHADER = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec3 shade;
layout(location = 3) in int chart;
out vec2 uv;
out vec3 tint;
flat out int face;
void main() {
    // Keep signed eye depth in w. Hardware clips the room walls against the view
    // pyramid before division; negative w cannot survive even with z set to zero.
    // No depth test or arbitrary near/far slices are needed for these convex meshes.
    gl_Position = vec4(position.xy, 0.0, position.z);
    uv = texcoord;
    tint = shade;
    face = chart;
}
)";

        constexpr const char *FRAGMENT_SHADER = R"(#version 300 es
precision highp float;
precision highp int;
uniform highp sampler2D sourceImage;
uniform highp sampler2D destinationImage;
in vec2 uv;
in vec3 tint;
flat in int face;
out vec4 color;
void main() {
    vec3 rgb = tint;
    if (face == 0) {
        rgb *= textureLod(sourceImage, uv, 0.0).rgb;
    } else if (face == 1) {
        rgb *= textureLod(destinationImage, uv, 0.0).rgb;
    } else if (face == 2) {
        vec2 p = uv * vec2(0.88, 0.83) + vec2(0.14, 0.19);
        float light = exp(-dot(p, p));
        rgb = mix(vec3(0.048, 0.060, 0.075), vec3(0.119, 0.147, 0.173), light);
    }
    color = vec4(rgb, 1.0);
}
)";

        enum class Chart : GLint { Paint = -1, Source = 0, Destination = 1, Sky = 2 };

        struct Clip {
            GLfloat x;
            GLfloat y;
            GLfloat w;
        };

        using Rgb = std::array<GLfloat, 3>;
        using Uv = std::array<GLfloat, 2>;
        using Shades = std::array<GLfloat, 4>;
        using Quad = std::array<Clip, 4>;

        struct Vertex {
            Clip clip;
            Uv uv;
            Rgb tint;
            Chart chart;
        };

        constexpr std::size_t STOPS = 20;
        constexpr std::size_t MAX_VERTICES = 3 + (STOPS * (6 + 3));  // sky, sides, one cap fan
        constexpr Shades FULL_LIGHT{1, 1, 1, 1};
        constexpr std::array<Uv, 4> FACE_UVS{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        constexpr Rgb WHITE{1, 1, 1};

        class Mesh {
          public:
            // Push initializes each vertex before it can be part of the uploaded prefix.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
            explicit Mesh(bool backward) : m_backward(backward) {
            }

            void sky() {
                for (const auto &point :
                    std::array<Clip, 3>{{{-1, -1, 1}, {3, -1, 1}, {-1, 3, 1}}}) {
                    push({point, {point.x, point.y}, {}, Chart::Sky});
                }
            }

            void triangle(const std::array<Clip, 3> &points, const Rgb &color) {
                for (const auto &point : points) {
                    push({point, {}, color, Chart::Paint});
                }
            }

            void quad(const Quad &points,
                Chart chart,
                const Rgb &color,
                const Shades &shades = FULL_LIGHT) {
                for (const auto INDEX : {0U, 1U, 2U, 0U, 2U, 3U}) {
                    // Reflect travel, not lettering. Keep the original quad diagonal,
                    // including on the floor/ceiling where it affects the gradient.
                    const auto &uv = FACE_UVS[INDEX];
                    const Uv COORD{m_backward ? 1.0F - uv[0] : uv[0], uv[1]};
                    const GLfloat LIGHT = shades[INDEX];
                    push({points[INDEX],
                        COORD,
                        {color[0] * LIGHT, color[1] * LIGHT, color[2] * LIGHT},
                        chart});
                }
            }

            [[nodiscard]] const Vertex *data() const {
                return m_vertices.data();
            }
            [[nodiscard]] GLsizeiptr bytes() const {
                return static_cast<GLsizeiptr>(m_size * sizeof(Vertex));
            }
            [[nodiscard]] GLsizei count() const {
                return static_cast<GLsizei>(m_size);
            }

          private:
            void push(const Vertex &vertex) {
                m_vertices[m_size++] = vertex;
            }

            // Only the populated prefix is uploaded; no per-frame heap or buffer allocation.
            std::array<Vertex, MAX_VERTICES> m_vertices;
            std::size_t m_size = 0;
            bool m_backward;
        };

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

        struct Pose {
            double progress;
            double half_height;
            double tilt;
            double zoom;
            Direction direction;
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

        GLuint compile_shader(GLenum stage, const char *source) {
            const GLuint SHADER = glCreateShader(stage);
            if (SHADER == 0) {
                return 0;
            }
            glShaderSource(SHADER, 1, &source, nullptr);
            glCompileShader(SHADER);
            GLint compiled = GL_FALSE;
            glGetShaderiv(SHADER, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_TRUE) {
                return SHADER;
            }
            std::array<GLchar, 1024> log{};
            glGetShaderInfoLog(SHADER, static_cast<GLsizei>(log.size()), nullptr, log.data());
            (void)std::fprintf(stderr, "hyprimate: Cube shader: %s\n", log.data());
            glDeleteShader(SHADER);
            return 0;
        }

        GLuint make_program() {
            const GLuint VERTEX = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
            if (VERTEX == 0) {
                return 0;
            }
            const GLuint FRAGMENT = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
            const GLuint PROGRAM = FRAGMENT != 0 ? glCreateProgram() : 0;
            GLint linked = GL_FALSE;
            if (PROGRAM != 0) {
                glAttachShader(PROGRAM, VERTEX);
                glAttachShader(PROGRAM, FRAGMENT);
                glLinkProgram(PROGRAM);
                glGetProgramiv(PROGRAM, GL_LINK_STATUS, &linked);
                glDetachShader(PROGRAM, VERTEX);
                glDetachShader(PROGRAM, FRAGMENT);
            }
            glDeleteShader(VERTEX);
            if (FRAGMENT != 0) {
                glDeleteShader(FRAGMENT);
            }
            if (linked == GL_TRUE) {
                return PROGRAM;
            }
            if (PROGRAM != 0) {
                std::array<GLchar, 1024> log{};
                glGetProgramInfoLog(PROGRAM, static_cast<GLsizei>(log.size()), nullptr, log.data());
                (void)std::fprintf(stderr, "hyprimate: Cube program: %s\n", log.data());
                glDeleteProgram(PROGRAM);
            }
            return 0;
        }

        const void *attribute_offset(std::size_t offset) {
            // GLES interprets this pointer as a byte offset into the bound VBO.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
            return reinterpret_cast<const void *>(offset);
        }
    }  // namespace

    bool Material::initialize() {
        if (m_program != 0) {
            return true;
        }
        m_program = make_program();
        if (m_program == 0) {
            return false;
        }
        const GLint SOURCE = glGetUniformLocation(m_program, "sourceImage");
        const GLint DESTINATION = glGetUniformLocation(m_program, "destinationImage");
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_buffer);
        glGenSamplers(1, &m_sampler);
        if (SOURCE < 0 || DESTINATION < 0 || m_vao == 0 || m_buffer == 0 || m_sampler == 0) {
            release();
            return false;
        }
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
        if (!gl::allocate_buffer(
                GL_ARRAY_BUFFER, sizeof(Vertex) * MAX_VERTICES, nullptr, GL_STREAM_DRAW)) {
            release();
            return false;
        }
        constexpr std::array<GLint, 3> SIZES{3, 2, 3};
        constexpr std::array<std::size_t, 3> OFFSETS{
            offsetof(Vertex, clip), offsetof(Vertex, uv), offsetof(Vertex, tint)};
        for (GLuint i = 0; i < SIZES.size(); ++i) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(
                i, SIZES[i], GL_FLOAT, GL_FALSE, sizeof(Vertex), attribute_offset(OFFSETS[i]));
        }
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(
            3, 1, GL_INT, sizeof(Vertex), attribute_offset(offsetof(Vertex, chart)));
        glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glUseProgram(m_program);
        glUniform1i(SOURCE, 0);
        glUniform1i(DESTINATION, 1);
        return true;
    }

    void Material::release() {
        if (m_program == 0) {
            return;
        }
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_buffer);
        glDeleteSamplers(1, &m_sampler);
        glDeleteProgram(m_program);
        abandon();
    }

    void Material::abandon() {
        m_program = m_vao = m_buffer = m_sampler = 0;
    }

    bool Material::draw(GLuint source,
        GLuint destination,
        GLuint output_texture,
        int width,
        int height,
        double progress,
        Direction direction,
        const Settings &settings) {
        if (m_program == 0 || source == 0 || destination == 0 || output_texture == 0 ||
            source == output_texture || destination == output_texture || width <= 0 ||
            height <= 0 || !std::isfinite(progress) || progress < 0.0 || progress > 1.0) {
            return false;
        }
        const double TILT = std::isfinite(settings.tilt_degrees)
                                ? std::clamp(settings.tilt_degrees, -20.0F, 20.0F)
                                : -20.0F;
        const double ZOOM =
            std::isfinite(settings.zoom) ? std::clamp(settings.zoom, 0.0F, 1.0F) : 0.0F;
        const Pose POSE{.progress = progress,
            .half_height = static_cast<double>(height) / width,
            .tilt = TILT * std::numbers::pi / 180.0,
            .zoom = ZOOM,
            .direction = direction};
        Mesh mesh(direction == Direction::Backward);
        switch (settings.variant) {
            case Variant::Solid:
                solid(mesh, POSE);
                break;
            case Variant::InsideOut:
                inside_out(mesh, POSE);
                break;
        }
        if (mesh.count() == 0) {
            return false;
        }

        const std::array<GLuint, 2> INPUTS{source, destination};
        for (GLuint i = 0; i < INPUTS.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindSampler(i, m_sampler);
            glBindTexture(GL_TEXTURE_2D, INPUTS[i]);
        }
        glUseProgram(m_program);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, mesh.bytes(), mesh.data());
        glDrawArrays(GL_TRIANGLES, 0, mesh.count());
        return true;
    }
}  // namespace hyprimate::workspace::cube
