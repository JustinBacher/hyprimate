#include "window/cloth/ClothFlutter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <span>
#include <thread>

#include "shared/Core.hpp"
#include "window/cloth/ClothPlayback.hpp"
#include "window/cloth/ClothPrivate.hpp"
#include "window/shared/SheetFrame.hpp"

namespace hyprimate {
    namespace {
        using cloth_detail::FALLBACK_LENGTH;
        using cloth_detail::GRID;
        using cloth_detail::STEP_SECONDS;
        using cloth_detail::add;
        using cloth_detail::lerp;
        using cloth_detail::mul;
        using cloth_detail::rest_grid;
        using cloth_detail::sub;

        constexpr double SETTLE_FROM = 0.84;
        double smooth_derivative(double start, double end, double value) noexcept {
            const double PROGRESS = std::clamp((value - start) / (end - start), 0.0, 1.0);
            return 6 * PROGRESS * (1 - PROGRESS) / (end - start);
        }
    }  // namespace

    bool cloth_detail::consume_movement(
        Run &run, ClothFlutter::MovementPlayback &playback, double wanted) noexcept {
        const unsigned START = run.output_tail.load(std::memory_order_relaxed);
        unsigned tail = START;
        const unsigned HEAD = run.output_head.load(std::memory_order_acquire);
        const bool PROGRESSED = tail != HEAD && playback.b().time < wanted;
        double newest_time = playback.b().time;
        for (; tail != HEAD && newest_time < wanted; ++tail) {
            newest_time = run.snapshots.at(tail % Run::OUTPUT_CAPACITY).time;
        }
        const unsigned CONSUMED = tail - START;
        if (CONSUMED == 1) {
            playback.newest = 1 - playback.newest;
            playback.b() = run.snapshots.at(START % Run::OUTPUT_CAPACITY);
        } else if (CONSUMED > 1) {
            playback.a() = run.snapshots.at((tail - 2) % Run::OUTPUT_CAPACITY);
            playback.b() = run.snapshots.at((tail - 1) % Run::OUTPUT_CAPACITY);
        }
        run.output_tail.store(tail, std::memory_order_release);
        playback.time =
            std::min(wanted, playback.b().time);  // a starved worker holds the newest pose
        return PROGRESSED;
    }

    void ClothFlutter::Run::sample(
        SheetGrid &positions, double time_seconds, SheetGrid &velocities) const noexcept {
        const int COUNT = recorded.load(std::memory_order_acquire);
        if (COUNT < 2) {
            positions = start;
            velocities = velocity;
            return;
        }
        const double FRAME_POS =
            std::clamp(time_seconds / STEP_SECONDS, 0.0, static_cast<double>(COUNT - 1));
        const int FRAME_BEFORE = std::min(static_cast<int>(FRAME_POS), COUNT - 2);
        const double FRACTION = FRAME_POS - FRAME_BEFORE;
        const double FRACTION_SQUARED = FRACTION * FRACTION;
        const double FRACTION_CUBED = FRACTION_SQUARED * FRACTION;
        const double H00 = (2 * FRACTION_CUBED) - (3 * FRACTION_SQUARED) + 1;
        const double H10 = FRACTION_CUBED - (2 * FRACTION_SQUARED) + FRACTION;
        const double H01 = (-2 * FRACTION_CUBED) + (3 * FRACTION_SQUARED);
        const double H11 = FRACTION_CUBED - FRACTION_SQUARED;
        const double D00 = ((6 * FRACTION_SQUARED) - (6 * FRACTION)) / STEP_SECONDS;
        const double D10 = (3 * FRACTION_SQUARED) - (4 * FRACTION) + 1;
        const double D01 = ((-6 * FRACTION_SQUARED) + (6 * FRACTION)) / STEP_SECONDS;
        const double D11 = (3 * FRACTION_SQUARED) - (2 * FRACTION);
        const std::span<const Frame> PUBLISHED(samples.get(), static_cast<std::size_t>(COUNT));
        const auto FRAME_BEFORE_ITER = std::next(PUBLISHED.begin(), FRAME_BEFORE);
        const auto *before = FRAME_BEFORE_ITER->begin();
        const auto *after = std::next(FRAME_BEFORE_ITER)->begin();
        auto *velocity = velocities.begin();
        std::size_t vertex_index = 0;
        for (Vec3 &position : positions) {
            const auto &sample_before = *before++;
            const auto &sample_after = *after++;
            Vec3 &current_velocity = *velocity;
            ++velocity;
            const Vec3 POS_BEFORE{
                .x = sample_before.position.x,
                .y = sample_before.position.y,
                .z = sample_before.position.z,
            };
            const Vec3 VEL_BEFORE{
                .x = sample_before.velocity.x,
                .y = sample_before.velocity.y,
                .z = sample_before.velocity.z,
            };
            const Vec3 POS_AFTER{
                .x = sample_after.position.x,
                .y = sample_after.position.y,
                .z = sample_after.position.z,
            };
            const Vec3 VEL_AFTER{
                .x = sample_after.velocity.x,
                .y = sample_after.velocity.y,
                .z = sample_after.velocity.z,
            };
            if (vertex_index++ < GRID) {
                // Interpolation between solved pocket positions cannot overshoot an end stop or
                // reorder samples.
                position = lerp(POS_BEFORE, POS_AFTER, FRACTION);
                current_velocity = lerp(VEL_BEFORE, VEL_AFTER, FRACTION);
                continue;
            }
            position = add(add(mul(POS_BEFORE, H00), mul(VEL_BEFORE, H10 * STEP_SECONDS)),
                add(mul(POS_AFTER, H01), mul(VEL_AFTER, H11 * STEP_SECONDS)));
            current_velocity = add(add(mul(POS_BEFORE, D00), mul(VEL_BEFORE, D10)),
                add(mul(POS_AFTER, D01), mul(VEL_AFTER, D11)));
            const double EXTRA =
                std::clamp(time_seconds - ((COUNT - 1) * STEP_SECONDS), 0.0, STEP_SECONDS);
            position = add(position, mul(current_velocity, EXTRA));
        }
    }

    void ClothFlutter::sample_vertices(SheetGrid &positions, SheetGrid &velocities) const noexcept {
        if (!active()) {
            positions = rest_grid();
            velocities = {};
            return;
        }
        if (m_fallback) {
            const double PROGRESS = std::clamp(m_fallback_time / FALLBACK_LENGTH, 0.0, 1.0);
            const double PROGRESS_SQUARED = PROGRESS * PROGRESS;
            const double PROGRESS_CUBED = PROGRESS_SQUARED * PROGRESS;
            const SheetGrid REST = rest_grid();
            const auto *start = m_fallback->positions.begin();
            const auto *speed = m_fallback->velocities.begin();
            const auto *rest = REST.begin();
            auto *velocity = velocities.begin();
            std::size_t vertex_index = 0;
            for (Vec3 &position : positions) {
                const Vec3 START_POS = *start++;
                const Vec3 SPEED = *speed++;
                const Vec3 &rest_position = *rest++;
                Vec3 &current_velocity = *velocity;
                ++velocity;
                if (m_phase != Phase::Closing) {
                    if (vertex_index < GRID) {
                        // A common convex return keeps the sleeve ordered and inside the rod even
                        // without a worker.
                        position =
                            lerp(START_POS, rest_position, PROGRESS_SQUARED * (3 - (2 * PROGRESS)));
                        current_velocity = mul(sub(rest_position, START_POS),
                            6 * PROGRESS * (1 - PROGRESS) / FALLBACK_LENGTH);
                    } else {
                        position = add(
                            add(mul(START_POS, (2 * PROGRESS_CUBED) - (3 * PROGRESS_SQUARED) + 1),
                                mul(SPEED,
                                    (PROGRESS_CUBED - (2 * PROGRESS_SQUARED) + PROGRESS) *
                                        FALLBACK_LENGTH)),
                            mul(rest_position, (-2 * PROGRESS_CUBED) + (3 * PROGRESS_SQUARED)));
                        current_velocity = add(
                            add(mul(START_POS,
                                    ((6 * PROGRESS_SQUARED) - (6 * PROGRESS)) / FALLBACK_LENGTH),
                                mul(SPEED, (3 * PROGRESS_SQUARED) - (4 * PROGRESS) + 1)),
                            mul(rest_position,
                                ((-6 * PROGRESS_SQUARED) + (6 * PROGRESS)) / FALLBACK_LENGTH));
                    }
                    if (m_phase == Phase::Movement && m_movement) {
                        current_velocity = add(current_velocity, m_movement->rigid_velocity);
                    }
                } else {
                    position = add(START_POS,
                        mul(SPEED, FALLBACK_LENGTH * (PROGRESS - (0.5 * PROGRESS_SQUARED))));
                    current_velocity = mul(SPEED, 1 - PROGRESS);
                }
                ++vertex_index;
            }
            return;
        }
        if (m_phase == Phase::Movement && m_movement) {
            const auto &previous = m_movement->a();
            const auto &next = m_movement->b();
            const double INTERVAL = std::max(STEP_SECONDS, next.time - previous.time);
            const double BLEND =
                std::clamp((m_movement->time - previous.time) / INTERVAL, 0.0, 1.0);
            const Vec3 SNAPSHOT_RIGID = lerp(previous.rigid_velocity, next.rigid_velocity, BLEND);
            const auto *previous_sample_it = previous.values.begin();
            const auto *next_sample_it = next.values.begin();
            auto *velocity = velocities.begin();
            for (Vec3 &position : positions) {
                const auto &previous_sample = *previous_sample_it++;
                const auto &next_sample = *next_sample_it++;
                const Vec3 PREVIOUS_POS{
                    .x = previous_sample.position.x,
                    .y = previous_sample.position.y,
                    .z = previous_sample.position.z,
                };
                const Vec3 NEXT_POS{
                    .x = next_sample.position.x,
                    .y = next_sample.position.y,
                    .z = next_sample.position.z,
                };
                const Vec3 PREVIOUS_VEL{
                    .x = previous_sample.velocity.x,
                    .y = previous_sample.velocity.y,
                    .z = previous_sample.velocity.z,
                };
                const Vec3 NEXT_VEL{
                    .x = next_sample.velocity.x,
                    .y = next_sample.velocity.y,
                    .z = next_sample.velocity.z,
                };
                // Snapshot velocities carry the rigid motion of their own steps; the displayed
                // sheet moves with the latest capture, which can already have started or stopped.
                *velocity = add(sub(lerp(PREVIOUS_VEL, NEXT_VEL, BLEND), SNAPSHOT_RIGID),
                    m_movement->rigid_velocity);
                ++velocity;
                position = lerp(PREVIOUS_POS, NEXT_POS, BLEND);
            }
            return;
        }
        m_run->sample(positions, m_t, velocities);
        if (m_phase == Phase::Opening) {
            const double SETTLE_BLEND = smoothstep(SETTLE_FROM, 1, m_t / m_run->length);
            const double SETTLE_RATE =
                smooth_derivative(SETTLE_FROM, 1, m_t / m_run->length) / m_run->length;
            const auto REST = rest_grid();
            auto *velocity = velocities.begin();
            const auto *rest = REST.begin();
            for (Vec3 &position : positions) {
                *velocity =
                    add(mul(*velocity, 1 - SETTLE_BLEND), mul(sub(*rest, position), SETTLE_RATE));
                position = lerp(position, *rest, SETTLE_BLEND);
                ++velocity;
                ++rest;
            }
        }
        for (auto &speed : velocities) {
            speed = m_transition_held ? Vec3{} : mul(speed, m_rate);
        }
    }
    bool ClothFlutter::wait_for_recording(double timeout_seconds) const noexcept {
        if (!m_run || !std::isfinite(timeout_seconds) || timeout_seconds < 0) {
            return false;
        }
        const auto DEADLINE =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_seconds);
        while (!m_run->finished() && std::chrono::steady_clock::now() < DEADLINE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_run->recorded.load(std::memory_order_acquire) == m_run->frames;
    }
    bool ClothFlutter::wait_for_snapshots(int count, double timeout_seconds) const noexcept {
        if (!m_run || count < 0 || (!m_run->live && count > m_run->frames) ||
            !std::isfinite(timeout_seconds) || timeout_seconds < 0) {
            return false;
        }
        const auto DEADLINE =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_seconds);
        while (m_run->recorded.load(std::memory_order_acquire) < count && !m_run->finished() &&
               std::chrono::steady_clock::now() < DEADLINE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_run->recorded.load(std::memory_order_acquire) >= count;
    }
    void ClothFlutter::frame(Vec2 size, SheetFrame &out) const noexcept {
        if (!active()) {
            flat_sheet(out);
            return;
        }
        SheetGrid world;
        SheetGrid velocity;  // sample_vertices writes both
        sample_vertices(world, velocity);
        const double WIDTH = std::isfinite(size.x) && size.x > 0 ? size.x : 1;
        const double HEIGHT = std::isfinite(size.y) && size.y > 0 ? size.y : 1;
        const double LENGTH_SCALE = std::sqrt(WIDTH * HEIGHT);
        for (auto &position : world) {
            position = {
                .x = position.x * WIDTH,
                .y = (-position.y * HEIGHT) - (HEIGHT / 2),
                .z = position.z * LENGTH_SCALE,
            };
        }
        project_sheet(world, {.x = WIDTH, .y = HEIGHT}, m_params.shading, out, m_params.shine);
    }
}  // namespace hyprimate
