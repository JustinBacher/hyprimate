#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "window/paper/PaperSurface.hpp"

namespace hyprimate {

    struct PaperRun;
    enum class PaperPlayback { Running, Completed, Interrupted };
    // Only the render/event thread calls start, present and cancel. The workers never access GL,
    // a window, or style-owned state; their only product is a completed material-space surface.
    class PaperWorkers {
      public:
        PaperWorkers();
        ~PaperWorkers();
        PaperWorkers(const PaperWorkers &) = delete;
        PaperWorkers(PaperWorkers &&) = delete;
        PaperWorkers &operator=(PaperWorkers &&) = delete;
        PaperWorkers &operator=(const PaperWorkers &) = delete;
        [[nodiscard]] bool begin(std::shared_ptr<PaperRun> &run,
            Transition kind,
            PaperParams params,
            std::uint64_t seed,
            Vec2 size,
            Vec2 cursor,
            std::shared_ptr<const PaperSurface> initial = {});
        PaperPlayback present(
            const std::shared_ptr<PaperRun> &run, double elapsed, PaperSurface &display);
        void cancel(std::shared_ptr<PaperRun> &run) noexcept;

      private:
        void work(const std::stop_token &stop);
        std::optional<std::uint64_t> m_replay_seed;
        std::mutex m_mutex;
        std::condition_variable_any m_changed;
        std::vector<std::shared_ptr<PaperRun>> m_runs;
        std::size_t m_next = 0;
        std::vector<std::jthread> m_threads;
    };
}  // namespace hyprimate
