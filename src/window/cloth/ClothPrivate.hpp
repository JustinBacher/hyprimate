#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>
#include <type_traits>

#include "window/cloth/ClothFlutter.hpp"

namespace hyprimate {
    namespace cloth_detail {
        inline constexpr int GRID = SHEET_CELLS + 1;
        inline constexpr double STEP_SECONDS = 1.0 / 120.0;
        inline constexpr double OPEN_LENGTH = 1.4;
        inline constexpr double GRAVITY = 9.81;
        inline constexpr double FALLBACK_LENGTH = 0.18;

        enum class TerminalReason : std::uint8_t {
            Running,
            Completed,
            Cancelled,
            QueueOverflow,
            SolverFailure,
            RecordingFailure,
            RecordingAllocationFailure,
        };

        inline Vec3 add(Vec3 left, Vec3 right) noexcept {
            return {.x = left.x + right.x, .y = left.y + right.y, .z = left.z + right.z};
        }
        inline Vec3 mul(Vec3 vector, double factor) noexcept {
            return {.x = vector.x * factor, .y = vector.y * factor, .z = vector.z * factor};
        }
        inline Vec3 sub(Vec3 left, Vec3 right) noexcept {
            return add(left, mul(right, -1));
        }
        inline Vec3 lerp(Vec3 start, Vec3 end, double fraction) noexcept {
            return add(mul(start, 1 - fraction), mul(end, fraction));
        }
        inline SheetGrid rest_grid() noexcept {
            SheetGrid out{};
            auto *vertex = out.begin();
            for (int j = 0; j < GRID; ++j) {
                for (int i = 0; i < GRID; ++i) {
                    *vertex = {
                        .x = (static_cast<double>(i) / SHEET_CELLS) - 0.5,
                        .y = -static_cast<double>(j) / SHEET_CELLS,
                        .z = 0,
                    };
                    ++vertex;
                }
            }
            return out;
        }
    }  // namespace cloth_detail

    struct ClothFlutter::Run {
        struct Float3 {
            float x, y, z;
        };
        struct VertexSample {
            Float3 position, velocity;
        };
        using Frame = std::array<VertexSample, SHEET_GRID_VERTICES>;
        static_assert(std::is_standard_layout_v<Float3> && std::is_standard_layout_v<VertexSample>);
        static_assert(sizeof(Float3) == 3 * sizeof(float) && alignof(Float3) == alignof(float));
        static_assert(offsetof(Float3, x) == 0 && offsetof(Float3, y) == sizeof(float) &&
                      offsetof(Float3, z) == 2 * sizeof(float));
        static_assert(
            sizeof(VertexSample) == 6 * sizeof(float) && alignof(VertexSample) == alignof(float));
        static_assert(offsetof(VertexSample, position) == 0 &&
                      offsetof(VertexSample, velocity) == 3 * sizeof(float));
        static_assert(sizeof(Frame) == SHEET_GRID_VERTICES * 6 * sizeof(float) &&
                      alignof(Frame) == alignof(float));
        struct Target {
            double time = 0;
            Box box{};
            ClothMovementParams movement{};
        };
        struct Snapshot {
            double time = 0;
            Vec3 rigid_velocity{};
            Frame values{};
        };
        // Buffer a full stall interval of targets at up to 1000 Hz, and the resulting 120 Hz
        // snapshot burst.
        static constexpr unsigned INPUT_CAPACITY = 256, OUTPUT_CAPACITY = 40;
        bool live = false;
        ClothMovementParams movement{};
        Vec3 publish_velocity{};  // rigid motion omitted by the strength-scaled translating
                                  // simulation frame
        Vec3 snapshot_rigid_velocity{};
        std::array<Target, INPUT_CAPACITY> targets{};
        std::array<Snapshot, OUTPUT_CAPACITY> snapshots{};
        std::atomic<unsigned> input_head{0}, input_tail{0}, output_head{0}, output_tail{0};
        std::atomic<bool> overflow{false};
        Transition kind = Transition::Open;
        Edge edge = Edge::Bottom;
        std::uint64_t seed = 0;
        Box window{}, screen{};
        double length = cloth_detail::OPEN_LENGTH;
        double max_initial_speed = 0;
        Vec3 travel{};
        int frames = 0;
        SheetGrid start{}, velocity{};
        // Every six-float position/velocity slot is written once, then published by recorded's
        // release store.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): Recording
        // length is determined at runtime.
        std::unique_ptr<Frame[]> samples;
        std::atomic<int> recorded{0}, outside_frame{0};
        // The worker publishes the terminal reason once; retained playbacks share the report.
        std::atomic<cloth_detail::TerminalReason> terminal{cloth_detail::TerminalReason::Running};
        std::atomic<bool> report_closed{false}, released{false};
        std::stop_source stop;
        std::thread worker;  // only the registry reaper joins it

        [[nodiscard]] bool finished() const noexcept {
            return terminal.load(std::memory_order_acquire) != cloth_detail::TerminalReason::Running;
        }

        void request_stop() noexcept {
            // A cancellation and the playback's report claim are ordered on the same flag.
            // A claim that already won may still be reported after this stop request.
            report_closed.store(true, std::memory_order_relaxed);
            stop.request_stop();
        }

        [[nodiscard]] static bool pinned(int row) noexcept {
            return row == 0;
        }
        void sample(
            SheetGrid &positions, double time_seconds, SheetGrid &velocities) const noexcept;
    };

    struct ClothFlutter::MovementPlayback {
        std::array<Run::Snapshot, 2> snapshots{};
        unsigned newest = 1;
        Run::Snapshot &a() noexcept {
            return snapshots[1 - newest];
        }
        Run::Snapshot &b() noexcept {
            return snapshots[newest];
        }
        const Run::Snapshot &a() const noexcept {
            return snapshots[1 - newest];
        }
        const Run::Snapshot &b() const noexcept {
            return snapshots[newest];
        }
        Box target{}, previous{};
        ClothMovementParams params{};
        Vec3 rigid_velocity{};
        double time = 0, last_motion = 0;
        bool producer = true, settling = false;
    };

    struct ClothFlutter::Fallback {
        SheetGrid positions{}, velocities{};  // real-time velocity
        double alpha = 1;
    };

    namespace cloth_detail {
        using Run = ClothFlutter::Run;
    }  // namespace cloth_detail
}  // namespace hyprimate
