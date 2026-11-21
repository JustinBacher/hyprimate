#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Check.hpp"
#include "FakeWorkspacePort.hpp"

using namespace hyprimate::workspace;
using namespace hyprimate::workspace::test;

namespace {
    using Image = std::shared_ptr<int>;
    using Fact = InventoryWorkspace<FakeWorkspacePort::Workspace, Image>;

    struct InventoryFakePort : FakeWorkspacePort {
        struct Command {
            int workspace = 0;
            Image wallpaper;
            PreviewCapturePlan plan;
        };
        using FakeWorkspacePort::capture;
        std::vector<Fact> facts;
        std::vector<Command> commands;
        std::vector<int> wallpaper_sides;
        std::shared_ptr<std::vector<Preview>> previews;
        CompositionPlacements placement_registry;
        std::uint64_t inventory_revision = 1;
        std::size_t index_reads = 0;
        std::size_t native_count = 0;
        double remote_pixels = 1920.0 * 1080.0;
        bool thumbnail_succeeds = true;
        bool fallback_succeeds = true;
        Image fallback = std::make_shared<int>(-1);
        bool monitor_scope = false;
        bool inventory_current = true;
        std::function<void()> during_capture;
        std::function<void()> during_fallback;
        [[nodiscard]] bool allowed() const {
            return inventory_current && scene_safe();
        }
        [[nodiscard]] bool owns(const Workspace &workspace, std::uint64_t owner) const {
            const auto FOUND = std::ranges::find_if(
                facts, [&](const auto &fact) { return fact.workspace.lock() == workspace.lock(); });
            return FOUND != facts.end() && !workspace.expired() &&
                   FOUND->endpoint.owner != UINT64_MAX && FOUND->endpoint.owner == owner;
        }
        bool includes(const JourneyEndpoint &candidate, const JourneyEndpoint &source,
            const JourneyEndpoint &destination) const {
            return !monitor_scope || candidate.owner == source.owner ||
                   candidate.owner == destination.owner;
        }

        [[nodiscard]] std::uint64_t revision() const {
            return inventory_revision;
        }
        [[nodiscard]] std::size_t workspace_count() const {
            return native_count == 0 ? facts.size() : native_count;
        }
        const std::vector<Fact> &workspaces() {
            ++index_reads;
            return facts;
        }
        CompositionPlacements &placements() {
            return placement_registry;
        }
        [[nodiscard]] PreviewCaptureFacts capture_facts(const Workspace &workspace) const {
            const auto FOUND = std::ranges::find_if(
                facts, [&](const auto &fact) { return fact.workspace.lock() == workspace.lock(); });
            return FOUND == facts.end() ? PreviewCaptureFacts{}
                                        : PreviewCaptureFacts{.ready = FOUND->owner_ready,
                                              .local = FOUND->local,
                                              .pixels = remote_pixels};
        }
        Image local_wallpaper(int side) {
            wallpaper_sides.push_back(side);
            if (during_fallback) {
                during_fallback();
            }
            return fallback_succeeds ? fallback : Image{};
        }
        Image capture(const Workspace &workspace, const Image &wallpaper, PreviewCapturePlan plan) {
            const auto WORKSPACE = workspace.lock();
            if (!WORKSPACE) {
                return {};
            }
            commands.push_back(
                {.workspace = WORKSPACE->number, .wallpaper = wallpaper, .plan = plan});
            if (during_capture) {
                during_capture();
            }
            return thumbnail_succeeds ? std::make_shared<int>(WORKSPACE->number) : Image{};
        }
        void publish(const std::shared_ptr<std::vector<Preview>> &value) {
            previews = value;
        }
    };

    struct Fixture {
        InventoryFakePort port;
        WorkspaceTurn<InventoryFakePort> turn{port};
        std::vector<std::shared_ptr<FakeWorkspace>> retained;
        InventoryFakePort::Composition source;
        InventoryFakePort::Composition destination;

        Fixture() {
            source = port.composition(add(1));
            destination = port.composition(add(5));
            source.placement = port.placements().get(1, {}, std::nullopt);
            destination.placement = port.placements().get(5, {}, std::nullopt);
        }
        std::shared_ptr<FakeWorkspace> add(int number) {
            auto workspace = std::make_shared<FakeWorkspace>(number);
            retained.push_back(workspace);
            const auto COMPOSITION = port.composition(workspace);
            port.facts.push_back({.workspace = workspace,
                .endpoint = COMPOSITION.endpoint(),
                .name = std::to_string(number),
                .wallpaper = {},
                .owner_ready = true,
                .local = true,
                .has_windows = true});
            return workspace;
        }
        void start() {
            CHECK(turn.stage(source));
            turn.begin(source, destination, 1.0, [](double value) { return value; });
            turn.frame(destination, 0.0);
            CHECK(turn.drawable());
        }
        void prepare() {
            turn.prepare_inventory(port);
        }
        const InventoryFakePort::Preview &preview(int number) const {
            const auto FOUND = std::ranges::find_if(*port.previews, [&](const auto &value) {
                return value.endpoint.numbered == static_cast<std::uint32_t>(number);
            });
            CHECK(FOUND != port.previews->end());
            return *FOUND;
        }
    };

    void scheduling_and_crossfade() {
        Fixture fixture;
        fixture.port.advance(12'345.0);
        fixture.add(8);
        fixture.add(4);
        fixture.add(2);
        fixture.add(3);
        fixture.prepare();
        CHECK(fixture.port.commands.empty());
        fixture.start();
        fixture.prepare();
        CHECK(fixture.port.previews->size() == 4);
        CHECK(fixture.port.commands.size() == 1);
        CHECK(fixture.port.commands.front().workspace == 2);
        CHECK(fixture.port.commands.front().plan.side == 384);
        CHECK(fixture.port.commands.front().plan.native_background);
        CHECK(!fixture.port.commands.front().plan.remote);
        CHECK(!fixture.port.commands.front().wallpaper);
        CHECK(fixture.port.wallpaper_sides == std::vector<int>{256});
        CHECK(fixture.preview(2).previous_image == fixture.port.fallback);
        CHECK(fixture.preview(2).live_mix == 0.F);
        fixture.port.advance(.04);
        fixture.prepare();
        CHECK(fixture.port.commands.size() == 2);
        CHECK(fixture.port.commands.back().workspace == 3);
        CHECK(std::abs(fixture.preview(2).live_mix - .5F) < 1e-5F);
        fixture.port.advance(.04);
        fixture.prepare();
        CHECK(fixture.port.commands.back().workspace == 4);
        CHECK(fixture.preview(2).live_mix == 1.F);
        CHECK(!fixture.preview(2).previous_image);
        fixture.prepare();
        CHECK(fixture.port.commands.back().workspace == 8);
        fixture.prepare();
        CHECK(fixture.port.commands.size() == 4);
        CHECK(fixture.port.index_reads == 1);
    }

    void invalidation_and_placement() {
        Fixture fixture;
        fixture.add(3);
        fixture.add(7);
        fixture.port.facts.back().endpoint.numbered.reset();
        fixture.port.facts.back().name = "name:retained";
        fixture.start();
        fixture.prepare();
        const auto PLACEMENT = fixture.port.previews->back().endpoint.placement;
        const auto OLD_PREVIEWS = fixture.port.previews;
        fixture.port.facts.back().name = "retained";
        ++fixture.port.inventory_revision;
        fixture.prepare();
        CHECK(fixture.port.index_reads == 2);
        CHECK(fixture.port.previews != OLD_PREVIEWS);
        CHECK(fixture.port.previews->back().endpoint.placement == PLACEMENT);
        CHECK(fixture.port.commands.back().workspace == 3);
        const auto RETAINED = fixture.port.previews;
        fixture.turn.presented(true);
        fixture.turn.begin(
            fixture.destination, fixture.source, 1.0, [](double value) { return value; });
        CHECK(fixture.turn.drawable());
        fixture.prepare();
        CHECK(fixture.port.index_reads == 2);
        CHECK(fixture.port.previews == RETAINED);
        CHECK(fixture.port.previews->size() == 2);
    }

    void scene_scope_precedes_capture_eligibility() {
        Fixture fixture;
        fixture.port.monitor_scope = true;
        fixture.source.owner = 10;
        fixture.destination.owner = 20;
        fixture.add(2);
        fixture.port.facts.back().endpoint.owner = 10;
        fixture.add(3);
        fixture.port.facts.back().endpoint.owner = 20;
        fixture.port.facts.back().owner_ready = false;
        fixture.add(4);
        fixture.port.facts.back().endpoint.owner = 30;
        fixture.start();
        fixture.prepare();
        CHECK(fixture.port.previews->size() == 2);
        CHECK(fixture.port.commands.size() == 1);
        CHECK(fixture.port.commands.front().workspace == 2);
        CHECK(!fixture.preview(3).image);
        fixture.prepare();
        CHECK(fixture.port.commands.size() == 1);
        const auto OLD = fixture.port.previews;
        // Moving an actual workspace onto an involved monitor changes membership;
        // no synthetic planet is inserted for an absent numbered workspace.
        fixture.port.facts.back().endpoint.owner = 20;
        fixture.port.facts.back().endpoint.monitor_x = 1920;
        ++fixture.port.inventory_revision;
        fixture.prepare();
        CHECK(fixture.port.previews != OLD);
        CHECK(fixture.port.previews->size() == 3);
        CHECK(fixture.preview(4).endpoint.owner == 20);
        CHECK(fixture.preview(4).endpoint.monitor_x == 1920);
        const auto REBUILT = fixture.port.previews;
        fixture.prepare();
        CHECK(fixture.port.previews == REBUILT);
        CHECK(fixture.port.index_reads == 2);
    }

    void fallbacks_and_capture_failures() {
        Fixture fixture;
        fixture.add(2);
        fixture.port.facts.back().has_windows = false;
        fixture.add(3);
        fixture.port.facts.back().wallpaper = std::make_shared<int>(33);
        fixture.port.facts.back().local = false;
        fixture.port.facts.back().has_windows = false;
        fixture.add(4);
        fixture.port.facts.back().owner_ready = false;
        fixture.port.facts.back().endpoint.owner = UINT64_MAX;
        fixture.add(6);
        fixture.port.facts.back().local = false;
        fixture.port.remote_pixels = 16'777'217.0;
        fixture.start();
        fixture.prepare();
        CHECK(fixture.port.commands.empty());
        CHECK(fixture.preview(2).image == fixture.port.fallback);
        CHECK(*fixture.preview(3).image == 33);
        CHECK(!fixture.preview(4).image);
        CHECK(!fixture.preview(6).image);

        fixture.port.facts[2].has_retiring = true;
        ++fixture.port.inventory_revision;
        fixture.port.thumbnail_succeeds = false;
        fixture.prepare();
        CHECK(fixture.port.commands.size() == 1);
        CHECK(fixture.port.commands.back().workspace == 2);
        CHECK(fixture.preview(2).image == fixture.port.fallback);
        CHECK(!fixture.preview(2).previous_image);
        fixture.prepare();
        CHECK(fixture.port.commands.size() == 1);
    }

    void remote_commands_and_budget() {
        Fixture fixture;
        fixture.add(3);
        const auto PAPER = std::make_shared<int>(30);
        fixture.port.facts.back().wallpaper = PAPER;
        fixture.port.facts.back().local = false;
        fixture.port.remote_pixels = 16'777'216.0;
        fixture.port.native_count = 1000;
        fixture.start();
        fixture.prepare();
        CHECK(fixture.port.commands.size() == 1);
        const auto &command = fixture.port.commands.front();
        CHECK(command.plan.remote);
        CHECK(command.wallpaper == PAPER);
        CHECK(!command.plan.native_background);
        CHECK(command.plan.side == 38);
        CHECK(fixture.port.wallpaper_sides.empty());
        fixture.port.native_count = 10'000'000;
        ++fixture.port.inventory_revision;
        fixture.prepare();
        CHECK(fixture.port.commands.back().plan.side == 1);
    }

    void live_owner_and_fallback_failure() {
        Fixture fixture;
        fixture.add(2);
        fixture.add(3);
        fixture.port.fallback_succeeds = false;
        fixture.port.facts.back().has_windows = false;
        fixture.start();
        fixture.prepare();
        CHECK(fixture.port.commands.size() == 1);
        CHECK(fixture.port.wallpaper_sides.size() == 1);
        CHECK(!fixture.preview(3).image);
        // Ownership changes after indexing must select scratch from fresh native facts.
        fixture.port.facts.back().local = false;
        fixture.prepare();
        CHECK(fixture.port.index_reads == 1);
        CHECK(fixture.port.commands.size() == 2);
        CHECK(fixture.port.commands.back().workspace == 3);
        CHECK(fixture.port.commands.back().plan.remote);
        CHECK(fixture.port.commands.back().plan.native_background);
        CHECK(fixture.port.wallpaper_sides.size() == 1);
    }

    void placement_tie_order() {
        Fixture fixture;
        fixture.add(8);
        fixture.port.facts.back().endpoint.numbered.reset();
        fixture.port.facts.back().name = "name:first";
        fixture.add(9);
        fixture.port.facts.back().endpoint.numbered.reset();
        fixture.port.facts.back().name = "name:second";
        fixture.start();
        fixture.prepare();
        const int FIRST = fixture.port.commands.front().workspace;
        const auto &first = (*fixture.port.previews)[0];
        const auto &second = (*fixture.port.previews)[1];
        CHECK(FIRST == (first.endpoint.placement < second.endpoint.placement ? 8 : 9));
        std::ranges::reverse(fixture.port.facts);
        ++fixture.port.inventory_revision;
        fixture.prepare();
        CHECK(fixture.port.commands.back().workspace == FIRST);
    }

    void privacy_denial_revokes_all_inventory_pixels() {
        for (const bool REMOTE : {false, true}) {
            Fixture fixture;
            fixture.add(2);
            fixture.add(3);
            fixture.port.facts.back().local = !REMOTE;
            fixture.port.facts.back().wallpaper = std::make_shared<int>(303);
            fixture.start();
            fixture.prepare();
            const auto RETAINED = fixture.port.previews;
            CHECK(fixture.preview(2).image);
            CHECK(fixture.preview(2).previous_image);
            fixture.port.during_capture = [&] {
                fixture.port.safety_facts.protected_content = true;
            };
            fixture.prepare();
            CHECK(RETAINED->empty());
            CHECK(fixture.port.commands.size() == 2);
            fixture.port.safety_facts.protected_content = false;
            fixture.port.during_capture = {};
            fixture.prepare();
            CHECK(!fixture.port.previews->empty());
            CHECK(fixture.port.index_reads == 2);
        }
    }

    void privacy_denial_during_fallback_and_cached_prepare() {
        Fixture fixture;
        fixture.add(2);
        fixture.start();
        fixture.port.during_fallback = [&] { fixture.port.inventory_current = false; };
        fixture.prepare();
        CHECK(!fixture.port.previews);
        CHECK(fixture.port.commands.empty());
        fixture.port.inventory_current = true;
        fixture.port.during_fallback = {};
        fixture.prepare();
        const auto RETAINED = fixture.port.previews;
        CHECK(!RETAINED->empty());
        fixture.port.inventory_current = false;
        fixture.prepare();
        CHECK(RETAINED->empty());
        CHECK(fixture.port.commands.size() == 1);

        fixture.port.inventory_current = true;
        fixture.prepare();
        const auto old_publication = fixture.port.previews;
        ++fixture.port.inventory_revision;
        fixture.port.during_fallback = [&] { fixture.port.inventory_current = false; };
        fixture.prepare();
        CHECK(old_publication && old_publication->empty());
    }

    void ownership_changes_revoke_cached_inventory_without_events() {
        Fixture fixture;
        fixture.add(2);
        fixture.add(3);
        fixture.port.facts.back().wallpaper = std::make_shared<int>(33);
        fixture.port.facts.back().has_windows = false;
        fixture.start();
        fixture.prepare();
        const auto RETAINED = fixture.port.previews;
        CHECK(fixture.preview(2).image);
        CHECK(fixture.preview(3).image);
        ++fixture.port.facts.back().endpoint.owner;
        fixture.prepare();
        CHECK(RETAINED->empty());
        CHECK(fixture.port.commands.size() == 1);
        fixture.prepare();
        CHECK(fixture.port.index_reads == 2);
        CHECK(fixture.preview(3).endpoint.owner == fixture.port.facts.back().endpoint.owner);
    }

    void capture_reentry_rejects_replaced_turn_and_owner() {
        for (const bool OWNER_CHANGED : {false, true}) {
            Fixture fixture;
            fixture.add(2);
            fixture.port.facts.back().local = false;
            fixture.port.facts.back().wallpaper = std::make_shared<int>(22);
            fixture.start();
            fixture.port.during_capture = [&] {
                if (OWNER_CHANGED) {
                    ++fixture.port.facts.back().endpoint.owner;
                } else {
                    fixture.port.inventory_current = false;
                }
            };
            fixture.prepare();
            CHECK(fixture.port.commands.size() == 1);
            CHECK(!fixture.port.previews);
        }
    }

    void capture_callbacks_only_finish_the_session_they_started() {
        enum class Event { Complete, Deny, Throw, Replace, ReplaceAndThrow, EndFails };
        for (const auto event : {Event::Complete, Event::Deny, Event::Throw, Event::Replace,
                 Event::ReplaceAndThrow, Event::EndFails}) {
            enum class Session { Capture, Replacement, Idle };
            auto session = Session::Capture;
            int submissions = 0, aborts = 0;
            bool completed = false, threw = false;
            try {
                completed = run_capture_pass([&] { return session == Session::Capture; },
                    [&] {
                        if (event == Event::Replace || event == Event::ReplaceAndThrow) {
                            session = Session::Replacement;
                        }
                        if (event == Event::Throw || event == Event::ReplaceAndThrow) {
                            throw event;
                        }
                        return event != Event::Deny;
                    },
                    [&] {
                        ++submissions;
                        if (event == Event::EndFails) { return false; }
                        session = Session::Idle;
                        return true;
                    },
                    [&] {
                        ++aborts;
                        session = Session::Idle;
                    });
            } catch (Event reason) {
                threw = reason == event;
            }
            CHECK(completed == (event == Event::Complete));
            CHECK(threw == (event == Event::Throw || event == Event::ReplaceAndThrow));
            CHECK(submissions == (event == Event::Complete || event == Event::EndFails ? 1 : 0));
            CHECK(aborts == (event == Event::Throw || event == Event::Deny || event == Event::EndFails ? 1 : 0));
            CHECK(session == (event == Event::Replace || event == Event::ReplaceAndThrow
                        ? Session::Replacement : Session::Idle));
        }
    }

    void retained_inventory_validation_precedes_reprepare() {
        Fixture fixture;
        fixture.add(2);
        WorkspaceInventory<InventoryFakePort::Workspace, InventoryFakePort::Preview> inventory;
        const auto OWNS = [&](const auto &workspace, auto owner) {
            return fixture.port.owns(workspace, owner);
        };
        CHECK(!inventory.current(fixture.port.revision(), OWNS));
        inventory.prepare(fixture.port, fixture.source.endpoint(), fixture.destination.endpoint());
        CHECK(inventory.current(fixture.port.revision(), OWNS));
        ++fixture.port.facts.back().endpoint.owner;
        CHECK(!inventory.current(fixture.port.revision(), OWNS));
        --fixture.port.facts.back().endpoint.owner;
        CHECK(inventory.current(fixture.port.revision(), OWNS));
        ++fixture.port.inventory_revision;
        CHECK(!inventory.current(fixture.port.revision(), OWNS));
    }

    struct HistorySignature {
        struct Allocation {
            int width = 0;
            int format = 0;
            int description = 0;
            bool operator==(const Allocation &) const = default;
        } allocation;
        int transform = 0;
        bool operator==(const HistorySignature &) const = default;
    };

    void wallpaper_history() {
        using WeakWorkspace = std::weak_ptr<FakeWorkspace>;
        using WeakMonitor = std::weak_ptr<int>;
        WallpaperHistory<WeakWorkspace, WeakMonitor, Image, HistorySignature> history;
        const auto MONITOR = std::make_shared<int>(1);
        const auto OTHER_MONITOR = std::make_shared<int>(2);
        const HistorySignature SIGNATURE{
            .allocation = {.width = 1920, .format = 4, .description = 7}, .transform = 0};
        const auto OBSERVE = [&](const WeakWorkspace &workspace,
                                 const HistorySignature &signature,
                                 auto &&copy) {
            const WallpaperObservation<WeakWorkspace, WeakMonitor, HistorySignature> OBSERVATION{
                .workspace = workspace, .monitor = MONITOR, .signature = signature};
            WorkspaceTurn<InventoryFakePort>::observe_wallpaper(
                history, OBSERVATION, std::forward<decltype(copy)>(copy));
        };
        std::vector<std::shared_ptr<FakeWorkspace>> retained;
        for (int number = 0; number < 64; ++number) {
            const auto WORKSPACE = std::make_shared<FakeWorkspace>(number);
            retained.push_back(WORKSPACE);
            OBSERVE(WeakWorkspace{WORKSPACE}, SIGNATURE, [number](const Image &image) {
                CHECK(!image);
                return std::make_shared<int>(number);
            });
        }
        const auto FIRST =
            history.find(WeakWorkspace{retained.front()}, WeakMonitor{MONITOR}, SIGNATURE);
        OBSERVE(WeakWorkspace{retained.front()}, SIGNATURE, [&](Image image) {
            CHECK(image == FIRST);
            return image;
        });
        const auto EXTRA = std::make_shared<FakeWorkspace>(64);
        OBSERVE(WeakWorkspace{EXTRA}, SIGNATURE, [](const Image &) {
            return std::make_shared<int>(64);
        });
        CHECK(history.find(WeakWorkspace{retained.front()}, WeakMonitor{MONITOR}, SIGNATURE) ==
              FIRST);
        CHECK(!history.find(WeakWorkspace{retained[1]}, WeakMonitor{MONITOR}, SIGNATURE));
        CHECK(
            !history.find(WeakWorkspace{retained.front()}, WeakMonitor{OTHER_MONITOR}, SIGNATURE));
        for (int field = 0; field < 4; ++field) {
            auto changed = SIGNATURE;
            if (field == 0) {
                ++changed.allocation.width;
            }
            if (field == 1) {
                ++changed.allocation.format;
            }
            if (field == 2) {
                ++changed.allocation.description;
            }
            if (field == 3) {
                ++changed.transform;
            }
            CHECK(!history.find(WeakWorkspace{retained.front()}, WeakMonitor{MONITOR}, changed));
        }
        auto rotated = SIGNATURE;
        ++rotated.transform;
        OBSERVE(WeakWorkspace{retained.front()}, rotated, [&](Image image) {
            CHECK(image == FIRST);
            return image;
        });
        history.remove_monitor(WeakMonitor{MONITOR});
        CHECK(!history.find(WeakWorkspace{retained.front()}, WeakMonitor{MONITOR}, rotated));
        OBSERVE(WeakWorkspace{retained.front()}, SIGNATURE, [](const Image &) { return Image{}; });
        CHECK(!history.find(WeakWorkspace{retained.front()}, WeakMonitor{MONITOR}, SIGNATURE));
        history.clear();
    }
}  // namespace

int main() {
    scheduling_and_crossfade();
    invalidation_and_placement();
    scene_scope_precedes_capture_eligibility();
    fallbacks_and_capture_failures();
    remote_commands_and_budget();
    live_owner_and_fallback_failure();
    privacy_denial_revokes_all_inventory_pixels();
    privacy_denial_during_fallback_and_cached_prepare();
    ownership_changes_revoke_cached_inventory_without_events();
    capture_reentry_rejects_replaced_turn_and_owner();
    capture_callbacks_only_finish_the_session_they_started();
    retained_inventory_validation_precedes_reprepare();
    placement_tie_order();
    wallpaper_history();
    return g_failures == 0 ? 0 : 1;
}
