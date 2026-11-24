#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <utility>

#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

#include "window/cloth/ClothSolveOrder.hpp"
#include "window/cloth/ClothSolver.hpp"

namespace hyprimate::cloth {
    namespace {
        constexpr int SUBSTEPS = 8;
        constexpr float DT = Solver::kTick / SUBSTEPS;
        constexpr float INVERSE_STEP_SQUARED = 1.0F / (DT * DT);
        constexpr float EDGE_ALPHA = 4e-6F * INVERSE_STEP_SQUARED;
        constexpr float BEND_ALPHA = 1.2F * INVERSE_STEP_SQUARED;
        constexpr float PI = 3.14159265358979323846F;
        constexpr std::size_t GRID = SHEET_CELLS + 1;
        constexpr std::size_t SEAM = 13;

        // Divide all three coordinates at once, without replacing division with a
        // rounded reciprocal. The unused lane carries no simulation state.
        Point divide(Point point, float divisor) noexcept {
#if defined(__SSE2__)
            const auto QUOTIENT =
                _mm_div_ps(_mm_setr_ps(point.x, point.y, point.z, 0), _mm_set1_ps(divisor));
            alignas(16) std::array<float, 4> lanes;
            _mm_store_ps(lanes.data(), QUOTIENT);
            return {lanes[0], lanes[1], lanes[2]};
#else
            return point / divisor;
#endif
        }

        // Preserve the signed angle and float reduction order of the reference cloth.
        float angle(Point edge, Point left, Point right, float product) noexcept {
            const float COSINE = dot(left, right) / std::sqrt(product);
            const float ABS_COSINE = std::min(std::abs(COSINE), 1.0F);
            const float MAGNITUDE =
                std::sqrt(1 - ABS_COSINE) * ((PI / 2) - (.175394F * ABS_COSINE));
            const float SIGN = dot(cross(right, left), edge) < 0 ? -1.0F : 1.0F;
            return SIGN * (COSINE < 0 ? PI - MAGNITUDE : MAGNITUDE);
        }

        std::size_t island(std::size_t vertex) noexcept {
            const char LABEL = SOLVE_ISLANDS[vertex];
            return static_cast<std::size_t>(LABEL <= '9' ? LABEL - '0' : LABEL - 'a' + 10);
        }

        template <typename Vertices>
        std::size_t constraint_island(const Vertices &vertices) noexcept {
            const auto FIRST = island(vertices.front());
            return std::ranges::all_of(
                       vertices, [&](auto vertex) { return island(vertex) == FIRST; })
                       ? FIRST
                       : SEAM;
        }

        struct Contact {
            Point normal;
            float depth{};
            float time = std::numeric_limits<float>::infinity();
        };

        bool contains(const Box &box, Point point) noexcept {
            return point.x > box.min.x && point.x < box.max.x && point.y > box.min.y &&
                   point.y < box.max.y && point.z > box.min.z && point.z < box.max.z;
        }

        // Already expanded for the vertex radius. Only return an entry on the original sweep;
        // an initially embedded point is handled by the depenetration pass.
        Contact box_entry(Point previous, Point next, const Box &box) noexcept {
            if (std::max(previous.x, next.x) <= box.min.x ||
                std::min(previous.x, next.x) >= box.max.x ||
                std::max(previous.y, next.y) <= box.min.y ||
                std::min(previous.y, next.y) >= box.max.y ||
                std::max(previous.z, next.z) <= box.min.z ||
                std::min(previous.z, next.z) >= box.max.z) {
                return {};
            }
            const std::array<float, 3> START{previous.x, previous.y, previous.z};
            const std::array<float, 3> END{next.x, next.y, next.z};
            const std::array<float, 3> MIN{box.min.x, box.min.y, box.min.z};
            const std::array<float, 3> MAX{box.max.x, box.max.y, box.max.z};
            constexpr std::array<Point, 3> AXES{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
            float enter = -std::numeric_limits<float>::infinity();
            float leave = 1;
            Contact entry;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const float CHANGE = END[axis] - START[axis];
                if (CHANGE == 0) {
                    continue;  // broad-phase already established overlap on this axis
                }
                const bool POSITIVE = CHANGE > 0;
                const float ENTER = ((POSITIVE ? MIN[axis] : MAX[axis]) - START[axis]) / CHANGE;
                const float LEAVE = ((POSITIVE ? MAX[axis] : MIN[axis]) - START[axis]) / CHANGE;
                if (ENTER > enter) {
                    enter = ENTER;
                    entry = POSITIVE ? Contact{AXES[axis] * -1, END[axis] - MIN[axis], ENTER}
                                     : Contact{AXES[axis], MAX[axis] - END[axis], ENTER};
                }
                leave = std::min(leave, LEAVE);
            }
            return enter >= 0 && enter < leave ? entry : Contact{};
        }

        void project(Contact contact, Point &position, Point &velocity, float friction) noexcept {
            if (!(contact.depth > 0)) {
                return;
            }
            position += contact.normal * contact.depth;
            const auto NORMAL_VELOCITY = contact.normal * dot(velocity, contact.normal);
            const auto TANGENT = velocity - NORMAL_VELOCITY;
            const float TANGENT_SPEED = std::sqrt(lengthSquared(TANGENT));
            if (TANGENT_SPEED > 0) {
                velocity -=
                    TANGENT * std::min((friction * contact.depth) / (TANGENT_SPEED * DT), 1.F);
            }
            velocity -= NORMAL_VELOCITY;
        }

        void extend(Box &box, Point point) noexcept {
            box.min = {std::min(box.min.x, point.x),
                std::min(box.min.y, point.y),
                std::min(box.min.z, point.z)};
            box.max = {std::max(box.max.x, point.x),
                std::max(box.max.y, point.y),
                std::max(box.max.z, point.z)};
        }

        Box empty_bounds() noexcept {
            constexpr float INF = std::numeric_limits<float>::infinity();
            return {{INF, INF, INF}, {-INF, -INF, -INF}};
        }

        Plane contact_plane(
            Point position, std::span<const Obstacle> obstacles, Plane backing) noexcept {
            Plane plane = backing;
            float best = lengthSquared(backing.normal) > 0
                             ? backing.offset - dot(backing.normal, position)
                             : -std::numeric_limits<float>::infinity();
            for (const auto &box : obstacles) {
                const auto LOCAL = position - box.center;
                const auto HALF = box.half_extent;
                const Point CLOSEST{std::clamp(LOCAL.x, -HALF.x, HALF.x),
                    std::clamp(LOCAL.y, -HALF.y, HALF.y),
                    std::clamp(LOCAL.z, -HALF.z, HALF.z)};
                const auto DELTA = LOCAL - CLOSEST;
                const float DISTANCE_SQUARED = lengthSquared(DELTA);
                if (DISTANCE_SQUARED > 0) {
                    const float DISTANCE = std::sqrt(DISTANCE_SQUARED);
                    if (-DISTANCE > best) {
                        const auto NORMAL = DELTA / DISTANCE;
                        plane = {NORMAL, dot(NORMAL, CLOSEST) + dot(NORMAL, box.center)};
                        best = -DISTANCE;
                    }
                } else {
                    const auto DEPTH =
                        HALF - Point{std::abs(LOCAL.x), std::abs(LOCAL.y), std::abs(LOCAL.z)};
                    const int AXIS = DEPTH.x < DEPTH.y ? (DEPTH.z < DEPTH.x ? 2 : 0)
                                                       : (DEPTH.z < DEPTH.y ? 2 : 1);
                    const float SMALLEST = AXIS == 0 ? DEPTH.x : AXIS == 1 ? DEPTH.y : DEPTH.z;
                    if (SMALLEST > best) {
                        const Point NORMAL = AXIS == 0   ? Point{std::copysign(1.F, LOCAL.x), 0, 0}
                                             : AXIS == 1 ? Point{0, std::copysign(1.F, LOCAL.y), 0}
                                                         : Point{0, 0, std::copysign(1.F, LOCAL.z)};
                        const float HALF_DISTANCE = AXIS == 0   ? HALF.x
                                                    : AXIS == 1 ? HALF.y
                                                                : HALF.z;
                        plane = {NORMAL, HALF_DISTANCE + dot(NORMAL, box.center)};
                        best = SMALLEST;
                    }
                }
            }
            return plane;
        }

        void resolve_boxes(Point previous,
            Point &position,
            Point &velocity,
            std::span<const Box> boxes,
            Plane backing,
            float friction) noexcept {
            Contact first;
            for (const auto &box : boxes) {
                const auto HIT = box_entry(previous, position, box);
                if (HIT.time < first.time) {
                    first = HIT;
                }
            }
            // Select on the true sweep, before either projection changes its direction.
            // Projecting a box's face first cannot hide an earlier backing collision: its
            // corrected endpoint is checked against the backing plane independently.
            project(first, position, velocity, friction);
            project({backing.normal, backing.offset - dot(backing.normal, position)},
                position,
                velocity,
                friction);
            const auto EMBEDDED = [&](Point candidate) {
                return std::ranges::any_of(
                    boxes, [&](const Box &box) { return contains(box, candidate); });
            };
            if (!EMBEDDED(position)) {
                return;
            }

            // Two radius-expanded boxes may overlap. Resolve against their union rather than
            // alternately projecting inside the other box. Stable box/face order breaks ties.
            Contact closest{.normal = {}, .depth = std::numeric_limits<float>::infinity()};
            Point target = position;
            const auto CONSIDER = [&](Point face, Point normal) {
                const float DEPTH = dot(face - position, normal);
                if (DEPTH > 0 && DEPTH < closest.depth &&
                    dot(backing.normal, face) >= backing.offset && !EMBEDDED(face)) {
                    closest = {normal, DEPTH};
                    target = face;
                }
            };
            for (const auto &box : boxes) {
                CONSIDER({box.min.x, position.y, position.z}, {-1, 0, 0});
                CONSIDER({box.max.x, position.y, position.z}, {1, 0, 0});
                CONSIDER({position.x, box.min.y, position.z}, {0, -1, 0});
                CONSIDER({position.x, box.max.y, position.z}, {0, 1, 0});
                CONSIDER({position.x, position.y, box.min.z}, {0, 0, -1});
                CONSIDER({position.x, position.y, box.max.z}, {0, 0, 1});
            }
            if (std::isfinite(closest.depth)) {
                project(closest, position, velocity, friction);
                position = target;
            }
        }
    }  // namespace

    Solver::Solver(const SheetGrid &rest,
        std::span<const Point> initialPositions,
        std::span<const Point> initialVelocities,
        std::span<const float> inverseMasses) {
        assert(initialPositions.size() == SHEET_GRID_VERTICES &&
               initialVelocities.size() == SHEET_GRID_VERTICES &&
               inverseMasses.size() == SHEET_GRID_VERTICES);
        static_assert(SHEET_GRID_VERTICES < std::numeric_limits<Index>::max());
        std::ranges::copy(initialPositions, m_positions.begin());
        std::ranges::copy(initialVelocities, m_velocities.begin());
        std::ranges::copy(inverseMasses, m_inverseMasses.begin());
        m_broad_bounds = empty_bounds();
        // Seed contact admission from the material bounds. Inherited folds first advance
        // into contact at the same point in the tick sequence as the reference solver.
        for (const auto &point : rest) {
            extend(m_broad_bounds,
                {static_cast<float>(point.x),
                    static_cast<float>(point.y),
                    static_cast<float>(point.z)});
        }
        struct HalfEdge {
            Index low, high, tip;
        };
        std::vector<HalfEdge> halfEdges;
        halfEdges.reserve(SHEET_TRIANGLES * 3);
        for (std::size_t i = 0; i < SHEET_TRIANGLES; ++i) {
            const auto &corners = sheet_triangle(i);
            for (std::size_t j = 0; j < 3; ++j) {
                const auto A = static_cast<Index>(corners[j]);
                const auto B = static_cast<Index>(corners[(j + 1) % 3]);
                halfEdges.push_back(
                    {std::min(A, B), std::max(A, B), static_cast<Index>(corners[(j + 2) % 3])});
            }
        }
        std::ranges::sort(
            halfEdges, {}, [](const HalfEdge &edge) { return std::tuple{edge.low, edge.high}; });
        std::vector<Edge> edges;
        std::vector<Hinge> hinges;
        constexpr std::size_t CELLS = SHEET_CELLS;
        constexpr auto BOUNDARY_EDGES = CELLS * 4;
        constexpr auto MESH_EDGES = ((SHEET_TRIANGLES * 3) + BOUNDARY_EDGES) / 2;
        constexpr auto STRIDE = CELLS + 1;
        edges.reserve(MESH_EDGES + (CELLS * CELLS));
        hinges.reserve(MESH_EDGES - BOUNDARY_EDGES);
        const auto REST_POINT = [&](Index index) {
            const auto &point = rest[index];
            return Point{static_cast<float>(point.x),
                static_cast<float>(point.y),
                static_cast<float>(point.z)};
        };
        const auto ADD_EDGE = [&](Index first, Index second, float length) {
            if (m_inverseMasses[first] > 0 || m_inverseMasses[second] > 0) {
                edges.push_back({{first, second}, length});
            }
        };
        for (std::size_t i = 0; i < halfEdges.size();) {
            const auto FIRST = halfEdges[i++];
            const auto ORIGIN = REST_POINT(FIRST.low);
            const auto EDGE = REST_POINT(FIRST.high) - ORIGIN;
            const float LENGTH = std::sqrt(lengthSquared(EDGE));
            ADD_EDGE(FIRST.low, FIRST.high, LENGTH);
            if (i == halfEdges.size() || halfEdges[i].low != FIRST.low ||
                halfEdges[i].high != FIRST.high) {
                continue;
            }
            const auto SECOND = halfEdges[i++];
            const auto DELTA = static_cast<std::size_t>(FIRST.high - FIRST.low);
            const unsigned TIP_BIT = DELTA == 1 ? 1 : DELTA == STRIDE - 1 ? 2 : 4;
            const bool DESCENDING =
                (static_cast<unsigned>(HINGE_TIP_ORDER[FIRST.low] - '0') & TIP_BIT) != 0;
            const auto LOW_TIP = std::min(FIRST.tip, SECOND.tip);
            const auto HIGH_TIP = std::max(FIRST.tip, SECOND.tip);
            const auto LEFT_TIP = DESCENDING ? HIGH_TIP : LOW_TIP;
            const auto RIGHT_TIP = DESCENDING ? LOW_TIP : HIGH_TIP;
            if (DELTA == STRIDE - 1) {
                ADD_EDGE(LOW_TIP,
                    HIGH_TIP,
                    std::sqrt(lengthSquared(REST_POINT(LOW_TIP) - REST_POINT(HIGH_TIP))));
            }
            if (m_inverseMasses[LEFT_TIP] > 0 || m_inverseMasses[RIGHT_TIP] > 0) {
                const auto LEFT_VECTOR = REST_POINT(LEFT_TIP) - ORIGIN;
                const auto RIGHT_VECTOR = REST_POINT(RIGHT_TIP) - ORIGIN;
                const auto LEFT_NORMAL = cross(LEFT_VECTOR, LEFT_VECTOR - EDGE);
                const auto RIGHT_NORMAL = cross(RIGHT_VECTOR - EDGE, RIGHT_VECTOR);
                const float PRODUCT = lengthSquared(LEFT_NORMAL) * lengthSquared(RIGHT_NORMAL);
                hinges.push_back({{FIRST.low, FIRST.high, LEFT_TIP, RIGHT_TIP},
                    PRODUCT > 0 ? angle(EDGE, LEFT_NORMAL, RIGHT_NORMAL, PRODUCT) : 0});
            }
        }
        make_attachments(edges);
        std::ranges::sort(edges, {}, [](const Edge &edge) {
            const auto [LOW, HIGH] = edge.vertices;
            const bool OTHER_DIAGONAL = static_cast<std::size_t>(HIGH - LOW) == GRID + 1;
            return std::tuple{LOW,
                LOW + (OTHER_DIAGONAL ? 1 : 0),
                HIGH - (OTHER_DIAGONAL ? 1 : 0),
                !OTHER_DIAGONAL};
        });
        std::ranges::sort(hinges, {}, [](const Hinge &hinge) {
            return std::tuple{
                *std::ranges::min_element(hinge.vertices), hinge.vertices[0], hinge.vertices[1]};
        });
        m_edges = make_batches(std::move(edges));
        m_hinges = make_batches(std::move(hinges));
    }

    template <typename Constraint>
    Solver::Batches<Constraint> Solver::make_batches(std::vector<Constraint> constraints) const {
        const auto GROUP = [](const Constraint &constraint) {
            return constraint_island(constraint.vertices);
        };
        std::ranges::stable_sort(constraints, {}, GROUP);
        Batches<Constraint> result;
        result.constraints.resize(constraints.size());
        std::vector<std::size_t> sizes;
        std::vector<std::size_t> levels;
        std::size_t first = 0;
        for (std::size_t group = 0; group <= SEAM; ++group) {
            std::size_t last = first;
            while (last < constraints.size() && GROUP(constraints[last]) == group) {
                ++last;
            }
            // Each level preserves the order of conflicting constraints and can run in parallel.
            std::array<std::size_t, SHEET_GRID_VERTICES> last_level{};
            sizes.clear();
            levels.clear();
            levels.reserve(last - first);
            for (std::size_t i = first; i < last; ++i) {
                std::size_t level = 0;
                for (auto vertex : constraints[i].vertices) {
                    level = std::max(level, last_level[vertex]);
                }
                for (auto vertex : constraints[i].vertices) {
                    last_level[vertex] = level + 1;
                }
                if (level == sizes.size()) {
                    sizes.push_back(0);
                }
                ++sizes[level];
                levels.push_back(level);
            }
            auto offset = first;
            for (auto &size : sizes) {
                const auto END = offset + size;
                size = offset;
                offset = END;
                result.ends.push_back(END);
            }
            for (std::size_t i = first; i < last; ++i) {
                result.constraints[sizes[levels[i - first]]++] = constraints[i];
            }
            result.groups[group + 1] = result.ends.size();
            first = last;
        }
        return result;
    }

    void Solver::make_attachments(const std::vector<Edge> &edges) {
        // Multi-source shortest paths over the fabric rather than through the volumes
        // folded by it. The constraint only limits extension from the nearest pin.
        using Neighbor = std::pair<Index, float>;
        struct Neighbors {
            std::array<Neighbor, 8> items;
            std::size_t count{};
        };
        std::array<Neighbors, SHEET_GRID_VERTICES> graph;
        for (const auto &edge : edges) {
            const auto [A, B] = edge.vertices;
            graph[A].items[graph[A].count++] = {B, edge.restLength};
            graph[B].items[graph[B].count++] = {A, edge.restLength};
        }
        using Item = std::pair<float, Index>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> todo;
        std::array<Index, SHEET_GRID_VERTICES> pin;
        std::array<float, SHEET_GRID_VERTICES> distance;
        distance.fill(std::numeric_limits<float>::infinity());
        for (std::size_t i = 0; i < SHEET_GRID_VERTICES; ++i) {
            if (m_inverseMasses[i] == 0) {
                pin[i] = static_cast<Index>(i);
                distance[i] = 0;
                todo.emplace(0, static_cast<Index>(i));
            }
        }
        if (todo.empty()) {
            return;
        }
        while (!todo.empty()) {
            const auto [DISTANCE, VERTEX] = todo.top();
            todo.pop();
            if (DISTANCE != distance[VERTEX]) {
                continue;
            }
            const auto &neighbors = graph[VERTEX];
            for (const auto &[NEIGHBOR, LENGTH] :
                std::span(neighbors.items).first(neighbors.count)) {
                const float THROUGH = DISTANCE + LENGTH;
                if (THROUGH < distance[NEIGHBOR]) {
                    distance[NEIGHBOR] = THROUGH;
                    pin[NEIGHBOR] = pin[VERTEX];
                    todo.emplace(THROUGH, NEIGHBOR);
                }
            }
        }
        m_attachments.reserve(SHEET_GRID_VERTICES);
        for (std::size_t i = 0; i < SHEET_GRID_VERTICES; ++i) {
            if (m_inverseMasses[i] != 0 && std::isfinite(distance[i])) {
                m_attachments.push_back({pin[i], static_cast<Index>(i), distance[i]});
            }
        }
        const auto GROUP = [](const Attachment &attachment) {
            return constraint_island(std::array{attachment.pin, attachment.vertex});
        };
        std::ranges::stable_sort(m_attachments, {}, GROUP);
        std::size_t end = 0;
        for (std::size_t group = 0; group <= SEAM; ++group) {
            while (end < m_attachments.size() && GROUP(m_attachments[end]) == group) {
                ++end;
            }
            m_attachment_groups[group + 1] = end;
        }
    }

    void Solver::solve_edges(std::size_t group) noexcept {
        const auto FIRST = m_edges.groups[group];
        const auto LAST = m_edges.groups[group + 1];
        std::size_t index = FIRST == 0 ? 0 : m_edges.ends[FIRST - 1];
        for (std::size_t batch = FIRST; batch < LAST; ++batch) {
            for (const auto END = m_edges.ends[batch]; index < END; ++index) {
                const auto &edge = m_edges.constraints[index];
                const auto [LOW, HIGH] = edge.vertices;
                const auto DELTA = m_positions[HIGH] - m_positions[LOW];
                const float LENGTH = std::sqrt(lengthSquared(DELTA));
                const float INV_LOW = m_inverseMasses[LOW];
                const float INV_HIGH = m_inverseMasses[HIGH];
                const float DENOMINATOR = LENGTH * (INV_LOW + INV_HIGH + EDGE_ALPHA);
                if (DENOMINATOR >= 1e-12F) {
                    const auto CORRECTION = divide(DELTA * (LENGTH - edge.restLength), DENOMINATOR);
                    m_positions[LOW] += CORRECTION * INV_LOW;
                    m_positions[HIGH] -= CORRECTION * INV_HIGH;
                }
            }
        }
    }

    void Solver::solve_hinges(std::size_t group) noexcept {
        const auto FIRST = m_hinges.groups[group];
        const auto LAST = m_hinges.groups[group + 1];
        std::size_t index = FIRST == 0 ? 0 : m_hinges.ends[FIRST - 1];
        for (std::size_t batch = FIRST; batch < LAST; ++batch) {
            for (const auto END = m_hinges.ends[batch]; index < END; ++index) {
                const auto &hinge = m_hinges.constraints[index];
                const auto [LOW, HIGH, LEFT, RIGHT] = hinge.vertices;
                const auto EDGE = m_positions[HIGH] - m_positions[LOW];
                const auto LEFT_TIP = m_positions[LEFT] - m_positions[HIGH];
                const auto RIGHT_TIP = m_positions[RIGHT] - m_positions[HIGH];
                auto left_normal = cross(m_positions[LEFT] - m_positions[LOW], LEFT_TIP);
                auto right_normal = cross(RIGHT_TIP, m_positions[RIGHT] - m_positions[LOW]);
                const float LEFT_AREA = lengthSquared(left_normal);
                const float RIGHT_AREA = lengthSquared(right_normal);
                const float AREA_PRODUCT = LEFT_AREA * RIGHT_AREA;
                const float LENGTH = std::sqrt(lengthSquared(EDGE));
                if (LENGTH < 1e-6F || AREA_PRODUCT < 1e-24F) {
                    continue;
                }
                float bend = angle(EDGE, left_normal, right_normal, AREA_PRODUCT) - hinge.restAngle;
                if (bend > PI) {
                    bend -= 2 * PI;
                } else if (bend < -PI) {
                    bend += 2 * PI;
                }
                if (bend == 0) {
                    continue;
                }
                left_normal = divide(left_normal, LEFT_AREA);
                right_normal = divide(right_normal, RIGHT_AREA);
                const auto LEFT_GRAD = left_normal * LENGTH;
                const auto RIGHT_GRAD = right_normal * LENGTH;
                const auto LOW_GRAD =
                    divide(left_normal * dot(LEFT_TIP, EDGE) + right_normal * dot(RIGHT_TIP, EDGE),
                        LENGTH);
                const auto HIGH_GRAD = Point{} - LOW_GRAD - LEFT_GRAD - RIGHT_GRAD;
                const float INV_LOW = m_inverseMasses[LOW];
                const float INV_HIGH = m_inverseMasses[HIGH];
                const float INV_LEFT = m_inverseMasses[LEFT];
                const float INV_RIGHT = m_inverseMasses[RIGHT];
                // One XPBD projection per substep: the multiplier always starts at zero.
                const float SCALE =
                    -bend /
                    ((INV_LOW * lengthSquared(LOW_GRAD)) + (INV_HIGH * lengthSquared(HIGH_GRAD)) +
                        (INV_LEFT * lengthSquared(LEFT_GRAD)) +
                        (INV_RIGHT * lengthSquared(RIGHT_GRAD)) + BEND_ALPHA);
                m_positions[LOW] += LOW_GRAD * (SCALE * INV_LOW);
                m_positions[HIGH] += HIGH_GRAD * (SCALE * INV_HIGH);
                m_positions[LEFT] += LEFT_GRAD * (SCALE * INV_LEFT);
                m_positions[RIGHT] += RIGHT_GRAD * (SCALE * INV_RIGHT);
            }
        }
    }

    void Solver::solve_attachments(std::size_t group) noexcept {
        const auto FIRST = m_attachment_groups[group];
        const auto LAST = m_attachment_groups[group + 1];
        for (const auto &constraint : std::span(m_attachments).subspan(FIRST, LAST - FIRST)) {
            const auto ANCHOR = m_positions[constraint.pin];
            auto &position = m_positions[constraint.vertex];
            const auto DELTA = position - ANCHOR;
            const float SQUARED = lengthSquared(DELTA);
            if (SQUARED > constraint.range * constraint.range) {
                position = ANCHOR + divide(DELTA * constraint.range, std::sqrt(SQUARED));
            }
        }
    }

    void Solver::step(const StepSettings &settings) noexcept {
        const float DAMPING = std::max(0.0F, 1 - settings.damping * DT);
        const float GRAVITY = settings.gravity * DT;
        const Point PADDING{settings.radius, settings.radius, settings.radius};
        const Point MIN = m_broad_bounds.min - PADDING;
        const Point MAX = m_broad_bounds.max + PADDING;
        std::array<Obstacle, kMaxBoxes> active;
        std::array<Box, kMaxBoxes> expanded;
        std::size_t count = 0;
        const auto OBSTACLES = settings.boxes.first(std::min(settings.boxes.size(), kMaxBoxes));
        for (std::size_t i = 0; i < OBSTACLES.size(); ++i) {
            const auto &box = OBSTACLES[i];
            const auto LOW = box.center - box.half_extent;
            const auto HIGH = box.center + box.half_extent;
            expanded[i] = {LOW - PADDING, HIGH + PADDING};
            if (MIN.x <= HIGH.x && MIN.y <= HIGH.y && MIN.z <= HIGH.z && MAX.x >= LOW.x &&
                MAX.y >= LOW.y && MAX.z >= LOW.z) {
                active[count++] = box;
            }
        }
        const auto NORMAL = settings.backing.normal;
        const bool HAS_BACKING = lengthSquared(NORMAL) > 0;
        const Point BACK_CORNER{NORMAL.x >= 0 ? MIN.x : MAX.x,
            NORMAL.y >= 0 ? MIN.y : MAX.y,
            NORMAL.z >= 0 ? MIN.z : MAX.z};
        const bool BACKING = HAS_BACKING && dot(NORMAL, BACK_CORNER) <= settings.backing.offset;
        const Plane SELECTED_BACKING = BACKING ? settings.backing : Plane{};
        const bool CONTACTS = BACKING || count != 0;
        std::array<Plane, SHEET_GRID_VERTICES> planes;
        if (CONTACTS) {
            const auto ACTIVE = std::span(active).first(count);
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                planes[i] = contact_plane(m_positions[i], ACTIVE, SELECTED_BACKING);
            }
        }
        for (int iteration = 0; iteration < SUBSTEPS; ++iteration) {
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                auto &velocity = m_velocities[i];
                if (m_inverseMasses[i] > 0) {
                    velocity.y += GRAVITY;
                    velocity *= DAMPING;
                }
                m_previous[i] = m_positions[i];
                m_positions[i] += velocity * DT;
            }
            for (std::size_t group = 0; group <= SEAM; ++group) {
                solve_hinges(group);
                solve_edges(group);
                solve_attachments(group);
            }
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                if (m_inverseMasses[i] == 0) {
                    continue;
                }
                auto &position = m_positions[i];
                auto &velocity = m_velocities[i];
                velocity = divide(position - m_previous[i], DT);
                if (CONTACTS) {
                    const auto plane = planes[i];
                    const float DEPTH =
                        settings.radius - (dot(plane.normal, position) - plane.offset);
                    project({plane.normal, DEPTH}, position, velocity, settings.friction);
                }
                if (count > 1) {
                    // Resolve seams where a face projection can push into an adjoining box.
                    const Plane EXPANDED_BACKING =
                        BACKING ? Plane{NORMAL, settings.backing.offset + settings.radius}
                                : Plane{};
                    resolve_boxes(m_previous[i],
                        position,
                        velocity,
                        std::span(expanded).first(OBSTACLES.size()),
                        EXPANDED_BACKING,
                        settings.friction);
                } else if (!OBSTACLES.empty()) {
                    // Catch a complete crossing even if it spans adjacent substeps. Ordinary
                    // shallow entries retain the contact plane selected for this tick.
                    Contact first;
                    for (const auto &box : std::span(expanded).first(OBSTACLES.size())) {
                        const auto HIT = box_entry(m_previous[i], position, box);
                        if (!(HIT.time < first.time)) {
                            continue;
                        }
                        const float THICKNESS = std::abs(dot(HIT.normal, box.max - box.min));
                        const float TRAVEL = -dot(HIT.normal, velocity) * kTick;
                        if (!contains(box, position) || TRAVEL >= THICKNESS) {
                            first = HIT;
                        }
                    }
                    project(first, position, velocity, settings.friction);
                }
            }
        }
        if (settings.speedCap > 0) {
            const float CAP_SQUARED = settings.speedCap * settings.speedCap;
            for (std::size_t i = 0; i < m_velocities.size(); ++i) {
                if (m_inverseMasses[i] > 0) {
                    const float SPEED_SQUARED = lengthSquared(m_velocities[i]);
                    if (SPEED_SQUARED > CAP_SQUARED) {
                        m_velocities[i] *= std::sqrt(CAP_SQUARED / SPEED_SQUARED);
                    }
                }
            }
        }
        if (HAS_BACKING || !OBSTACLES.empty()) {
            m_broad_bounds = empty_bounds();
            const Point FALL{0, (36 * (DT * DT)) * settings.gravity, 0};
            for (std::size_t i = 0; i < m_positions.size(); ++i) {
                extend(m_broad_bounds, m_positions[i]);
                extend(m_broad_bounds, m_positions[i] + m_velocities[i] * kTick + FALL);
            }
        }
    }
}  // namespace hyprimate::cloth
