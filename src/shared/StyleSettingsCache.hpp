#pragma once

#include <optional>
#include <utility>

// The disengaged last value forces preparation even when the initial settings are defaults.
template <class Settings, class Prepared>
struct SStyleSettingsCache {
    std::optional<Settings> last;
    Prepared prepared{};

    template <class Prepare>
    bool update(const Settings &settings, Prepare &&prepare) {
        if (last && *last == settings) {
            return false;
        }
        prepared = std::forward<Prepare>(prepare)(settings);
        last = settings;
        return true;
    }
};
