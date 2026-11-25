#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <span>

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/cm/ColorManagement.hpp>

#include "Check.hpp"
#include "window/bubble/BubbleStyle.hpp"

// Hyprland headers initialize these host globals before main, although this fixture never
// uses color management or logging.
Log::CLogger::CLogger() = default;
CHyprColor::CHyprColor(float red, float green, float blue, float alpha)
    : r(red), g(green), b(blue), a(alpha) {
}

namespace NColorManagement {
    const SPCPRimaries &getPrimaries(ePrimaries /*name*/) {
        static const SPCPRimaries empty;
        return empty;
    }

    WP<const CImageDescription> CImageDescription::from(const SImageDescription & /*description*/) {
        return {};
    }
}  // namespace NColorManagement

// These tests never initialize or render a GPU object. A shader destructor being reached
// would mean the fixture unexpectedly crossed that boundary.
CBubbleRenderer::~CBubbleRenderer() = default;
CShader::~CShader() {
    std::abort();
}

namespace {
    using namespace hyprimate;
    using animation::Clock;
    using animation::MonitorId;
    constexpr MonitorId WAKE_MONITOR = 1;
    constexpr MonitorId OTHER_MONITOR = 2;

    struct ScreenDriver {
        SSharedStyleResources shared;
        SBubbleResources resources{shared};
        Clock::time_point now = Clock::now();
        std::array<int, 3> final_repaints{};
        std::array<int, 3> scanout_rechecks{};

        void tick(MonitorId monitor) {
            const bool BEFORE = SBubbleModule::Screen::needs_frame(resources, monitor);
            animation::ScreenFacts facts{};
            facts.monitor = monitor;
            facts.now = now;
            // Bubble is no longer selected and no transition is retained: tick_screen
            // keeps it enabled only while this monitor still needs a frame.
            facts.enabled = BEFORE;
            SBubbleModule::Screen::tick(resources, facts);
            const bool AFTER = SBubbleModule::Screen::needs_frame(resources, monitor);
            if (BEFORE && !AFTER) {
                ++final_repaints[monitor];
                ++scanout_rechecks[monitor];
            }
        }

        void advance() {
            now += std::chrono::milliseconds(20);
        }
    };

    void add_wake(CBubbleVestige &vestige) {
        BubbleTrailPoint point{.half_width = 16, .thickness = 600, .strand = 1};
        std::uint64_t wake = 0;
        for (int i = 0; i < 20; ++i) {
            wake = vestige.trail(
                WAKE_MONITOR, 1, wake, {.x = 100.0 + (i * 20), .y = 300}, std::span{&point, 1}, {});
        }
        CHECK(wake != 0);
        CHECK(vestige.active(WAKE_MONITOR));
    }

    void idle_monitor_does_not_drop_wake(bool idle_first) {
        ScreenDriver driver;
        auto &vestige = driver.resources.vestige;
        add_wake(vestige);
        driver.advance();
        if (idle_first) {
            driver.tick(OTHER_MONITOR);
            CHECK(vestige.active(WAKE_MONITOR));
            driver.tick(WAKE_MONITOR);
        } else {
            driver.tick(WAKE_MONITOR);
            driver.tick(OTHER_MONITOR);
            CHECK(vestige.active(WAKE_MONITOR));
        }
        for (int i = 0; i < 500 && vestige.active(WAKE_MONITOR); ++i) {
            driver.advance();
            driver.tick(WAKE_MONITOR);
        }
        CHECK(!vestige.active(WAKE_MONITOR));
        CHECK(driver.final_repaints[WAKE_MONITOR] == 1);
        CHECK(driver.scanout_rechecks[WAKE_MONITOR] == 1);
    }

    void cancelled_deposit_remains_until_own_tick(bool wake_first) {
        ScreenDriver driver;
        auto &vestige = driver.resources.vestige;
        add_wake(vestige);
        auto clock = makeShared<SBubbleImpactClock>();
        const BubbleImpact IMPACT{.radius = 5, .delay = 1};
        CHECK(vestige.deposit(OTHER_MONITOR, {.x = 200, .y = 200}, std::span{&IMPACT, 1}, clock));
        CHECK(vestige.active(OTHER_MONITOR));
        clock->cancelled = true;
        driver.advance();
        if (wake_first) {
            driver.tick(WAKE_MONITOR);
            CHECK(vestige.active(OTHER_MONITOR));
            driver.tick(OTHER_MONITOR);
        } else {
            driver.tick(OTHER_MONITOR);
            driver.tick(WAKE_MONITOR);
        }
        CHECK(driver.final_repaints[OTHER_MONITOR] == 1);
        CHECK(driver.scanout_rechecks[OTHER_MONITOR] == 1);
        for (int i = 0; i < 500 && vestige.active(WAKE_MONITOR); ++i) {
            driver.advance();
            driver.tick(WAKE_MONITOR);
        }
        CHECK(driver.final_repaints[WAKE_MONITOR] == 1);
        CHECK(driver.scanout_rechecks[WAKE_MONITOR] == 1);
    }

    void global_shutdown_clears_and_rejects_new_effects() {
        ScreenDriver driver;
        auto &vestige = driver.resources.vestige;
        add_wake(vestige);
        const BubbleImpact IMPACT{.radius = 5, .delay = 1};
        CHECK(vestige.deposit(OTHER_MONITOR, {.x = 200, .y = 200}, std::span{&IMPACT, 1}, {}));
        animation::ScreenFacts shutdown{};
        SBubbleModule::Screen::tick(driver.resources, shutdown);
        CHECK(!vestige.active(WAKE_MONITOR));
        CHECK(!vestige.active(OTHER_MONITOR));
        CHECK(!vestige.deposit(OTHER_MONITOR, {.x = 200, .y = 200}, std::span{&IMPACT, 1}, {}));
        animation::ScreenFacts local{};
        local.monitor = OTHER_MONITOR;
        local.now = driver.now;
        local.enabled = true;
        SBubbleModule::Screen::tick(driver.resources, local);
        CHECK(!vestige.deposit(OTHER_MONITOR, {.x = 200, .y = 200}, std::span{&IMPACT, 1}, {}));
        shutdown.enabled = true;
        SBubbleModule::Screen::tick(driver.resources, shutdown);
        CHECK(vestige.deposit(OTHER_MONITOR, {.x = 200, .y = 200}, std::span{&IMPACT, 1}, {}));
    }
}  // namespace

int main() {
    idle_monitor_does_not_drop_wake(true);
    idle_monitor_does_not_drop_wake(false);
    cancelled_deposit_remains_until_own_tick(true);
    cancelled_deposit_remains_until_own_tick(false);
    global_shutdown_clears_and_rejects_new_effects();
    return g_failures == 0 ? 0 : 1;
}
