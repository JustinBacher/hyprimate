#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include "shader/HyprEnvironmentInteractions.hpp"
#include "window/hyprland/HyprWindowPort.hpp"

namespace hyprimate::shader {
    namespace {
        using Clock = std::chrono::steady_clock;
        constexpr std::size_t MAX_TRACKED_OUTPUTS = 32;
        constexpr std::uint64_t MAX_EXACT_FLOAT_ID = 0x00ff'ffffULL;

        struct Box {
            double x = 0, y = 0, w = 0, h = 0;
        };

        struct Snapshot {
            Box body;
            PHLWORKSPACEREF workspace;
            PHLMONITORREF owner;
        };

        struct OutputHistory {
            PHLMONITORREF monitor;
            Box previous;
            Clock::time_point sampled{};
            bool used = false;
            bool previous_valid = false;
            bool open_delivered = false;
            bool close_delivered = false;
        };

        struct Track {
            PHLWINDOWREF window;
            std::array<OutputHistory, MAX_TRACKED_OUTPUTS> outputs{};
            Box close_body;
            PHLWORKSPACEREF workspace;
            PHLMONITORREF owner;
            std::uint64_t token = 0, last_active_sample = 0;
            float opaque_id = 0;
            bool used = false;
            bool open_pending = false;
            bool closed = false;
            bool close_body_valid = false;
            bool destroyed = false;
            bool workspace_known = false;
            bool priority = false, held = false;
        };

        struct OutputEpoch {
            PHLMONITORREF monitor;
            std::uint64_t epoch = 0;
            std::uint64_t privacy_epoch = 0;
            bool used = false;
        };

        bool usable_output(const PHLMONITOR &monitor) {
            if (!monitor || !monitor->m_enabled || !monitor->m_dpmsStatus || monitor->isMirror()) {
                return false;
            }
            const auto box = monitor->logicalBox();
            return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) &&
                   std::isfinite(box.h) && box.w > 0 && box.h > 0;
        }

        bool unlocked() {
            return g_pSessionLockManager && !g_pSessionLockManager->isSessionLocked();
        }

        bool usable_box(const Box &box) {
            return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) &&
                   std::isfinite(box.h) && box.w > 0 && box.h > 0;
        }

        bool overlaps(const Box &lhs, const Box &rhs) {
            return lhs.x < rhs.x + rhs.w && rhs.x < lhs.x + lhs.w &&
                   lhs.y < rhs.y + rhs.h && rhs.y < lhs.y + lhs.h;
        }

        Box sweep_box(const Box &previous, const Box &current) {
            const double left = std::min(previous.x, current.x);
            const double top = std::min(previous.y, current.y);
            const double right = std::max(previous.x + previous.w, current.x + current.w);
            const double bottom = std::max(previous.y + previous.h, current.y + current.h);
            return {left, top, right - left, bottom - top};
        }

        bool same_box(const Box &lhs, const Box &rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
        }

        Box presented_body(const PHLWINDOW &window, const Render::SWindowRenderPresentation &presentation) {
            const auto body = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
            return {body.x + presentation.workspaceOffset.x + presentation.floatingOffset.x,
                body.y + presentation.workspaceOffset.y + presentation.floatingOffset.y,
                body.w,
                body.h};
        }

        bool resize_mode(eMouseBindMode mode) {
            return mode == MBIND_RESIZE || mode == MBIND_RESIZE_FORCE_RATIO ||
                   mode == MBIND_RESIZE_BLOCK_RATIO;
        }

        bool pinned(const PHLWINDOW &window) {
            return static_cast<bool>(window->m_state & Desktop::View::WINDOW_STATE_PINNED);
        }

        bool visible_for_monitor(const PHLWINDOW &window,
            const PHLMONITOR &owner,
            const Render::SWindowRenderPresentation &presentation) {
            const auto workspace = window->m_workspace;
            return window->mapped() && !window->isHidden() && presentation.alphaVisible &&
                   (pinned(window) || !workspace || workspace->visible()) && owner;
        }

        bool is_no_screen_share(const PHLWINDOW &window) {
            return window && window->m_ruleApplicator &&
                   window->m_ruleApplicator->noScreenShare().valueOrDefault();
        }


        std::array<float, 4> normalized(const Box &box, const Box &output) {
            return {static_cast<float>((box.x - output.x) / output.w),
                static_cast<float>((box.y - output.y) / output.h),
                static_cast<float>(box.w / output.w),
                static_cast<float>(box.h / output.h)};
        }
    }  // namespace

    class HyprEnvironmentInteractions::Impl {
      public:
        explicit Impl(CHyprWindowPort &port) : m_port(port) {
            const auto &bus = ::Event::bus();
            m_open = bus->m_events.window.openLate.listen([this](const PHLWINDOW &window) {
                if (m_active && window) {
                    admit_open(window);
                }
            });
            m_close = bus->m_events.window.close.listen([this](const PHLWINDOW &window) {
                if (m_active && window) {
                    admit_close(window);
                }
            });
            m_destroy = bus->m_events.window.destroy.listen([this](const PHLWINDOWREF &window) {
                if (m_active) {
                    mark_destroyed(window);
                }
            });
            m_layout = bus->m_events.monitor.layoutChanged.listen([this] { clear(); });
            if (g_pSessionLockManager) {
                m_lock = g_pSessionLockManager->m_events.lock.listen([this] { clear(); });
            }
        }

        void clear() {
            for (auto &track : m_tracks) {
                track = {};
            }
            m_active = false;
            bump_epoch();
        }

        bool privacy_changed(const PHLMONITOR &monitor) {
            if (!m_active || !usable_output(monitor) || !unlocked()) {
                return false;
            }
            bool revoked = false;
            for (auto &track : m_tracks) {
                if (!track.used) {
                    continue;
                }
                const auto window = track.window.lock();
                if (is_no_screen_share(window)) {
                    track = {};
                    revoked = true;
                }
            }
            if (revoked) {
                bump_privacy_epoch();
            }
            auto *output = epoch_for(monitor);
            if (!output || output->privacy_epoch == m_privacy_epoch) {
                return false;
            }
            output->privacy_epoch = m_privacy_epoch;
            return true;
        }

        HyprEnvironmentInteractions::Batch collect(const PHLMONITOR &monitor) {
            HyprEnvironmentInteractions::Batch batch{};
            if (!usable_output(monitor)) {
                return batch;
            }
            if (!unlocked()) {
                if (m_active) {
                    clear();
                }
                batch.reset = true;
                return batch;
            }

            m_active = true;
            if (++m_sample_serial == 0) {
                ++m_sample_serial;
            }
            auto *output_epoch = epoch_for(monitor);
            if (!output_epoch || output_epoch->epoch != m_epoch) {
                batch.reset = true;
                if (output_epoch) {
                    output_epoch->epoch = m_epoch;
                }
            }

            const auto output_box_native = monitor->logicalBox();
            const Box output_box{output_box_native.x, output_box_native.y,
                output_box_native.w, output_box_native.h};
            const auto now = Clock::now();
            const auto &windows = Desktop::windowState()->windows();

            for (const auto &window : windows) {
                if (!window) {
                    continue;
                }
                auto *track = find(window);
                const bool no_screen_share = is_no_screen_share(window);
                if (no_screen_share) {
                    if (track) {
                        bump_privacy_epoch();
                        *track = {};
                    }
                    continue;
                }

                const auto owner = window->m_monitor.lock();
                std::optional<Snapshot> snapshot;
                if (owner) {
                    const auto presentation = window->presentation().renderPresentation(Render::eSceneMode::MONITOR);
                    if (visible_for_monitor(window, owner, presentation)) {
                        const auto body = presented_body(window, presentation);
                        if (usable_box(body)) {
                            snapshot = Snapshot{body, PHLWORKSPACEREF{window->m_workspace},
                                PHLMONITORREF{owner}};
                        }
                    }
                }
                if (!snapshot) {
                    if (track && !track->closed) {
                        *track = {};
                    }
                    continue;
                }

                bool held = false;
                bool resize_held = false;
                bool native_move = window->positionAnimation()->isBeingAnimated();
                bool native_resize = window->sizeAnimation()->isBeingAnimated();
                if (g_layoutManager) {
                    const auto &drag = g_layoutManager->dragController();
                    const auto target = drag ? drag->target() : nullptr;
                    if (target && target->window() == window) {
                        const auto mode = drag->mode();
                        held = mode == MBIND_MOVE || resize_mode(mode);
                        resize_held = resize_mode(mode);
                    }
                }

                if (!track) {
                    track = admit(window, false, held || native_move || native_resize);
                }
                if (!track || track->closed) {
                    continue;
                }

                const auto workspace = snapshot->workspace.lock();
                const auto prior_workspace = track->workspace.lock();
                const auto owner_monitor = snapshot->owner.lock();
                const auto prior_owner = track->owner.lock();
                const bool workspace_changed = track->workspace_known &&
                    prior_workspace != workspace;
                const bool held_seam_transfer = workspace_changed && held &&
                    prior_workspace && workspace && prior_workspace->visible() &&
                    workspace->visible() && prior_owner && owner_monitor &&
                    prior_owner->m_enabled && owner_monitor->m_enabled &&
                    prior_owner != owner_monitor;
                if (workspace_changed && !held_seam_transfer) {
                    invalidate(*track);
                    track->open_pending = false;
                    track->held = held;
                    track->workspace = snapshot->workspace;
                    track->owner = snapshot->owner;
                    continue;
                }
                track->workspace = snapshot->workspace;
                track->owner = snapshot->owner;
                track->workspace_known = true;
                track->window = PHLWINDOWREF{window};

                auto *history = output_for(*track, monitor);
                if (!history) {
                    continue;
                }
                const Box previous = history->previous_valid ? history->previous : snapshot->body;
                const bool moved = !same_box(previous, snapshot->body);
                const bool resized = previous.w != snapshot->body.w || previous.h != snapshot->body.h;
                const bool opening = track->open_pending && !history->open_delivered;
                const bool active_motion = held || native_move || native_resize || moved;
                track->held = held;
                if (active_motion) {
                    track->priority = true;
                    track->last_active_sample = m_sample_serial;
                }
                Event event = Event::Open;
                bool emit = false;
                if (opening) {
                    event = Event::Open;
                    emit = true;
                } else if (active_motion) {
                    event = (resize_held || native_resize || resized) ? Event::Resize : Event::Move;
                    emit = true;
                }

                if (emit && batch.count < MAX_INTERACTIONS) {
                    const Box travel = history->previous_valid ? sweep_box(previous, snapshot->body) : snapshot->body;
                    if (overlaps(travel, output_box)) {
                        const auto current = normalized(snapshot->body, output_box);
                        const auto old = normalized(previous, output_box);
                        auto &record = batch.records[batch.count];
                        record[0] = current;
                        record[1] = old;
                        const double seconds = history->previous_valid
                            ? std::chrono::duration<double>(now - history->sampled).count()
                            : 0.0;
                        if (seconds > 0 && std::isfinite(seconds)) {
                            for (std::size_t i = 0; i < 4; ++i) {
                                record[2][i] = static_cast<float>((current[i] - old[i]) / seconds);
                            }
                        }
                        record[3] = {static_cast<float>(event), 1.F,
                            track->opaque_id, held ? 1.F : 0.F};
                        batch.windows[batch.count] = track->token;
                        ++batch.count;
                        if (opening) {
                            history->open_delivered = true;
                        }
                    }
                }

                history->previous = snapshot->body;
                history->sampled = now;
                history->previous_valid = true;
            }

            emit_closes(monitor, output_box, now, batch);
            if (output_epoch && output_epoch->epoch != m_epoch) {
                batch.reset = true;
                output_epoch->epoch = m_epoch;
            }
            retire_closed_tracks();
            return batch;
        }

      private:
        Track *find(const PHLWINDOW &window) {
            for (auto &track : m_tracks) {
                if (track.used && track.window.lock() == window) {
                    return &track;
                }
            }
            return nullptr;
        }

        Track *free_track(bool priority) {
            for (auto &track : m_tracks) {
                if (!track.used || (track.closed && track.destroyed)) {
                    return &track;
                }
            }
            if (!priority) {
                return nullptr;
            }
            Track *candidate = nullptr;
            for (auto &track : m_tracks) {
                if (track.held || track.closed) {
                    continue;
                }
                if (!track.priority) {
                    candidate = &track;
                    break;
                }
                if (!candidate || track.last_active_sample < candidate->last_active_sample) {
                    candidate = &track;
                }
            }
            if (candidate) {
                *candidate = {};
            }
            return candidate;
        }

        float next_opaque_id() {
            for (std::size_t attempt = 0; attempt <= MAX_INTERACTIONS; ++attempt) {
                const auto candidate = m_next_opaque_id;
                m_next_opaque_id = candidate >= MAX_EXACT_FLOAT_ID ? 1U : candidate + 1U;
                const bool in_use = std::ranges::any_of(m_tracks, [candidate](const auto &track) {
                    return track.used && track.opaque_id == static_cast<float>(candidate);
                });
                if (!in_use) {
                    return static_cast<float>(candidate);
                }
            }
            return 0;
        }

        Track *admit(const PHLWINDOW &window, bool opened, bool priority) {
            if (!window) {
                return nullptr;
            }
            auto *track = free_track(priority);
            if (!track) {
                return nullptr;
            }
            const auto token = m_port.identity(PHLWINDOWREF{window});
            const auto opaque_id = next_opaque_id();
            if (token == 0 || opaque_id == 0) {
                return nullptr;
            }
            *track = {};
            track->used = true;
            track->window = PHLWINDOWREF{window};
            track->token = token;
            track->opaque_id = opaque_id;
            track->open_pending = opened;
            track->priority = priority || opened;
            track->last_active_sample = m_sample_serial;
            return track;
        }

        void admit_open(const PHLWINDOW &window) {
            if (is_no_screen_share(window)) {
                return;
            }
            auto *track = find(window);
            if (!track) {
                track = admit(window, true, true);
            }
            if (track) {
                track->open_pending = true;
                track->closed = false;
                track->destroyed = false;
                track->priority = true;
                track->held = false;
                track->last_active_sample = m_sample_serial;
                track->workspace = PHLWORKSPACEREF{window->m_workspace};
                track->workspace_known = true;
                track->owner = PHLMONITORREF{window->m_monitor.lock()};
            }
        }

        void admit_close(const PHLWINDOW &window) {
            auto *track = find(window);
            if (is_no_screen_share(window)) {
                if (track) {
                    bump_privacy_epoch();
                    *track = {};
                }
                return;
            }
            if (!track) {
                track = admit(window, false, true);
            }
            if (!track) {
                return;
            }
            track->open_pending = false;
            track->closed = true;
            track->destroyed = false;
            track->close_body_valid = false;
            track->priority = true;
            track->held = false;
            track->last_active_sample = m_sample_serial;
            track->window = PHLWINDOWREF{window};
            const auto owner = window->m_monitor.lock();
            track->owner = PHLMONITORREF{owner};
            if (window->mapped() && !window->isHidden() && owner) {
                const auto presentation = window->presentation().renderPresentation(Render::eSceneMode::MONITOR);
                if (visible_for_monitor(window, owner, presentation)) {
                    track->close_body = presented_body(window, presentation);
                    track->close_body_valid = usable_box(track->close_body);
                    track->workspace = PHLWORKSPACEREF{window->m_workspace};
                    track->workspace_known = true;
                }
            }
        }

        void mark_destroyed(const PHLWINDOWREF &window) {
            const auto object = window.lock();
            if (!object) {
                return;
            }
            if (auto *track = find(object); track && track->closed) {
                track->destroyed = true;
                track->window.reset();
            } else if (track) {
                *track = {};
            }
        }

        OutputHistory *output_for(Track &track, const PHLMONITOR &monitor) {
            for (auto &output : track.outputs) {
                if (output.used && output.monitor.lock() == monitor) {
                    return &output;
                }
            }
            for (auto &output : track.outputs) {
                if (!output.used || output.monitor.expired()) {
                    output = {};
                    output.used = true;
                    output.monitor = PHLMONITORREF{monitor};
                    return &output;
                }
            }
            return nullptr;
        }

        OutputEpoch *epoch_for(const PHLMONITOR &monitor) {
            for (auto &output : m_outputs) {
                if (output.used && output.monitor.lock() == monitor) {
                    return &output;
                }
            }
            for (auto &output : m_outputs) {
                if (!output.used || output.monitor.expired()) {
                    output = {};
                    output.used = true;
                    output.monitor = PHLMONITORREF{monitor};
                    output.privacy_epoch = m_privacy_epoch;
                    return &output;
                }
            }
            return nullptr;
        }

        void invalidate(Track &track) {
            for (auto &output : track.outputs) {
                output.previous_valid = false;
            }
        }

        void bump_epoch() {
            if (++m_epoch == 0) {
                ++m_epoch;
            }
        }

        void bump_privacy_epoch() {
            if (++m_privacy_epoch == 0) {
                ++m_privacy_epoch;
            }
            bump_epoch();
        }

        void emit_closes(const PHLMONITOR &monitor,
            const Box &output_box,
            Clock::time_point now,
            HyprEnvironmentInteractions::Batch &batch) {
            for (auto &track : m_tracks) {
                if (!track.used || !track.closed || !track.close_body_valid) {
                    continue;
                }
                if (const auto window = track.window.lock(); is_no_screen_share(window)) {
                    bump_privacy_epoch();
                    track = {};
                    continue;
                }
                auto *history = output_for(track, monitor);
                if (!history) {
                    continue;
                }
                if (history->close_delivered || !overlaps(track.close_body, output_box)) {
                    continue;
                }
                if (batch.count == MAX_INTERACTIONS) {
                    continue;
                }
                auto &record = batch.records[batch.count];
                const auto current = normalized(track.close_body, output_box);
                const auto previous = history->previous_valid
                    ? normalized(history->previous, output_box)
                    : current;
                record[0] = current;
                record[1] = previous;
                const double seconds = history->previous_valid
                    ? std::chrono::duration<double>(now - history->sampled).count()
                    : 0.0;
                if (seconds > 0 && std::isfinite(seconds)) {
                    for (std::size_t i = 0; i < 4; ++i) {
                        record[2][i] = static_cast<float>((current[i] - previous[i]) / seconds);
                    }
                }
                record[3] = {static_cast<float>(Event::Close), 1.F,
                    track.opaque_id, 0.F};
                batch.windows[batch.count] = track.token;
                ++batch.count;
                history->close_delivered = true;
                history->previous = track.close_body;
                history->sampled = now;
                history->previous_valid = true;
            }
        }

        void retire_closed_tracks() {
            for (auto &track : m_tracks) {
                if (!track.used || !track.closed || !track.destroyed) {
                    continue;
                }
                bool pending = false;
                for (const auto &monitor : State::monitorState()->monitors()) {
                    if (!usable_output(monitor)) {
                        continue;
                    }
                    const auto native = monitor->logicalBox();
                    const Box output{native.x, native.y, native.w, native.h};
                    if (!overlaps(track.close_body, output)) {
                        continue;
                    }
                    const auto found = std::find_if(track.outputs.begin(), track.outputs.end(),
                        [&](const auto &history) {
                            return history.used && history.monitor.lock() == monitor;
                        });
                    if (found == track.outputs.end() || !found->close_delivered) {
                        pending = true;
                        break;
                    }
                }
                if (!pending) {
                    track = {};
                }
            }
        }

        CHyprWindowPort &m_port;
        std::array<Track, MAX_INTERACTIONS> m_tracks{};
        std::array<OutputEpoch, MAX_TRACKED_OUTPUTS> m_outputs{};
        std::uint64_t m_epoch = 1, m_privacy_epoch = 1;
        std::uint64_t m_sample_serial = 0;
        std::uint32_t m_next_opaque_id = 1;
        bool m_active = false;
        CHyprSignalListener m_open, m_close, m_destroy, m_layout, m_lock;
    };

    HyprEnvironmentInteractions::HyprEnvironmentInteractions(CHyprWindowPort &port)
        : m_impl(std::make_unique<Impl>(port)) {
    }

    HyprEnvironmentInteractions::~HyprEnvironmentInteractions() = default;
    HyprEnvironmentInteractions::HyprEnvironmentInteractions(HyprEnvironmentInteractions &&) noexcept = default;
    HyprEnvironmentInteractions &HyprEnvironmentInteractions::operator=(HyprEnvironmentInteractions &&) noexcept = default;

    HyprEnvironmentInteractions::Batch HyprEnvironmentInteractions::collect(const PHLMONITOR &monitor) {
        return m_impl->collect(monitor);
    }

    bool HyprEnvironmentInteractions::privacy_changed(const PHLMONITOR &monitor) {
        return m_impl->privacy_changed(monitor);
    }

    void HyprEnvironmentInteractions::clear() {
        m_impl->clear();
    }
}  // namespace hyprimate::shader
