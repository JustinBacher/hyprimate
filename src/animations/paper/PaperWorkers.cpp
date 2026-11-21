#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <stop_token>
#include <system_error>
#include <utility>

#include "animations/paper/PaperCrumple.hpp"
#include "animations/paper/PaperWorkers.hpp"

namespace hyprimate {
    namespace {
        struct Load {
            Transition kind;
            PaperParams params;
            std::uint64_t seed;
            Vec2 size, cursor;
            std::shared_ptr<const PaperSurface> initial;
        };
        constexpr double MAX_BACKLOG = 0.05;
        // Publish between physical ticks. Waiting for a 60 Hz batch hides several completed
        // states when the last tick has many contacts, even on a 144 Hz output.
        constexpr double PUBLICATION_SLICE = 1.0 / 240.0;
    }  // namespace

    struct PaperRun {
        PaperCrumple model;  // exclusive to the worker holding busy
        PaperSurface writing, pending;
        Load load{};
        std::stop_source cancellation;
        std::uint64_t revision = 0;
        double remaining = 0;
        bool busy = false, begin = false, done = false, failed = false, published = false;
        bool runnable() const {
            return !busy && !cancellation.stop_requested() && !done && (begin || remaining > 0);
        }
    };

    PaperWorkers::PaperWorkers() {
        // A private compositor can replay a failing event without changing the public
        // configuration, or making successive events share a pattern. Normal sessions
        // keep the independently seeded animation-manager sequence.
        if (const char *value = std::getenv("HYPRIMATE_PAPER_REPLAY_SEED")) {
            std::uint64_t seed = 0;
            const char *end = value + std::strlen(value);
            const auto [position, error] = std::from_chars(value, end, seed);
            if (error == std::errc{} && position == end) {
                m_replay_seed = seed;
            }
        }
    }

    PaperWorkers::~PaperWorkers() {
        {
            const std::scoped_lock LOCK(m_mutex);
            for (const auto &run : m_runs) {
                run->cancellation.request_stop();
            }
        }
        for (auto &thread : m_threads) {
            thread.request_stop();
        }
        m_changed.notify_all();
        // jthreads join before the queue and its mutex are destroyed.
    }

    bool PaperWorkers::begin(std::shared_ptr<PaperRun> &run,
        Transition kind,
        PaperParams params,
        std::uint64_t seed,
        Vec2 size,
        Vec2 cursor,
        std::shared_ptr<const PaperSurface> initial) {
        // Only the event thread adds/removes queue entries. Start one worker per actual
        // simultaneous window, leaving CPU headroom for Hyprland and other clients.
        static const auto LIMIT = std::clamp(std::thread::hardware_concurrency() / 3U, 1U, 4U);
        const auto LOAD_COUNT = m_runs.size() + (run ? 0U : 1U);
        const auto WANTED = std::min<std::size_t>(LOAD_COUNT, LIMIT);
        if (m_threads.size() < WANTED) {
            try {
                m_threads.reserve(LIMIT);
                while (m_threads.size() < WANTED) {
                    m_threads.emplace_back([this](const std::stop_token &stop) { work(stop); });
                }
            } catch (const std::bad_alloc &) {
                // Existing workers remain available even if the pool cannot grow.
                if (m_threads.empty()) return false;
            } catch (const std::system_error &) {
                // A process/thread limit must not escape through Hyprland's map callback.
                if (m_threads.empty()) return false;
            }
        }
        const std::scoped_lock LOCK(m_mutex);
        if (!run) {
            try {
                auto next = std::make_shared<PaperRun>();
                m_runs.push_back(next);
                run = std::move(next);
            } catch (const std::bad_alloc &) {
                return false;
            }
        }
        if (params.simulation_quality == PaperQuality::Adaptive &&
            (size.x * size.y < 160000.0 ||
                LOAD_COUNT > std::min<std::size_t>(m_threads.size(), 3))) {
            params.simulation_quality = PaperQuality::Low;
        }
        if (m_replay_seed) {
            seed = (*m_replay_seed)++;
        }
        run->load = {kind, params, seed, size, cursor, std::move(initial)};
        ++run->revision;
        run->remaining = 0;
        run->published = run->done = run->failed = false;
        run->begin = true;
        m_changed.notify_one();
        return true;
    }

    PaperPlayback PaperWorkers::present(
        const std::shared_ptr<PaperRun> &run, double elapsed, PaperSurface &display) {
        if (!run) {
            return PaperPlayback::Completed;
        }
        if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > STALL_SECONDS) {
            return PaperPlayback::Interrupted;
        }
        const std::scoped_lock LOCK(m_mutex);
        if (run->failed) {
            return PaperPlayback::Interrupted;
        }
        if (run->published) {
            std::swap(display, run->pending);
            run->published = false;
        }
        if (run->done) {
            return PaperPlayback::Completed;
        }
        // Backpressure stretches time instead of skipping contact steps or blocking a compositor
        // frame.
        run->remaining = std::min(run->remaining + elapsed, MAX_BACKLOG);
        if (run->runnable()) {
            m_changed.notify_one();
        }
        return PaperPlayback::Running;
    }

    void PaperWorkers::cancel(std::shared_ptr<PaperRun> &run) noexcept {
        if (!run) {
            return;
        }
        {
            const std::scoped_lock LOCK(m_mutex);
            run->cancellation.request_stop();
            std::erase(m_runs, run);
            if (m_next >= m_runs.size()) {
                m_next = 0;
            }
        }
        run.reset();  // a worker finishing a cancelled quantum retains its own reference
    }

    void PaperWorkers::work(const std::stop_token &stop) {
        std::unique_lock lock(m_mutex);
        while (!stop.stop_requested()) {
            const auto READY = [&] {
                return std::ranges::any_of(
                    m_runs, [](const auto &item) { return item->runnable(); });
            };
            if (!m_changed.wait(lock, stop, READY)) {
                return;
            }
            if (stop.stop_requested()) {
                return;
            }
            while (!m_runs[m_next]->runnable()) {
                m_next = (m_next + 1) % m_runs.size();
            }
            auto run = m_runs[m_next];
            m_next = (m_next + 1) % m_runs.size();
            run->busy = true;
            const auto REVISION = run->revision;
            const auto BEGIN = std::exchange(run->begin, false);
            const auto LOAD = BEGIN ? std::optional{std::move(run->load)} : std::nullopt;
            const double DELTA = BEGIN ? 0.0 : std::min(run->remaining, PUBLICATION_SLICE);
            run->remaining -= DELTA;
            lock.unlock();
            bool active = false;
            bool failed = false;
            try {
                if (LOAD) {
                    run->model.begin(LOAD->kind,
                        LOAD->params,
                        LOAD->seed,
                        LOAD->size,
                        LOAD->cursor,
                        run->cancellation.get_token(),
                        LOAD->initial.get());
                    active = run->model.active();
                    failed = !active;
                } else {
                    active = run->model.advance(DELTA);
                }
                if (!run->cancellation.stop_requested()) {
                    run->model.frame(run->writing);
                }
            } catch (...) {
                // A failed allocation/invalid mesh ends only this effect; the caller releases its
                // handle.
                active = false;
                failed = true;
            }
            lock.lock();
            run->busy = false;
            if (!run->cancellation.stop_requested() && run->revision == REVISION) {
                std::swap(run->writing, run->pending);
                run->published = true;
                run->failed = failed;
                run->done = !active;
            }
            if (run->runnable()) {
                m_changed.notify_one();
            }
            // Never destroy a cancelled solver with the scheduler lock held.
            lock.unlock();
            run.reset();
            lock.lock();
        }
    }
}  // namespace hyprimate
