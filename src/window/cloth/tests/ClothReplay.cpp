// Development-only public-API replay. See ClothReplay.md for the contract and limitations.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "window/cloth/ClothFlutter.hpp"

namespace {
    using namespace hyprimate;
    constexpr int GRID = SHEET_CELLS + 1;
    constexpr Box SCREEN{0, 0, 1920, 1080};
    constexpr Box WINDOW{400, 240, 900, 500};
    using Clock = std::chrono::steady_clock;

    void require(bool condition, const char *message) {
        if (!condition) {
            std::cerr << "replay: " << message << '\n';
            std::exit(1);
        }
    }

    void sample(const char *name,
        int frame,
        double time,
        const Box &box,
        ClothFlutter &cloth,
        std::optional<Edge> edge = std::nullopt,
        const Box *obstacle = nullptr,
        const SheetGrid *clear_positions = nullptr) {
        SheetGrid positions{};
        SheetGrid velocities{};
        SheetFrame projected;
        cloth.sample_vertices(positions, velocities);
        cloth.frame({box.w, box.h}, projected);
        double xmin = 1e9;
        double xmax = -1e9;
        double ymin = 1e9;
        double ymax = -1e9;
        double zmin = 1e9;
        double zmax = -1e9;
        double hemx = 0;
        double hemy = 0;
        double hemz = 0;
        double rodx = 0;
        double rody = 0;
        double rodz = 0;
        double velocity_x = 0;
        double velocity_y = 0;
        double velocity_z = 0;
        double speed_squared = 0;
        double max_obstacle_penetration = 0;
        int obstacle_vertices = 0;
        int crossings = 0;
        bool fully_outside = false;
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const Vec3 &position = positions[i];
            const Vec3 &velocity = velocities[i];
            const Vec2 &projected_position = projected.position[i];
            require(std::isfinite(position.x) && std::isfinite(position.y) &&
                        std::isfinite(position.z) && std::isfinite(velocity.x) &&
                        std::isfinite(velocity.y) && std::isfinite(velocity.z) &&
                        std::isfinite(projected_position.x) && std::isfinite(projected_position.y),
                "non-finite sample");
            xmin = std::min(xmin, projected_position.x);
            xmax = std::max(xmax, projected_position.x);
            ymin = std::min(ymin, projected_position.y);
            ymax = std::max(ymax, projected_position.y);
            zmin = std::min(zmin, position.z);
            zmax = std::max(zmax, position.z);
            velocity_x += velocity.x;
            velocity_y += velocity.y;
            velocity_z += velocity.z;
            speed_squared +=
                velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z;
            if (i >= positions.size() - GRID) {
                hemx += position.x;
                hemy += position.y;
                hemz += position.z;
            }
            if (i < GRID) {
                rodx += position.x;
                rody += position.y;
                rodz += position.z;
            }
            if (i > 0 && i < GRID && position.x < positions[i - 1].x - 1e-6) {
                ++crossings;
            }
            const double world_x = box.x + (box.w / 2) + (position.x * box.w);
            const double world_y = box.y - position.y * box.h;
            if (obstacle && world_x >= obstacle->x && world_x <= obstacle->x + obstacle->w &&
                world_y >= obstacle->y && world_y <= obstacle->y + obstacle->h) {
                ++obstacle_vertices;
                max_obstacle_penetration = std::max(max_obstacle_penetration,
                    std::min({world_x - obstacle->x,
                        obstacle->x + obstacle->w - world_x,
                        world_y - obstacle->y,
                        obstacle->y + obstacle->h - world_y}));
            }
            if (edge) {
                switch (*edge) {
                    case Edge::Left:
                        fully_outside |= world_x >= SCREEN.x;
                        break;
                    case Edge::Right:
                        fully_outside |= world_x <= SCREEN.x + SCREEN.w;
                        break;
                    case Edge::Top:
                        fully_outside |= world_y >= SCREEN.y;
                        break;
                    case Edge::Bottom:
                        fully_outside |= world_y <= SCREEN.y + SCREEN.h;
                        break;
                }
            }
        }
        if (edge) {
            fully_outside = !fully_outside;
        }
        const double vertex_count = static_cast<double>(positions.size());
        double paired_pose_delta = 0;
        if (clear_positions) {
            for (std::size_t i = 0; i < positions.size(); ++i) {
                paired_pose_delta = std::max({paired_pose_delta,
                    std::abs(positions[i].x - (*clear_positions)[i].x),
                    std::abs(positions[i].y - (*clear_positions)[i].y),
                    std::abs(positions[i].z - (*clear_positions)[i].z)});
            }
        }
        const auto &center = positions[(GRID / 2) * GRID + GRID / 2];
        const auto &center_velocity = velocities[(GRID / 2) * GRID + GRID / 2];
        std::cout << name << ',' << frame << ',' << std::fixed << std::setprecision(9) << time
                  << ',' << static_cast<int>(cloth.active()) << ','
                  << static_cast<int>(cloth.closed()) << ',' << cloth.opacity() << ',' << xmin
                  << ',' << xmax << ',' << ymin << ',' << ymax << ',' << zmax - zmin << ','
                  << (hemx - rodx) / GRID << ',' << (hemy - rody) / GRID << ','
                  << (hemz - rodz) / GRID << ',' << positions[GRID - 1].x - positions[0].x << ','
                  << velocity_x / vertex_count << ',' << velocity_y / vertex_count << ','
                  << velocity_z / vertex_count << ',' << std::sqrt(speed_squared / vertex_count)
                  << ',' << center.x << ',' << center.y << ',' << center.z << ','
                  << center_velocity.x << ',' << center_velocity.y << ',' << center_velocity.z
                  << ',' << positions[0].x << ',' << positions[GRID - 1].x << ',' << crossings
                  << ',' << obstacle_vertices << ',' << max_obstacle_penetration << ','
                  << static_cast<int>(fully_outside) << ',' << paired_pose_delta << '\n';
    }

    void transitions() {
        for (int variant = 0; variant < 3; ++variant) {
            ClothParams params{.duration = variant == 0   ? .4
                                           : variant == 2 ? 2.
                                                          : .8,
                .flutter = variant == 0   ? 0.
                           : variant == 1 ? .5
                                          : 1.,
                .variation = .5};
            const Box box = variant == 2 ? Box{600, 130, 450, 800} : WINDOW;
            const std::string name = "open" + std::to_string(variant);
            ClothFlutter cloth;
            const auto started = Clock::now();
            cloth.begin(Transition::Open, params, 7 + variant, box, {});
            require(cloth.wait_for_snapshots(1), "open first publication timeout");
            const auto first = Clock::now();
            require(cloth.wait_for_recording(), "open recording timeout");
            const auto complete = Clock::now();
            std::cerr << name << " first_ms="
                      << std::chrono::duration<double, std::milli>(first - started).count()
                      << " record_ms="
                      << std::chrono::duration<double, std::milli>(complete - started).count()
                      << '\n';
            sample(name.c_str(), 0, 0, box, cloth);
            const int frames = static_cast<int>(std::ceil(params.duration * 120)) + 24;
            for (int frame = 1; frame <= frames; ++frame) {
                cloth.advance(1. / 120);
                sample(name.c_str(), frame, frame / 120., box, cloth);
            }
        }
        constexpr std::array edges{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};
        const std::array<Box, 4> obstacles{{
            {1300, 0, 300, 1080},  // right side, retained for a left-edge close
            {100, 0, 300, 1080},  // left side, retained for a right-edge close
            {100, 0, 300, 1080},  // left side, retained for a top-edge close
            {1300, 0, 300, 1080},  // right side, retained for a bottom-edge close
        }};
        for (int edge = 0; edge < 4; ++edge) {
            std::vector<SheetGrid> clear_poses;
            clear_poses.reserve(121);
            ClothFlutter clear;
            clear.begin(Transition::Close,
                {.duration = .8, .flutter = .5, .variation = .5},
                17,
                WINDOW,
                {},
                ClothCloseGeometry{edges[edge], SCREEN});
            require(clear.wait_for_recording(), "clear close recording timeout");
            const std::string clear_name = "close" + std::to_string(edge) + "u";
            for (int frame = 0; frame <= 120; ++frame) {
                if (frame != 0) {
                    clear.advance(1. / 120);
                }
                SheetGrid positions{};
                SheetGrid velocities{};
                clear.sample_vertices(positions, velocities);
                clear_poses.push_back(positions);
                sample(clear_name.c_str(), frame, frame / 120., WINDOW, clear, edges[edge]);
            }

            ClothFlutter blocked;
            const Box &obstacle = obstacles[edge];
            blocked.begin(Transition::Close,
                {.duration = .8, .flutter = .5, .variation = .5},
                17,
                WINDOW,
                std::span<const Box>(&obstacle, 1),
                ClothCloseGeometry{edges[edge], SCREEN});
            require(blocked.wait_for_recording(), "obstructed close recording timeout");
            const std::string blocked_name = "close" + std::to_string(edge) + "b";
            for (int frame = 0; frame <= 120; ++frame) {
                if (frame != 0) {
                    blocked.advance(1. / 120);
                }
                sample(blocked_name.c_str(),
                    frame,
                    frame / 120.,
                    WINDOW,
                    blocked,
                    edges[edge],
                    &obstacle,
                    &clear_poses[frame]);
            }
        }
    }

    void movement() {
        for (int variant = 0; variant < 3; ++variant) {
            ClothFlutter cloth;
            ClothMovementParams params{.flutter = variant == 0 ? 0. : 1.,
                .variation = 0,
                .springiness = variant == 0 ? 0. : 1.};
            const std::string name = "move" + std::to_string(variant);
            Box box = WINDOW;
            cloth.begin_movement(params, 33 + variant, box);
            require(cloth.wait_for_snapshots(2), "movement startup timeout");
            sample(name.c_str(), 0, 0, box, cloth);
            const double step = variant == 2 ? 1. / 60 : 1. / 120;
            double time = 0;
            for (int frame = 1; frame <= (variant == 2 ? 30 : 60); ++frame) {
                const double sign = time < .2 ? 1. : time < .4 ? -1. : 0.;
                box.x += (variant == 1 ? 0 : 700) * step * sign;
                box.y += (variant == 1 ? 1100 : 0) * step * sign;
                if (frame == (variant == 2 ? 12 : 24)) {
                    box.w *= .8;
                    box.h *= 1.1;
                    params.springiness = variant == 2 ? .25 : 1.;
                    params.flutter = .7;
                    params.gravity = 2.;
                    cloth.set_movement_params(params);
                }
                time += step;
                const int snapshots = 2 + static_cast<int>(std::floor(time * 120 + 1e-7));
                ClothPhysics::pause_workers_for_testing(true, snapshots);
                cloth.update_movement(box, step);
                cloth.advance(step);
                if (cloth.moving()) {
                    require(cloth.wait_for_snapshots(snapshots), "movement publication timeout");
                    cloth.advance(0);
                }
                sample(name.c_str(), frame, time, box, cloth);
            }
            sample((name + "_before_close").c_str(), 0, time, box, cloth);
            ClothPhysics::pause_workers_for_testing(false);
            cloth.begin(Transition::Close,
                {.duration = .8, .flutter = .5},
                47,
                box,
                {},
                ClothCloseGeometry{Edge::Right, SCREEN});
            sample((name + "_after_close").c_str(), 0, time, box, cloth);
            require(cloth.wait_for_recording(), "handoff close recording timeout");
            for (int frame = 1; frame <= 96; ++frame) {
                cloth.advance(1. / 120);
                sample((name + "_close").c_str(), frame, frame / 120., box, cloth, Edge::Right);
            }
        }
    }
}  // namespace
int main() {
    const ClothPhysics physics;
    std::cout << "scenario,frame,time,active,closed,opacity,xmin,xmax,ymin,ymax,depth,hem_lag_x,"
                 "hem_lag_y,hem_lag_z,spread,vx,vy,vz,speed_rms,centerx,centery,centerz,center_vx,"
                 "center_vy,center_vz,header_left,header_right,crossings,obstacle_vertices,"
                 "obstacle_penetration,fully_outside,paired_pose_delta\n";
    transitions();
    movement();
}
