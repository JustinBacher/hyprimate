#include <algorithm>
#include <cmath>

#include "Check.hpp"
#include "animations/bubble/BubbleOptics.hpp"

int main() {
    const auto t = hyprimate::bubbleThinFilmTable(301, 33, 1500);
    CHECK(t.rgb.size() == 301 * 33 * 3);
    const auto sample = [&](int d, int c, int channel) {
        return t.rgb[(3 * ((c * 301) + (d / 5))) + channel];
    };
    for (const float value : t.rgb) {
        CHECK(std::isfinite(value) && value >= 0 && value <= 1.05);
    }
    for (int c = 0; c < 33; ++c) {
        for (int k = 0; k < 3; ++k) {
            CHECK(sample(0, c, k) == 0);
        }
    }
    for (int k = 0; k < 3; ++k) {
        CHECK(sample(200, 0, k) > 0.98);
    }
    float maximum = 0;
    for (int d = 50; d <= 200; d += 5) {
        maximum = std::max(maximum,
            (0.2126F * sample(d, 32, 0)) + (0.7152F * sample(d, 32, 1)) +
                (0.0722F * sample(d, 32, 2)));
    }
    CHECK(maximum > 0.06 && maximum < 0.09);
    const auto spread = [&](int d) {
        const auto [lo, hi] = std::minmax({sample(d, 32, 0), sample(d, 32, 1), sample(d, 32, 2)});
        return hi - lo;
    };
    CHECK(std::max(spread(450), spread(500)) > spread(200));
    CHECK(hyprimate::bubbleThinFilmTable(1, 2, 100).rgb.empty());
    return g_failures == 0 ? 0 : 1;
}
