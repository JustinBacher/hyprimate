// Time the real worker/publication path, independently of render/driver/vsync. A new buffer
// address means the compositor received a pose; repeated presentations do not count as motion.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "window/paper/PaperWorkers.hpp"

namespace {
    using namespace hyprimate;
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::duration<double, std::milli>;

    struct Sample {
        std::shared_ptr<PaperRun> run;
        PaperSurface surface;
        int poses = 0;
        double first_ms = 0, finish_ms = 0, last_ms = 0;
        std::vector<double> gaps;
    };

    bool measure(PaperWorkers &workers, unsigned hz, std::size_t count, Transition kind) {
        std::vector<Sample> windows(count);
        std::vector<double> submission;
        const auto started = Clock::now();
        const double dt = 1.0 / hz;
        for (std::size_t index = 0; index < count; ++index) {
            PaperParams params;
            params.duration = kind == Transition::Open ? .6 : .5;
            params.fold_density = 1;
            if (!workers.begin(
                    windows[index].run, kind, params, 441 + index, {1280, 720}, {.5, .5})) {
                for (auto &window : windows) {
                    workers.cancel(window.run);
                }
                return false;
            }
        }
        auto last = started;
        for (unsigned frame = 1; frame < 10 * hz; ++frame) {
            std::this_thread::sleep_until(started + std::chrono::duration<double>(frame * dt));
            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - last).count();
            const double time = Milliseconds(now - started).count();
            last = now;
            std::size_t active = 0;
            for (auto &window : windows) {
                if (!window.run) {
                    continue;
                }
                const auto *previous = window.surface.points.data();
                const auto status = workers.present(window.run, elapsed, window.surface);
                if (window.surface.points.data() != previous && !window.surface.points.empty()) {
                    ++window.poses;
                    if (window.first_ms == 0) {
                        window.first_ms = time;
                    } else {
                        window.gaps.push_back(time - window.last_ms);
                    }
                    window.last_ms = time;
                }
                if (status != PaperPlayback::Running) {
                    workers.cancel(window.run);
                    window.finish_ms = status == PaperPlayback::Completed ? time : 0;
                } else {
                    ++active;
                }
            }
            submission.push_back(Milliseconds(Clock::now() - now).count());
            if (active == 0) {
                break;
            }
        }
        std::ranges::sort(submission);
        bool passed = true;
        for (std::size_t i = 0; i < count; ++i) {
            std::ranges::sort(windows[i].gaps);
            const auto &window = windows[i];
            std::printf("%s,%u,%zu,%zu,%d,%.2f,%.2f,%.2f,%.2f,%.3f\n",
                kind == Transition::Open ? "open" : "close",
                hz,
                count,
                i,
                window.poses,
                window.first_ms,
                window.finish_ms,
                window.gaps.empty() ? 0 : window.gaps[(window.gaps.size() - 1) * 95 / 100],
                window.gaps.empty() ? 0 : window.gaps.back(),
                submission[(submission.size() - 1) * 95 / 100]);
            passed &= window.poses > 1 && window.finish_ms > 0;
        }
        for (auto &window : windows) {
            workers.cancel(window.run);
        }
        std::fflush(stdout);
        return passed;
    }
}  // namespace

int main() {
    PaperWorkers workers;
    std::puts(
        "event,hz,windows,index,poses,first_ms,finish_ms,p95_pose_gap_ms,max_pose_gap_ms,total_"
        "present_p95_ms");
    bool passed = true;
    for (const auto hz : {60U, 120U, 144U}) {
        for (const auto count : {1U, 4U}) {
            for (const auto kind : {Transition::Close, Transition::Open}) {
                passed &= measure(workers, hz, count, kind);
            }
        }
    }
    return passed ? 0 : 1;
}
