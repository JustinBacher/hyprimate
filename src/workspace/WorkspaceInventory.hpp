#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "workspace/WorkspaceIdentity.hpp"
#include "workspace/WorkspaceJourney.hpp"

namespace hyprimate::workspace {

    template <class Workspace, class Monitor, class Signature>
    struct WallpaperObservation {
        Workspace workspace;
        Monitor monitor;
        Signature signature;
    };

    // Only wallpaper thumbnails survive turns. Client pixels live in the turn inventory.
    template <class Workspace, class Monitor, class Image, class Signature>
    class WallpaperHistory {
        struct Entry {
            Workspace workspace;
            Monitor monitor;
            Image image;
            Signature signature;
        };

      public:
        template <class Copy>
        void remember(
            Workspace workspace, Monitor monitor, const Signature &signature, Copy &&copy) {
            std::erase_if(m_entries, [](const auto &entry) {
                return entry.workspace.expired() || entry.monitor.expired();
            });
            const auto FOUND = std::ranges::find_if(m_entries,
                [&](const auto &entry) { return entry.workspace.lock() == workspace.lock(); });
            Image image{};
            if (FOUND != m_entries.end()) {
                if (FOUND->signature.allocation == signature.allocation) {
                    image = std::move(FOUND->image);
                }
                m_entries.erase(FOUND);
            } else if (m_entries.size() >= 64) {
                m_entries.erase(m_entries.begin());
            }
            image = std::forward<Copy>(copy)(std::move(image));
            if (image) {
                m_entries.push_back(
                    {std::move(workspace), std::move(monitor), std::move(image), signature});
            }
        }
        [[nodiscard]] Image find(
            const Workspace &workspace, const Monitor &monitor, const Signature &signature) const {
            const auto FOUND = std::ranges::find_if(m_entries, [&](const auto &entry) {
                return entry.workspace.lock() == workspace.lock() &&
                       entry.monitor.lock() == monitor.lock() && entry.signature == signature;
            });
            return FOUND == m_entries.end() ? Image{} : FOUND->image;
        }
        void remove_monitor(const Monitor &monitor) {
            std::erase_if(m_entries, [&](const auto &entry) {
                return entry.monitor.expired() || entry.monitor.lock() == monitor.lock();
            });
        }
        void clear() {
            m_entries.clear();
        }

      private:
        std::vector<Entry> m_entries;
    };

    template <class Workspace, class Image>
    struct InventoryWorkspace {
        Workspace workspace;
        JourneyEndpoint endpoint;
        std::string name;
        Image wallpaper;
        bool owner_ready = false;
        bool local = false;
        bool has_windows = false;
        bool has_retiring = false;
    };

    struct PreviewCaptureFacts {
        bool ready = false;
        bool local = false;
        double pixels = 0.0;
    };

    struct PreviewCapturePlan {
        int side = 0;
        bool remote = false;
        bool native_background = false;
    };

    template <class Workspace, class Preview>
    class WorkspaceInventory {
        using Image = decltype(Preview::image);
        struct Job {
            Workspace workspace;
            std::size_t index = 0;
            double priority = 0.0;
            Image wallpaper;
            double started = 0.0;
        };

      public:
        template <class Port>
        void prepare(
            Port &port, const JourneyEndpoint &source, const JourneyEndpoint &destination) {
            const auto REVISION = port.revision();
            if (!m_previews || m_source != source.identity ||
                m_destination != destination.identity || m_revision != REVISION) {
                index(port, source, destination);
                m_source = source.identity;
                m_destination = destination.identity;
                m_revision = REVISION;
            }
            const double NOW = port.now();
            if (m_next < m_jobs.size()) {
                auto &job = m_jobs[m_next++];
                const auto FACTS = port.capture_facts(job.workspace);
                // Remote full-size scratch allocations have the same hard ceiling as before.
                if (FACTS.ready && (FACTS.local || FACTS.pixels <= 16'777'216.0)) {
                    const auto IMAGE = port.capture(job.workspace,
                        job.wallpaper,
                        PreviewCapturePlan{.side = m_side,
                            .remote = !FACTS.local,
                            .native_background = !job.wallpaper});
                    if (IMAGE) {
                        auto &preview = (*m_previews)[job.index];
                        preview.previous_image = std::move(preview.image);
                        preview.image = IMAGE;
                        preview.live_mix = 0.F;
                        job.started = NOW;
                    }
                }
            }
            blend(NOW);
            port.publish(m_previews);
        }
        [[nodiscard]] int thumbnail_side() const {
            return m_side;
        }

      private:
        static double priority(const JourneyEndpoint &candidate,
            const JourneyEndpoint &source,
            const JourneyEndpoint &destination) {
            if (!candidate.numbered || !source.numbered || !destination.numbered) {
                return 1e12;
            }
            const auto LIMITS = std::minmax(*source.numbered, *destination.numbered);
            const double DISTANCE =
                std::abs(static_cast<double>(*candidate.numbered) - *source.numbered);
            return DISTANCE +
                   (*candidate.numbered < LIMITS.first || *candidate.numbered > LIMITS.second
                           ? 1e10
                           : 0.0);
        }
        template <class Port>
        void index(Port &port, const JourneyEndpoint &source, const JourneyEndpoint &destination) {
            const auto &facts = port.workspaces();
            m_previews = std::make_shared<std::vector<Preview>>();
            m_previews->reserve(facts.size());
            m_jobs.clear();
            m_jobs.reserve(facts.size());
            m_next = 0;
            m_blending = 0;
            // 2M texels including mip levels and one local wallpaper fallback.
            constexpr double BASE_TEXELS = (2'097'152 * .75) - (256 * 256);
            m_side = std::clamp(
                static_cast<int>(
                    std::sqrt(BASE_TEXELS / std::max<std::size_t>(1, port.workspace_count()))),
                1,
                384);
            Image shared_wallpaper{};
            bool sampled_local = false;
            CompositionPlacements &placements = port.placements();
            for (const auto &fact : facts) {
                auto endpoint = fact.endpoint;
                endpoint.placement =
                    endpoint.special ? placements.get(std::nullopt, {}, fact.name)
                                     : placements.get(endpoint.numbered, fact.name, std::nullopt);
                if (endpoint.placement == source.placement ||
                    endpoint.placement == destination.placement) {
                    continue;
                }
                auto &preview = m_previews->emplace_back(
                    Preview{.endpoint = endpoint, .image = {}, .previous_image = {}});
                if (!fact.owner_ready) {
                    continue;
                }
                if (!fact.wallpaper && fact.local && !sampled_local) {
                    sampled_local = true;
                    shared_wallpaper = port.local_wallpaper(256);
                }
                preview.image = fact.wallpaper;
                if (!preview.image && fact.local) {
                    preview.image = shared_wallpaper;
                }
                if (preview.image && !fact.has_windows && !fact.has_retiring) {
                    continue;
                }
                m_jobs.push_back({.workspace = fact.workspace,
                    .index = m_previews->size() - 1,
                    .priority = priority(endpoint, source, destination),
                    .wallpaper = fact.wallpaper});
            }
            std::ranges::sort(m_jobs, [&](const auto &left, const auto &right) {
                return left.priority != right.priority
                           ? left.priority < right.priority
                           : (*m_previews)[left.index].endpoint.placement <
                                 (*m_previews)[right.index].endpoint.placement;
            });
        }
        void blend(double now) {
            for (auto index = m_blending; index < m_next; ++index) {
                auto &preview = (*m_previews)[m_jobs[index].index];
                const float FRACTION =
                    std::clamp(static_cast<float>(now - m_jobs[index].started) / .08F, 0.F, 1.F);
                preview.live_mix = (FRACTION * FRACTION) * (3.F - (2.F * FRACTION));
                if (FRACTION == 1.F) {
                    preview.previous_image.reset();
                    if (m_blending == index) {
                        ++m_blending;
                    }
                }
            }
        }
        std::shared_ptr<std::vector<Preview>> m_previews;
        std::vector<Job> m_jobs;
        std::size_t m_next = 0;
        std::size_t m_blending = 0;
        int m_side = 0;
        std::int64_t m_source = 0;
        std::int64_t m_destination = 0;
        std::uint64_t m_revision = 0;
    };
}  // namespace hyprimate::workspace
