#include "window/cloth/ClothWorkers.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "perf/LiveProfiler.hpp"
#include "window/cloth/ClothPrivate.hpp"
#include "window/cloth/ClothSimulation.hpp"

namespace hyprimate {
    namespace cloth_detail {
        // Workers share this test pause flag and threshold.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic<bool> s_pause{false};
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic<int> s_pause_after{0};

        struct Lease {
            Lease() = default;
            Lease(const Lease &) = delete;
            Lease &operator=(const Lease &) = delete;
            Lease(Lease &&) = delete;
            Lease &operator=(Lease &&) = delete;

            std::shared_ptr<Run> run;
            ~Lease() {
                if (run) {
                    run->request_stop();
                    run->released.store(true, std::memory_order_release);
                }
            }
        };
    }  // namespace cloth_detail

    namespace {
        using cloth_detail::Run;
        using cloth_detail::TerminalReason;

        class Registry {
          public:
            // NOLINTNEXTLINE(performance-unnecessary-value-param): A reference adds a dependent
            // load per reaper poll in GCC -O2.
            Registry() : reaper([this](std::stop_token stop) { reap(stop); }) {
            }
            Registry(const Registry &) = delete;
            Registry &operator=(const Registry &) = delete;
            Registry(Registry &&) = delete;
            Registry &operator=(Registry &&) = delete;
            ~Registry() {
                reaper.request_stop();
                changed.notify_all();
                reaper.join();
                for (const auto &run : runs) {
                    run->request_stop();
                }
                for (const auto &run : runs) {
                    if (run->worker.joinable()) {
                        run->worker.join();
                    }
                }
                runs.clear();
            }
            template <typename Work>
            void launch(const std::shared_ptr<Run> &run, Work &&work) {
                const std::scoped_lock LOCK(mutex);
                runs.push_back(run);
                try {
                    // A terminal reason can be published before the thread constructor returns.
                    // Serialize member publication with the reaper's inspection.
                    run->worker = std::thread([run, work = std::forward<Work>(work)] {
                        auto reason = TerminalReason::SolverFailure;
                        try {
                            reason = work(*run);
                        } catch (...) {
                            // Any unexpected exception terminates this attempt, not the runtime.
                        }
                        if (reason != TerminalReason::Completed && run->stop.stop_requested()) {
                            reason = TerminalReason::Cancelled;
                        }
                        run->terminal.store(reason, std::memory_order_release);
                    });
                } catch (...) {
                    runs.pop_back();
                    throw;
                }
                changed.notify_one();
            }

          private:
            void reap(const std::stop_token &stop) {
                std::unique_lock lock(mutex);
                while (!stop.stop_requested()) {
                    changed.wait_for(lock, std::chrono::milliseconds(2));
                    for (auto iterator = runs.begin(); iterator != runs.end();) {
                        // Both the worker closure and the releasing Lease may still own Run after
                        // publishing their flags. The registry must be the sole owner before it
                        // can move the last reference to the reaper thread.
                        if (!(*iterator)->finished() ||
                            !(*iterator)->released.load(std::memory_order_acquire) ||
                            iterator->use_count() != 1) {
                            ++iterator;
                            continue;
                        }
                        auto done = std::move(*iterator);
                        iterator = runs.erase(iterator);
                        lock.unlock();
                        if (done->worker.joinable()) {
                            done->worker.join();
                        }
                        {
                            const perf::CpuScope PROFILE{
                                {perf::Domain::Window,
                                    "cloth",
                                    done->live
                                        ? "move"
                                        : (done->kind == Transition::Close ? "close" : "open")},
                                perf::Phase::Worker};
                            done.reset();  // Free the recording off the playback thread.
                        }
                        lock.lock();
                        iterator = runs.begin();  // add() may have reallocated the vector while the
                                                  // lock was released
                    }
                }
            }
            std::mutex mutex;
            std::condition_variable changed;
            std::vector<std::shared_ptr<Run>> runs;
            std::jthread reaper;
        };
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): Physics lifetime owns
        // the shared worker reaper.
        std::unique_ptr<Registry> s_registry;
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): Serializes worker
        // runtime ownership and admission.
        std::mutex s_lifecycle_mutex;
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): Protected by
        // s_lifecycle_mutex.
        unsigned s_physics_owners = 0;
    }  // namespace

    namespace cloth_detail {
        WorkerAdmission::WorkerAdmission() : m_lock(s_lifecycle_mutex) {
        }

        bool WorkerAdmission::available() const noexcept {
            return bool(s_registry);
        }

        std::shared_ptr<Lease> WorkerAdmission::launch_transition(
            const std::shared_ptr<Run> &run, SimInput &&input) const {
            auto lease = std::make_shared<Lease>();
            lease->run = run;
            s_registry->launch(
                run, [input = std::move(input)](Run &run) { return simulate(run, input); });
            return lease;
        }

        std::shared_ptr<Lease> WorkerAdmission::launch_movement(
            const std::shared_ptr<Run> &run, double phase) const {
            auto lease = std::make_shared<Lease>();
            lease->run = run;
            s_registry->launch(run, [phase](Run &run) { return simulate_movement(run, phase); });
            return lease;
        }
    }  // namespace cloth_detail

    ClothPhysics::ClothPhysics() {
        const std::scoped_lock LOCK(s_lifecycle_mutex);
        if (s_physics_owners != 0) {
            ++s_physics_owners;
            return;
        }
        s_registry = std::make_unique<Registry>();
        s_physics_owners = 1;
    }
    ClothPhysics::~ClothPhysics() {
        const std::scoped_lock LOCK(s_lifecycle_mutex);
        if (--s_physics_owners != 0) {
            return;
        }
        s_registry.reset();
        cloth_detail::s_pause.store(false, std::memory_order_release);
    }
    void ClothPhysics::pause_workers_for_testing(bool pause, int after_snapshots_count) noexcept {
        cloth_detail::s_pause_after.store(
            std::max(0, after_snapshots_count), std::memory_order_relaxed);
        cloth_detail::s_pause.store(pause, std::memory_order_release);
    }
}  // namespace hyprimate
